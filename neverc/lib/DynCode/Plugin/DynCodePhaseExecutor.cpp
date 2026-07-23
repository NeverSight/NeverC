#include "DynCodePhaseExecutor.h"
#include "DynCodePhaseCAPI.h"
#include "DynCodeProof.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Errc.h"
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

using namespace llvm;
using neverc::plugin::PluginArtifactOwnership;
using neverc::plugin::PluginArtifactRegistry;
using neverc::plugin::PluginArtifactSlot;
using neverc::plugin::PluginArtifactTypeDescriptor;
using neverc::plugin::PluginPhaseDefinition;
using neverc::plugin::PluginPhaseExecutor;
using neverc::plugin::PluginArtifactHandleKind;
using neverc::plugin::PluginTaskContext;
using neverc::plugin::samePluginInterfaceID;

namespace neverc {
namespace dyncode {
namespace {

struct DynCodePipelineArtifact {
  std::shared_ptr<DynCodePipelineValue> Value;
};

NevercStatus phaseStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

std::pair<uint64_t, uint64_t> idKey(NevercInterfaceID ID) {
  return {ID.High, ID.Low};
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

Error verifyValueArtifact(const void *Payload) {
  const auto *Artifact = static_cast<const DynCodePipelineArtifact *>(Payload);
  if (!Artifact || !Artifact->Value)
    return createStringError(errc::invalid_argument,
                             "dyncode phase artifact is null");
  return Error::success();
}

} // namespace

struct DynCodePhasePipeline::Impl final : DynCodePhaseRuntimeAccess {
  PluginTaskContext &Task;
  std::shared_ptr<DynCodePhaseProcessService> Service;
  DynCodePhaseRegistry Registry;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  NevercInterfaceID ActivePhase{};
  const DynCodePipelineValue *ActiveValue = nullptr;
  bool Frozen = false;
  std::string EmptyTriple;
  std::map<std::pair<uint64_t, uint64_t>, uint32_t> Executions;

  Impl(PluginTaskContext &TaskValue,
       std::shared_ptr<DynCodePhaseProcessService> ServiceValue,
       DynCodePhaseRegistry RegistryValue)
      : Task(TaskValue), Service(std::move(ServiceValue)),
        Registry(std::move(RegistryValue)) {}

  NevercTaskHandle taskHandle() const override { return Task.handle(); }

  Error initialize() {
    std::set<std::pair<uint64_t, uint64_t>> Registered;
    for (const DynCodePhaseDefinition &Phase : Registry.phases()) {
      for (NevercInterfaceID ID :
           {Phase.InputArtifact, Phase.OutputArtifact}) {
        if (!Registered.insert(idKey(ID)).second)
          continue;
        PluginArtifactTypeDescriptor Descriptor;
        Descriptor.ID = ID;
        Descriptor.Name = "dyncode.product." + std::to_string(ID.High) + "." +
                          std::to_string(ID.Low);
        Descriptor.Ownership = PluginArtifactOwnership::Owned;
        Descriptor.Destroy = [](void *Payload) {
          delete static_cast<DynCodePipelineArtifact *>(Payload);
        };
        Descriptor.Verify = verifyValueArtifact;
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
    // Only ordinary transitions get a default (no-op passthrough) Provider; the
    // four sealed gates run host-only and reject Providers by policy.
    for (const DynCodePhaseDefinition &Phase : Registry.phases()) {
      if (Phase.isSealedGate())
        continue;
      if (Error E = Executor->setBuiltinProvider(
              Phase.Phase,
              [this, ID = Phase.Phase](const NevercPhaseFrame *Frame,
                                       NevercPhaseResult *Result) {
                return runBuiltin(ID, Frame, Result);
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

  NevercPhaseRoute route() {
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    Route.TargetTriple = {EmptyTriple.data(), EmptyTriple.size()};
    Route.CPU = {EmptyTriple.data(), EmptyTriple.size()};
    Route.Features = {EmptyTriple.data(), EmptyTriple.size()};
    return Route;
  }

  bool validFrame(const NevercPhaseFrame *Frame) const {
    return Frame && sameHandle(Frame->Task, Task.handle()) &&
           samePluginInterfaceID(Frame->Phase, ActivePhase);
  }

  Error beginPhase(NevercInterfaceID Phase, const DynCodePipelineValue *Value) {
    ActivePhase = Phase;
    ActiveValue = Value;
    if (Error E = Service->attach(*this)) {
      ActivePhase = {};
      ActiveValue = nullptr;
      return E;
    }
    return Error::success();
  }

  void endPhase() {
    Service->detach(Task.handle());
    ActivePhase = {};
    ActiveValue = nullptr;
  }

  Expected<const DynCodePipelineArtifact *>
  resolve(const NevercPhaseFrame *Frame, NevercArtifactHandle Handle) {
    if (!validFrame(Frame))
      return createStringError(errc::invalid_argument,
                               "dyncode phase frame is out of scope");
    const DynCodePhaseDefinition *Phase = Registry.find(Frame->Phase);
    if (!Phase)
      return createStringError(errc::invalid_argument,
                               "dyncode phase is not registered");
    NevercInterfaceID Type{};
    if (sameHandle(Handle, Frame->Input))
      Type = Phase->InputArtifact;
    else if (sameHandle(Handle, Frame->CurrentOutput))
      Type = Phase->OutputArtifact;
    else
      return createStringError(errc::invalid_argument,
                               "dyncode phase artifact is out of scope");
    const void *Payload = nullptr;
    NevercStatus Status =
        Executor->resolveArtifactPayload(Task, Handle, Type, &Payload);
    if (!neverc_status_is_ok(Status) || !Payload)
      return createStringError(errc::invalid_argument,
                               "dyncode phase artifact is invalid");
    return static_cast<const DynCodePipelineArtifact *>(Payload);
  }

  NevercStatus publish(std::shared_ptr<DynCodePipelineValue> Value,
                       NevercInterfaceID Type, NevercPhaseResult *Result) {
    if (!Value || !Result)
      return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Artifact = new DynCodePipelineArtifact{std::move(Value)};
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
    const DynCodePhaseDefinition *Definition = Registry.find(PhaseID);
    if (!Input || !Definition) {
      if (!Input)
        consumeError(Input.takeError());
      return phaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    }
    // Default transform is an identity passthrough: advance the artifact type
    // and bump the generation so the published candidate is a fresh product.
    auto Next = std::make_shared<DynCodePipelineValue>(
        Definition->OutputArtifact, (*Input)->Value->generation() + 1);
    return publish(std::move(Next), Definition->OutputArtifact, Result);
  }

  Expected<std::shared_ptr<DynCodePipelineValue>>
  executeTransition(const DynCodePhaseDefinition &Definition,
                    const std::shared_ptr<DynCodePipelineValue> &Input) {
    DynCodePipelineArtifact View{Input};
    auto InputHandle = Executor->createArtifactView(
        Task, Definition.InputArtifact, &View, Input->generation());
    if (!InputHandle)
      return InputHandle.takeError();
    NevercArtifactHandle Handle = *InputHandle;
    auto Release = make_scope_exit(
        [&] { (void)Task.handles().release(Handle, PluginArtifactHandleKind); });
    PluginArtifactSlot Output(Artifacts.find(Definition.OutputArtifact));
    if (Error E = beginPhase(Definition.Phase, Input.get()))
      return std::move(E);
    auto End = make_scope_exit([&] { endPhase(); });
    if (Error E = Executor->execute(Task.session(), Task, Definition.Phase,
                                    route(), Handle, Output))
      return std::move(E);
    const auto *Published =
        static_cast<const DynCodePipelineArtifact *>(Output.payload());
    if (!Published || !Published->Value)
      return createStringError(errc::invalid_argument,
                               "dyncode transition published no value");
    return Published->Value;
  }

  Expected<std::shared_ptr<DynCodePipelineValue>>
  executeSealedGate(const DynCodePhaseDefinition &Definition,
                    const std::shared_ptr<DynCodePipelineValue> &Input) {
    // Sealed host gates (ir.final_verify, mir.final_verify, verify, commit) run
    // entirely inside the host: no plugin Provider/Interceptor/SKIP is possible
    // -- the core executor rejects those at registration by policy -- and no
    // full upstream replacement can bypass them because the host verifier runs
    // on whatever value reaches the gate.  The executor performs the structural chain
    // check and advances; later tasks attach the real IR/MIR/image verifiers and
    // issue the DynCodePhaseProof bound to this generation/route.
    if (!samePluginInterfaceID(Input->type(), Definition.InputArtifact))
      return createStringError(errc::invalid_argument,
                               "dyncode sealed gate received the wrong artifact");
    if (Task.checkCancelled().Code != NEVERC_STATUS_OK)
      return createStringError(inconvertibleErrorCode(),
                               "dyncode sealed gate task is cancelled");
    return std::make_shared<DynCodePipelineValue>(
        Definition.OutputArtifact, Input->generation() + 1);
  }

  NevercStatus getPhaseInfo(const NevercPhaseFrame *Frame,
                            NevercDynCodePhaseInfo *OutInfo) override {
    if (!validFrame(Frame) || !ActiveValue)
      return phaseStatus(NEVERC_STATUS_WRONG_SCOPE);
    NevercDynCodePhaseInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_DYNCODE_PHASE_API_MAJOR,
                    NEVERC_DYNCODE_PHASE_API_MINOR, 0};
    Value.Generation = ActiveValue->generation();
    return writeRecord(OutInfo, Value);
  }

  NevercStatus getRequest(const NevercPhaseFrame *, NevercArtifactHandle,
                          NevercDynCodeRequestHandle *) override {
    return phaseStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
  NevercStatus getImage(const NevercPhaseFrame *, NevercArtifactHandle,
                        NevercDynCodeImageHandle *) override {
    return phaseStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
  NevercStatus getReport(const NevercPhaseFrame *, NevercArtifactHandle,
                         NevercDynCodeReportHandle *) override {
    return phaseStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
};

DynCodePhasePipeline::DynCodePhasePipeline(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

DynCodePhasePipeline::~DynCodePhasePipeline() = default;

Expected<std::unique_ptr<DynCodePhasePipeline>>
DynCodePhasePipeline::create(PluginTaskContext &Task) {
  auto Service = findDynCodePhaseProcessService(Task.processServices());
  if (!Service)
    return createStringError(errc::not_supported,
                             "dyncode phase interface is not registered");
  auto Registry = DynCodePhaseRegistry::create();
  if (!Registry)
    return Registry.takeError();
  auto State =
      std::make_unique<Impl>(Task, std::move(Service), std::move(*Registry));
  if (Error E = State->initialize())
    return std::move(E);
  return std::unique_ptr<DynCodePhasePipeline>(
      new DynCodePhasePipeline(std::move(State)));
}

Error DynCodePhasePipeline::addObserver(
    StringRef PluginID, const NevercObserverDescriptor &Descriptor) {
  return State->Executor->addObserver(PluginID, Descriptor);
}

Error DynCodePhasePipeline::addInterceptor(
    StringRef PluginID, const NevercInterceptorDescriptor &Descriptor) {
  return State->Executor->addInterceptor(PluginID, Descriptor);
}

Error DynCodePhasePipeline::addProvider(
    StringRef PluginID, const NevercProviderDescriptor &Descriptor) {
  return State->Executor->addProvider(PluginID, Descriptor);
}

Error DynCodePhasePipeline::selectProvider(NevercInterfaceID Phase,
                                           StringRef PluginID) {
  return State->Executor->selectProvider(Phase, PluginID);
}

Error DynCodePhasePipeline::setBuiltinProvider(
    NevercInterfaceID Phase, PluginPhaseExecutor::BuiltinProvider Provider) {
  return State->Executor->setBuiltinProvider(Phase, std::move(Provider));
}

Error DynCodePhasePipeline::freeze() { return State->freeze(); }

Expected<std::shared_ptr<DynCodePipelineValue>>
DynCodePhasePipeline::execute(std::shared_ptr<DynCodePipelineValue> Input,
                              NevercInterfaceID ThroughPhase) {
  if (!Input)
    return createStringError(errc::invalid_argument,
                             "dyncode phase execution needs an input value");
  if (Error E = State->freeze())
    return std::move(E);

  ArrayRef<DynCodePhaseDefinition> Phases = State->Registry.phases();
  const bool RunAll = ThroughPhase.High == 0 && ThroughPhase.Low == 0;
  std::shared_ptr<DynCodePipelineValue> Current = std::move(Input);
  for (const DynCodePhaseDefinition &Definition : Phases) {
    if (!samePluginInterfaceID(Current->type(), Definition.InputArtifact))
      return createStringError(
          errc::invalid_argument,
          "dyncode phase input artifact does not match the chain");
    uint32_t &Count = State->Executions[idKey(Definition.Phase)];
    ++Count;
    if (Count > Definition.MaximumReruns + 1)
      return createStringError(
          errc::invalid_argument,
          "dyncode phase did not converge within its rerun budget");
    auto Output = Definition.isSealedGate()
                      ? State->executeSealedGate(Definition, Current)
                      : State->executeTransition(Definition, Current);
    if (!Output)
      return Output.takeError();
    Current = std::move(*Output);
    if (!RunAll && samePluginInterfaceID(Definition.Phase, ThroughPhase))
      break;
  }
  return Current;
}

const DynCodePhaseRegistry &DynCodePhasePipeline::registry() const {
  return State->Registry;
}

uint32_t DynCodePhasePipeline::rerunCount(NevercInterfaceID Phase) const {
  auto It = State->Executions.find(idKey(Phase));
  return It == State->Executions.end() || It->second == 0 ? 0 : It->second - 1;
}

} // namespace dyncode
} // namespace neverc
