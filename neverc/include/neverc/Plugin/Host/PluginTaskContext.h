#ifndef NEVERC_PLUGIN_HOST_PLUGINTASKCONTEXT_H
#define NEVERC_PLUGIN_HOST_PLUGINTASKCONTEXT_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace neverc::plugin {

class PluginProcessServices;
class PluginSession;
class PluginHandleArena;
class PluginTaskContextTestPeer;

class PluginTaskContext {
public:
  ~PluginTaskContext();

  PluginTaskContext(const PluginTaskContext &) = delete;
  PluginTaskContext &operator=(const PluginTaskContext &) = delete;

  llvm::Error end();
  llvm::Expected<NevercStatus>
  invokeCallback(llvm::StringRef PluginID, llvm::StringRef CallbackName,
                 std::function<NevercStatus()> Callback,
                 bool CheckCancellation = true,
                 uint64_t *OutDiagnosticTransactionID = nullptr,
                 bool DeferRecoverableDisposition = false,
                 const void *ArtifactMutationDomain = nullptr);
  std::optional<uint64_t>
  currentArtifactMutationCapability(const void *Domain) const;
  bool validatesArtifactMutationCapability(const void *Domain,
                                           uint64_t Token) const;
  void retainCallbackContext(std::shared_ptr<void> Context);
  NevercTaskHandle handle() const { return Handle; }
  NevercTaskKind kind() const { return Kind; }
  PluginTaskContext *parent() const { return Parent; }
  PluginSession &session() const { return Session; }
  PluginProcessServices &processServices() const { return ProcessServices; }
  PluginHandleArena &handles() { return *HandleArena; }
  const PluginHandleArena &handles() const { return *HandleArena; }
  bool isEnding() const;
  bool isEnded() const;
  uint64_t activeChildCount() const {
    return ActiveChildren.load(std::memory_order_acquire);
  }

  NevercStatus queryState(llvm::StringRef PluginID, void **OutState) const;
  NevercStatus checkCancelled() const;

private:
  struct PluginState;

  PluginTaskContext(PluginSession &Session, NevercTaskKind Kind,
                    PluginTaskContext *Parent);

  static llvm::Expected<std::unique_ptr<PluginTaskContext>>
  create(PluginSession &Session, NevercTaskKind Kind,
         PluginTaskContext *Parent);
  llvm::Error initialize();
  llvm::Error rollbackBegunPlugins();
  PluginSession &Session;
  PluginProcessServices &ProcessServices;
  NevercTaskKind Kind;
  PluginTaskContext *Parent;
  NevercTaskHandle Handle{};
  std::unique_ptr<PluginHandleArena> HandleArena;
  std::vector<std::unique_ptr<PluginState>> PluginStates;
  std::vector<std::shared_ptr<void>> RetainedCallbackContexts;
  std::atomic<uint64_t> ActiveChildren{0};
  std::atomic<uint64_t> ActiveCallbacks{0};
  mutable std::mutex LifecycleMutex;
  bool RegisteredWithSession = false;
  bool RegisteredScope = false;
  bool Ending = false;
  bool Ended = false;

  friend class PluginProcessServices;
  friend class PluginSession;
  friend class PluginTaskContextTestPeer;
};

} // namespace neverc::plugin

#endif
