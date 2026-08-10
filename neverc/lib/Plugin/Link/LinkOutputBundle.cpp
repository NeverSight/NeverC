#include "LinkOutputBundle.h"
#include "LinkPhaseCAPI.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error outputError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link output bundle: " + Message);
}

NevercStatus outputStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

template <typename T>
NevercStatus writeRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return outputStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? outputStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

} // namespace

LinkOutputBundle::LinkOutputBundle(
    PluginTaskContext &TaskValue,
    std::shared_ptr<PluginBinaryImage> ImageValue,
    std::unique_ptr<neverc::OutputBundleTransaction> TransactionValue)
    : Task(TaskValue), Image(std::move(ImageValue)),
      Transaction(std::move(TransactionValue)) {}

Expected<std::shared_ptr<LinkOutputBundle>>
LinkOutputBundle::create(
    PluginTaskContext &Task, neverc::OutputCoordinator &Coordinator,
    std::shared_ptr<PluginBinaryImage> Image, StringRef MainPath,
    ArrayRef<PluginLinkSideOutput> SideOutputs,
    neverc::OutputBundleTransaction::FaultInjector InjectFault) {
  if (!Image || Image->state() != NEVERC_BINARY_IMAGE_CANDIDATE ||
      MainPath.empty())
    return outputError("candidate image and main path are required");
  std::set<std::string> Names;
  std::vector<neverc::OutputBundleFile> Outputs;
  Outputs.push_back(
      {"main", MainPath.str(),
       std::vector<uint8_t>(Image->bytes().begin(),
                            Image->bytes().end()),
         true, true});
  Names.insert("main");
  for (const PluginLinkSideOutput &Side : SideOutputs) {
    if (Side.Name.empty() || Side.Path.empty() ||
        !Names.insert(Side.Name).second)
      return outputError("side output names and paths must be unique");
    Outputs.push_back({Side.Name, Side.Path, Side.Bytes, false});
  }
  NevercTaskHandle TaskHandle = Task.handle();
  auto Transaction = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs, {},
      std::move(InjectFault),
      {TaskHandle.Owner, TaskHandle.Value});
  if (!Transaction)
    return Transaction.takeError();
  auto Bundle = std::shared_ptr<LinkOutputBundle>(
      new LinkOutputBundle(Task, std::move(Image),
                           std::move(*Transaction)));
  if (Error E = Bundle->initializeHandle())
    return std::move(E);
  return Bundle;
}

Error LinkOutputBundle::initializeHandle() {
  auto Created =
      Task.handles().create(PluginOutputBundleHandleKind, this);
  if (!Created)
    return Created.takeError();
  Handle = *Created;
  return Error::success();
}

LinkOutputBundle::~LinkOutputBundle() {
  if (!neverc_handle_is_null(Handle))
    (void)Task.handles().release(
        Handle, PluginOutputBundleHandleKind);
}

Error LinkOutputBundle::verifyAndPrepare() {
  if (Image->state() != NEVERC_BINARY_IMAGE_VERIFIED)
    return outputError("image must pass its sealed verifier first");
  return Transaction->prepare();
}

Expected<neverc::OutputBundleSummary> LinkOutputBundle::commit() {
  auto Result = Transaction->commit();
  if (Result) {
    Image->markCommitted();
    return std::move(*Result);
  }
  const neverc::OutputBundleSummary Current = Transaction->summary();
  if (Current.State == neverc::OutputBundleState::Committed)
    Image->markCommitted();
  else if (Current.State == neverc::OutputBundleState::FailedPartial)
    Image->markFailedPartial();
  else
    consumeError(Image->abort());
  return Result.takeError();
}

Error LinkOutputBundle::abort() {
  Error Result = Transaction->abort();
  if (Image->state() != NEVERC_BINARY_IMAGE_COMMITTED &&
      Image->state() != NEVERC_BINARY_IMAGE_FAILED_PARTIAL)
    Result = joinErrors(std::move(Result), Image->abort());
  return Result;
}

neverc::OutputBundleSummary LinkOutputBundle::summary() const {
  return Transaction->summary();
}

namespace {

struct LinkOutputArtifact {
  std::shared_ptr<PluginBinaryImage> Image;
  std::shared_ptr<LinkOutputBundle> Bundle;
  uint64_t Generation = 0;
};

Error verifyOutputArtifact(const void *Payload) {
  const auto *Artifact =
      static_cast<const LinkOutputArtifact *>(Payload);
  if (!Artifact || !Artifact->Image)
    return outputError("phase artifact has no BinaryImage");
  switch (Artifact->Image->state()) {
  case NEVERC_BINARY_IMAGE_CANDIDATE:
    return verifyBinaryImage(*Artifact->Image);
  case NEVERC_BINARY_IMAGE_VERIFIED:
    return Error::success();
  case NEVERC_BINARY_IMAGE_COMMITTED:
    if (!Artifact->Bundle ||
        Artifact->Bundle->summary().State !=
            neverc::OutputBundleState::Committed)
      return outputError("committed image has no committed bundle");
    return Error::success();
  default:
    return outputError("phase artifact contains a failed image");
  }
}

std::pair<uint64_t, uint64_t> idKey(NevercInterfaceID ID) {
  return {ID.High, ID.Low};
}

} // namespace

struct LinkOutputPipeline::Impl final : LinkPhaseRuntimeAccess {
  PluginTaskContext &Task;
  neverc::OutputCoordinator &Coordinator;
  std::shared_ptr<LinkPhaseProcessService> Service;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  NevercInterfaceID ActivePhase{};
  const LinkOutputArtifact *ActiveArtifact = nullptr;
  bool Frozen = false;
  std::string MainPath;
  std::vector<PluginLinkSideOutput> SideOutputs;
  neverc::OutputBundleTransaction::FaultInjector InjectFault;
  std::string LateCommitFailure;

  Impl(PluginTaskContext &TaskValue,
       neverc::OutputCoordinator &CoordinatorValue,
       std::shared_ptr<LinkPhaseProcessService> ServiceValue,
       PluginPhaseGraph GraphValue)
      : Task(TaskValue), Coordinator(CoordinatorValue),
        Service(std::move(ServiceValue)),
        Graph(std::move(GraphValue)) {}

  NevercTaskHandle taskHandle() const override {
    return Task.handle();
  }

  Error initialize() {
    const NevercInterfaceID Phases[] = {
        {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
         NEVERC_PHASE_LINK_POST_EMIT_LOW},
        {NEVERC_PHASE_LINK_IMAGE_VERIFY_HIGH,
         NEVERC_PHASE_LINK_IMAGE_VERIFY_LOW},
        {NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_HIGH,
         NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_LOW},
        {NEVERC_PHASE_LINK_COMMIT_HIGH,
         NEVERC_PHASE_LINK_COMMIT_LOW},
    };
    std::set<std::pair<uint64_t, uint64_t>> Registered;
    for (NevercInterfaceID PhaseID : Phases) {
      const PluginPhaseDefinition *Phase = Graph.find(PhaseID);
      if (!Phase)
        return outputError("output phase is absent from the schema");
      for (NevercInterfaceID Type :
           {Phase->InputArtifact, Phase->OutputArtifact}) {
        if (!Registered.insert(idKey(Type)).second)
          continue;
        PluginArtifactTypeDescriptor Descriptor;
        Descriptor.ID = Type;
        Descriptor.Name =
            "link.output." + std::to_string(Type.Low);
        Descriptor.Ownership = PluginArtifactOwnership::Owned;
        Descriptor.Destroy = [](void *Payload) {
          delete static_cast<LinkOutputArtifact *>(Payload);
        };
        Descriptor.Verify = verifyOutputArtifact;
        auto Added = Artifacts.registerType(std::move(Descriptor));
        if (!Added)
          return Added.takeError();
      }
    }
    if (Error E = Artifacts.freeze())
      return E;
    Executor = std::make_unique<PluginPhaseExecutor>(
        Graph, Artifacts);
    if (Error E = Executor->importSessionRegistrations(Task.session()))
      return E;
    for (NevercInterfaceID PhaseID : Phases)
      if (Error E = Executor->setBuiltinProvider(
              PhaseID,
              [this, PhaseID](const NevercPhaseFrame *Frame,
                              NevercPhaseResult *Result) {
                return runBuiltin(PhaseID, Frame, Result);
              }))
        return E;
    return Error::success();
  }

  Error freeze() {
    if (Frozen)
      return Error::success();
    if (Error E = Executor->freeze())
      return E;
    Frozen = true;
    return Error::success();
  }

  NevercPhaseRoute route() const {
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    return Route;
  }

  bool validFrame(const NevercPhaseFrame *Frame) const {
    return Frame && sameHandle(Frame->Task, Task.handle()) &&
           samePluginInterfaceID(Frame->Phase, ActivePhase);
  }

  Expected<const LinkOutputArtifact *>
  resolve(const NevercPhaseFrame *Frame,
          NevercArtifactHandle Handle) {
    if (!validFrame(Frame))
      return outputError("phase frame is out of scope");
    const PluginPhaseDefinition *Phase =
        Graph.find(Frame->Phase);
    if (!Phase)
      return outputError("output phase is not registered");
    NevercInterfaceID Type{};
    if (sameHandle(Handle, Frame->Input))
      Type = Phase->InputArtifact;
    else if (sameHandle(Handle, Frame->CurrentOutput))
      Type = Phase->OutputArtifact;
    else
      return outputError("image artifact is out of scope");
    const void *Payload = nullptr;
    NevercStatus Status = Executor->resolveArtifactPayload(
        Task, Handle, Type, &Payload);
    if (!neverc_status_is_ok(Status) || !Payload)
      return outputError("image artifact handle is invalid");
    return static_cast<const LinkOutputArtifact *>(Payload);
  }

  NevercStatus publish(LinkOutputArtifact Artifact,
                       NevercInterfaceID Type,
                       NevercPhaseResult *Result) {
    if (!Artifact.Image || !Result)
      return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Payload =
        new LinkOutputArtifact(std::move(Artifact));
    auto Candidate =
        Executor->createCandidate(Task, Type, Payload);
    if (!Candidate) {
      delete Payload;
      consumeError(Candidate.takeError());
      return outputStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result->Action = NEVERC_PHASE_REPLACE;
    Result->Output = *Candidate;
    return neverc_status_ok();
  }

  NevercStatus runBuiltin(NevercInterfaceID PhaseID,
                          const NevercPhaseFrame *Frame,
                          NevercPhaseResult *Result) {
    auto Input =
        resolve(Frame, Frame ? Frame->Input : NevercArtifactHandle{});
    const PluginPhaseDefinition *Definition = Graph.find(PhaseID);
    if (!Input || !Definition) {
      if (!Input)
        consumeError(Input.takeError());
      return outputStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    LinkOutputArtifact Output = **Input;
    ++Output.Generation;
    if (samePluginInterfaceID(
            PhaseID,
            {NEVERC_PHASE_LINK_IMAGE_VERIFY_HIGH,
             NEVERC_PHASE_LINK_IMAGE_VERIFY_LOW})) {
      auto Bundle = LinkOutputBundle::create(
          Task, Coordinator, Output.Image, MainPath, SideOutputs,
          InjectFault);
      if (!Bundle) {
        consumeError(Bundle.takeError());
        return outputStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
      Output.Bundle = std::move(*Bundle);
      if (Error E = Output.Image->verify()) {
        consumeError(std::move(E));
        return outputStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (samePluginInterfaceID(
                   PhaseID,
                   {NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_HIGH,
                    NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_LOW})) {
      if (!Output.Bundle) {
        return outputStatus(NEVERC_STATUS_INVALID_STATE);
      }
      if (Error E = Output.Bundle->verifyAndPrepare()) {
        consumeError(std::move(E));
        return outputStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (samePluginInterfaceID(
                   PhaseID,
                   {NEVERC_PHASE_LINK_COMMIT_HIGH,
                    NEVERC_PHASE_LINK_COMMIT_LOW})) {
      if (!Output.Bundle)
        return outputStatus(NEVERC_STATUS_INVALID_STATE);
      auto Committed = Output.Bundle->commit();
      if (!Committed) {
        std::string Failure =
            toString(Committed.takeError()).str().str();
        if (Output.Bundle->summary().State !=
            neverc::OutputBundleState::Committed)
          return outputStatus(NEVERC_STATUS_PLUGIN_FAILURE);
        LateCommitFailure = std::move(Failure);
      }
    }
    return publish(std::move(Output), Definition->OutputArtifact,
                   Result);
  }

  Expected<LinkOutputArtifact>
  executePhase(NevercInterfaceID PhaseID,
               const LinkOutputArtifact &Input) {
    const PluginPhaseDefinition *Phase = Graph.find(PhaseID);
    if (!Phase)
      return outputError("output phase is not registered");
    auto InputHandle = Executor->createArtifactView(
        Task, Phase->InputArtifact, &Input, Input.Generation);
    if (!InputHandle)
      return InputHandle.takeError();
    NevercArtifactHandle Handle = *InputHandle;
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(
          Handle, PluginArtifactHandleKind);
    });
    PluginArtifactSlot Output(Artifacts.find(Phase->OutputArtifact));
    ActivePhase = PhaseID;
    ActiveArtifact = &Input;
    if (Error E = Service->attach(*this)) {
      ActivePhase = {};
      ActiveArtifact = nullptr;
      return std::move(E);
    }
    auto Detach = make_scope_exit([&] {
      Service->detach(Task.handle());
      ActivePhase = {};
      ActiveArtifact = nullptr;
    });
    if (Error E = Executor->execute(
            Task.session(), Task, PhaseID, route(), Handle, Output))
      return std::move(E);
    const auto *Published =
        static_cast<const LinkOutputArtifact *>(Output.payload());
    if (!Published || !Published->Image)
      return outputError("output phase published no image");
    return *Published;
  }

  Error notifyAfterCommit(const LinkOutputArtifact &Artifact) {
    NevercInterfaceID PhaseID{
        NEVERC_PHASE_LINK_AFTER_COMMIT_HIGH,
        NEVERC_PHASE_LINK_AFTER_COMMIT_LOW};
    const PluginPhaseDefinition *Phase = Graph.find(PhaseID);
    if (!Phase)
      return outputError("after-commit event is not registered");
    auto Handle = Executor->createArtifactView(
        Task, Phase->InputArtifact, &Artifact,
        Artifact.Generation);
    if (!Handle)
      return Handle.takeError();
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(
          *Handle, PluginArtifactHandleKind);
    });
    ActivePhase = PhaseID;
    ActiveArtifact = &Artifact;
    if (Error E = Service->attach(*this)) {
      ActivePhase = {};
      ActiveArtifact = nullptr;
      return std::move(E);
    }
    auto Detach = make_scope_exit([&] {
      Service->detach(Task.handle());
      ActivePhase = {};
      ActiveArtifact = nullptr;
    });
    return Executor->notify(Task.session(), Task, PhaseID, route(),
                            *Handle);
  }

  NevercStatus getGraph(
      const NevercPhaseFrame *, NevercArtifactHandle,
      NevercLinkPhaseGraphInfo *) override {
    return outputStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }

  NevercStatus publishGraph(
      const NevercPhaseFrame *, NevercLinkGraphHandle,
      NevercArtifactHandle *) override {
    return outputStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }

  NevercStatus getImage(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercLinkPhaseImageInfo *OutInfo) override {
    auto Payload = resolve(Frame, Artifact);
    if (!Payload) {
      consumeError(Payload.takeError());
      return outputStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    NevercLinkPhaseImageInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_LINK_PHASE_API_MAJOR,
                    NEVERC_LINK_PHASE_API_MINOR, 0};
    auto Capability = Executor->currentArtifactMutationCapability(Task);
    Value.Link =
        Capability
            ? &(*Payload)->Image->capabilityLinkAPI(*Executor, *Capability)
            : &(*Payload)->Image->readOnlyLinkAPI();
    Value.Image = (*Payload)->Image->handle();
    Value.Outputs =
        (*Payload)->Bundle ? (*Payload)->Bundle->handle()
                           : NevercOutputBundleHandle{};
    Value.State = (*Payload)->Image->state();
    Value.Generation = (*Payload)->Generation;
    return writeRecord(OutInfo, Value);
  }
};

LinkOutputPipeline::LinkOutputPipeline(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

LinkOutputPipeline::~LinkOutputPipeline() = default;

Expected<std::unique_ptr<LinkOutputPipeline>>
LinkOutputPipeline::create(PluginTaskContext &Task,
                           neverc::OutputCoordinator &Coordinator) {
  auto Service = findLinkPhaseProcessService(Task.processServices());
  if (!Service)
    return outputError("Link phase interface is not registered");
  auto Graph = PluginPhaseGraph::createBuiltinLinkGraph();
  if (!Graph)
    return Graph.takeError();
  auto State = std::make_unique<Impl>(
      Task, Coordinator, std::move(Service), std::move(*Graph));
  if (Error E = State->initialize())
    return std::move(E);
  return std::unique_ptr<LinkOutputPipeline>(
      new LinkOutputPipeline(std::move(State)));
}

Error LinkOutputPipeline::addObserver(
    StringRef PluginID,
    const NevercObserverDescriptor &Descriptor) {
  return State->Executor->addObserver(PluginID, Descriptor);
}

Error LinkOutputPipeline::addInterceptor(
    StringRef PluginID,
    const NevercInterceptorDescriptor &Descriptor) {
  return State->Executor->addInterceptor(PluginID, Descriptor);
}

Error LinkOutputPipeline::freeze() { return State->freeze(); }

Expected<LinkOutputResult> LinkOutputPipeline::execute(
    std::shared_ptr<PluginBinaryImage> Image, StringRef MainPath,
    ArrayRef<PluginLinkSideOutput> SideOutputs,
    neverc::OutputBundleTransaction::FaultInjector InjectFault) {
  if (!Image ||
      Image->state() != NEVERC_BINARY_IMAGE_CANDIDATE ||
      MainPath.empty())
    return outputError("candidate image and main path are required");
  if (Error E = State->freeze())
    return std::move(E);
  State->MainPath = MainPath.str();
  State->SideOutputs.assign(SideOutputs.begin(), SideOutputs.end());
  State->InjectFault = std::move(InjectFault);
  State->LateCommitFailure.clear();
  auto Clear = make_scope_exit([&] {
    State->MainPath.clear();
    State->SideOutputs.clear();
    State->InjectFault = {};
  });

  LinkOutputArtifact Current{std::move(Image), {}, 1};
  const NevercInterfaceID Phases[] = {
      {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
       NEVERC_PHASE_LINK_POST_EMIT_LOW},
      {NEVERC_PHASE_LINK_IMAGE_VERIFY_HIGH,
       NEVERC_PHASE_LINK_IMAGE_VERIFY_LOW},
      {NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_HIGH,
       NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_LOW},
      {NEVERC_PHASE_LINK_COMMIT_HIGH,
       NEVERC_PHASE_LINK_COMMIT_LOW},
  };
  for (NevercInterfaceID PhaseID : Phases) {
    auto Next = State->executePhase(PhaseID, Current);
    if (!Next) {
      Error Failure = Next.takeError();
      if (Current.Bundle &&
          Current.Bundle->summary().State ==
              neverc::OutputBundleState::Committed) {
        if (Error E = State->notifyAfterCommit(Current))
          Failure =
              joinErrors(std::move(Failure), std::move(E));
      } else if (Current.Bundle) {
        consumeError(Current.Bundle->abort());
      } else {
        consumeError(Current.Image->abort());
      }
      return std::move(Failure);
    }
    Current = std::move(*Next);
  }
  Error NotifyFailure = State->notifyAfterCommit(Current);
  if (!State->LateCommitFailure.empty()) {
    Error CommitFailure = outputError(
        "commit completed with a late durability failure: " +
        State->LateCommitFailure);
    if (NotifyFailure)
      return joinErrors(std::move(CommitFailure),
                        std::move(NotifyFailure));
    return std::move(CommitFailure);
  }
  if (NotifyFailure)
    return std::move(NotifyFailure);
  return LinkOutputResult{Current.Image, Current.Bundle,
                          Current.Bundle->summary()};
}

} // namespace neverc::plugin
