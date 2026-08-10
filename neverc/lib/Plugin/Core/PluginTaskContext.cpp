#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error taskError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus taskStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool validTaskKind(NevercTaskKind Kind) {
  return Kind >= NEVERC_TASK_INVOCATION && Kind <= NEVERC_TASK_DYNCODE;
}

Error validateTaskCallbackStatus(const PluginModule &Module,
                                 StringRef Callback,
                                 NevercStatus Status) {
  if (Status.Code == NEVERC_STATUS_OK) {
    if (Status.Flags == 0 && Status.Detail == 0)
      return Error::success();
    return taskError("plugin '" + Module.descriptor().PluginID +
                     "' callback '" + Callback +
                     "' returned an invalid success status");
  }
  if (Status.Code < NEVERC_STATUS_INVALID_ARGUMENT ||
      Status.Code > NEVERC_STATUS_NOT_FOUND)
    return taskError("plugin '" + Module.descriptor().PluginID +
                     "' callback '" + Callback +
                     "' returned an unknown status code");
  return taskError("plugin '" + Module.descriptor().PluginID +
                   "' callback '" + Callback +
                   "' failed with status code " + Twine(Status.Code));
}

} // namespace

struct PluginTaskContext::PluginState {
  explicit PluginState(std::shared_ptr<const PluginModule> ModuleValue)
      : Module(std::move(ModuleValue)) {}

  std::shared_ptr<const PluginModule> Module;
  void *State = nullptr;
  bool Begun = false;
};

PluginTaskContext::PluginTaskContext(PluginSession &SessionValue,
                                     NevercTaskKind KindValue,
                                     PluginTaskContext *ParentValue)
    : Session(SessionValue),
      ProcessServices(SessionValue.ProcessServices), Kind(KindValue),
      Parent(ParentValue) {
  PluginStates.reserve(Session.Modules.size());
  for (const auto &Module : Session.Modules)
    PluginStates.push_back(std::make_unique<PluginState>(Module));
}

PluginTaskContext::~PluginTaskContext() {
  Error E = end();
  if (!isEnded()) {
    if (E) {
      auto Message = toString(std::move(E));
      report_fatal_error(
          Twine("PluginTaskContext destroyed before successful end: ") +
              Message.str(),
          /*gen_crash_diag=*/false);
    }
    report_fatal_error(
        "PluginTaskContext end returned success without reaching the ended "
        "state",
        /*gen_crash_diag=*/false);
  }
  consumeError(std::move(E));
}

Expected<std::unique_ptr<PluginTaskContext>>
PluginTaskContext::create(PluginSession &Session, NevercTaskKind Kind,
                          PluginTaskContext *Parent) {
  auto Task = std::unique_ptr<PluginTaskContext>(
      new PluginTaskContext(Session, Kind, Parent));
  if (Error E = Task->initialize()) {
    std::lock_guard<std::mutex> Lock(Task->LifecycleMutex);
    Task->Ended = true;
    return std::move(E);
  }
  return std::move(Task);
}

Error PluginTaskContext::initialize() {
  if (!validTaskKind(Kind))
    return taskError("plugin task has an invalid task kind");
  if (Parent) {
    if (&Parent->Session != &Session)
      return taskError(
          "plugin child task belongs to a different session");
    std::lock_guard<std::mutex> ParentLock(Parent->LifecycleMutex);
    if (Parent->Ending || Parent->Ended)
      return taskError(
          "cannot create a child task from an ending parent");
    Parent->ActiveChildren.fetch_add(1, std::memory_order_acq_rel);
  }

  auto Owner = ProcessServices.allocateOwnerToken();
  if (!Owner) {
    if (Parent)
      Parent->ActiveChildren.fetch_sub(1, std::memory_order_acq_rel);
    return Owner.takeError();
  }
  Handle.Owner = *Owner;
  Handle.Value =
      (UINT64_C(3) << 48) | (UINT64_C(1) << 32) | UINT64_C(1);

  if (Error E = Session.registerTask(*this)) {
    if (Parent)
      Parent->ActiveChildren.fetch_sub(1, std::memory_order_acq_rel);
    return std::move(E);
  }
  RegisteredWithSession = true;
  if (Error E = ProcessServices.registerTaskScope(Handle, *this)) {
    Session.unregisterTask(*this);
    RegisteredWithSession = false;
    if (Parent)
      Parent->ActiveChildren.fetch_sub(1, std::memory_order_acq_rel);
    return std::move(E);
  }
  RegisteredScope = true;
  HandleArena = std::make_unique<PluginHandleArena>(
      ProcessServices, Session.handle().Owner, Handle.Owner);

  auto failInitialization = [&](Error Primary) -> Error {
    Error Cleanup = rollbackBegunPlugins();
    HandleArena->invalidateAll();
    ProcessServices.unregisterTaskScope(Handle);
    RegisteredScope = false;
    Session.unregisterTask(*this);
    RegisteredWithSession = false;
    if (Parent)
      Parent->ActiveChildren.fetch_sub(1, std::memory_order_acq_rel);
    {
      std::lock_guard<std::mutex> Lock(LifecycleMutex);
      Ended = true;
    }
    return joinErrors(std::move(Primary), std::move(Cleanup));
  };

  for (auto &State : PluginStates) {
    const PluginDescriptorRecord &Descriptor = State->Module->descriptor();
    if (!State->Module->hasTaskBegin())
      continue;
    void *SessionState = nullptr;
    NevercStatus SessionStateStatus =
        Session.queryState(Descriptor.PluginID, &SessionState);
    if (SessionStateStatus.Code != NEVERC_STATUS_OK)
      return failInitialization(
          taskError("cannot resolve plugin session state for TaskBegin"));

    void *OutState = nullptr;
    auto Result = Session.invokeCallback(
        Descriptor.PluginID, "TaskBegin",
        [&] {
          return State->Module->invokeTaskBegin(
              &ProcessServices.coreAPI(), Handle, Kind,
              State->Module->processState(), SessionState, &OutState);
        },
        false, this);
    if (!Result)
      return failInitialization(Result.takeError());
    if (Error E = validateTaskCallbackStatus(*State->Module, "TaskBegin",
                                             *Result)) {
      if (Result->Code != NEVERC_STATUS_OK && OutState != nullptr)
        E = joinErrors(
            std::move(E),
            taskError("failed TaskBegin returned a non-null state"));
      return failInitialization(std::move(E));
    }
    State->State = OutState;
    State->Begun = true;
  }
  return Error::success();
}

Error PluginTaskContext::rollbackBegunPlugins() {
  Error CleanupErrors = Error::success();
  for (auto It = PluginStates.rbegin(); It != PluginStates.rend(); ++It) {
    PluginState &State = **It;
    if (!State.Begun)
      continue;
    const PluginDescriptorRecord &Descriptor = State.Module->descriptor();
    void *SessionState = nullptr;
    NevercStatus SessionStateStatus =
        Session.queryState(Descriptor.PluginID, &SessionState);
    if (SessionStateStatus.Code != NEVERC_STATUS_OK) {
      CleanupErrors = joinErrors(
          std::move(CleanupErrors),
          taskError("cannot resolve plugin session state for TaskEnd"));
    } else if (State.Module->hasTaskEnd()) {
      auto Result = Session.invokeCallback(
          Descriptor.PluginID, "TaskEnd",
          [&] {
            return State.Module->invokeTaskEnd(
                &ProcessServices.coreAPI(), Handle, Kind,
                State.Module->processState(), SessionState, State.State);
          },
          false, this);
      if (!Result)
        CleanupErrors =
            joinErrors(std::move(CleanupErrors), Result.takeError());
      else if (Error E = validateTaskCallbackStatus(
                   *State.Module, "TaskEnd", *Result))
        CleanupErrors =
            joinErrors(std::move(CleanupErrors), std::move(E));
    }
    State.State = nullptr;
    State.Begun = false;
  }
  return CleanupErrors;
}

Error PluginTaskContext::end() {
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (Ended)
      return Error::success();
    if (Ending)
      return taskError("plugin task end is already in progress");
    uint64_t Children = ActiveChildren.load(std::memory_order_acquire);
    if (Children != 0)
      return taskError("cannot end plugin task while " + Twine(Children) +
                       " child task(s) remain");
    uint64_t Callbacks =
        ActiveCallbacks.load(std::memory_order_acquire);
    if (Callbacks != 0)
      return taskError("cannot end plugin task while " + Twine(Callbacks) +
                       " callback(s) are active");
    Ending = true;
  }

  Error CleanupErrors =
      RegisteredScope ? ProcessServices.prepareTaskScopeEnd(Handle)
                      : Error::success();
  CleanupErrors =
      joinErrors(std::move(CleanupErrors), rollbackBegunPlugins());
  if (HandleArena)
    HandleArena->invalidateAll();
  if (RegisteredScope) {
    ProcessServices.unregisterTaskScope(Handle);
    RegisteredScope = false;
  }
  if (RegisteredWithSession) {
    Session.unregisterTask(*this);
    RegisteredWithSession = false;
  }
  if (Parent)
    Parent->ActiveChildren.fetch_sub(1, std::memory_order_acq_rel);
  PluginStates.clear();
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    Ending = false;
    Ended = true;
  }
  return CleanupErrors;
}

Expected<NevercStatus> PluginTaskContext::invokeCallback(
    StringRef PluginID, StringRef CallbackName,
    std::function<NevercStatus()> Callback, bool CheckCancellation,
    uint64_t *OutDiagnosticTransactionID, bool DeferRecoverableDisposition,
    const void *ArtifactMutationDomain) {
  {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (Ending || Ended)
      return taskError("cannot invoke a callback on an ending plugin task");
    ActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
  }
  auto ReleaseActiveCallback = make_scope_exit(
      [&] { ActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel); });
  return Session.invokeCallback(
      PluginID, CallbackName, std::move(Callback), CheckCancellation, this,
      OutDiagnosticTransactionID, DeferRecoverableDisposition,
      ArtifactMutationDomain);
}

std::optional<uint64_t>
PluginTaskContext::currentArtifactMutationCapability(const void *Domain) const {
  return ProcessServices.currentArtifactMutationCapability(*this, Domain);
}

bool PluginTaskContext::validatesArtifactMutationCapability(
    const void *Domain, uint64_t Token) const {
  return ProcessServices.validatesArtifactMutationCapability(*this, Domain,
                                                             Token);
}

void PluginTaskContext::retainCallbackContext(std::shared_ptr<void> Context) {
  assert(Context && "cannot retain a null plugin callback context");
  std::lock_guard<std::mutex> Lock(LifecycleMutex);
  RetainedCallbackContexts.push_back(std::move(Context));
}

bool PluginTaskContext::isEnding() const {
  std::lock_guard<std::mutex> Lock(LifecycleMutex);
  return Ending;
}

bool PluginTaskContext::isEnded() const {
  std::lock_guard<std::mutex> Lock(LifecycleMutex);
  return Ended;
}

NevercStatus PluginTaskContext::queryState(StringRef PluginID,
                                           void **OutState) const {
  if (!OutState)
    return taskStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutState = nullptr;
  auto It = llvm::find_if(PluginStates, [&](const auto &State) {
    return State->Module->descriptor().PluginID == PluginID;
  });
  if (It == PluginStates.end())
    return taskStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  *OutState = (*It)->State;
  return neverc_status_ok();
}

NevercStatus PluginTaskContext::checkCancelled() const {
  return Session.isCancelled() ? taskStatus(NEVERC_STATUS_CANCELLED)
                               : neverc_status_ok();
}

} // namespace neverc::plugin
