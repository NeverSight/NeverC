#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <thread>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error executionError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus executionStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool validHeader(const NevercABITableHeader &Header, uint64_t RequiredSize) {
  return Header.StructSize >= RequiredSize &&
         Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Header.Minor <= NEVERC_PLUGIN_ABI_MINOR && Header.Flags == 0;
}

Expected<std::string> copyString(NevercStringView View, const Twine &Field,
                                 bool AllowEmpty) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return executionError(Field + " has an invalid string view");
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0') ||
      !json::isUTF8(Text))
    return executionError(Field + " is not valid UTF-8");
  return Text.str();
}

bool validResult(const NevercPhaseResult &Result) {
  constexpr uint64_t Required =
      offsetof(NevercPhaseResult, Proof) + sizeof(NevercPhaseResult::Proof);
  return validHeader(Result.Header, Required) && Result.Reserved == 0 &&
         Result.Action <= NEVERC_PHASE_SKIP;
}

NevercPhaseResult emptyResult() {
  NevercPhaseResult Result{};
  Result.Header = {sizeof(Result), NEVERC_PLUGIN_ABI_MAJOR,
                   NEVERC_PLUGIN_ABI_MINOR, 0};
  Result.Action = NEVERC_PHASE_CONTINUE;
  return Result;
}

bool nonnull(NevercHandle Handle) {
  return Handle.Owner != 0 && Handle.Value != 0;
}

bool canonicalNull(NevercHandle Handle) {
  return Handle.Owner == 0 && Handle.Value == 0;
}

bool matchingString(StringRef Constraint, NevercStringView Actual) {
  if (Constraint.empty())
    return true;
  if (!Actual.Data && Actual.Length != 0)
    return false;
  return Constraint == StringRef(Actual.Data ? Actual.Data : "",
                                 static_cast<size_t>(Actual.Length));
}

bool equalString(StringRef Expected, NevercStringView Actual) {
  if (!Actual.Data && Actual.Length != 0)
    return false;
  return Expected == StringRef(Actual.Data ? Actual.Data : "",
                               static_cast<size_t>(Actual.Length));
}

struct RouteIdentity {
  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;
  uint32_t ExecutionLevel = 0;
};

Expected<RouteIdentity> copyRouteIdentity(const NevercPhaseRoute &Route) {
  if (!validHeader(Route.Header, sizeof(Route)) || Route.Reserved != 0)
    return executionError("phase route is invalid");
  auto TargetTriple = copyString(Route.TargetTriple, "target triple", true);
  if (!TargetTriple)
    return TargetTriple.takeError();
  auto CPU = copyString(Route.CPU, "CPU", true);
  if (!CPU)
    return CPU.takeError();
  auto Features = copyString(Route.Features, "features", true);
  if (!Features)
    return Features.takeError();
  auto ObjectFormat = copyString(Route.ObjectFormat, "object format", true);
  if (!ObjectFormat)
    return ObjectFormat.takeError();
  return RouteIdentity{std::move(*TargetTriple), std::move(*CPU),
                       std::move(*Features), std::move(*ObjectFormat),
                       Route.ExecutionLevel};
}

bool sameRoute(const RouteIdentity &Expected, const NevercPhaseRoute &Actual) {
  if (!validHeader(Actual.Header, sizeof(Actual)) || Actual.Reserved != 0)
    return false;
  return equalString(Expected.TargetTriple, Actual.TargetTriple) &&
         equalString(Expected.CPU, Actual.CPU) &&
         equalString(Expected.Features, Actual.Features) &&
         equalString(Expected.ObjectFormat, Actual.ObjectFormat) &&
         Expected.ExecutionLevel == Actual.ExecutionLevel;
}

struct ActivePhaseInvocation {
  const PluginTaskContext *Task = nullptr;
  NevercInterfaceID Phase{};
};

thread_local std::vector<ActivePhaseInvocation> ActivePhases;

} // namespace

struct PluginPhaseExecutor::ObserverBinding {
  std::string PluginID;
  NevercObserverDescriptor Descriptor{};
};

struct PluginPhaseExecutor::InterceptorBinding {
  std::string PluginID;
  NevercInterceptorDescriptor Descriptor{};
};

struct PluginPhaseExecutor::ProviderBinding {
  std::string PluginID;
  NevercProviderDescriptor Descriptor{};
  std::string ProviderID;
  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;
};

struct PluginPhaseExecutor::BuiltinBinding {
  NevercInterfaceID Phase{};
  BuiltinProvider Provider;
};

struct PluginPhaseExecutor::Selection {
  NevercInterfaceID Phase{};
  std::string PluginID;
};

struct PluginPhaseExecutor::FallbackSelection {
  NevercInterfaceID Phase{};
};

struct PluginPhaseExecutor::CandidateState {
  std::shared_ptr<const PluginArtifactType> Type;
  void *Payload = nullptr;
  uint64_t Generation = 0;
  bool IsCandidate = false;
  bool Consumed = false;
};

struct PluginPhaseExecutor::ProofState {
  NevercInterfaceID Phase{};
  NevercArtifactHandle Input{};
  std::shared_ptr<const PluginArtifactType> InputType;
  const void *InputPayload = nullptr;
  uint64_t InputGeneration = 0;
  std::shared_ptr<const PluginArtifactType> OutputType;
  const void *OutputPayload = nullptr;
  uint64_t OutputGeneration = 0;
  RouteIdentity Route;
};

struct PluginPhaseExecutor::ChainContext {
  PluginPhaseExecutor &Executor;
  PluginSession &Session;
  PluginTaskContext &Task;
  const PluginPhaseDefinition &Phase;
  NevercPhaseFrame &Frame;
  std::vector<const InterceptorBinding *> Interceptors;
  const ProviderBinding *Provider = nullptr;
  const BuiltinBinding *Builtin = nullptr;
  bool AllowRecoverableFallback = false;
  std::string Failure;
  NevercStatusCode FailureCode = NEVERC_STATUS_PLUGIN_FAILURE;
};

struct PluginPhaseExecutor::ContinuationContext {
  ChainContext *Chain = nullptr;
  size_t NextIndex = 0;
  uint64_t Generation = 0;
  std::thread::id Thread;
  NevercPhaseResult DownstreamResult{};
  std::atomic<bool> Active{true};
  std::atomic<bool> Called{false};
  bool HasDownstreamResult = false;
};

namespace {

void failChain(PluginPhaseExecutor::ChainContext &Context,
               NevercStatusCode Code, const Twine &Message) {
  if (Context.Failure.empty()) {
    Context.Failure = Message.str();
    Context.FailureCode = Code;
  }
}

bool discardCandidate(PluginTaskContext &Task, NevercArtifactHandle Handle) {
  if (!nonnull(Handle))
    return true;
  void *Raw = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginArtifactHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return false;
  auto *State = static_cast<PluginPhaseExecutor::CandidateState *>(Raw);
  if (!State->IsCandidate || State->Consumed)
    return false;
  return Task.handles().release(Handle, PluginArtifactHandleKind).Code ==
         NEVERC_STATUS_OK;
}

bool validateCallbackStatus(PluginPhaseExecutor::ChainContext &Context,
                            StringRef Kind, StringRef PluginID,
                            NevercStatus Status) {
  constexpr NevercStatusFlags KnownFlags =
      NEVERC_STATUS_FLAG_RECOVERABLE |
      NEVERC_STATUS_FLAG_OUTPUT_ALREADY_COMMITTED |
      NEVERC_STATUS_FLAG_OUTPUT_MAY_BE_PARTIAL |
      NEVERC_STATUS_FLAG_OUTPUT_RECOVERY_REQUIRED |
      NEVERC_STATUS_FLAG_DURABILITY_UNCONFIRMED;
  if ((Status.Flags & ~KnownFlags) != 0) {
    failChain(Context, NEVERC_STATUS_INVALID_DESCRIPTOR,
              Kind + " callback for plugin '" + PluginID +
                  "' returned unknown status flags");
    return false;
  }
  if (Status.Code == NEVERC_STATUS_OK) {
    if (Status.Flags == 0 && Status.Detail == 0)
      return true;
    failChain(Context, NEVERC_STATUS_INVALID_DESCRIPTOR,
              Kind + " callback for plugin '" + PluginID +
                  "' returned an invalid success status");
    return false;
  }
  if (Status.Code < NEVERC_STATUS_INVALID_ARGUMENT ||
      Status.Code > NEVERC_STATUS_NOT_FOUND) {
    failChain(Context, NEVERC_STATUS_INVALID_DESCRIPTOR,
              Kind + " callback for plugin '" + PluginID +
                  "' returned an unknown status");
    return false;
  }
  // Detail is the host-issued locator for where inside the callback the failure
  // came from, as the object reader/writer providers already report; without it
  // a builtin provider failure is a bare code with nothing to trace it to.
  std::string Message = (Kind + " callback for plugin '" + PluginID +
                         "' failed with status code " + Twine(Status.Code))
                            .str();
  if (Status.Detail != 0)
    Message += " (detail " + std::to_string(Status.Detail) + ")";
  failChain(Context, Status.Code, Message);
  return false;
}

NevercStatus
invokePluginCallback(PluginPhaseExecutor::ChainContext &Context,
                     StringRef PluginID, StringRef Name,
                     std::function<NevercStatus()> Callback,
                     uint64_t *OutDiagnosticTransactionID = nullptr,
                     bool MayReplaceArtifact = false) {
  auto Result = Context.Task.invokeCallback(
      PluginID, Name, std::move(Callback), true, OutDiagnosticTransactionID,
      true, MayReplaceArtifact ? &Context.Executor : nullptr);
  if (!Result) {
    failChain(Context, NEVERC_STATUS_PLUGIN_FAILURE,
              toString(Result.takeError()));
    return executionStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  return *Result;
}

bool providerMatches(const PluginPhaseExecutor::ProviderBinding &Provider,
                     const NevercPhaseRoute &Route) {
  const NevercPhaseRoute &Constraint = Provider.Descriptor.Route;
  return matchingString(Provider.TargetTriple, Route.TargetTriple) &&
         matchingString(Provider.CPU, Route.CPU) &&
         matchingString(Provider.Features, Route.Features) &&
         matchingString(Provider.ObjectFormat, Route.ObjectFormat) &&
         (Constraint.ExecutionLevel == 0 ||
          Constraint.ExecutionLevel == Route.ExecutionLevel);
}

NevercStatus writeResult(NevercPhaseResult *OutResult,
                         const NevercPhaseResult &Value) {
  if (!OutResult || OutResult->Header.StructSize < sizeof(uint32_t))
    return executionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  uint32_t Capacity = OutResult->Header.StructSize;
  NevercPhaseResult Produced = Value;
  Produced.Header.StructSize = sizeof(Produced);
  size_t Bytes = std::min<size_t>(Capacity, sizeof(Produced));
  std::memcpy(OutResult, &Produced, Bytes);
  OutResult->Header.StructSize = sizeof(Produced);
  if (Capacity < sizeof(NevercPhaseResult))
    return executionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

} // namespace

PluginPhaseExecutor::PluginPhaseExecutor(
    const PluginPhaseGraph &GraphValue,
    const PluginArtifactRegistry &ArtifactsValue)
    : Graph(GraphValue), Artifacts(ArtifactsValue) {}

PluginPhaseExecutor::~PluginPhaseExecutor() = default;

Error PluginPhaseExecutor::validatePhaseRegistration(
    NevercInterfaceID Phase, NevercPhasePolicy RequiredPolicy,
    StringRef Kind) const {
  if (!Graph.isFinalized())
    return executionError(
        "cannot register phase callbacks before graph finalization");
  const PluginPhaseDefinition *Definition = Graph.find(Phase);
  if (!Definition)
    return executionError(Kind + " references an unknown phase");
  if ((Definition->Policy & NEVERC_PHASE_SEALED_HOST_GATE) != 0 &&
      Kind != "observer")
    return executionError("sealed phase '" + Definition->CanonicalName +
                          "' rejects " + Kind + " registration");
  if ((Definition->Policy & RequiredPolicy) == 0)
    return executionError("phase '" + Definition->CanonicalName +
                          "' does not allow " + Kind);
  return Error::success();
}

Error PluginPhaseExecutor::addObserver(
    StringRef PluginID, const NevercObserverDescriptor &Descriptor) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError("cannot add an observer after phase plan freeze");
  constexpr uint64_t Required =
      offsetof(NevercObserverDescriptor, DestroyUserData) +
      sizeof(NevercObserverDescriptor::DestroyUserData);
  if (!validHeader(Descriptor.Header, Required) || !Descriptor.Callback ||
      Descriptor.Reserved != 0 || Descriptor.Points == 0)
    return executionError("observer descriptor is invalid");
  if (Error E = validatePhaseRegistration(Descriptor.Phase,
                                          NEVERC_PHASE_OBSERVABLE, "observer"))
    return E;
  const PluginPhaseDefinition &Phase = *Graph.find(Descriptor.Phase);
  if ((Descriptor.Points & ~Phase.ObserverPoints) != 0)
    return executionError("observer requests a point not allowed by phase");
  Observers.push_back({PluginID.str(), Descriptor});
  return Error::success();
}

Error PluginPhaseExecutor::addInterceptor(
    StringRef PluginID, const NevercInterceptorDescriptor &Descriptor) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError("cannot add an interceptor after phase plan freeze");
  constexpr uint64_t Required =
      offsetof(NevercInterceptorDescriptor, DestroyUserData) +
      sizeof(NevercInterceptorDescriptor::DestroyUserData);
  if (!validHeader(Descriptor.Header, Required) || !Descriptor.Callback)
    return executionError("interceptor descriptor is invalid");
  if (Error E = validatePhaseRegistration(
          Descriptor.Phase, NEVERC_PHASE_INTERCEPTABLE, "interceptor"))
    return E;
  Interceptors.push_back({PluginID.str(), Descriptor});
  return Error::success();
}

Error PluginPhaseExecutor::addProvider(
    StringRef PluginID, const NevercProviderDescriptor &Descriptor) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError("cannot add a Provider after phase plan freeze");
  constexpr uint64_t Required =
      offsetof(NevercProviderDescriptor, DestroyUserData) +
      sizeof(NevercProviderDescriptor::DestroyUserData);
  if (!validHeader(Descriptor.Header, Required) || !Descriptor.Callback ||
      Descriptor.Reserved != 0 ||
      !validHeader(Descriptor.Route.Header, sizeof(Descriptor.Route)) ||
      Descriptor.Route.Reserved != 0 ||
      (Descriptor.Deterministic != NEVERC_FALSE &&
       Descriptor.Deterministic != NEVERC_TRUE) ||
      (Descriptor.Cacheable != NEVERC_FALSE &&
       Descriptor.Cacheable != NEVERC_TRUE) ||
      (Descriptor.FallbackSafe != NEVERC_FALSE &&
       Descriptor.FallbackSafe != NEVERC_TRUE))
    return executionError("provider descriptor is invalid");
  if (Error E = validatePhaseRegistration(Descriptor.Phase,
                                          NEVERC_PHASE_REPLACEABLE, "provider"))
    return E;
  auto ProviderID = copyString(Descriptor.ProviderID, "provider ID", false);
  if (!ProviderID)
    return ProviderID.takeError();
  auto TargetTriple =
      copyString(Descriptor.Route.TargetTriple, "target triple", true);
  if (!TargetTriple)
    return TargetTriple.takeError();
  auto CPU = copyString(Descriptor.Route.CPU, "CPU", true);
  if (!CPU)
    return CPU.takeError();
  auto Features = copyString(Descriptor.Route.Features, "features", true);
  if (!Features)
    return Features.takeError();
  auto ObjectFormat =
      copyString(Descriptor.Route.ObjectFormat, "object format", true);
  if (!ObjectFormat)
    return ObjectFormat.takeError();

  ProviderBinding Binding;
  Binding.PluginID = PluginID.str();
  Binding.Descriptor = Descriptor;
  Binding.ProviderID = std::move(*ProviderID);
  Binding.TargetTriple = std::move(*TargetTriple);
  Binding.CPU = std::move(*CPU);
  Binding.Features = std::move(*Features);
  Binding.ObjectFormat = std::move(*ObjectFormat);
  Binding.Descriptor.ProviderID = {};
  Binding.Descriptor.Route.TargetTriple = {};
  Binding.Descriptor.Route.CPU = {};
  Binding.Descriptor.Route.Features = {};
  Binding.Descriptor.Route.ObjectFormat = {};
  Providers.push_back(std::move(Binding));
  return Error::success();
}

Error PluginPhaseExecutor::setBuiltinProvider(NevercInterfaceID Phase,
                                              BuiltinProvider Provider) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError(
        "cannot set a builtin Provider after phase plan freeze");
  if (!Provider)
    return executionError("builtin phase provider is empty");
  if (!Graph.find(Phase))
    return executionError("builtin provider references an unknown phase");
  auto It = llvm::find_if(Builtins, [&](const BuiltinBinding &Binding) {
    return samePluginInterfaceID(Binding.Phase, Phase);
  });
  if (It == Builtins.end())
    Builtins.push_back({Phase, std::move(Provider)});
  else
    It->Provider = std::move(Provider);
  return Error::success();
}

Error PluginPhaseExecutor::selectProvider(NevercInterfaceID Phase,
                                          StringRef PluginID) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError("cannot select a Provider after phase plan freeze");
  if (!Graph.find(Phase) || PluginID.empty())
    return executionError("provider selection is invalid");
  auto It = llvm::find_if(Selections, [&](const Selection &SelectionValue) {
    return samePluginInterfaceID(SelectionValue.Phase, Phase);
  });
  if (It == Selections.end())
    Selections.push_back({Phase, PluginID.str()});
  else
    It->PluginID = PluginID.str();
  return Error::success();
}

Error PluginPhaseExecutor::enableRecoverableBuiltinFallback(
    NevercInterfaceID Phase) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError("cannot enable fallback after phase plan freeze");
  const PluginPhaseDefinition *Definition = Graph.find(Phase);
  if (!Definition || (Definition->Policy & NEVERC_PHASE_REPLACEABLE) == 0 ||
      !Definition->HasBuiltinFallback)
    return executionError("recoverable fallback is not available for phase");
  if (!llvm::any_of(FallbackSelections,
                    [&](const FallbackSelection &SelectionValue) {
                      return samePluginInterfaceID(SelectionValue.Phase, Phase);
                    }))
    FallbackSelections.push_back({Phase});
  return Error::success();
}

Error PluginPhaseExecutor::setProofVerifier(ProofVerifier Verifier) {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return executionError(
        "cannot set a proof verifier after phase plan freeze");
  VerifyProof = std::move(Verifier);
  return Error::success();
}

Error PluginPhaseExecutor::freeze() {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  if (Frozen)
    return Error::success();
  if (!Graph.isFinalized())
    return executionError(
        "cannot freeze a phase plan before graph finalization");
  Frozen = true;
  return Error::success();
}

bool PluginPhaseExecutor::isFrozen() const {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  return Frozen;
}

bool PluginPhaseExecutor::hasBindings(NevercInterfaceID Phase) const {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  return llvm::any_of(Observers,
                      [&](const ObserverBinding &Binding) {
                        return samePluginInterfaceID(Binding.Descriptor.Phase,
                                                     Phase);
                      }) ||
         llvm::any_of(Interceptors,
                      [&](const InterceptorBinding &Binding) {
                        return samePluginInterfaceID(Binding.Descriptor.Phase,
                                                     Phase);
                      }) ||
         llvm::any_of(Providers, [&](const ProviderBinding &Binding) {
           return samePluginInterfaceID(Binding.Descriptor.Phase, Phase);
         });
}

bool PluginPhaseExecutor::hasInterceptors(NevercInterfaceID Phase) const {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  return llvm::any_of(Interceptors, [&](const InterceptorBinding &Binding) {
    return samePluginInterfaceID(Binding.Descriptor.Phase, Phase);
  });
}

bool PluginPhaseExecutor::hasProvider(NevercInterfaceID Phase) const {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  return llvm::any_of(Providers, [&](const ProviderBinding &Binding) {
    return samePluginInterfaceID(Binding.Descriptor.Phase, Phase);
  });
}

bool PluginPhaseExecutor::hasProvider(NevercInterfaceID Phase,
                                      const NevercPhaseRoute &Route) const {
  std::lock_guard<std::mutex> ConfigurationLock(ConfigurationMutex);
  return llvm::any_of(Providers, [&](const ProviderBinding &Binding) {
    return samePluginInterfaceID(Binding.Descriptor.Phase, Phase) &&
           providerMatches(Binding, Route);
  });
}

std::optional<uint64_t> PluginPhaseExecutor::currentArtifactMutationCapability(
    const PluginTaskContext &Task) const {
  return Task.currentArtifactMutationCapability(this);
}

bool PluginPhaseExecutor::validatesArtifactMutationCapability(
    const PluginTaskContext &Task, uint64_t Token) const {
  return Task.validatesArtifactMutationCapability(this, Token);
}

std::vector<std::string> PluginPhaseExecutor::fallbackProvenance() const {
  std::lock_guard<std::mutex> ProvenanceLock(ProvenanceMutex);
  return FallbackProvenance;
}

Error PluginPhaseExecutor::importSessionRegistrations(
    const PluginSession &Session) {
  for (const auto &Module : Session.plugins()) {
    const PluginPublishedRegistration *Registration = Module->registration();
    if (!Registration)
      continue;
    for (const PluginRegistrationRecord &Record : Registration->records()) {
      switch (Record.Kind) {
      case PluginRegistrationKind::Observer:
        if (!Graph.find(Record.Observer.Phase))
          break;
        if (Error E =
                addObserver(Module->descriptor().PluginID, Record.Observer))
          return E;
        break;
      case PluginRegistrationKind::Interceptor:
        if (!Graph.find(Record.Interceptor.Phase))
          break;
        if (Error E = addInterceptor(Module->descriptor().PluginID,
                                     Record.Interceptor))
          return E;
        break;
      case PluginRegistrationKind::Provider: {
        if (!Graph.find(Record.Provider.Phase))
          break;
        NevercProviderDescriptor Descriptor = Record.Provider;
        Descriptor.ProviderID = {Record.ProviderID.data(),
                                 Record.ProviderID.size()};
        Descriptor.Route.TargetTriple = {Record.TargetTriple.data(),
                                         Record.TargetTriple.size()};
        Descriptor.Route.CPU = {Record.CPU.data(), Record.CPU.size()};
        Descriptor.Route.Features = {Record.Features.data(),
                                     Record.Features.size()};
        Descriptor.Route.ObjectFormat = {Record.ObjectFormat.data(),
                                         Record.ObjectFormat.size()};
        if (Error E = addProvider(Module->descriptor().PluginID, Descriptor))
          return E;
        break;
      }
      case PluginRegistrationKind::Phase:
        if (const PluginPhaseDefinition *Definition =
                Graph.find(Record.Interface)) {
          if (Definition->CanonicalName != Record.CanonicalName ||
              !samePluginInterfaceID(Definition->InputArtifact,
                                     Record.Phase.InputArtifact) ||
              !samePluginInterfaceID(Definition->OutputArtifact,
                                     Record.Phase.OutputArtifact) ||
              Definition->Policy != Record.Phase.Policy ||
              Definition->ObserverPoints != Record.Phase.ObserverPoints)
            return executionError("registered phase '" + Record.CanonicalName +
                                  "' disagrees with the finalized phase graph");
        }
        break;
      case PluginRegistrationKind::Interface:
      case PluginRegistrationKind::Option:
      case PluginRegistrationKind::VFSProvider:
      case PluginRegistrationKind::IRPass:
      case PluginRegistrationKind::IRAnalysis:
      case PluginRegistrationKind::MIRPass:
      case PluginRegistrationKind::Target:
      case PluginRegistrationKind::TargetABI:
      case PluginRegistrationKind::CallingConvention:
      case PluginRegistrationKind::MCSchema:
      case PluginRegistrationKind::MCEncoder:
      case PluginRegistrationKind::MCDecoder:
      case PluginRegistrationKind::MCAsmBackend:
      case PluginRegistrationKind::ObjectFormat:
      case PluginRegistrationKind::CodeGenEdge:
      case PluginRegistrationKind::LinkerProvider:
      case PluginRegistrationKind::ObjectMergeProvider:
      case PluginRegistrationKind::BinaryImageVerifier:
      case PluginRegistrationKind::LTOProvider:
        break;
      }
    }
  }
  return Error::success();
}

Expected<NevercArtifactHandle>
PluginPhaseExecutor::createCandidate(PluginTaskContext &Task,
                                     NevercInterfaceID Type, void *Payload) {
  if (!Payload)
    return executionError("artifact candidate payload is null");
  auto ArtifactType = Artifacts.find(Type);
  if (!ArtifactType)
    return executionError("artifact candidate type is not registered");
  auto *Candidate =
      new CandidateState{std::move(ArtifactType), Payload, 0, true, false};
  auto Handle = Task.handles().create(
      PluginArtifactHandleKind, Candidate, [](void *RawCandidate) {
        auto *State = static_cast<CandidateState *>(RawCandidate);
        auto DeleteState = make_scope_exit([&] { delete State; });
        if (State->IsCandidate && !State->Consumed && State->Payload)
          State->Type->destroyPayload(State->Payload);
      });
  if (!Handle) {
    try {
      Candidate->Type->destroyPayload(Candidate->Payload);
    } catch (...) {
    }
    delete Candidate;
    return Handle.takeError();
  }
  return NevercArtifactHandle{Handle->Owner, Handle->Value};
}

Expected<NevercArtifactHandle> PluginPhaseExecutor::createArtifactView(
    PluginTaskContext &Task, NevercInterfaceID Type, const void *Payload,
    uint64_t Generation) {
  if (!Payload || Generation == 0)
    return executionError("artifact view identity is invalid");
  auto ArtifactType = Artifacts.find(Type);
  if (!ArtifactType)
    return executionError("artifact view type is not registered");
  auto *View =
      new CandidateState{std::move(ArtifactType), const_cast<void *>(Payload),
                         Generation, false, false};
  auto Handle =
      Task.handles().create(PluginArtifactHandleKind, View, [](void *RawView) {
        delete static_cast<CandidateState *>(RawView);
      });
  if (!Handle) {
    delete View;
    return Handle.takeError();
  }
  return NevercArtifactHandle{Handle->Owner, Handle->Value};
}

NevercStatus PluginPhaseExecutor::resolveArtifactPayload(
    PluginTaskContext &Task, NevercArtifactHandle Artifact,
    NevercInterfaceID ExpectedType, const void **OutPayload) {
  if (!OutPayload)
    return executionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPayload = nullptr;
  void *RawArtifact = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Artifact, PluginArtifactHandleKind, &RawArtifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *State = static_cast<CandidateState *>(RawArtifact);
  if (!State || !State->Payload)
    return executionStatus(NEVERC_STATUS_STALE_HANDLE);
  if (!samePluginInterfaceID(State->Type->id(), ExpectedType))
    return executionStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutPayload = State->Payload;
  return neverc_status_ok();
}

bool PluginPhaseExecutor::isActiveContinuation(
    const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation) {
  if (!Frame || !Continuation ||
      !validHeader(Continuation->Header, sizeof(*Continuation)) ||
      !Continuation->Context)
    return false;
  auto *State = static_cast<ContinuationContext *>(Continuation->Context);
  std::lock_guard<std::mutex> Lock(ContinuationMutex);
  auto It = llvm::find_if(
      Continuations, [&](const auto &Known) { return Known.get() == State; });
  if (It == Continuations.end())
    return false;
  return State->Active.load(std::memory_order_acquire) &&
         State->Generation == Continuation->Generation &&
         State->Thread == std::this_thread::get_id() && State->Chain &&
         Frame == &State->Chain->Frame;
}

Expected<NevercProofHandle> PluginPhaseExecutor::createEquivalenceProof(
    PluginTaskContext &Task, NevercInterfaceID PhaseID,
    NevercArtifactHandle Input, const PluginArtifactSlot &OutputSlot,
    const NevercPhaseRoute &Route) {
  const PluginPhaseDefinition *Phase = Graph.find(PhaseID);
  if (!Phase || (Phase->Policy & NEVERC_PHASE_SKIPPABLE_WITH_PROOF) == 0)
    return executionError("phase does not accept equivalence proofs");
  auto RouteKey = copyRouteIdentity(Route);
  if (!RouteKey)
    return RouteKey.takeError();

  void *RawInput = nullptr;
  NevercStatus InputStatus =
      Task.handles().resolve(Input, PluginArtifactHandleKind, &RawInput);
  if (InputStatus.Code != NEVERC_STATUS_OK)
    return executionError("equivalence proof input handle is invalid");
  auto *InputState = static_cast<CandidateState *>(RawInput);
  if (InputState->IsCandidate || InputState->Consumed ||
      InputState->Generation == 0)
    return executionError(
        "equivalence proof input is not a published artifact view");
  if (!samePluginInterfaceID(InputState->Type->id(), Phase->InputArtifact))
    return executionError("equivalence proof input has the wrong type");

  PluginArtifactSlot::Snapshot Output = OutputSlot.snapshot();
  if (!Output.Type || !Output.Payload || Output.Generation == 0)
    return executionError("equivalence proof output slot is empty");
  if (!samePluginInterfaceID(Output.Type->id(), Phase->OutputArtifact))
    return executionError("equivalence proof output has the wrong type");

  auto *Proof = new ProofState{
      PhaseID,
      Input,
      InputState->Type,
      InputState->Payload,
      InputState->Generation,
      Output.Type,
      Output.Payload,
      Output.Generation,
      std::move(*RouteKey),
  };
  auto Handle =
      Task.handles().create(PluginProofHandleKind, Proof, [](void *RawProof) {
        delete static_cast<ProofState *>(RawProof);
      });
  if (!Handle) {
    delete Proof;
    return Handle.takeError();
  }
  return NevercProofHandle{Handle->Owner, Handle->Value};
}

NevercStatus PluginPhaseExecutor::invokeProvider(ChainContext &Context,
                                                 NevercPhaseResult &OutResult) {
  if (Context.Provider) {
    NevercPhaseResult Result = emptyResult();
    bool KeepResult = false;
    auto DiscardResult = make_scope_exit([&] {
      if (!KeepResult)
        discardCandidate(Context.Task, Result.Output);
    });
    uint64_t DiagnosticTransactionID = 0;
    NevercStatus Status = invokePluginCallback(
        Context, Context.Provider->PluginID,
        Context.Phase.CanonicalName + "/provider",
        [&] {
          return Context.Provider->Descriptor.Callback(
              &Context.Frame, &Result, Context.Provider->Descriptor.UserData);
        },
        &DiagnosticTransactionID, true);
    bool Recoverable = Status.Code != NEVERC_STATUS_OK &&
                       Status.Code != NEVERC_STATUS_CANCELLED &&
                       Status.Flags == NEVERC_STATUS_FLAG_RECOVERABLE;
    if (Recoverable && Context.AllowRecoverableFallback &&
        Context.Provider->Descriptor.FallbackSafe == NEVERC_TRUE &&
        Context.Builtin && Context.Failure.empty() &&
        !Context.Session.isCancelled()) {
      if (!discardCandidate(Context.Task, Result.Output)) {
        failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
                  "recoverable Provider effects could not be discarded");
        return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
      }
      Context.Session.diagnostics().discardTransaction(DiagnosticTransactionID);
      Result = emptyResult();
      {
        std::lock_guard<std::mutex> ProvenanceLock(
            Context.Executor.ProvenanceMutex);
        Context.Executor.FallbackProvenance.push_back(
            ("phase '" + Context.Phase.CanonicalName +
             "' fell back from plugin '" + Context.Provider->PluginID +
             "' after status " + Twine(Status.Code))
                .str());
      }
      Status = Context.Builtin->Provider(&Context.Frame, &Result);
      if (!validateCallbackStatus(Context, "Builtin Provider", "host", Status))
        return executionStatus(Context.FailureCode);
      if (!validResult(Result) || Result.Action != NEVERC_PHASE_REPLACE ||
          !nonnull(Result.Output) || !canonicalNull(Result.Proof)) {
        failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
                  "Builtin Provider returned an invalid phase result");
        return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
      }
      OutResult = Result;
      KeepResult = true;
      return neverc_status_ok();
    }
    if (!validateCallbackStatus(Context, "Provider", Context.Provider->PluginID,
                                Status))
      return executionStatus(Context.FailureCode);
    if (!validResult(Result) || Result.Action != NEVERC_PHASE_REPLACE ||
        !nonnull(Result.Output) || !canonicalNull(Result.Proof)) {
      failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
                "Provider returned an invalid phase result");
      return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
    }
    OutResult = Result;
    KeepResult = true;
    return neverc_status_ok();
  }

  if (!Context.Builtin) {
    failChain(Context, NEVERC_STATUS_CAPABILITY_UNAVAILABLE,
              "phase has no selected or builtin Provider");
    return executionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
  NevercPhaseResult Result = emptyResult();
  bool KeepResult = false;
  auto DiscardResult = make_scope_exit([&] {
    if (!KeepResult)
      discardCandidate(Context.Task, Result.Output);
  });
  NevercStatus Status = Context.Builtin->Provider(&Context.Frame, &Result);
  if (!validateCallbackStatus(Context, "Builtin Provider", "host", Status))
    return executionStatus(Context.FailureCode);
  if (!validResult(Result) || Result.Action != NEVERC_PHASE_REPLACE ||
      !nonnull(Result.Output) || !canonicalNull(Result.Proof)) {
    failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
              "Builtin Provider returned an invalid phase result");
    return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
  }
  OutResult = Result;
  KeepResult = true;
  return neverc_status_ok();
}

NevercStatus PluginPhaseExecutor::invokeChain(ChainContext &Context,
                                              size_t Index,
                                              NevercPhaseResult &OutResult) {
  if (Index == Context.Interceptors.size())
    return invokeProvider(Context, OutResult);

  const InterceptorBinding &Binding = *Context.Interceptors[Index];
  auto ContinuationState = std::make_unique<ContinuationContext>();
  ContinuationState->Chain = &Context;
  ContinuationState->NextIndex = Index + 1;
  ContinuationState->Thread = std::this_thread::get_id();
  uint64_t Generation =
      NextContinuationGeneration.load(std::memory_order_relaxed);
  for (;;) {
    if (Generation == 0 || Generation == std::numeric_limits<uint64_t>::max()) {
      failChain(Context, NEVERC_STATUS_RESOURCE_EXHAUSTED,
                "phase continuation generation space is exhausted");
      return executionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    if (NextContinuationGeneration.compare_exchange_weak(
            Generation, Generation + 1, std::memory_order_relaxed,
            std::memory_order_relaxed))
      break;
  }
  ContinuationState->Generation = Generation;
  ContinuationContext *RawContinuation = ContinuationState.get();
  {
    std::lock_guard<std::mutex> Lock(ContinuationMutex);
    Continuations.push_back(std::move(ContinuationState));
  }

  NevercPhaseContinuation Continuation{};
  Continuation.Header = {sizeof(Continuation), NEVERC_PLUGIN_ABI_MAJOR,
                         NEVERC_PLUGIN_ABI_MINOR, 0};
  Continuation.InvokeNext = invokeNext;
  Continuation.Context = RawContinuation;
  Continuation.Generation = RawContinuation->Generation;
  NevercPhaseResult Result = emptyResult();
  bool KeepCallbackResult = false;
  auto DiscardCallbackResult = make_scope_exit([&] {
    if (!KeepCallbackResult)
      discardCandidate(Context.Task, Result.Output);
  });
  NevercStatus Status = invokePluginCallback(
      Context, Binding.PluginID, Context.Phase.CanonicalName + "/interceptor",
      [&] {
        return Binding.Descriptor.Callback(&Context.Frame, &Continuation,
                                           &Result,
                                           Binding.Descriptor.UserData);
      },
      nullptr, true);
  RawContinuation->Active.store(false, std::memory_order_release);
  bool KeepDownstreamResult = false;
  auto DiscardDownstreamResult = make_scope_exit([&] {
    if (!KeepDownstreamResult && RawContinuation->HasDownstreamResult &&
        RawContinuation->DownstreamResult.Action == NEVERC_PHASE_REPLACE &&
        nonnull(RawContinuation->DownstreamResult.Output))
      discardCandidate(Context.Task, RawContinuation->DownstreamResult.Output);
  });
  if (!validateCallbackStatus(Context, "Interceptor", Binding.PluginID, Status))
    return executionStatus(Context.FailureCode);
  if (!Context.Failure.empty())
    return executionStatus(Context.FailureCode);
  if (!validResult(Result)) {
    failChain(Context, NEVERC_STATUS_INVALID_DESCRIPTOR,
              "Interceptor returned an invalid phase result");
    return executionStatus(NEVERC_STATUS_INVALID_DESCRIPTOR);
  }

  bool Called = RawContinuation->Called.load(std::memory_order_acquire);
  if (Called) {
    if (Result.Action != NEVERC_PHASE_CONTINUE ||
        !canonicalNull(Result.Output) || !canonicalNull(Result.Proof)) {
      failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
                "Interceptor called InvokeNext but did not return an empty "
                "CONTINUE result");
      return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
    }
    if (!RawContinuation->HasDownstreamResult) {
      failChain(Context, NEVERC_STATUS_INVALID_STATE,
                "InvokeNext completed without a downstream result");
      return executionStatus(NEVERC_STATUS_INVALID_STATE);
    }
    OutResult = RawContinuation->DownstreamResult;
    KeepDownstreamResult = true;
    return neverc_status_ok();
  }

  if (Result.Action == NEVERC_PHASE_REPLACE) {
    if ((Context.Phase.Policy & NEVERC_PHASE_REPLACEABLE) == 0 ||
        !nonnull(Result.Output) || !canonicalNull(Result.Proof)) {
      failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
                "Interceptor returned an invalid REPLACE result");
      return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
    }
    OutResult = Result;
    KeepCallbackResult = true;
    return neverc_status_ok();
  }
  if (Result.Action == NEVERC_PHASE_SKIP) {
    if ((Context.Phase.Policy & NEVERC_PHASE_SKIPPABLE_WITH_PROOF) == 0 ||
        !nonnull(Result.Proof) || !nonnull(Result.Output)) {
      failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
                "Interceptor returned an invalid SKIP result");
      return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
    }
    OutResult = Result;
    return neverc_status_ok();
  }
  failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
            "Interceptor returned CONTINUE without calling InvokeNext");
  return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
}

NevercStatus NEVERC_CALL PluginPhaseExecutor::invokeNext(
    NevercPhaseContinuation *Continuation, const NevercPhaseFrame *Frame,
    NevercPhaseResult *OutResult) {
  if (!Continuation || !Frame || !OutResult ||
      !validHeader(Continuation->Header, sizeof(*Continuation)) ||
      OutResult->Header.StructSize < sizeof(NevercPhaseResult))
    return executionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *State = static_cast<ContinuationContext *>(Continuation->Context);
  if (!State || !State->Active.load(std::memory_order_acquire) ||
      State->Generation != Continuation->Generation)
    return executionStatus(NEVERC_STATUS_INVALID_STATE);
  ChainContext &Context = *State->Chain;
  if (State->Thread != std::this_thread::get_id()) {
    failChain(Context, NEVERC_STATUS_WRONG_SCOPE,
              "InvokeNext was called from a different thread");
    return executionStatus(NEVERC_STATUS_WRONG_SCOPE);
  }
  if (State->Called.exchange(true, std::memory_order_acq_rel)) {
    failChain(Context, NEVERC_STATUS_POLICY_VIOLATION,
              "InvokeNext was called more than once");
    return executionStatus(NEVERC_STATUS_POLICY_VIOLATION);
  }
  if (Frame != &Context.Frame ||
      !samePluginInterfaceID(Frame->Phase, Context.Frame.Phase) ||
      Frame->Session.Owner != Context.Frame.Session.Owner ||
      Frame->Session.Value != Context.Frame.Session.Value ||
      Frame->Task.Owner != Context.Frame.Task.Owner ||
      Frame->Task.Value != Context.Frame.Task.Value) {
    failChain(Context, NEVERC_STATUS_WRONG_SCOPE,
              "InvokeNext received a frame from another scope");
    return executionStatus(NEVERC_STATUS_WRONG_SCOPE);
  }

  NevercPhaseResult NextResult = emptyResult();
  NevercStatus Status =
      Context.Executor.invokeChain(Context, State->NextIndex, NextResult);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->DownstreamResult = NextResult;
  State->HasDownstreamResult = true;
  Context.Frame.CurrentOutput = NextResult.Output;
  return writeResult(OutResult, NextResult);
}

Error PluginPhaseExecutor::execute(PluginSession &Session,
                                   PluginTaskContext &Task,
                                   NevercInterfaceID PhaseID,
                                   const NevercPhaseRoute &Route,
                                   NevercArtifactHandle Input,
                                   PluginArtifactSlot &OutputSlot) {
  if (&Task.session() != &Session)
    return executionError("phase task belongs to a different session");
  NevercStatus Cancellation = Task.checkCancelled();
  if (Cancellation.Code != NEVERC_STATUS_OK)
    return executionError("phase task is cancelled or no longer active");
  if (Error E = freeze())
    return E;
  const PluginPhaseDefinition *Phase = Graph.find(PhaseID);
  if (!Phase)
    return executionError("cannot execute an unknown phase");
  auto RouteKey = copyRouteIdentity(Route);
  if (!RouteKey)
    return RouteKey.takeError();
  if (!samePluginInterfaceID(OutputSlot.expectedType(), Phase->OutputArtifact))
    return executionError("phase output slot has the wrong artifact type");
  if (llvm::any_of(ActivePhases, [&](const ActivePhaseInvocation &Active) {
        return Active.Task == &Task &&
               samePluginInterfaceID(Active.Phase, PhaseID);
      }))
    return executionError("phase '" + Phase->CanonicalName +
                          "' recursively invoked itself");

  bool ExecutionSucceeded = false;
  auto CancelOnFailure = make_scope_exit([&] {
    if (!ExecutionSucceeded)
      Session.cancel();
  });
  ActivePhases.push_back({&Task, PhaseID});
  auto PopPhase = make_scope_exit([] { ActivePhases.pop_back(); });

  void *RawInput = nullptr;
  NevercStatus InputStatus =
      Task.handles().resolve(Input, PluginArtifactHandleKind, &RawInput);
  if (InputStatus.Code != NEVERC_STATUS_OK)
    return executionError("phase input artifact handle is invalid");
  auto *InputState = static_cast<CandidateState *>(RawInput);
  if (InputState->IsCandidate || InputState->Consumed ||
      InputState->Generation == 0 ||
      !samePluginInterfaceID(InputState->Type->id(), Phase->InputArtifact))
    return executionError("phase input artifact has the wrong type or state");

  NevercArtifactHandle ExistingOutput{};
  PluginArtifactSlot::Snapshot ExistingSnapshot = OutputSlot.snapshot();
  if (ExistingSnapshot.Payload) {
    auto View = createArtifactView(Task, ExistingSnapshot.Type->id(),
                                   ExistingSnapshot.Payload,
                                   ExistingSnapshot.Generation);
    if (!View)
      return View.takeError();
    ExistingOutput = *View;
  }
  auto ReleaseExistingOutput = make_scope_exit([&] {
    if (nonnull(ExistingOutput))
      (void)Task.handles().release(ExistingOutput, PluginArtifactHandleKind);
  });

  NevercPhaseFrame Frame{};
  Frame.Header = {sizeof(Frame), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  Frame.Session = Session.handle();
  Frame.Task = Task.handle();
  Frame.Phase = PhaseID;
  Frame.Route = Route;
  Frame.Input = Input;
  Frame.CurrentOutput = ExistingOutput;

  ChainContext Context{*this, Session, Task, *Phase, Frame};
  for (const InterceptorBinding &Binding : Interceptors)
    if (samePluginInterfaceID(Binding.Descriptor.Phase, PhaseID))
      Context.Interceptors.push_back(&Binding);

  std::vector<const ProviderBinding *> MatchingProviders;
  for (const ProviderBinding &Binding : Providers)
    if (samePluginInterfaceID(Binding.Descriptor.Phase, PhaseID) &&
        providerMatches(Binding, Route))
      MatchingProviders.push_back(&Binding);
  auto Selected = llvm::find_if(Selections, [&](const Selection &Value) {
    return samePluginInterfaceID(Value.Phase, PhaseID);
  });
  if (Selected != Selections.end()) {
    std::vector<const ProviderBinding *> SelectedProviders;
    llvm::copy_if(MatchingProviders, std::back_inserter(SelectedProviders),
                  [&](const ProviderBinding *Provider) {
                    return Provider->PluginID == Selected->PluginID;
                  });
    if (SelectedProviders.empty())
      return executionError("selected Provider '" + Selected->PluginID +
                            "' is unavailable for phase");
    if (SelectedProviders.size() != 1)
      return executionError("selected plugin '" + Selected->PluginID +
                            "' has multiple matching Providers for phase");
    Context.Provider = SelectedProviders.front();
  } else if (MatchingProviders.size() == 1) {
    Context.Provider = MatchingProviders.front();
  } else if (MatchingProviders.size() > 1) {
    return executionError("multiple plugin Providers match phase '" +
                          Phase->CanonicalName + "'");
  }
  auto Builtin = llvm::find_if(Builtins, [&](const BuiltinBinding &Binding) {
    return samePluginInterfaceID(Binding.Phase, PhaseID);
  });
  if (Builtin != Builtins.end())
    Context.Builtin = &*Builtin;
  Context.AllowRecoverableFallback = llvm::any_of(
      FallbackSelections, [&](const FallbackSelection &SelectionValue) {
        return samePluginInterfaceID(SelectionValue.Phase, PhaseID);
      });

  auto invokeObservers = [&](NevercObserverPoint Point, bool Reverse) -> Error {
    auto InvokeOne = [&](const ObserverBinding &Binding) -> Error {
      if (!samePluginInterfaceID(Binding.Descriptor.Phase, PhaseID) ||
          (Binding.Descriptor.Points & Point) == 0)
        return Error::success();
      NevercStatus Status = invokePluginCallback(
          Context, Binding.PluginID, Phase->CanonicalName + "/observer", [&] {
            return Binding.Descriptor.Callback(&Frame, Point,
                                               Binding.Descriptor.UserData);
          });
      if (!validateCallbackStatus(Context, "Observer", Binding.PluginID,
                                  Status))
        return executionError(Context.Failure);
      return Error::success();
    };
    if (!Reverse) {
      for (const ObserverBinding &Binding : Observers)
        if (Error E = InvokeOne(Binding))
          return E;
    } else {
      for (auto It = Observers.rbegin(); It != Observers.rend(); ++It)
        if (Error E = InvokeOne(*It))
          return E;
    }
    return Error::success();
  };

  if (Error E = invokeObservers(NEVERC_OBSERVER_BEFORE, false))
    return E;

  NevercPhaseResult Result = emptyResult();
  NevercStatus ChainStatus = invokeChain(Context, 0, Result);
  if (ChainStatus.Code != NEVERC_STATUS_OK)
    return executionError(Context.Failure.empty()
                              ? "phase callback chain failed"
                              : Context.Failure);
  if (Task.checkCancelled().Code != NEVERC_STATUS_OK)
    return executionError("phase task was cancelled during execution");
  if (Result.Action == NEVERC_PHASE_SKIP) {
    if (!nonnull(Result.Output) || !nonnull(Result.Proof) ||
        !nonnull(ExistingOutput) ||
        Result.Output.Owner != ExistingOutput.Owner ||
        Result.Output.Value != ExistingOutput.Value)
      return executionError(
          "phase SKIP did not preserve the current output artifact");

    void *RawOutput = nullptr;
    NevercStatus OutputStatus = Task.handles().resolve(
        Result.Output, PluginArtifactHandleKind, &RawOutput);
    if (OutputStatus.Code != NEVERC_STATUS_OK)
      return executionError("phase SKIP output handle is invalid");
    auto *OutputState = static_cast<CandidateState *>(RawOutput);
    if (OutputState->IsCandidate || OutputState->Consumed ||
        OutputState->Generation == 0)
      return executionError(
          "phase SKIP output is not a published artifact view");

    void *RawProof = nullptr;
    NevercStatus ProofStatus =
        Task.handles().resolve(Result.Proof, PluginProofHandleKind, &RawProof);
    if (ProofStatus.Code != NEVERC_STATUS_OK)
      return executionError("phase SKIP proof handle is invalid");
    auto *Proof = static_cast<ProofState *>(RawProof);
    PluginArtifactSlot::Snapshot CurrentOutput = OutputSlot.snapshot();
    if (!samePluginInterfaceID(Proof->Phase, PhaseID) ||
        Proof->Input.Owner != Input.Owner ||
        Proof->Input.Value != Input.Value ||
        Proof->InputPayload != InputState->Payload ||
        Proof->InputGeneration != InputState->Generation ||
        !samePluginInterfaceID(Proof->InputType->id(),
                               InputState->Type->id()) ||
        Proof->OutputPayload != OutputState->Payload ||
        Proof->OutputGeneration != OutputState->Generation ||
        !samePluginInterfaceID(Proof->OutputType->id(),
                               OutputState->Type->id()) ||
        CurrentOutput.Payload != OutputState->Payload ||
        CurrentOutput.Generation != OutputState->Generation ||
        !sameRoute(Proof->Route, Route))
      return executionError(
          "phase SKIP proof does not match the current invocation");
    if (VerifyProof)
      if (Error E =
              VerifyProof(Session, Task, Frame, Result.Output, Result.Proof))
        return E;
    Frame.CurrentOutput = Result.Output;
    if (Error E = invokeObservers(NEVERC_OBSERVER_AFTER, true))
      return E;
    ExecutionSucceeded = true;
    return Error::success();
  }
  if (Result.Action != NEVERC_PHASE_REPLACE || !nonnull(Result.Output))
    return executionError("phase chain produced no candidate artifact");

  void *RawCandidate = nullptr;
  NevercStatus ResolveStatus = Task.handles().resolve(
      Result.Output, PluginArtifactHandleKind, &RawCandidate);
  if (ResolveStatus.Code != NEVERC_STATUS_OK)
    return executionError("phase returned an invalid artifact handle");
  auto *Candidate = static_cast<CandidateState *>(RawCandidate);
  if (!Candidate->IsCandidate || Candidate->Consumed)
    return executionError("phase returned an already-consumed artifact");
  if (!samePluginInterfaceID(Candidate->Type->id(), Phase->OutputArtifact))
    return executionError("phase returned the wrong artifact type");

  auto Transaction = PluginArtifactTransaction::create(
      Artifacts, Candidate->Type->id(), Candidate->Payload);
  if (!Transaction)
    return Transaction.takeError();
  Candidate->Consumed = true;
  bool CandidateReleased = false;
  auto ReleaseCandidate = make_scope_exit([&] {
    if (!CandidateReleased)
      (void)Task.handles().release(Result.Output, PluginArtifactHandleKind);
  });

  if (Error E = (*Transaction)->verify())
    return E;
  Frame.CurrentOutput = Result.Output;
  if (Error E = invokeObservers(NEVERC_OBSERVER_AFTER, true))
    return E;
  if (Error E = (*Transaction)->commit(OutputSlot))
    return E;
  if (Error E = invokeObservers(NEVERC_OBSERVER_AFTER_COMMIT, false))
    return E;
  NevercStatus ReleaseStatus =
      Task.handles().release(Result.Output, PluginArtifactHandleKind);
  CandidateReleased = true;
  if (ReleaseStatus.Code != NEVERC_STATUS_OK)
    return executionError("failed to retire candidate artifact handle");
  ExecutionSucceeded = true;
  return Error::success();
}

Error PluginPhaseExecutor::notify(PluginSession &Session,
                                  PluginTaskContext &Task,
                                  NevercInterfaceID PhaseID,
                                  const NevercPhaseRoute &Route,
                                  NevercArtifactHandle Artifact) {
  if (&Task.session() != &Session)
    return executionError("phase task belongs to a different session");
  if (Task.checkCancelled().Code != NEVERC_STATUS_OK)
    return executionError("phase task is cancelled or no longer active");
  if (Error E = freeze())
    return E;
  const PluginPhaseDefinition *Phase = Graph.find(PhaseID);
  if (!Phase)
    return executionError("cannot notify an unknown phase");
  constexpr NevercPhasePolicy ControlPolicy =
      NEVERC_PHASE_INTERCEPTABLE | NEVERC_PHASE_REPLACEABLE |
      NEVERC_PHASE_SKIPPABLE_WITH_PROOF | NEVERC_PHASE_SEALED_HOST_GATE;
  if ((Phase->Policy & NEVERC_PHASE_OBSERVABLE) == 0 ||
      (Phase->Policy & ControlPolicy) != 0 ||
      !samePluginInterfaceID(Phase->InputArtifact, Phase->OutputArtifact))
    return executionError("phase is not a read-only lifecycle event");
  auto RouteKey = copyRouteIdentity(Route);
  if (!RouteKey)
    return RouteKey.takeError();
  if (llvm::any_of(ActivePhases, [&](const ActivePhaseInvocation &Active) {
        return Active.Task == &Task &&
               samePluginInterfaceID(Active.Phase, PhaseID);
      }))
    return executionError("phase '" + Phase->CanonicalName +
                          "' recursively invoked itself");

  void *RawArtifact = nullptr;
  NevercStatus ArtifactStatus =
      Task.handles().resolve(Artifact, PluginArtifactHandleKind, &RawArtifact);
  if (ArtifactStatus.Code != NEVERC_STATUS_OK)
    return executionError("event artifact handle is invalid");
  auto *ArtifactState = static_cast<CandidateState *>(RawArtifact);
  if (ArtifactState->IsCandidate || ArtifactState->Consumed ||
      ArtifactState->Generation == 0 ||
      !samePluginInterfaceID(ArtifactState->Type->id(), Phase->InputArtifact))
    return executionError("event artifact has the wrong type or state");

  bool Succeeded = false;
  auto CancelOnFailure = make_scope_exit([&] {
    if (!Succeeded)
      Session.cancel();
  });
  ActivePhases.push_back({&Task, PhaseID});
  auto PopPhase = make_scope_exit([] { ActivePhases.pop_back(); });

  NevercPhaseFrame Frame{};
  Frame.Header = {sizeof(Frame), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  Frame.Session = Session.handle();
  Frame.Task = Task.handle();
  Frame.Phase = PhaseID;
  Frame.Route = Route;
  Frame.Input = Artifact;
  Frame.CurrentOutput = Artifact;
  ChainContext Context{*this, Session, Task, *Phase, Frame};

  auto InvokePoint = [&](NevercObserverPoint Point, bool Reverse) -> Error {
    if ((Phase->ObserverPoints & Point) == 0)
      return Error::success();
    auto InvokeOne = [&](const ObserverBinding &Binding) -> Error {
      if (!samePluginInterfaceID(Binding.Descriptor.Phase, PhaseID) ||
          (Binding.Descriptor.Points & Point) == 0)
        return Error::success();
      NevercStatus Status = invokePluginCallback(
          Context, Binding.PluginID, Phase->CanonicalName + "/observer", [&] {
            return Binding.Descriptor.Callback(&Frame, Point,
                                               Binding.Descriptor.UserData);
          });
      if (!validateCallbackStatus(Context, "Observer", Binding.PluginID,
                                  Status))
        return executionError(Context.Failure);
      return Error::success();
    };
    if (!Reverse) {
      for (const ObserverBinding &Binding : Observers)
        if (Error E = InvokeOne(Binding))
          return E;
    } else {
      for (auto It = Observers.rbegin(); It != Observers.rend(); ++It)
        if (Error E = InvokeOne(*It))
          return E;
    }
    return Error::success();
  };

  if (Error E = InvokePoint(NEVERC_OBSERVER_BEFORE, false))
    return E;
  if (Error E = InvokePoint(NEVERC_OBSERVER_AFTER, true))
    return E;
  if (Error E = InvokePoint(NEVERC_OBSERVER_AFTER_COMMIT, false))
    return E;
  Succeeded = true;
  return Error::success();
}

} // namespace neverc::plugin
