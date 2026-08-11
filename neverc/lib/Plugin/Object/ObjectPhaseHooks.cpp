#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "ObjectPhaseBridge.h"
#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

struct ObjectGraphArtifact {
  std::shared_ptr<PluginObjectGraph> Graph;
};

struct ObjectImageArtifact {
  std::shared_ptr<PluginObjectImage> Image;
};

NevercStatus objectPhaseStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercInterfaceID graphArtifactID() {
  return {NEVERC_PHASE_OBJECT_PRE_WRITE_INPUT_HIGH,
          NEVERC_PHASE_OBJECT_PRE_WRITE_INPUT_LOW};
}

NevercInterfaceID imageArtifactID() {
  return {NEVERC_PHASE_OBJECT_WRITE_OUTPUT_HIGH,
          NEVERC_PHASE_OBJECT_WRITE_OUTPUT_LOW};
}

#define NEVERC_OBJECT_PHASE_ID(Symbol)                                         \
  NevercInterfaceID {                                                          \
    NEVERC_PHASE_OBJECT_##Symbol##_HIGH, NEVERC_PHASE_OBJECT_##Symbol##_LOW    \
  }

NevercInterfaceID preWritePhaseID() {
  return NEVERC_OBJECT_PHASE_ID(PRE_WRITE);
}

NevercInterfaceID postLayoutPhaseID() {
  return NEVERC_OBJECT_PHASE_ID(POST_LAYOUT);
}

NevercInterfaceID writePhaseID() { return NEVERC_OBJECT_PHASE_ID(WRITE); }

NevercInterfaceID postWritePhaseID() {
  return NEVERC_OBJECT_PHASE_ID(POST_WRITE);
}

NevercInterfaceID finalVerifyPhaseID() {
  return NEVERC_OBJECT_PHASE_ID(FINAL_VERIFY);
}

NevercInterfaceID commitPhaseID() { return NEVERC_OBJECT_PHASE_ID(COMMIT); }

#undef NEVERC_OBJECT_PHASE_ID

template <typename T> NevercStatus writeRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return objectPhaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return objectPhaseStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? objectPhaseStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

Error verifyGraphArtifact(const void *Payload) {
  const auto *Artifact = static_cast<const ObjectGraphArtifact *>(Payload);
  if (!Artifact || !Artifact->Graph)
    return createStringError(errc::invalid_argument,
                             "object graph artifact is null");
  return verifyPluginObjectGraph(*Artifact->Graph);
}

Error verifyImageArtifact(const void *Payload) {
  const auto *Artifact = static_cast<const ObjectImageArtifact *>(Payload);
  if (!Artifact || !Artifact->Image)
    return createStringError(errc::invalid_argument,
                             "object image artifact is null");
  if (Artifact->Image->state() == PluginObjectImageState::Aborted ||
      Artifact->Image->state() == PluginObjectImageState::FailedPartial)
    return createStringError(errc::invalid_argument,
                             "object image artifact is not publishable");
  return Error::success();
}

NevercObjectImageState imageState(PluginObjectImageState State) {
  switch (State) {
  case PluginObjectImageState::Candidate:
    return NEVERC_OBJECT_IMAGE_CANDIDATE;
  case PluginObjectImageState::Verified:
    return NEVERC_OBJECT_IMAGE_VERIFIED;
  case PluginObjectImageState::Committed:
    return NEVERC_OBJECT_IMAGE_COMMITTED;
  case PluginObjectImageState::Aborted:
    return NEVERC_OBJECT_IMAGE_ABORTED;
  case PluginObjectImageState::FailedPartial:
    return NEVERC_OBJECT_IMAGE_FAILED_PARTIAL;
  }
  return NEVERC_OBJECT_IMAGE_FAILED_PARTIAL;
}

} // namespace

struct ObjectPhasePipeline::Impl final : ObjectPhaseRuntimeAccess {
  struct GraphBridgeEntry {
    PluginObjectGraph *Graph = nullptr;
    uint64_t CapabilityToken = 0;
    std::unique_ptr<ObjectPluginBridge> Bridge;
  };

  struct OwnedRoute {
    std::string TargetTriple;
    std::string CPU;
    std::string Features;
    std::string ObjectFormat;
    uint32_t ExecutionLevel = 0;

    NevercPhaseRoute view() const {
      NevercPhaseRoute Route{};
      Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                      NEVERC_PLUGIN_ABI_MINOR, 0};
      Route.TargetTriple = {TargetTriple.data(), TargetTriple.size()};
      Route.CPU = {CPU.data(), CPU.size()};
      Route.Features = {Features.data(), Features.size()};
      Route.ObjectFormat = {ObjectFormat.data(), ObjectFormat.size()};
      Route.ExecutionLevel = ExecutionLevel;
      return Route;
    }
  };

  PluginTaskContext &Task;
  std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  std::shared_ptr<ObjectPhaseProcessService> Service;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  std::unique_ptr<ObjectWriterProvider> Writer;

  NevercInterfaceID ActivePhase{};
  const ObjectOutputDestination *ActiveDestination = nullptr;
  ArrayRef<uint8_t> ActiveNativeImage;
  uint64_t NativeGraphGeneration = 0;
  std::vector<GraphBridgeEntry> ActiveGraphBridges;

  OwnedRoute ActiveRoute;
  bool Frozen = false;

  Impl(PluginTaskContext &TaskValue,
       std::shared_ptr<const PluginTargetSnapshot> SnapshotValue,
       std::shared_ptr<ObjectPhaseProcessService> ServiceValue,
       PluginPhaseGraph GraphValue)
      : Task(TaskValue), Snapshot(std::move(SnapshotValue)),
        Service(std::move(ServiceValue)), Graph(std::move(GraphValue)) {}

  NevercTaskHandle taskHandle() const override { return Task.handle(); }

  Error initialize() {
    auto GraphType = Artifacts.registerType(
        {graphArtifactID(),
         "object.graph",
         PluginArtifactOwnership::Owned,
         {},
         [](void *Payload) {
           delete static_cast<ObjectGraphArtifact *>(Payload);
         },
         verifyGraphArtifact});
    if (!GraphType)
      return GraphType.takeError();
    auto ImageType = Artifacts.registerType(
        {imageArtifactID(),
         "object.image",
         PluginArtifactOwnership::Owned,
         {},
         [](void *Payload) {
           delete static_cast<ObjectImageArtifact *>(Payload);
         },
         verifyImageArtifact});
    if (!ImageType)
      return ImageType.takeError();
    if (Error E = Artifacts.freeze())
      return E;

    auto CreatedWriter = ObjectWriterProvider::create(Snapshot);
    if (!CreatedWriter)
      return CreatedWriter.takeError();
    Writer = std::move(*CreatedWriter);

    Executor = std::make_unique<PluginPhaseExecutor>(Graph, Artifacts);
    if (Error E = Executor->importSessionRegistrations(Task.session()))
      return E;
    if (Error E = Executor->setBuiltinProvider(
            preWritePhaseID(),
            [this](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
              return copyGraph(Frame, Result, true);
            }))
      return E;
    if (Error E = Executor->setBuiltinProvider(
            postLayoutPhaseID(),
            [this](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
              return copyGraph(Frame, Result, false);
            }))
      return E;
    if (Error E = Executor->setBuiltinProvider(
            writePhaseID(),
            [this](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
              return writeObject(Frame, Result);
            }))
      return E;
    if (Error E = Executor->setBuiltinProvider(
            postWritePhaseID(),
            [this](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
              return copyImage(Frame, Result);
            }))
      return E;
    if (Error E = Executor->setBuiltinProvider(
            finalVerifyPhaseID(),
            [this](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
              return verifyImage(Frame, Result);
            }))
      return E;
    return Executor->setBuiltinProvider(
        commitPhaseID(),
        [this](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
          return commitImage(Frame, Result);
        });
  }

  Error freeze() {
    if (Frozen)
      return Error::success();
    if (Error E = Executor->freeze())
      return E;
    Frozen = true;
    return Error::success();
  }

  NevercPhaseRoute route() const { return ActiveRoute.view(); }

  OwnedRoute makeRoute(NevercTargetKey Target,
                       NevercObjectFormatID FormatID) const {
    OwnedRoute Result;
    Result.TargetTriple.assign(Target.RawTriple.Data,
                               static_cast<size_t>(Target.RawTriple.Length));
    Result.CPU.assign(Target.CPU.Data, static_cast<size_t>(Target.CPU.Length));
    const auto *Data = reinterpret_cast<const uint8_t *>(Target.Features.Data);
    for (uint64_t Index = 0; Data && Index != Target.Features.Count; ++Index) {
      const auto *Feature = reinterpret_cast<const NevercStringView *>(
          Data + Index * Target.Features.ElementStride);
      if (!Result.Features.empty())
        Result.Features.push_back(',');
      Result.Features.append(Feature->Data,
                             static_cast<size_t>(Feature->Length));
    }
    if (const auto *Format = Writer->registry().find(FormatID))
      Result.ObjectFormat = Format->CanonicalName;
    Result.ExecutionLevel = static_cast<uint32_t>(Target.ExecutionLevel);
    return Result;
  }

  void setRoute(NevercTargetKey Target, NevercObjectFormatID FormatID) {
    ActiveRoute = makeRoute(Target, FormatID);
  }

  void setRoute(const PluginObjectGraph &ObjectGraph) {
    setRoute(ObjectGraph.targetKey(), ObjectGraph.formatID());
  }

  Error beginPhase(NevercInterfaceID Phase) {
    ActivePhase = Phase;
    if (Error E = Service->attach(*this)) {
      ActivePhase = {};
      return E;
    }
    return Error::success();
  }

  void endPhase() {
    ActiveGraphBridges.clear();
    Service->detach(Task.handle());
    ActivePhase = {};
  }

  bool validFrame(const NevercPhaseFrame *Frame) const {
    return Frame && sameHandle(Frame->Task, Task.handle()) &&
           sameID(Frame->Phase, ActivePhase);
  }

  template <typename ArtifactT>
  Expected<const ArtifactT *> resolve(const NevercPhaseFrame *Frame,
                                      NevercArtifactHandle Handle,
                                      NevercInterfaceID Type) {
    if (!validFrame(Frame) || (!sameHandle(Handle, Frame->Input) &&
                               !sameHandle(Handle, Frame->CurrentOutput)))
      return createStringError(errc::invalid_argument,
                               "object phase artifact is out of scope");
    const void *Payload = nullptr;
    NevercStatus Status =
        Executor->resolveArtifactPayload(Task, Handle, Type, &Payload);
    if (!neverc_status_is_ok(Status) || !Payload)
      return createStringError(errc::invalid_argument,
                               "object phase artifact is invalid");
    return static_cast<const ArtifactT *>(Payload);
  }

  NevercStatus publishGraph(std::shared_ptr<PluginObjectGraph> Value,
                            NevercPhaseResult *Result) {
    if (!Result || !Value)
      return objectPhaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Artifact = new ObjectGraphArtifact{std::move(Value)};
    auto Candidate =
        Executor->createCandidate(Task, graphArtifactID(), Artifact);
    if (!Candidate) {
      delete Artifact;
      consumeError(Candidate.takeError());
      return objectPhaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result->Action = NEVERC_PHASE_REPLACE;
    Result->Output = *Candidate;
    return neverc_status_ok();
  }

  NevercStatus publishImage(std::shared_ptr<PluginObjectImage> Value,
                            NevercPhaseResult *Result) {
    if (!Result || !Value)
      return objectPhaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Artifact = new ObjectImageArtifact{std::move(Value)};
    auto Candidate =
        Executor->createCandidate(Task, imageArtifactID(), Artifact);
    if (!Candidate) {
      delete Artifact;
      consumeError(Candidate.takeError());
      return objectPhaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result->Action = NEVERC_PHASE_REPLACE;
    Result->Output = *Candidate;
    return neverc_status_ok();
  }

  NevercStatus copyGraph(const NevercPhaseFrame *Frame,
                         NevercPhaseResult *Result, bool ClearLayoutProof) {
    auto Input = resolve<ObjectGraphArtifact>(
        Frame, Frame ? Frame->Input : NevercArtifactHandle{},
        graphArtifactID());
    if (!Input) {
      consumeError(Input.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    auto Copy = std::make_shared<PluginObjectGraph>(*(*Input)->Graph);
    if (ClearLayoutProof)
      Copy->clearLayoutProof();
    return publishGraph(std::move(Copy), Result);
  }

  NevercStatus copyImage(const NevercPhaseFrame *Frame,
                         NevercPhaseResult *Result) {
    auto Input = resolve<ObjectImageArtifact>(
        Frame, Frame ? Frame->Input : NevercArtifactHandle{},
        imageArtifactID());
    if (!Input) {
      consumeError(Input.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    return publishImage((*Input)->Image, Result);
  }

  NevercStatus writeObject(const NevercPhaseFrame *Frame,
                           NevercPhaseResult *Result) {
    auto Input = resolve<ObjectGraphArtifact>(
        Frame, Frame ? Frame->Input : NevercArtifactHandle{},
        graphArtifactID());
    if (!Input || !ActiveDestination) {
      if (!Input)
        consumeError(Input.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    Expected<std::unique_ptr<PluginObjectImage>> Image =
        !ActiveNativeImage.empty() &&
                (*Input)->Graph->generation() == NativeGraphGeneration
            ? Writer->beginImage(Task, (*Input)->Graph->formatID(),
                                 (*Input)->Graph->targetKey().TargetID,
                                 (*Input)->Graph->generation(),
                                 ActiveNativeImage, *ActiveDestination)
            : Writer->beginWrite(Task, *(*Input)->Graph, *ActiveDestination);
    if (!Image) {
      consumeError(Image.takeError());
      return objectPhaseStatus(NEVERC_STATUS_PLUGIN_FAILURE);
    }
    return publishImage(std::shared_ptr<PluginObjectImage>(std::move(*Image)),
                        Result);
  }

  NevercStatus verifyImage(const NevercPhaseFrame *Frame,
                           NevercPhaseResult *Result) {
    auto Input = resolve<ObjectImageArtifact>(
        Frame, Frame ? Frame->Input : NevercArtifactHandle{},
        imageArtifactID());
    if (!Input) {
      consumeError(Input.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    if (Error E = (*Input)->Image->verify()) {
      consumeError(std::move(E));
      return objectPhaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    }
    return publishImage((*Input)->Image, Result);
  }

  NevercStatus commitImage(const NevercPhaseFrame *Frame,
                           NevercPhaseResult *Result) {
    auto Input = resolve<ObjectImageArtifact>(
        Frame, Frame ? Frame->Input : NevercArtifactHandle{},
        imageArtifactID());
    if (!Input) {
      consumeError(Input.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    auto Committed = (*Input)->Image->commit();
    if (!Committed) {
      consumeError(Committed.takeError());
      return objectPhaseStatus(NEVERC_STATUS_PLUGIN_FAILURE);
    }
    return publishImage((*Input)->Image, Result);
  }

  Expected<std::shared_ptr<PluginObjectGraph>>
  executeGraphPhase(NevercInterfaceID Phase,
                    const std::shared_ptr<PluginObjectGraph> &Input) {
    ObjectGraphArtifact View{Input};
    auto InputHandle = Executor->createArtifactView(Task, graphArtifactID(),
                                                    &View, Input->generation());
    if (!InputHandle)
      return InputHandle.takeError();
    NevercArtifactHandle Handle = *InputHandle;
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(Handle, PluginArtifactHandleKind);
    });
    PluginArtifactSlot Output(Artifacts.find(graphArtifactID()));
    if (Error E = beginPhase(Phase))
      return std::move(E);
    auto End = make_scope_exit([&] { endPhase(); });
    if (Error E = Executor->execute(Task.session(), Task, Phase, route(),
                                    Handle, Output))
      return std::move(E);
    const auto *Published =
        static_cast<const ObjectGraphArtifact *>(Output.payload());
    if (!Published || !Published->Graph)
      return createStringError(errc::invalid_argument,
                               "object graph phase published no graph");
    return Published->Graph;
  }

  Expected<std::shared_ptr<PluginObjectImage>>
  executeImagePhase(NevercInterfaceID Phase,
                    const std::shared_ptr<PluginObjectImage> &Input) {
    ObjectImageArtifact View{Input};
    auto InputHandle = Executor->createArtifactView(
        Task, imageArtifactID(), &View, Input->graphGeneration());
    if (!InputHandle)
      return InputHandle.takeError();
    NevercArtifactHandle Handle = *InputHandle;
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(Handle, PluginArtifactHandleKind);
    });
    PluginArtifactSlot Output(Artifacts.find(imageArtifactID()));
    if (Error E = beginPhase(Phase))
      return std::move(E);
    auto End = make_scope_exit([&] { endPhase(); });
    if (Error E = Executor->execute(Task.session(), Task, Phase, route(),
                                    Handle, Output))
      return std::move(E);
    const auto *Published =
        static_cast<const ObjectImageArtifact *>(Output.payload());
    if (!Published || !Published->Image)
      return createStringError(errc::invalid_argument,
                               "object image phase published no image");
    return Published->Image;
  }

  NevercStatus getGraph(const NevercPhaseFrame *Frame,
                        NevercArtifactHandle Artifact,
                        NevercObjectPhaseGraphInfo *OutInfo) override {
    auto Payload =
        resolve<ObjectGraphArtifact>(Frame, Artifact, graphArtifactID());
    if (!Payload) {
      consumeError(Payload.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    auto Capability = Executor->currentArtifactMutationCapability(Task);
    const uint64_t CapabilityToken = Capability.value_or(0);
    PluginObjectGraph *Graph = (*Payload)->Graph.get();
    auto Entry =
        std::find_if(ActiveGraphBridges.begin(), ActiveGraphBridges.end(),
                     [&](const GraphBridgeEntry &Candidate) {
                       return Candidate.Graph == Graph &&
                              Candidate.CapabilityToken == CapabilityToken;
                     });
    if (Entry == ActiveGraphBridges.end()) {
      GraphBridgeEntry NewEntry;
      NewEntry.Graph = Graph;
      NewEntry.CapabilityToken = CapabilityToken;
      NewEntry.Bridge =
          Capability
              ? std::make_unique<ObjectPluginBridge>(Task, *Graph, *Executor,
                                                     *Capability)
              : std::make_unique<ObjectPluginBridge>(Task, *Graph, false);
      ActiveGraphBridges.push_back(std::move(NewEntry));
      Entry = std::prev(ActiveGraphBridges.end());
    }
    ObjectPluginBridge &Bridge = *Entry->Bridge;
    auto GraphHandle = Bridge.graph();
    if (!GraphHandle) {
      consumeError(GraphHandle.takeError());
      return objectPhaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    NevercObjectPhaseGraphInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_OBJECT_PHASE_API_MAJOR,
                    NEVERC_OBJECT_PHASE_API_MINOR, 0};
    Value.Object = &Bridge.api();
    Value.Graph = *GraphHandle;
    if ((*Payload)->Graph->hasLayoutProof()) {
      auto Proof = Bridge.layoutProof();
      if (!Proof) {
        consumeError(Proof.takeError());
        return objectPhaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Value.LayoutProof = *Proof;
    }
    Value.Generation = (*Payload)->Graph->generation();
    return writeRecord(OutInfo, Value);
  }

  NevercStatus getImage(const NevercPhaseFrame *Frame,
                        NevercArtifactHandle Artifact,
                        NevercObjectImageInfo *OutInfo) override {
    auto Payload =
        resolve<ObjectImageArtifact>(Frame, Artifact, imageArtifactID());
    if (!Payload) {
      consumeError(Payload.takeError());
      return objectPhaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    auto Summary = (*Payload)->Image->outputSummary();
    if (!Summary) {
      consumeError(Summary.takeError());
      return objectPhaseStatus(NEVERC_STATUS_INVALID_STATE);
    }
    NevercObjectImageInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_OBJECT_PHASE_API_MAJOR,
                    NEVERC_OBJECT_PHASE_API_MINOR, 0};
    Value.Image = (*Payload)->Image->handle();
    Value.FormatID = (*Payload)->Image->formatID();
    Value.TargetID = (*Payload)->Image->targetID();
    Value.GraphGeneration = (*Payload)->Image->graphGeneration();
    Value.State = imageState((*Payload)->Image->state());
    Value.OutputKind = Summary->Kind;
    Value.OutputState = Summary->State;
    Value.OutputFlags = Summary->Flags;
    Value.Size = Summary->Size;
    Value.PublicationGeneration = Summary->PublicationGeneration;
    std::copy(std::begin(Summary->Digest), std::end(Summary->Digest),
              Value.Digest);
    Value.Builder = (*Payload)->Image->binaryBuilder();
    const StringRef Provenance = (*Payload)->Image->provenance();
    Value.Provenance = {Provenance.data(), Provenance.size()};
    if ((*Payload)->Image->layoutReport()) {
      Value.HasLayoutReport = NEVERC_TRUE;
      Value.LayoutReport = *(*Payload)->Image->layoutReport();
    }
    auto Capability = Executor->currentArtifactMutationCapability(Task);
    Value.Binary =
        Capability
            ? (*Payload)->Image->capabilityBinaryAPI(*Executor, *Capability)
            : (*Payload)->Image->readOnlyBinaryAPI();
    return writeRecord(OutInfo, Value);
  }
};

ObjectPhasePipeline::ObjectPhasePipeline(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

ObjectPhasePipeline::~ObjectPhasePipeline() = default;

Expected<std::unique_ptr<ObjectPhasePipeline>> ObjectPhasePipeline::create(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Snapshot) {
  if (!Snapshot)
    return createStringError(errc::invalid_argument,
                             "object phase pipeline has no target snapshot");
  auto Service = findObjectPhaseProcessService(Task.processServices());
  if (!Service)
    return createStringError(errc::not_supported,
                             "object phase interface is not registered");
  auto Graph = PluginPhaseGraph::createBuiltinObjectGraph();
  if (!Graph)
    return Graph.takeError();
  auto State = std::make_unique<Impl>(Task, std::move(Snapshot),
                                      std::move(Service), std::move(*Graph));
  if (Error E = State->initialize())
    return std::move(E);
  return std::unique_ptr<ObjectPhasePipeline>(
      new ObjectPhasePipeline(std::move(State)));
}

Error ObjectPhasePipeline::addObserver(
    StringRef PluginID, const NevercObserverDescriptor &Descriptor) {
  if (State->Frozen)
    return createStringError(errc::operation_not_permitted,
                             "object phase pipeline is frozen");
  return State->Executor->addObserver(PluginID, Descriptor);
}

Error ObjectPhasePipeline::addInterceptor(
    StringRef PluginID, const NevercInterceptorDescriptor &Descriptor) {
  if (State->Frozen)
    return createStringError(errc::operation_not_permitted,
                             "object phase pipeline is frozen");
  return State->Executor->addInterceptor(PluginID, Descriptor);
}

bool ObjectPhasePipeline::hasPluginBindings() const {
  return State->Executor->hasBindings(preWritePhaseID()) ||
         State->Executor->hasBindings(postLayoutPhaseID()) ||
         State->Executor->hasBindings(writePhaseID()) ||
         State->Executor->hasBindings(postWritePhaseID()) ||
         State->Executor->hasBindings(finalVerifyPhaseID()) ||
         State->Executor->hasBindings(commitPhaseID());
}

bool ObjectPhasePipeline::hasInterceptors() const {
  return State->Executor->hasInterceptors(preWritePhaseID()) ||
         State->Executor->hasInterceptors(postLayoutPhaseID()) ||
         State->Executor->hasInterceptors(writePhaseID()) ||
         State->Executor->hasInterceptors(postWritePhaseID()) ||
         State->Executor->hasInterceptors(finalVerifyPhaseID()) ||
         State->Executor->hasInterceptors(commitPhaseID());
}

bool ObjectPhasePipeline::mayReplaceArtifact(
    NevercTargetKey Target, NevercObjectFormatID FormatID) const {
  if (hasInterceptors())
    return true;
  const Impl::OwnedRoute Route = State->makeRoute(Target, FormatID);
  const NevercPhaseRoute RouteView = Route.view();
  return State->Executor->hasProvider(preWritePhaseID(), RouteView) ||
         State->Executor->hasProvider(postLayoutPhaseID(), RouteView) ||
         State->Executor->hasProvider(writePhaseID(), RouteView) ||
         State->Executor->hasProvider(postWritePhaseID(), RouteView) ||
         State->Executor->hasProvider(finalVerifyPhaseID(), RouteView) ||
         State->Executor->hasProvider(commitPhaseID(), RouteView);
}

bool ObjectPhasePipeline::mayReplaceWriteArtifact(
    NevercTargetKey Target, NevercObjectFormatID FormatID) const {
  const Impl::OwnedRoute Route = State->makeRoute(Target, FormatID);
  return State->Executor->hasInterceptors(writePhaseID()) ||
         State->Executor->hasProvider(writePhaseID(), Route.view());
}

bool ObjectPhasePipeline::hasPluginOwnedGraphWriter(
    NevercObjectFormatID FormatID) const {
  return State->Writer->hasPluginOwnedGraphWriter(FormatID);
}

Error ObjectPhasePipeline::freeze() { return State->freeze(); }

Expected<std::shared_ptr<PluginObjectImage>>
ObjectPhasePipeline::execute(const PluginObjectGraph &InputGraph,
                             const ObjectOutputDestination &Destination) {
  return executeNative(InputGraph, {}, Destination);
}

Expected<std::shared_ptr<PluginObjectImage>>
ObjectPhasePipeline::executeNative(const PluginObjectGraph &InputGraph,
                                   ArrayRef<uint8_t> NativeImage,
                                   const ObjectOutputDestination &Destination) {
  return executeNative(InputGraph, NativeImage, Destination, {});
}

Expected<std::shared_ptr<PluginObjectImage>>
ObjectPhasePipeline::executeNative(const PluginObjectGraph &InputGraph,
                                   ArrayRef<uint8_t> NativeImage,
                                   const ObjectOutputDestination &Destination,
                                   ObjectPhaseSemanticValidators Validators) {
  auto ValidateGraph = [&Validators](const PluginObjectGraph &Object) -> Error {
    if (!Validators.Graph)
      return Error::success();
    return Validators.Graph(Object);
  };
  auto ValidateImage = [&Validators](const PluginObjectImage &Image) -> Error {
    if (!Validators.Image)
      return Error::success();
    auto Bytes = Image.pendingBytes();
    if (!Bytes)
      return Bytes.takeError();
    return Validators.Image(*Bytes);
  };

  if (Error E = State->freeze())
    return std::move(E);
  if (Error E = verifyPluginObjectGraph(InputGraph))
    return std::move(E);
  if (Error E = ValidateGraph(InputGraph))
    return std::move(E);
  State->setRoute(InputGraph);
  State->ActiveNativeImage = NativeImage;
  State->NativeGraphGeneration = InputGraph.generation();
  auto ClearNativeImage = make_scope_exit([&] {
    State->ActiveNativeImage = {};
    State->NativeGraphGeneration = 0;
  });

  auto CurrentGraph = std::make_shared<PluginObjectGraph>(InputGraph);
  CurrentGraph->clearLayoutProof();
  auto PreWrite = State->executeGraphPhase(preWritePhaseID(), CurrentGraph);
  if (!PreWrite)
    return PreWrite.takeError();
  CurrentGraph = std::move(*PreWrite);
  if (Error E = verifyPluginObjectGraph(*CurrentGraph))
    return std::move(E);
  if (Error E = ValidateGraph(*CurrentGraph))
    return std::move(E);

  constexpr unsigned MaxRelayoutAttempts = 8;
  bool LayoutAccepted = false;
  for (unsigned Attempt = 0; Attempt != MaxRelayoutAttempts; ++Attempt) {
    CurrentGraph->issueLayoutProof();
    auto PostLayout =
        State->executeGraphPhase(postLayoutPhaseID(), CurrentGraph);
    if (!PostLayout)
      return PostLayout.takeError();
    CurrentGraph = std::move(*PostLayout);
    if (Error E = verifyPluginObjectGraph(*CurrentGraph))
      return std::move(E);
    if (Error E = ValidateGraph(*CurrentGraph))
      return std::move(E);
    const PluginObjectLayoutProof *Proof = CurrentGraph->layoutProof();
    if (Proof && Proof->GraphGeneration == CurrentGraph->generation()) {
      LayoutAccepted = true;
      break;
    }
  }
  if (!LayoutAccepted)
    return createStringError(
        errc::invalid_argument,
        "object post-layout phase exceeded the relayout limit");

  State->ActiveDestination = &Destination;
  auto ClearDestination =
      make_scope_exit([&] { State->ActiveDestination = nullptr; });
  ObjectGraphArtifact GraphView{CurrentGraph};
  auto GraphHandle = State->Executor->createArtifactView(
      State->Task, graphArtifactID(), &GraphView, CurrentGraph->generation());
  if (!GraphHandle)
    return GraphHandle.takeError();
  NevercArtifactHandle InputHandle = *GraphHandle;
  auto ReleaseGraph = make_scope_exit([&] {
    (void)State->Task.handles().release(InputHandle, PluginArtifactHandleKind);
  });
  PluginArtifactSlot WriteOutput(State->Artifacts.find(imageArtifactID()));
  if (Error E = State->beginPhase(writePhaseID()))
    return std::move(E);
  auto EndWrite = make_scope_exit([&] { State->endPhase(); });
  if (Error E = State->Executor->execute(State->Task.session(), State->Task,
                                         writePhaseID(), State->route(),
                                         InputHandle, WriteOutput))
    return std::move(E);
  const auto *Written =
      static_cast<const ObjectImageArtifact *>(WriteOutput.payload());
  if (!Written || !Written->Image)
    return createStringError(errc::invalid_argument,
                             "object write phase published no image");
  std::shared_ptr<PluginObjectImage> CurrentImage = Written->Image;
  EndWrite.release();
  State->endPhase();

  ObjectImageSemanticValidator BoundPostWriteImage;
  if (Validators.BindPrePostWriteImage) {
    auto BaselineBytes = CurrentImage->pendingBytes();
    if (!BaselineBytes)
      return BaselineBytes.takeError();
    auto Bound = Validators.BindPrePostWriteImage(*BaselineBytes);
    if (!Bound)
      return Bound.takeError();
    BoundPostWriteImage = std::move(*Bound);
    if (!BoundPostWriteImage)
      return createStringError(
          errc::invalid_argument,
          "pre/post-write image validator factory returned no validator");
  }

  auto PostWrite = State->executeImagePhase(postWritePhaseID(), CurrentImage);
  if (!PostWrite)
    return PostWrite.takeError();
  CurrentImage = std::move(*PostWrite);
  if (BoundPostWriteImage) {
    auto PostWriteBytes = CurrentImage->pendingBytes();
    if (!PostWriteBytes)
      return PostWriteBytes.takeError();
    if (Error E = BoundPostWriteImage(*PostWriteBytes))
      return std::move(E);
  }
  if (Error E = ValidateImage(*CurrentImage))
    return std::move(E);
  if (Validators.Commit)
    if (Error E =
            CurrentImage->setCommitter(std::move(Validators.Commit)))
      return std::move(E);
  if (Error E = CurrentImage->finish())
    return std::move(E);

  for (NevercInterfaceID Phase : {finalVerifyPhaseID(), commitPhaseID()}) {
    auto Next = State->executeImagePhase(Phase, CurrentImage);
    if (!Next)
      return Next.takeError();
    CurrentImage = std::move(*Next);
  }
  if (CurrentImage->state() != PluginObjectImageState::Committed)
    return createStringError(errc::io_error,
                             "object phase pipeline did not commit output");
  return CurrentImage;
}

Expected<std::shared_ptr<PluginObjectImage>>
ObjectPhasePipeline::verifyAndCommitFinished(
    NevercTargetKey Target, std::shared_ptr<PluginObjectImage> Image) {
  if (!Image)
    return createStringError(errc::invalid_argument,
                             "object image candidate is null");
  if (Error E = State->freeze())
    return std::move(E);
  State->setRoute(Target, Image->formatID());
  std::shared_ptr<PluginObjectImage> CurrentImage = std::move(Image);
  for (NevercInterfaceID Phase : {finalVerifyPhaseID(), commitPhaseID()}) {
    auto Next = State->executeImagePhase(Phase, CurrentImage);
    if (!Next)
      return Next.takeError();
    CurrentImage = std::move(*Next);
  }
  if (CurrentImage->state() != PluginObjectImageState::Committed)
    return createStringError(errc::io_error,
                             "object image candidate was not committed");
  return CurrentImage;
}

} // namespace neverc::plugin
