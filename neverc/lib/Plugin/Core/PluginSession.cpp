#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include <algorithm>
#include <chrono>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

thread_local std::vector<const PluginModule *> ActivePluginCallbacks;

Error sessionError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus sessionStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

Error validateCallbackStatus(const PluginModule &Module, StringRef Callback,
                             NevercStatus Status) {
  if (Status.Code == NEVERC_STATUS_OK) {
    if (Status.Flags == 0 && Status.Detail == 0)
      return Error::success();
    return sessionError("plugin '" + Module.descriptor().PluginID +
                        "' callback '" + Callback +
                        "' returned an invalid success status");
  }
  if (Status.Code < NEVERC_STATUS_INVALID_ARGUMENT ||
      Status.Code > NEVERC_STATUS_NOT_FOUND)
    return sessionError("plugin '" + Module.descriptor().PluginID +
                        "' callback '" + Callback +
                        "' returned an unknown status code");
  return sessionError("plugin '" + Module.descriptor().PluginID +
                      "' callback '" + Callback +
                      "' failed with status code " + Twine(Status.Code));
}

} // namespace

struct PluginSession::PluginState {
  explicit PluginState(std::shared_ptr<const PluginModule> ModuleValue)
      : Module(std::move(ModuleValue)) {}

  std::shared_ptr<const PluginModule> Module;
  void *State = nullptr;
  bool Begun = false;
  std::recursive_mutex CallbackMutex;
};

PluginSession::PluginSession(
    PluginProcessServices &ProcessServicesValue,
    std::vector<std::shared_ptr<const PluginModule>> ModulesValue,
    PluginOptionParseResult OptionsValue,
    std::vector<uint64_t> AncestorSessionOwnersValue)
    : ProcessServices(ProcessServicesValue), Modules(std::move(ModulesValue)),
      AncestorSessionOwners(std::move(AncestorSessionOwnersValue)),
      Options(std::move(OptionsValue)) {
  PluginStates.reserve(Modules.size());
  for (const auto &Module : Modules)
    PluginStates.push_back(std::make_unique<PluginState>(Module));
}

PluginSession::~PluginSession() {
  Error E = end();
  if (!isEnded()) {
    if (E) {
      auto Message = toString(std::move(E));
      report_fatal_error(
          Twine("PluginSession destroyed before successful end: ") +
              Message.str(),
          /*gen_crash_diag=*/false);
    }
    report_fatal_error(
        "PluginSession end returned success without reaching the ended state",
        /*gen_crash_diag=*/false);
  }
  consumeError(std::move(E));
}

Expected<std::unique_ptr<PluginSession>>
PluginSession::create(PluginProcessServices &ProcessServices,
                      PluginActivationPlan &Plan,
                      PluginOptionParseResult Options) {
  if (Error E = activatePluginPlan(ProcessServices, Plan))
    return std::move(E);
  return createFromModules(ProcessServices, Plan.plugins(),
                           std::move(Options), {});
}

Expected<std::unique_ptr<PluginSession>>
PluginSession::createFromModules(
    PluginProcessServices &ProcessServices,
    ArrayRef<std::shared_ptr<const PluginModule>> Modules,
    PluginOptionParseResult Options,
    std::vector<uint64_t> AncestorSessionOwners) {
  std::vector<std::shared_ptr<const PluginModule>> OwnedModules(
      Modules.begin(), Modules.end());
  auto Session = std::unique_ptr<PluginSession>(
      new PluginSession(ProcessServices, std::move(OwnedModules),
                        std::move(Options),
                        std::move(AncestorSessionOwners)));
  if (Error E = Session->initialize()) {
    std::lock_guard<std::mutex> Lock(Session->LifecycleMutex);
    Session->Ended = true;
    return std::move(E);
  }
  return std::move(Session);
}

Error PluginSession::initialize() {
  SessionLease = ProcessServices.registry().acquireSessionLease();
  if (!SessionLease)
    return sessionError(
        "plugin registry refused a new session activity lease");
  Snapshot = ProcessServices.registry().acquireSnapshot();
  if (!Snapshot) {
    SessionLease.reset();
    return sessionError("plugin registry refused a session snapshot");
  }
  RegistryGeneration = Snapshot->generation();

  for (const auto &Module : Modules) {
    const PluginModule *Published =
        Snapshot->findByID(Module->descriptor().PluginID);
    if (Published != Module.get() || !Module->processBegun() ||
        !Module->registered()) {
      Snapshot.reset();
      SessionLease.reset();
      return sessionError("plugin '" + Module->descriptor().PluginID +
                          "' is not active in the session snapshot");
    }
  }

  auto Owner = ProcessServices.allocateOwnerToken();
  if (!Owner) {
    Snapshot.reset();
    SessionLease.reset();
    return Owner.takeError();
  }
  Handle.Owner = *Owner;
  Handle.Value =
      (UINT64_C(2) << 48) | (UINT64_C(1) << 32) | UINT64_C(1);
  if (Error E = ProcessServices.registerSessionScope(Handle, *this)) {
    Snapshot.reset();
    SessionLease.reset();
    return std::move(E);
  }
  RegisteredScope = true;
  HandleArena = std::make_unique<PluginHandleArena>(
      ProcessServices, Handle.Owner, Handle.Owner);
  if (Error E = ProcessServices.prepareSessionScope(Handle, *this)) {
    HandleArena->invalidateAll();
    ProcessServices.unregisterSessionScope(Handle);
    RegisteredScope = false;
    Snapshot.reset();
    SessionLease.reset();
    return E;
  }

  for (auto &State : PluginStates) {
    const PluginDescriptorRecord &Descriptor = State->Module->descriptor();
    if (!State->Module->hasSessionBegin())
      continue;
    void *OutState = nullptr;
    auto Result = invokeCallback(
        Descriptor.PluginID, "SessionBegin",
        [&] {
          return State->Module->invokeSessionBegin(
              &ProcessServices.coreAPI(), Handle,
              State->Module->processState(), &OutState);
        },
        false);
    if (!Result) {
      Error Primary = Result.takeError();
      Error Cleanup = rollbackBegunPlugins();
      HandleArena->invalidateAll();
      ProcessServices.unregisterSessionScope(Handle);
      RegisteredScope = false;
      Snapshot.reset();
      SessionLease.reset();
      {
        std::lock_guard<std::mutex> Lock(LifecycleMutex);
        Ended = true;
      }
      return joinErrors(std::move(Primary), std::move(Cleanup));
    }
    if (Error E =
            validateCallbackStatus(*State->Module, "SessionBegin", *Result)) {
      if (Result->Code != NEVERC_STATUS_OK && OutState != nullptr)
        E = joinErrors(
            std::move(E),
            sessionError("failed SessionBegin returned a non-null state"));
      Error Cleanup = rollbackBegunPlugins();
      HandleArena->invalidateAll();
      ProcessServices.unregisterSessionScope(Handle);
      RegisteredScope = false;
      Snapshot.reset();
      SessionLease.reset();
      {
        std::lock_guard<std::mutex> Lock(LifecycleMutex);
        Ended = true;
      }
      return joinErrors(std::move(E), std::move(Cleanup));
    }
    State->State = OutState;
    State->Begun = true;
  }
  return Error::success();
}

Error PluginSession::rollbackBegunPlugins() {
  Error CleanupErrors = Error::success();
  for (auto It = PluginStates.rbegin(); It != PluginStates.rend(); ++It) {
    PluginState &State = **It;
    if (!State.Begun)
      continue;
    const PluginDescriptorRecord &Descriptor = State.Module->descriptor();
    if (State.Module->hasSessionEnd()) {
      auto Result = invokeCallback(
          Descriptor.PluginID, "SessionEnd",
          [&] {
            return State.Module->invokeSessionEnd(
                &ProcessServices.coreAPI(), Handle,
                State.Module->processState(), State.State);
          },
          false);
      if (!Result)
        CleanupErrors =
            joinErrors(std::move(CleanupErrors), Result.takeError());
      else if (Error E =
                   validateCallbackStatus(*State.Module, "SessionEnd",
                                          *Result))
        CleanupErrors =
            joinErrors(std::move(CleanupErrors), std::move(E));
    }
    State.State = nullptr;
    State.Begun = false;
  }
  return CleanupErrors;
}

Expected<std::unique_ptr<PluginSession>>
PluginSession::createChild() const {
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (Ending || Ended)
      return sessionError(
          "cannot create a child from an ending plugin session");
  }
  std::vector<uint64_t> Ancestors = AncestorSessionOwners;
  Ancestors.push_back(Handle.Owner);
  return createFromModules(ProcessServices, Modules, Options,
                           std::move(Ancestors));
}

Expected<std::unique_ptr<PluginTaskContext>>
PluginSession::createTask(NevercTaskKind Kind, PluginTaskContext *Parent) {
  return PluginTaskContext::create(*this, Kind, Parent);
}

Expected<NevercStatus> PluginSession::invokeCallback(
    StringRef PluginID, StringRef CallbackName,
    std::function<NevercStatus()> Callback, bool CheckCancellation,
    PluginTaskContext *CurrentTask, uint64_t *OutDiagnosticTransactionID,
    bool DeferRecoverableDisposition, const void *ArtifactMutationDomain) {
  if (OutDiagnosticTransactionID)
    *OutDiagnosticTransactionID = 0;
  if (!Callback)
    return sessionError("plugin callback body is empty");
  PluginState *State = findPluginState(PluginID);
  if (!State)
    return sessionError("plugin '" + PluginID +
                        "' is not selected in this session");
  if (CurrentTask && &CurrentTask->session() != this)
    return sessionError("plugin callback task belongs to a different session");
  if (CheckCancellation && isCancelled())
    return sessionStatus(NEVERC_STATUS_CANCELLED);
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (Ended || (Ending && CheckCancellation))
      return sessionError("cannot invoke callback '" + CallbackName +
                          "' on an ending plugin session");
    ActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
  }
  auto ReleaseActiveCallback = make_scope_exit(
      [&] { ActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel); });

  RegistryActivityLease CallbackLease =
      ProcessServices.registry().acquireCallbackLease();
  if (!CallbackLease)
    return sessionError("registry refused callback '" + CallbackName +
                        "' for plugin '" + PluginID + "'");

  const PluginDescriptorRecord &Descriptor = State->Module->descriptor();
  bool IsReentrant =
      llvm::is_contained(ActivePluginCallbacks, State->Module.get());
  if (IsReentrant && Descriptor.Reentrancy != NEVERC_REENTRANCY_ALLOWED)
    return sessionStatus(NEVERC_STATUS_REENTRANCY_DENIED);

  std::unique_lock<std::recursive_mutex> CallbackLock;
  if (Descriptor.Concurrency == NEVERC_CONCURRENCY_SESSION_SERIAL)
    CallbackLock = std::unique_lock<std::recursive_mutex>(State->CallbackMutex);
  else if (Descriptor.Concurrency == NEVERC_CONCURRENCY_PROCESS_SERIAL)
    CallbackLock = std::unique_lock<std::recursive_mutex>(
        ProcessServices.processSerialGate());

  if (CheckCancellation && isCancelled())
    return sessionStatus(NEVERC_STATUS_CANCELLED);

  try {
    uint64_t DiagnosticTransactionID = Diagnostics.beginTransaction();

    ActivePluginCallbacks.push_back(State->Module.get());
    auto PopActivePluginCallback =
        make_scope_exit([&] { ActivePluginCallbacks.pop_back(); });

    auto EnteredScope = ProcessServices.enterCallbackScope(
        *this, CurrentTask, PluginID, CallbackName, DiagnosticTransactionID,
        ArtifactMutationDomain);
    if (!EnteredScope) {
      consumeError(EnteredScope.takeError());
      return sessionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    auto PopCallbackScope = make_scope_exit(
        [&] { ProcessServices.leaveCallbackScope(*this, CurrentTask); });

    if (OutDiagnosticTransactionID)
      *OutDiagnosticTransactionID = DiagnosticTransactionID;
    std::string TraceName =
        (Twine("plugin:") + PluginID + "/" + CallbackName).str();
    TimeTraceScope TimeScope(TraceName);
    const auto CallbackStart = std::chrono::steady_clock::now();
    auto RecordStats = [&](bool IsError) {
      const auto Elapsed = std::chrono::steady_clock::now() - CallbackStart;
      auto Nanos =
          std::chrono::duration_cast<std::chrono::nanoseconds>(Elapsed).count();
      CallbackStats.record(PluginID, CallbackName,
                           Nanos < 0 ? 0 : static_cast<uint64_t>(Nanos),
                           IsError);
    };

    NevercStatus Result;
    try {
      Result = Callback();
    } catch (...) {
      RecordStats(/*IsError=*/true);
      Diagnostics.emitImplicit(*this, CurrentTask, PluginID, CallbackName,
                               (Twine("plugin callback '") + CallbackName +
                                "' raised an exception across the C ABI")
                                   .str(),
                               DiagnosticTransactionID);
      cancel();
      return sessionStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
    }
    RecordStats(Result.Code != NEVERC_STATUS_OK &&
                Result.Code != NEVERC_STATUS_CANCELLED);
    if (Result.Code != NEVERC_STATUS_OK &&
        Result.Code != NEVERC_STATUS_CANCELLED) {
      bool DeferredRecoverable =
          DeferRecoverableDisposition &&
          (Result.Flags & NEVERC_STATUS_FLAG_RECOVERABLE) != 0;
      if (!DeferredRecoverable &&
          !Diagnostics.ownsDetail(Result.Detail, PluginID,
                                  DiagnosticTransactionID)) {
        Diagnostics.emitImplicit(
            *this, CurrentTask, PluginID, CallbackName,
            (Twine("plugin callback '") + CallbackName +
             "' failed without a structured diagnostic (status " +
             Twine(static_cast<unsigned>(Result.Code)) + ")")
                .str(),
            DiagnosticTransactionID);
      }
      if (!DeferredRecoverable)
        cancel();
    }
    return Result;
  } catch (...) {
    return sessionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
}

Error PluginSession::registerDeferredCallback(
    StringRef Domain, StringRef CallbackID, StringRef PluginID,
    DeferredCallback Callback) {
  if (Domain.empty() || Domain.contains('\0') || CallbackID.empty() ||
      CallbackID.contains('\0') || PluginID.empty() || !Callback)
    return sessionError("deferred callback registration is invalid");
  if (!findPluginState(PluginID))
    return sessionError("deferred callback plugin is not selected");
  std::string Key =
      (Twine(Domain) + Twine("\x1f") + CallbackID).str();
  std::lock_guard<std::mutex> Lock(DeferredCallbackMutex);
  if (DeferredCallbacks.contains(Key))
    return sessionError("deferred callback ID is already registered");
  DeferredCallbacks[Key] = {PluginID.str(), std::move(Callback)};
  return Error::success();
}

void PluginSession::unregisterDeferredCallback(
    StringRef Domain, StringRef CallbackID) {
  std::string Key =
      (Twine(Domain) + Twine("\x1f") + CallbackID).str();
  std::lock_guard<std::mutex> Lock(DeferredCallbackMutex);
  DeferredCallbacks.erase(Key);
}

Expected<NevercStatus> PluginSession::invokeDeferredCallback(
    StringRef Domain, StringRef CallbackID, const void *Context,
    int32_t *OutExitCode) {
  if (Domain.empty() || CallbackID.empty() || !OutExitCode)
    return sessionError("deferred callback invocation is invalid");
  std::string Key =
      (Twine(Domain) + Twine("\x1f") + CallbackID).str();
  DeferredCallbackRecord Record;
  {
    std::lock_guard<std::mutex> Lock(DeferredCallbackMutex);
    auto It = DeferredCallbacks.find(Key);
    if (It == DeferredCallbacks.end())
      return sessionError("deferred callback is not registered");
    Record = It->second;
  }
  return invokeCallback(
      Record.PluginID,
      (Twine("deferred/") + Domain + "/" + CallbackID).str(),
      [&] { return Record.Callback(Context, OutExitCode); });
}

StringRef PluginSession::currentCallbackPluginID() const {
  if (ActivePluginCallbacks.empty())
    return {};
  const PluginModule *Active = ActivePluginCallbacks.back();
  for (const auto &Module : Modules)
    if (Module.get() == Active)
      return Module->descriptor().PluginID;
  return {};
}

Error PluginSession::end() {
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (Ended)
      return Error::success();
    if (Ending)
      return sessionError("plugin session end is already in progress");
    uint64_t Tasks = ActiveTasks.load(std::memory_order_acquire);
    if (Tasks != 0) {
      std::string Message =
          (Twine("cannot end plugin session while ") + Twine(Tasks) +
           " active task(s) remain")
              .str();
      for (uint64_t Owner : ActiveTaskOwners)
        Message += " " + std::to_string(Owner);
      return sessionError(Message);
    }
    uint64_t Callbacks =
        ActiveCallbacks.load(std::memory_order_acquire);
    if (Callbacks != 0)
      return sessionError("cannot end plugin session while " +
                          Twine(Callbacks) +
                          " callback(s) are active");
    Ending = true;
  }

  {
    std::lock_guard<std::mutex> Lock(DeferredCallbackMutex);
    DeferredCallbacks.clear();
  }

  Error CleanupErrors = rollbackBegunPlugins();
  if (HandleArena)
    HandleArena->invalidateAll();
  if (RegisteredScope) {
    ProcessServices.unregisterSessionScope(Handle);
    RegisteredScope = false;
  }
  Snapshot.reset();
  SessionLease.reset();
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    Ending = false;
    Ended = true;
  }
  return CleanupErrors;
}

bool PluginSession::isEnded() const {
  std::lock_guard<std::mutex> Lock(LifecycleMutex);
  return Ended;
}

NevercStatus PluginSession::queryState(StringRef PluginID,
                                       void **OutState) const {
  if (!OutState)
    return sessionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutState = nullptr;
  const PluginState *State = findPluginState(PluginID);
  if (!State)
    return sessionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  *OutState = State->State;
  return neverc_status_ok();
}

PluginSession::PluginState *
PluginSession::findPluginState(StringRef PluginID) {
  auto It = llvm::find_if(PluginStates, [&](const auto &State) {
    return State->Module->descriptor().PluginID == PluginID;
  });
  return It == PluginStates.end() ? nullptr : It->get();
}

const PluginSession::PluginState *
PluginSession::findPluginState(StringRef PluginID) const {
  auto It = llvm::find_if(PluginStates, [&](const auto &State) {
    return State->Module->descriptor().PluginID == PluginID;
  });
  return It == PluginStates.end() ? nullptr : It->get();
}

Error PluginSession::registerTask(PluginTaskContext &Task) {
  std::lock_guard<std::mutex> Lock(LifecycleMutex);
  if (Ending || Ended)
    return sessionError(
        "cannot create a task in an ending plugin session");
  if (Cancelled.load(std::memory_order_acquire))
    return sessionError(
        "cannot create a task in a cancelled plugin session");
  ActiveTaskOwners.push_back(Task.handle().Owner);
  ActiveTasks.fetch_add(1, std::memory_order_acq_rel);
  return Error::success();
}

void PluginSession::unregisterTask(PluginTaskContext &Task) {
  std::lock_guard<std::mutex> Lock(LifecycleMutex);
  ActiveTaskOwners.erase(
      std::remove(ActiveTaskOwners.begin(), ActiveTaskOwners.end(),
                  Task.handle().Owner),
      ActiveTaskOwners.end());
  ActiveTasks.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace neverc::plugin
