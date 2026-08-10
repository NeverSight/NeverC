#ifndef NEVERC_PLUGIN_HOST_PLUGINSESSION_H
#define NEVERC_PLUGIN_HOST_PLUGINSESSION_H

#include "neverc/Plugin/Host/PluginCallbackStats.h"
#include "neverc/Plugin/Host/PluginDiagnostics.h"
#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;
class PluginHandleArena;

class PluginSession {
public:
  using DeferredCallback =
      std::function<NevercStatus(const void *Context, int32_t *OutExitCode)>;

  static llvm::Expected<std::unique_ptr<PluginSession>>
  create(PluginProcessServices &ProcessServices, PluginActivationPlan &Plan,
         PluginOptionParseResult Options = {});

  ~PluginSession();

  PluginSession(const PluginSession &) = delete;
  PluginSession &operator=(const PluginSession &) = delete;

  llvm::Expected<std::unique_ptr<PluginSession>> createChild() const;
  llvm::Expected<std::unique_ptr<PluginTaskContext>>
  createTask(NevercTaskKind Kind, PluginTaskContext *Parent = nullptr);

  llvm::Expected<NevercStatus>
  invokeCallback(llvm::StringRef PluginID, llvm::StringRef CallbackName,
                 std::function<NevercStatus()> Callback,
                 bool CheckCancellation = true,
                 PluginTaskContext *CurrentTask = nullptr,
                 uint64_t *OutDiagnosticTransactionID = nullptr,
                 bool DeferRecoverableDisposition = false,
                 const void *ArtifactMutationDomain = nullptr);
  llvm::Error registerDeferredCallback(llvm::StringRef Domain,
                                       llvm::StringRef CallbackID,
                                       llvm::StringRef PluginID,
                                       DeferredCallback Callback);
  void unregisterDeferredCallback(llvm::StringRef Domain,
                                  llvm::StringRef CallbackID);
  llvm::Expected<NevercStatus> invokeDeferredCallback(
      llvm::StringRef Domain, llvm::StringRef CallbackID,
      const void *Context, int32_t *OutExitCode);
  llvm::StringRef currentCallbackPluginID() const;

  llvm::Error end();
  void cancel() { Cancelled.store(true, std::memory_order_release); }
  bool isCancelled() const {
    return Cancelled.load(std::memory_order_acquire);
  }
  bool isEnded() const;

  NevercSessionHandle handle() const { return Handle; }
  uint64_t registryGeneration() const { return RegistryGeneration; }
  uint64_t activeTaskCount() const {
    return ActiveTasks.load(std::memory_order_acquire);
  }
  const PluginOptionParseResult &options() const { return Options; }
  llvm::ArrayRef<std::shared_ptr<const PluginModule>> plugins() const {
    return Modules;
  }
  llvm::ArrayRef<uint64_t> ancestorSessionOwners() const {
    return AncestorSessionOwners;
  }
  PluginHandleArena &handles() { return *HandleArena; }
  const PluginHandleArena &handles() const { return *HandleArena; }
  PluginProcessServices &processServices() const { return ProcessServices; }
  PluginDiagnostics &diagnostics() { return Diagnostics; }
  const PluginDiagnostics &diagnostics() const { return Diagnostics; }
  PluginCallbackStats &callbackStats() { return CallbackStats; }
  const PluginCallbackStats &callbackStats() const { return CallbackStats; }

  NevercStatus queryState(llvm::StringRef PluginID, void **OutState) const;

private:
  struct PluginState;
  struct DeferredCallbackRecord {
    std::string PluginID;
    DeferredCallback Callback;
  };

  PluginSession(PluginProcessServices &ProcessServices,
                std::vector<std::shared_ptr<const PluginModule>> Modules,
                PluginOptionParseResult Options,
                std::vector<uint64_t> AncestorSessionOwners);

  static llvm::Expected<std::unique_ptr<PluginSession>>
  createFromModules(
      PluginProcessServices &ProcessServices,
      llvm::ArrayRef<std::shared_ptr<const PluginModule>> Modules,
      PluginOptionParseResult Options,
      std::vector<uint64_t> AncestorSessionOwners);

  llvm::Error initialize();
  llvm::Error rollbackBegunPlugins();
  PluginState *findPluginState(llvm::StringRef PluginID);
  const PluginState *findPluginState(llvm::StringRef PluginID) const;
  llvm::Error registerTask(PluginTaskContext &Task);
  void unregisterTask(PluginTaskContext &Task);

  PluginProcessServices &ProcessServices;
  std::vector<std::shared_ptr<const PluginModule>> Modules;
  std::vector<std::unique_ptr<PluginState>> PluginStates;
  std::vector<uint64_t> AncestorSessionOwners;
  PluginOptionParseResult Options;
  PluginDiagnostics Diagnostics;
  PluginCallbackStats CallbackStats;
  RegistrySnapshotLease Snapshot;
  RegistryActivityLease SessionLease;
  std::unique_ptr<PluginHandleArena> HandleArena;
  NevercSessionHandle Handle{};
  uint64_t RegistryGeneration = 0;
  std::atomic<bool> Cancelled{false};
  std::atomic<uint64_t> ActiveTasks{0};
  std::atomic<uint64_t> ActiveCallbacks{0};
  mutable std::mutex DeferredCallbackMutex;
  llvm::StringMap<DeferredCallbackRecord> DeferredCallbacks;
  mutable std::mutex LifecycleMutex;
  std::vector<uint64_t> ActiveTaskOwners;
  bool RegisteredScope = false;
  bool Ending = false;
  bool Ended = false;

  friend class PluginProcessServices;
  friend class PluginTaskContext;
};

} // namespace neverc::plugin

#endif
