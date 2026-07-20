#ifndef NEVERC_PLUGIN_HOST_PLUGINPROCESSSERVICES_H
#define NEVERC_PLUGIN_HOST_PLUGINPROCESSSERVICES_H

#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace neverc::plugin {

class PluginSession;
class PluginTaskContext;

class PluginHostService {
public:
  virtual ~PluginHostService() = default;
  virtual llvm::Error validatePluginRegistrations(
      llvm::ArrayRef<std::shared_ptr<const PluginModule>>) {
    return llvm::Error::success();
  }
  virtual llvm::Error sessionScopeRegistered(NevercSessionHandle,
                                             PluginSession &) {
    return llvm::Error::success();
  }
  virtual llvm::Error taskScopeEnding(NevercTaskHandle) {
    return llvm::Error::success();
  }
  virtual void taskScopeUnregistered(NevercTaskHandle) noexcept {}
  virtual void sessionScopeUnregistered(NevercSessionHandle) noexcept {}
};

class OwnerTokenAllocator {
public:
  explicit OwnerTokenAllocator(uint64_t FirstToken = 1);

  llvm::Expected<uint64_t> allocate();

private:
  std::atomic<uint64_t> NextToken;
};

class PluginProcessServices {
public:
  PluginProcessServices(
      std::string HostBuildID, uint32_t LLVMMajor,
      llvm::ArrayRef<llvm::StringRef> StaticOptionSpellings = {});

  PluginProcessServices(const PluginProcessServices &) = delete;
  PluginProcessServices &operator=(const PluginProcessServices &) = delete;

  PluginRegistry &registry() { return Registry; }
  const PluginRegistry &registry() const { return Registry; }
  PluginInterfaceRegistry &interfaces() { return Interfaces; }
  const PluginInterfaceRegistry &interfaces() const { return Interfaces; }
  const NevercCoreAPI &coreAPI() const { return CoreAPI; }
  PluginOptionRegistry &options() { return Options; }
  const PluginOptionRegistry &options() const { return Options; }
  OutputCoordinator &outputCoordinator() { return Outputs; }

  llvm::Expected<uint64_t> allocateOwnerToken() {
    return OwnerTokens.allocate();
  }

  std::recursive_mutex &processSerialGate() { return ProcessSerialGate; }
  std::shared_mutex &llvmOptionGate();

  llvm::Error registerSessionScope(NevercSessionHandle Handle,
                                   PluginSession &Session);
  llvm::Error prepareSessionScope(NevercSessionHandle Handle,
                                  PluginSession &Session);
  llvm::Error validatePluginRegistrations(
      llvm::ArrayRef<std::shared_ptr<const PluginModule>> Modules);
  void unregisterSessionScope(NevercSessionHandle Handle);
  llvm::Error registerTaskScope(NevercTaskHandle Handle,
                                PluginTaskContext &Task);
  llvm::Error prepareTaskScopeEnd(NevercTaskHandle Handle);
  void unregisterTaskScope(NevercTaskHandle Handle);
  NevercStatus querySessionState(NevercSessionHandle Handle,
                                 llvm::StringRef PluginID,
                                 void **OutState);
  NevercStatus queryTaskState(NevercTaskHandle Handle,
                              llvm::StringRef PluginID, void **OutState);
  NevercStatus queryPluginOptionValueCount(
      NevercSessionHandle Handle, llvm::StringRef PluginID,
      llvm::StringRef Spelling, uint64_t *OutCount);
  NevercStatus queryPluginOptionValue(
      NevercSessionHandle Handle, llvm::StringRef PluginID,
      llvm::StringRef Spelling, uint64_t Index, NevercStringView *OutValue);
  NevercStatus checkCancelled(NevercTaskHandle Handle);
  NevercStatus emitDiagnostic(
      const NevercDiagnosticDescriptor &Descriptor,
      NevercDiagnosticHandle &OutDiagnostic);
  NevercStatus classifyScopeOwner(uint64_t ExpectedSessionOwner,
                                  uint64_t ExpectedScopeOwner,
                                  uint64_t ActualOwner);
  PluginSession *findSessionScope(NevercSessionHandle Handle);
  PluginTaskContext *findTaskScope(NevercTaskHandle Handle);
  llvm::Error registerHostService(NevercInterfaceID Interface,
                                  std::shared_ptr<PluginHostService> Service);
  std::shared_ptr<PluginHostService>
  findHostService(NevercInterfaceID Interface) const;
  bool currentCallbackHasSuffix(
      const PluginTaskContext &Task,
      llvm::StringRef Suffix) const;
  void enterCallbackScope(PluginSession &Session,
                          PluginTaskContext *Task,
                          llvm::StringRef PluginID,
                          llvm::StringRef CallbackName,
                          uint64_t DiagnosticTransactionID);
  void leaveCallbackScope(PluginSession &Session,
                          PluginTaskContext *Task);

  llvm::Error shutdown();

private:
  PluginInterfaceRegistry Interfaces;
  PluginOptionRegistry Options;
  OutputCoordinator Outputs;
  NevercCoreAPI CoreAPI{};
  OwnerTokenAllocator OwnerTokens;
  std::recursive_mutex ProcessSerialGate;
  std::mutex ScopeMutex;
  std::unordered_map<uint64_t, PluginSession *> Sessions;
  std::unordered_map<uint64_t, PluginTaskContext *> Tasks;
  mutable std::mutex HostServiceMutex;
  std::map<std::pair<uint64_t, uint64_t>,
           std::shared_ptr<PluginHostService>>
      HostServices;
  PluginRegistry Registry;
};

} // namespace neverc::plugin

#endif
