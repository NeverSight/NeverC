#include "LinkPhaseExecutor.h"
#include "ComdatSelectionProvider.h"
#include "ICFProvider.h"
#include "LayoutVerifier.h"
#include "LinkLayoutProvider.h"
#include "LinkPhaseCAPI.h"
#include "LivenessVerifier.h"
#include "RelaxationExecutor.h"
#include "RelocationProvider.h"
#include "RelocationVerifier.h"
#include "ResolutionVerifier.h"
#include "SectionGCProvider.h"
#include "SymbolResolutionProvider.h"
#include "SynthesisVerifier.h"
#include "SyntheticSectionProvider.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

struct LinkGraphArtifact {
  std::shared_ptr<PluginLinkGraph> Graph;
};

NevercStatus phaseStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

template <typename T> NevercStatus writeRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return phaseStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value) ? phaseStatus(NEVERC_STATUS_ABI_MISMATCH)
                                  : neverc_status_ok();
}

Error verifyGraphArtifact(const void *Payload) {
  const auto *Artifact = static_cast<const LinkGraphArtifact *>(Payload);
  if (!Artifact || !Artifact->Graph)
    return createStringError(errc::invalid_argument,
                             "LinkGraph phase artifact is null");
  if (Error E = verifyPluginLinkGraph(*Artifact->Graph))
    return E;
  switch (Artifact->Graph->state()) {
  case NEVERC_LINK_STATE_SYMBOLS_RESOLVED:
    return verifyLinkSymbolResolution(*Artifact->Graph);
  case NEVERC_LINK_STATE_COMDAT_SELECTED:
    if (Error E = verifyLinkSymbolResolution(*Artifact->Graph))
      return E;
    return verifyLinkComdatSelection(*Artifact->Graph);
  case NEVERC_LINK_STATE_GC_COMPLETE:
    return verifyLinkLiveness(*Artifact->Graph);
  case NEVERC_LINK_STATE_ICF_COMPLETE:
    return verifyLinkFolding(*Artifact->Graph);
  case NEVERC_LINK_STATE_SYNTHETICS_READY:
    return verifyLinkSynthetics(*Artifact->Graph);
  case NEVERC_LINK_STATE_THUNKS_RELAXED:
    return verifyLinkRelaxation(*Artifact->Graph);
  case NEVERC_LINK_STATE_LAYOUT_COMPLETE:
    return verifyLinkLayout(*Artifact->Graph);
  case NEVERC_LINK_STATE_RELOCATIONS_APPLIED:
    return verifyLinkRelocations(*Artifact->Graph);
  default:
    return Error::success();
  }
}

std::pair<uint64_t, uint64_t> idKey(NevercInterfaceID ID) {
  return {ID.High, ID.Low};
}

} // namespace

struct LinkPhasePipeline::Impl final : LinkPhaseRuntimeAccess {
  PluginTaskContext &Task;
  std::shared_ptr<LinkPhaseProcessService> Service;
  LinkPhaseRegistry Registry;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  NevercInterfaceID ActivePhase{};
  std::unique_ptr<LinkGraphPluginBridge> ActiveBridge;
  const LinkGraphArtifact *ActiveArtifact = nullptr;
  bool ActiveMutationAllowed = false;
  bool Frozen = false;
  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::map<std::pair<uint64_t, uint64_t>, uint32_t> Executions;
  std::map<std::pair<uint64_t, uint64_t>,
           LinkPhasePipeline::BuiltinGraphProvider>
      GraphProviders;
  std::string LastNativeProviderError;

  Impl(PluginTaskContext &TaskValue,
       std::shared_ptr<LinkPhaseProcessService> ServiceValue,
       LinkPhaseRegistry RegistryValue)
      : Task(TaskValue), Service(std::move(ServiceValue)),
        Registry(std::move(RegistryValue)) {}

  NevercTaskHandle taskHandle() const override { return Task.handle(); }

  Error initialize() {
    std::set<std::pair<uint64_t, uint64_t>> Registered;
    for (size_t Index = 0; Index != Registry.graph().size(); ++Index) {
      const PluginPhaseDefinition &Phase = Registry.graph().phaseAt(Index);
      for (NevercInterfaceID ID : {Phase.InputArtifact, Phase.OutputArtifact}) {
        if (!Registered.insert(idKey(ID)).second)
          continue;
        PluginArtifactTypeDescriptor Descriptor;
        Descriptor.ID = ID;
        Descriptor.Name = "link.product." + std::to_string(ID.High) + "." +
                          std::to_string(ID.Low);
        Descriptor.Ownership = PluginArtifactOwnership::Owned;
        Descriptor.Destroy = [](void *Payload) {
          delete static_cast<LinkGraphArtifact *>(Payload);
        };
        Descriptor.Verify = verifyGraphArtifact;
        auto Type = Artifacts.registerType(std::move(Descriptor));
        if (!Type)
          return Type.takeError();
      }
    }
    if (Error E = Artifacts.freeze())
      return E;

    Executor =
        std::make_unique<PluginPhaseExecutor>(Registry.graph(), Artifacts);
    if (Error E = Executor->importSessionRegistrations(Task.session()))
      return E;
    for (const LinkTransitionDefinition &Transition : Registry.transitions()) {
      if (Error E = Executor->setBuiltinProvider(
              Transition.Phase,
              [this, Phase = Transition.Phase](const NevercPhaseFrame *Frame,
                                               NevercPhaseResult *Result) {
                return runBuiltin(Phase, Frame, Result);
              }))
        return E;
    }
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

  NevercPhaseRoute route(const PluginLinkGraph &Graph) {
    NevercTargetKey Target = Graph.targetKey();
    TargetTriple.assign(Target.RawTriple.Data ? Target.RawTriple.Data : "",
                        Target.RawTriple.Length);
    CPU.assign(Target.CPU.Data ? Target.CPU.Data : "", Target.CPU.Length);
    Features.clear();
    const auto *Bytes = reinterpret_cast<const uint8_t *>(Target.Features.Data);
    for (uint64_t Index = 0; Bytes && Index != Target.Features.Count; ++Index) {
      const auto *Feature = reinterpret_cast<const NevercStringView *>(
          Bytes + Index * Target.Features.ElementStride);
      if (!Features.empty())
        Features.push_back(',');
      Features.append(Feature->Data ? Feature->Data : "", Feature->Length);
    }
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    Route.TargetTriple = {TargetTriple.data(), TargetTriple.size()};
    Route.CPU = {CPU.data(), CPU.size()};
    Route.Features = {Features.data(), Features.size()};
    return Route;
  }

  bool validFrame(const NevercPhaseFrame *Frame) const {
    return Frame && sameHandle(Frame->Task, Task.handle()) &&
           samePluginInterfaceID(Frame->Phase, ActivePhase);
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
    ActiveBridge.reset();
    ActiveArtifact = nullptr;
    ActiveMutationAllowed = false;
    Service->detach(Task.handle());
    ActivePhase = {};
  }

  Expected<const LinkGraphArtifact *> resolve(const NevercPhaseFrame *Frame,
                                              NevercArtifactHandle Handle) {
    if (!validFrame(Frame))
      return createStringError(errc::invalid_argument,
                               "Link phase frame is out of scope");
    const PluginPhaseDefinition *Phase = Registry.graph().find(Frame->Phase);
    if (!Phase)
      return createStringError(errc::invalid_argument,
                               "Link phase is not registered");
    NevercInterfaceID Type{};
    if (sameHandle(Handle, Frame->Input))
      Type = Phase->InputArtifact;
    else if (sameHandle(Handle, Frame->CurrentOutput))
      Type = Phase->OutputArtifact;
    else
      return createStringError(errc::invalid_argument,
                               "Link phase artifact is out of scope");
    const void *Payload = nullptr;
    NevercStatus Status =
        Executor->resolveArtifactPayload(Task, Handle, Type, &Payload);
    if (!neverc_status_is_ok(Status) || !Payload)
      return createStringError(errc::invalid_argument,
                               "Link phase artifact is invalid");
    return static_cast<const LinkGraphArtifact *>(Payload);
  }

  NevercStatus publish(std::shared_ptr<PluginLinkGraph> Graph,
                       NevercInterfaceID Type, NevercPhaseResult *Result) {
    if (!Graph || !Result)
      return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Artifact = new LinkGraphArtifact{std::move(Graph)};
    auto Candidate = Executor->createCandidate(Task, Type, Artifact);
    if (!Candidate) {
      delete Artifact;
      consumeError(Candidate.takeError());
      return phaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result->Action = NEVERC_PHASE_REPLACE;
    Result->Output = *Candidate;
    return neverc_status_ok();
  }

  NevercStatus runBuiltin(NevercInterfaceID PhaseID,
                          const NevercPhaseFrame *Frame,
                          NevercPhaseResult *Result) {
    auto Input = resolve(Frame, Frame ? Frame->Input : NevercArtifactHandle{});
    const LinkTransitionDefinition *Transition =
        Registry.findTransition(PhaseID);
    const PluginPhaseDefinition *Definition = Registry.graph().find(PhaseID);
    if (!Input || !Transition || !Definition) {
      if (!Input)
        consumeError(Input.takeError());
      return phaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    std::shared_ptr<PluginLinkGraph> Copy;
    auto Native = GraphProviders.find(idKey(PhaseID));
    if (Native != GraphProviders.end()) {
      auto Provided = Native->second(*(*Input)->Graph);
      if (!Provided) {
        LastNativeProviderError = toString(Provided.takeError()).str().str();
        return phaseStatus(NEVERC_STATUS_PLUGIN_FAILURE);
      }
      Copy = std::move(*Provided);
      if (!Copy)
        return phaseStatus(NEVERC_STATUS_INVALID_STATE);
      if (Copy->state() == Transition->InputState)
        Copy->setState(Transition->OutputState);
      if (Copy->state() != Transition->OutputState)
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    } else {
      Copy = std::make_shared<PluginLinkGraph>(*(*Input)->Graph);
    }
    if (Native != GraphProviders.end()) {
      // The native provider has already produced the transition output.
    } else if (Transition->OutputState == NEVERC_LINK_STATE_SYMBOLS_RESOLVED) {
      auto Records = resolveLinkSymbols(*Copy);
      if (!Records) {
        consumeError(Records.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState == NEVERC_LINK_STATE_COMDAT_SELECTED) {
      auto Records = selectLinkComdats(*Copy);
      if (!Records) {
        consumeError(Records.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState == NEVERC_LINK_STATE_GC_COMPLETE) {
      auto Records = markLiveLinkAtoms(*Copy);
      if (!Records) {
        consumeError(Records.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState == NEVERC_LINK_STATE_ICF_COMPLETE) {
      auto Records = foldIdenticalLinkAtoms(*Copy);
      if (!Records) {
        consumeError(Records.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState == NEVERC_LINK_STATE_SYNTHETICS_READY) {
      auto Records = materializeLinkSynthetics(*Copy);
      if (!Records) {
        consumeError(Records.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState == NEVERC_LINK_STATE_THUNKS_RELAXED) {
      auto Relaxed = executeLinkRelaxation(*Copy);
      if (!Relaxed) {
        consumeError(Relaxed.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState == NEVERC_LINK_STATE_LAYOUT_COMPLETE) {
      auto Layout = layoutLinkGraph(*Copy);
      if (!Layout) {
        consumeError(Layout.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Transition->OutputState ==
               NEVERC_LINK_STATE_RELOCATIONS_APPLIED) {
      auto Relocations = applyLinkRelocations(*Copy);
      if (!Relocations) {
        consumeError(Relocations.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    } else if (Copy->state() == Transition->InputState) {
      Copy->setState(Transition->OutputState);
    }
    return publish(std::move(Copy), Definition->OutputArtifact, Result);
  }

  Expected<std::shared_ptr<PluginLinkGraph>>
  executePhase(const LinkTransitionDefinition &Transition,
               const std::shared_ptr<PluginLinkGraph> &Input) {
    const PluginPhaseDefinition *Phase =
        Registry.graph().find(Transition.Phase);
    if (!Phase)
      return createStringError(errc::invalid_argument,
                               "Link transition is not registered");
    LinkGraphArtifact View{Input};
    auto InputHandle = Executor->createArtifactView(Task, Phase->InputArtifact,
                                                    &View, Input->generation());
    if (!InputHandle)
      return InputHandle.takeError();
    NevercArtifactHandle Handle = *InputHandle;
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(Handle, PluginArtifactHandleKind);
    });
    PluginArtifactSlot Output(Artifacts.find(Phase->OutputArtifact));
    if (Error E = beginPhase(Transition.Phase))
      return std::move(E);
    auto End = make_scope_exit([&] { endPhase(); });
    if (Error E = Executor->execute(Task.session(), Task, Transition.Phase,
                                    route(*Input), Handle, Output))
      return std::move(E);
    const auto *Published =
        static_cast<const LinkGraphArtifact *>(Output.payload());
    if (!Published || !Published->Graph)
      return createStringError(errc::invalid_argument,
                               "Link transition published no LinkGraph");
    return Published->Graph;
  }

  NevercStatus getGraph(const NevercPhaseFrame *Frame,
                        NevercArtifactHandle Artifact,
                        NevercLinkPhaseGraphInfo *OutInfo) override {
    auto Payload = resolve(Frame, Artifact);
    if (!Payload) {
      consumeError(Payload.takeError());
      return phaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    const bool MutationAllowed =
        Task.processServices().currentCallbackHasSuffix(Task, "/interceptor") ||
        Task.processServices().currentCallbackHasSuffix(Task, "/provider");
    if (ActiveArtifact != *Payload ||
        ActiveMutationAllowed != MutationAllowed) {
      ActiveBridge = std::make_unique<LinkGraphPluginBridge>(
          Task, *(*Payload)->Graph, MutationAllowed);
      ActiveArtifact = *Payload;
      ActiveMutationAllowed = MutationAllowed;
    }
    auto GraphHandle = ActiveBridge->graph();
    if (!GraphHandle) {
      consumeError(GraphHandle.takeError());
      return phaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    NevercLinkPhaseGraphInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_LINK_PHASE_API_MAJOR,
                    NEVERC_LINK_PHASE_API_MINOR, 0};
    Value.Link = &ActiveBridge->api();
    Value.Graph = *GraphHandle;
    Value.State = (*Payload)->Graph->state();
    Value.Generation = (*Payload)->Graph->generation();
    if (Value.State >= NEVERC_LINK_STATE_LAYOUT_COMPLETE) {
      const PluginPhaseDefinition *Definition =
          Registry.graph().find(Frame->Phase);
      NevercInterfaceID ArtifactType{};
      if (Definition)
        ArtifactType = sameHandle(Artifact, Frame->Input)
                           ? Definition->InputArtifact
                           : Definition->OutputArtifact;
      auto Proof = ActiveBridge->issueProof(Value.State, ArtifactType);
      if (!Proof) {
        consumeError(Proof.takeError());
        return phaseStatus(NEVERC_STATUS_VERIFICATION_FAILED);
      }
      Value.Proof = *Proof;
    }
    return writeRecord(OutInfo, Value);
  }

  NevercStatus publishGraph(const NevercPhaseFrame *Frame,
                            NevercLinkGraphHandle Graph,
                            NevercArtifactHandle *OutArtifact) override {
    if (!validFrame(Frame) || !OutArtifact || !ActiveBridge ||
        ActiveBridge->hasActiveMutation() || !ActiveMutationAllowed)
      return phaseStatus(NEVERC_STATUS_INVALID_STATE);
    *OutArtifact = {};
    PluginLinkGraph *Resolved = nullptr;
    NevercStatus Status = ActiveBridge->resolveGraph(Graph, &Resolved);
    if (!neverc_status_is_ok(Status))
      return Status;
    const PluginPhaseDefinition *Definition =
        Registry.graph().find(Frame->Phase);
    const LinkTransitionDefinition *Transition =
        Registry.findTransition(Frame->Phase);
    if (!Definition || !Transition)
      return phaseStatus(NEVERC_STATUS_INVALID_STATE);
    auto Copy = std::make_shared<PluginLinkGraph>(*Resolved);
    if (Copy->state() == Transition->InputState)
      Copy->setState(Transition->OutputState);
    auto *Artifact = new LinkGraphArtifact{std::move(Copy)};
    auto Candidate =
        Executor->createCandidate(Task, Definition->OutputArtifact, Artifact);
    if (!Candidate) {
      delete Artifact;
      consumeError(Candidate.takeError());
      return phaseStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutArtifact = *Candidate;
    return neverc_status_ok();
  }

  NevercStatus getImage(const NevercPhaseFrame *, NevercArtifactHandle,
                        NevercLinkPhaseImageInfo *) override {
    return phaseStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
};

LinkPhasePipeline::LinkPhasePipeline(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

LinkPhasePipeline::~LinkPhasePipeline() = default;

Expected<std::unique_ptr<LinkPhasePipeline>>
LinkPhasePipeline::create(PluginTaskContext &Task) {
  auto Service = findLinkPhaseProcessService(Task.processServices());
  if (!Service)
    return createStringError(errc::not_supported,
                             "Link phase interface is not registered");
  auto Registry = LinkPhaseRegistry::create();
  if (!Registry)
    return Registry.takeError();
  auto State =
      std::make_unique<Impl>(Task, std::move(Service), std::move(*Registry));
  if (Error E = State->initialize())
    return std::move(E);
  return std::unique_ptr<LinkPhasePipeline>(
      new LinkPhasePipeline(std::move(State)));
}

Error LinkPhasePipeline::addObserver(
    StringRef PluginID, const NevercObserverDescriptor &Descriptor) {
  return State->Executor->addObserver(PluginID, Descriptor);
}

Error LinkPhasePipeline::addInterceptor(
    StringRef PluginID, const NevercInterceptorDescriptor &Descriptor) {
  return State->Executor->addInterceptor(PluginID, Descriptor);
}

Error LinkPhasePipeline::addProvider(
    StringRef PluginID, const NevercProviderDescriptor &Descriptor) {
  return State->Executor->addProvider(PluginID, Descriptor);
}

Error LinkPhasePipeline::selectProvider(NevercInterfaceID Phase,
                                        StringRef PluginID) {
  return State->Executor->selectProvider(Phase, PluginID);
}

Error LinkPhasePipeline::setBuiltinProvider(
    NevercInterfaceID Phase, PluginPhaseExecutor::BuiltinProvider Provider) {
  return State->Executor->setBuiltinProvider(Phase, std::move(Provider));
}

Error LinkPhasePipeline::setBuiltinGraphProvider(
    NevercInterfaceID Phase, BuiltinGraphProvider Provider) {
  if (State->Frozen)
    return createStringError(
        errc::invalid_argument,
        "cannot set a native LinkGraph Provider after phase plan freeze");
  if (!Provider || !State->Registry.findTransition(Phase))
    return createStringError(
        errc::invalid_argument,
        "native LinkGraph Provider references an invalid transition");
  State->GraphProviders[idKey(Phase)] = std::move(Provider);
  return Error::success();
}

Error LinkPhasePipeline::freeze() { return State->freeze(); }

Expected<std::shared_ptr<PluginLinkGraph>>
LinkPhasePipeline::execute(std::shared_ptr<PluginLinkGraph> Input,
                           NevercLinkState ThroughState) {
  if (!Input || ThroughState > NEVERC_LINK_STATE_IMAGE_EMITTED)
    return createStringError(errc::invalid_argument,
                             "Link phase execution bounds are invalid");
  if (Error E = State->freeze())
    return std::move(E);
  std::shared_ptr<PluginLinkGraph> Current = std::move(Input);
  while (Current->state() < ThroughState) {
    const LinkTransitionDefinition *Transition = nullptr;
    for (const LinkTransitionDefinition &Candidate :
         State->Registry.transitions())
      if (Candidate.InputState == Current->state()) {
        Transition = &Candidate;
        break;
      }
    if (!Transition)
      return createStringError(
          errc::invalid_argument,
          "LinkGraph state has no registered outgoing transition");
    uint32_t &Count = State->Executions[idKey(Transition->Phase)];
    ++Count;
    if (Count > Transition->MaximumReruns + 1)
      return createStringError(
          errc::invalid_argument,
          "Link transition did not converge within its rerun budget");
    State->LastNativeProviderError.clear();
    auto Output = State->executePhase(*Transition, Current);
    if (!Output) {
      Error ExecutionError = Output.takeError();
      if (State->LastNativeProviderError.empty())
        return std::move(ExecutionError);
      return joinErrors(std::move(ExecutionError),
                        createStringError(errc::invalid_argument,
                                          "native LinkGraph Provider failed: " +
                                              State->LastNativeProviderError));
    }
    if ((*Output)->state() > Transition->OutputState)
      return createStringError(
          errc::invalid_argument,
          "Link transition published a graph beyond its output state");
    Current = std::move(*Output);
  }
  if (Current->state() != ThroughState)
    return createStringError(
        errc::invalid_argument,
        "Link phase execution overshot the requested state");
  return Current;
}

bool LinkPhasePipeline::requiresNativeProjection() const {
  for (const LinkTransitionDefinition &Transition :
       State->Registry.transitions())
    if (State->Executor->hasBindings(Transition.Phase))
      return true;
  return false;
}

const LinkPhaseRegistry &LinkPhasePipeline::registry() const {
  return State->Registry;
}

uint32_t LinkPhasePipeline::rerunCount(NevercInterfaceID Phase) const {
  auto It = State->Executions.find(idKey(Phase));
  return It == State->Executions.end() || It->second == 0 ? 0 : It->second - 1;
}

} // namespace neverc::plugin
