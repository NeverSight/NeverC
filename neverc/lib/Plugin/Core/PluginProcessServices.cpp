#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/CoreAPIBridge.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus scopeStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

Error scopeError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

struct CallbackScope {
  PluginProcessServices *ProcessServices = nullptr;
  PluginSession *Session = nullptr;
  PluginTaskContext *Task = nullptr;
  std::string PluginID;
  std::string CallbackName;
  uint64_t DiagnosticTransactionID = 0;
};

thread_local std::vector<CallbackScope> CallbackScopes;

const CallbackScope *
currentCallbackScope(const PluginProcessServices &ProcessServices) {
  for (auto It = CallbackScopes.rbegin(); It != CallbackScopes.rend(); ++It)
    if (It->ProcessServices == &ProcessServices)
      return &*It;
  return nullptr;
}

} // namespace

OwnerTokenAllocator::OwnerTokenAllocator(uint64_t FirstToken)
    : NextToken(FirstToken) {}

Expected<uint64_t> OwnerTokenAllocator::allocate() {
  uint64_t Current = NextToken.load(std::memory_order_relaxed);
  for (;;) {
    if (Current == 0)
      return createStringError(errc::not_enough_memory,
                               "plugin owner token space is exhausted");
    uint64_t Next = Current == std::numeric_limits<uint64_t>::max()
                        ? 0
                        : Current + 1;
    if (NextToken.compare_exchange_weak(Current, Next,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed))
      return Current;
  }
}

PluginProcessServices::PluginProcessServices(
    std::string HostBuildID, uint32_t LLVMMajor,
    ArrayRef<StringRef> StaticOptionSpellings)
    : Options(StaticOptionSpellings),
      Registry(std::move(HostBuildID), LLVMMajor, &Interfaces, &CoreAPI,
               &Options) {
  initializeCoreAPI(CoreAPI, *this);
  NevercInterfaceID CoreInterface{NEVERC_INTERFACE_CORE_HIGH,
                                  NEVERC_INTERFACE_CORE_LOW};
  cantFail(Interfaces.registerInterface(CoreInterface, NEVERC_INTERFACE_STABLE,
                                        &CoreAPI, {}));
}

std::shared_mutex &PluginProcessServices::llvmOptionGate() {
  return pluginLLVMOptionGate();
}

Error PluginProcessServices::registerSessionScope(NevercSessionHandle Handle,
                                                  PluginSession &Session) {
  if (Handle.Owner == 0 || Handle.Value == 0)
    return scopeError("cannot register a null plugin session handle");
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  if (!Sessions.emplace(Handle.Owner, &Session).second)
    return scopeError("duplicate plugin session owner token");
  return Error::success();
}

Error PluginProcessServices::prepareSessionScope(
    NevercSessionHandle Handle, PluginSession &Session) {
  std::vector<std::shared_ptr<PluginHostService>> Services;
  {
    std::lock_guard<std::mutex> Lock(HostServiceMutex);
    for (const auto &Entry : HostServices)
      Services.push_back(Entry.second);
  }
  size_t Prepared = 0;
  for (; Prepared != Services.size(); ++Prepared) {
    if (Error E =
            Services[Prepared]->sessionScopeRegistered(Handle, Session)) {
      while (Prepared != 0)
        Services[--Prepared]->sessionScopeUnregistered(Handle);
      return E;
    }
  }
  return Error::success();
}

Error PluginProcessServices::validatePluginRegistrations(
    ArrayRef<std::shared_ptr<const PluginModule>> Modules) {
  std::vector<std::shared_ptr<PluginHostService>> Services;
  {
    std::lock_guard<std::mutex> Lock(HostServiceMutex);
    for (const auto &Entry : HostServices)
      Services.push_back(Entry.second);
  }
  for (const auto &Service : Services)
    if (Error E = Service->validatePluginRegistrations(Modules))
      return E;
  return Error::success();
}

void PluginProcessServices::unregisterSessionScope(
    NevercSessionHandle Handle) {
  bool Removed = false;
  {
    std::lock_guard<std::mutex> Lock(ScopeMutex);
    auto It = Sessions.find(Handle.Owner);
    if (It != Sessions.end() && It->second->handle().Value == Handle.Value) {
      Sessions.erase(It);
      Removed = true;
    }
  }
  if (!Removed)
    return;
  std::vector<std::shared_ptr<PluginHostService>> Services;
  {
    std::lock_guard<std::mutex> Lock(HostServiceMutex);
    for (const auto &Entry : HostServices)
      Services.push_back(Entry.second);
  }
  for (const auto &Service : Services)
    Service->sessionScopeUnregistered(Handle);
}

Error PluginProcessServices::registerTaskScope(NevercTaskHandle Handle,
                                               PluginTaskContext &Task) {
  if (Handle.Owner == 0 || Handle.Value == 0)
    return scopeError("cannot register a null plugin task handle");
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  if (!Tasks.emplace(Handle.Owner, &Task).second)
    return scopeError("duplicate plugin task owner token");
  return Error::success();
}

Error PluginProcessServices::prepareTaskScopeEnd(
    NevercTaskHandle Handle) {
  {
    std::lock_guard<std::mutex> Lock(ScopeMutex);
    auto It = Tasks.find(Handle.Owner);
    if (It == Tasks.end() || It->second->handle().Value != Handle.Value)
      return scopeError("cannot prepare an unregistered plugin task");
  }

  std::vector<std::shared_ptr<PluginHostService>> Services;
  {
    std::lock_guard<std::mutex> Lock(HostServiceMutex);
    for (const auto &Entry : HostServices)
      Services.push_back(Entry.second);
  }
  Error Errors = Error::success();
  for (const auto &Service : Services)
    Errors =
        joinErrors(std::move(Errors), Service->taskScopeEnding(Handle));
  return Errors;
}

void PluginProcessServices::unregisterTaskScope(NevercTaskHandle Handle) {
  bool Removed = false;
  {
    std::lock_guard<std::mutex> Lock(ScopeMutex);
    auto It = Tasks.find(Handle.Owner);
    if (It != Tasks.end() && It->second->handle().Value == Handle.Value) {
      Tasks.erase(It);
      Removed = true;
    }
  }
  if (!Removed)
    return;
  std::vector<std::shared_ptr<PluginHostService>> Services;
  {
    std::lock_guard<std::mutex> Lock(HostServiceMutex);
    for (const auto &Entry : HostServices)
      Services.push_back(Entry.second);
  }
  for (const auto &Service : Services)
    Service->taskScopeUnregistered(Handle);
}

NevercStatus
PluginProcessServices::querySessionState(NevercSessionHandle Handle,
                                         StringRef PluginID,
                                         void **OutState) {
  if (!OutState)
    return scopeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutState = nullptr;
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Sessions.find(Handle.Owner);
  if (It == Sessions.end() || It->second->handle().Value != Handle.Value)
    return scopeStatus(NEVERC_STATUS_STALE_HANDLE);
  const CallbackScope *Scope = currentCallbackScope(*this);
  if (!Scope)
    return scopeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Scope->Session != It->second)
    return scopeStatus(NEVERC_STATUS_WRONG_SESSION);
  return It->second->queryState(PluginID, OutState);
}

NevercStatus PluginProcessServices::queryTaskState(NevercTaskHandle Handle,
                                                   StringRef PluginID,
                                                   void **OutState) {
  if (!OutState)
    return scopeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutState = nullptr;
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Tasks.find(Handle.Owner);
  if (It == Tasks.end() || It->second->handle().Value != Handle.Value)
    return scopeStatus(NEVERC_STATUS_STALE_HANDLE);
  const CallbackScope *Scope = currentCallbackScope(*this);
  if (!Scope)
    return scopeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Scope->Task != It->second)
    return scopeStatus(NEVERC_STATUS_WRONG_SCOPE);
  return It->second->queryState(PluginID, OutState);
}

NevercStatus PluginProcessServices::queryPluginOptionValueCount(
    NevercSessionHandle Handle, StringRef PluginID, StringRef Spelling,
    uint64_t *OutCount) {
  if (!OutCount || PluginID.empty() || Spelling.empty())
    return scopeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Sessions.find(Handle.Owner);
  if (It == Sessions.end() || It->second->handle().Value != Handle.Value)
    return scopeStatus(NEVERC_STATUS_STALE_HANDLE);
  const CallbackScope *Scope = currentCallbackScope(*this);
  if (!Scope)
    return scopeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Scope->Session != It->second)
    return scopeStatus(NEVERC_STATUS_WRONG_SESSION);
  const ParsedPluginOption *Option =
      It->second->options().find(PluginID, Spelling);
  if (!Option)
    return scopeStatus(NEVERC_STATUS_NOT_FOUND);
  *OutCount = Option->Values.size();
  return neverc_status_ok();
}

NevercStatus PluginProcessServices::queryPluginOptionValue(
    NevercSessionHandle Handle, StringRef PluginID, StringRef Spelling,
    uint64_t Index, NevercStringView *OutValue) {
  if (!OutValue || PluginID.empty() || Spelling.empty())
    return scopeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = {};
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Sessions.find(Handle.Owner);
  if (It == Sessions.end() || It->second->handle().Value != Handle.Value)
    return scopeStatus(NEVERC_STATUS_STALE_HANDLE);
  const CallbackScope *Scope = currentCallbackScope(*this);
  if (!Scope)
    return scopeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Scope->Session != It->second)
    return scopeStatus(NEVERC_STATUS_WRONG_SESSION);
  const ParsedPluginOption *Option =
      It->second->options().find(PluginID, Spelling);
  if (!Option || Index >= Option->Values.size())
    return scopeStatus(NEVERC_STATUS_NOT_FOUND);
  const std::string &Value = Option->Values[static_cast<size_t>(Index)];
  *OutValue = {Value.data(), Value.size()};
  return neverc_status_ok();
}

NevercStatus
PluginProcessServices::checkCancelled(NevercTaskHandle Handle) {
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Tasks.find(Handle.Owner);
  if (It == Tasks.end() || It->second->handle().Value != Handle.Value)
    return scopeStatus(NEVERC_STATUS_STALE_HANDLE);
  const CallbackScope *Scope = currentCallbackScope(*this);
  if (!Scope)
    return scopeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Scope->Task != It->second)
    return scopeStatus(NEVERC_STATUS_WRONG_SCOPE);
  return It->second->checkCancelled();
}

NevercStatus PluginProcessServices::emitDiagnostic(
    const NevercDiagnosticDescriptor &Descriptor,
    NevercDiagnosticHandle &OutDiagnostic) {
  const CallbackScope *Scope = currentCallbackScope(*this);
  if (!Scope || !Scope->Session)
    return scopeStatus(NEVERC_STATUS_INVALID_STATE);
  return Scope->Session->diagnostics().emit(
      *Scope->Session, Scope->Task, Scope->PluginID,
      Scope->CallbackName, Scope->DiagnosticTransactionID, Descriptor,
      OutDiagnostic);
}

NevercStatus PluginProcessServices::classifyScopeOwner(
    uint64_t ExpectedSessionOwner, uint64_t ExpectedScopeOwner,
    uint64_t ActualOwner) {
  if (ExpectedSessionOwner == 0 || ExpectedScopeOwner == 0 ||
      ActualOwner == 0)
    return scopeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  uint64_t ActualSessionOwner = 0;
  if (auto SessionIt = Sessions.find(ActualOwner);
      SessionIt != Sessions.end()) {
    ActualSessionOwner = SessionIt->second->handle().Owner;
  } else if (auto TaskIt = Tasks.find(ActualOwner);
             TaskIt != Tasks.end()) {
    ActualSessionOwner = TaskIt->second->session().handle().Owner;
  } else {
    return scopeStatus(NEVERC_STATUS_STALE_HANDLE);
  }
  if (ActualSessionOwner != ExpectedSessionOwner)
    return scopeStatus(NEVERC_STATUS_WRONG_SESSION);
  if (ActualOwner != ExpectedScopeOwner)
    return scopeStatus(NEVERC_STATUS_WRONG_SCOPE);
  return neverc_status_ok();
}

PluginSession *
PluginProcessServices::findSessionScope(NevercSessionHandle Handle) {
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Sessions.find(Handle.Owner);
  if (It == Sessions.end() || It->second->handle().Value != Handle.Value)
    return nullptr;
  return It->second;
}

PluginTaskContext *
PluginProcessServices::findTaskScope(NevercTaskHandle Handle) {
  std::lock_guard<std::mutex> Lock(ScopeMutex);
  auto It = Tasks.find(Handle.Owner);
  if (It == Tasks.end() || It->second->handle().Value != Handle.Value)
    return nullptr;
  return It->second;
}

Error PluginProcessServices::registerHostService(
    NevercInterfaceID Interface,
    std::shared_ptr<PluginHostService> Service) {
  if ((Interface.High == 0 && Interface.Low == 0) || !Service)
    return scopeError("host service registration is invalid");
  std::lock_guard<std::mutex> Lock(HostServiceMutex);
  auto Key = std::make_pair(Interface.High, Interface.Low);
  if (!HostServices.emplace(Key, std::move(Service)).second)
    return scopeError("duplicate host service interface");
  return Error::success();
}

std::shared_ptr<PluginHostService> PluginProcessServices::findHostService(
    NevercInterfaceID Interface) const {
  std::lock_guard<std::mutex> Lock(HostServiceMutex);
  auto It = HostServices.find(std::make_pair(Interface.High, Interface.Low));
  return It == HostServices.end() ? nullptr : It->second;
}

void PluginProcessServices::enterCallbackScope(PluginSession &Session,
                                               PluginTaskContext *Task,
                                               StringRef PluginID,
                                               StringRef CallbackName,
                                               uint64_t DiagnosticTransactionID) {
  CallbackScopes.push_back({this, &Session, Task, PluginID.str(),
                            CallbackName.str(),
                            DiagnosticTransactionID});
}

void PluginProcessServices::leaveCallbackScope(PluginSession &Session,
                                               PluginTaskContext *Task) {
  if (CallbackScopes.empty())
    return;
  const CallbackScope &Scope = CallbackScopes.back();
  if (Scope.ProcessServices == this && Scope.Session == &Session &&
      Scope.Task == Task)
    CallbackScopes.pop_back();
}

Error PluginProcessServices::shutdown() { return Registry.shutdown(); }

} // namespace neverc::plugin
