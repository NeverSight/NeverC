#ifndef NEVERC_PLUGIN_HOST_PLUGINREGISTRY_H
#define NEVERC_PLUGIN_HOST_PLUGINREGISTRY_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem/UniqueID.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginInterfaceRegistry;
class PluginOptionRegistry;
class PluginProcessServices;
class PluginActivationPlan;
class PluginPublishedRegistration;
class PluginSession;
class PluginTaskContext;

llvm::Error activatePluginPlan(PluginProcessServices &ProcessServices,
                               PluginActivationPlan &Plan);

struct OwnedCompatibilityKey {
  std::string ProducerBuildID;
  std::string TargetABIKey;
  uint32_t LLVMMajor = 0;
};

struct OwnedInterfaceRequirement {
  NevercInterfaceID Interface{};
  uint16_t Major = 0;
  uint16_t MinimumMinor = 0;
  bool Required = false;
  NevercInterfaceStability Stability = NEVERC_INTERFACE_STABLE;
  OwnedCompatibilityKey Compatibility;
};

struct OwnedPluginDependency {
  std::string PluginID;
  NevercVersionRange Version{};
  std::string MinimumPrerelease;
  std::string MinimumBuildMetadata;
  std::string MaximumPrerelease;
  std::string MaximumBuildMetadata;
  NevercDependencyKind Kind = NEVERC_DEPENDENCY_REQUIRED;
};

struct PluginDescriptorRecord {
  uint16_t ABIMajor = 0;
  uint16_t ABIMinor = 0;
  uint64_t ABIFlags = 0;
  std::string PluginID;
  std::string DisplayName;
  NevercSemanticVersion Version{};
  std::string VersionPrerelease;
  std::string VersionBuildMetadata;
  NevercConcurrencyModel Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  NevercReentrancyModel Reentrancy = NEVERC_REENTRANCY_NONE;
  std::vector<OwnedInterfaceRequirement> RequiredInterfaces;
  std::vector<OwnedInterfaceRequirement> OptionalInterfaces;
  std::vector<OwnedPluginDependency> Dependencies;
  NevercProcessBeginFn ProcessBegin = nullptr;
  NevercRegisterPluginFn Register = nullptr;
  NevercSessionBeginFn SessionBegin = nullptr;
  NevercSessionEndFn SessionEnd = nullptr;
  NevercTaskBeginFn TaskBegin = nullptr;
  NevercTaskEndFn TaskEnd = nullptr;
  NevercPluginDestroyFn Destroy = nullptr;
};

class PluginModule {
public:
  ~PluginModule();

  PluginModule(const PluginModule &) = delete;
  PluginModule &operator=(const PluginModule &) = delete;

  llvm::StringRef path() const;
  llvm::sys::fs::UniqueID identity() const;
  const PluginDescriptorRecord &descriptor() const;
  const PluginPublishedRegistration *registration() const;

private:
  struct Storage;
  explicit PluginModule(std::unique_ptr<Storage> StorageValue);

  bool processBegun() const;
  bool registered() const;
  bool hasProcessBegin() const;
  bool hasRegister() const;
  bool hasSessionBegin() const;
  bool hasSessionEnd() const;
  bool hasTaskBegin() const;
  bool hasTaskEnd() const;
  bool hasDestroy() const;
  NevercStatus invokeProcessBegin(const NevercCoreAPI *Core,
                                  void **OutProcessState) const;
  NevercStatus invokeRegister(const NevercCoreAPI *Core,
                              const NevercRegistrarAPI *Registrar,
                              void *RegistrarContext,
                              void *ProcessState) const;
  NevercStatus invokeSessionBegin(const NevercCoreAPI *Core,
                                  NevercSessionHandle Session,
                                  void *ProcessState,
                                  void **OutSessionState) const;
  NevercStatus invokeSessionEnd(const NevercCoreAPI *Core,
                                NevercSessionHandle Session,
                                void *ProcessState,
                                void *SessionState) const;
  NevercStatus invokeTaskBegin(const NevercCoreAPI *Core,
                               NevercTaskHandle Task, NevercTaskKind Kind,
                               void *ProcessState, void *SessionState,
                               void **OutTaskState) const;
  NevercStatus invokeTaskEnd(const NevercCoreAPI *Core,
                             NevercTaskHandle Task, NevercTaskKind Kind,
                             void *ProcessState, void *SessionState,
                             void *TaskState) const;
  NevercStatus invokeDestroy(const NevercCoreAPI *Core,
                             void *ProcessState) const;
  std::string runtimeError() const;
  void *processState() const;
  void setProcessState(void *State);
  void clearProcessState();
  void publishRegistration(
      std::unique_ptr<PluginPublishedRegistration> Registration);
  void clearRegistration();

  std::unique_ptr<Storage> Impl;
  friend class PluginRegistry;
  friend class PluginSession;
  friend class PluginTaskContext;
  friend llvm::Error activatePluginPlan(PluginProcessServices &,
                                        PluginActivationPlan &);
};

class RegistrySnapshot {
public:
  uint64_t generation() const { return Generation; }
  llvm::ArrayRef<std::shared_ptr<const PluginModule>> modules() const {
    return Modules;
  }
  const PluginModule *findByID(llvm::StringRef PluginID) const;

private:
  uint64_t Generation = 0;
  std::vector<std::shared_ptr<const PluginModule>> Modules;
  friend class PluginRegistry;
};

class RegistrySnapshotLease {
public:
  RegistrySnapshotLease() = default;
  RegistrySnapshotLease(RegistrySnapshotLease &&Other) noexcept;
  RegistrySnapshotLease &operator=(RegistrySnapshotLease &&Other) noexcept;
  ~RegistrySnapshotLease();

  RegistrySnapshotLease(const RegistrySnapshotLease &) = delete;
  RegistrySnapshotLease &operator=(const RegistrySnapshotLease &) = delete;

  explicit operator bool() const { return static_cast<bool>(Snapshot); }
  const RegistrySnapshot &operator*() const { return *Snapshot; }
  const RegistrySnapshot *operator->() const { return Snapshot.get(); }
  void reset();

private:
  struct LeaseState {
    std::atomic<uint64_t> Count{0};
  };

  RegistrySnapshotLease(std::shared_ptr<const RegistrySnapshot> SnapshotValue,
                        std::shared_ptr<LeaseState> StateValue);

  std::shared_ptr<const RegistrySnapshot> Snapshot;
  std::shared_ptr<LeaseState> State;
  friend class PluginRegistry;
};

class RegistryActivityLease {
public:
  enum class Kind { Session, Callback };

  RegistryActivityLease() = default;
  RegistryActivityLease(RegistryActivityLease &&Other) noexcept;
  RegistryActivityLease &operator=(RegistryActivityLease &&Other) noexcept;
  ~RegistryActivityLease();

  RegistryActivityLease(const RegistryActivityLease &) = delete;
  RegistryActivityLease &operator=(const RegistryActivityLease &) = delete;

  explicit operator bool() const { return static_cast<bool>(State); }
  void reset();

private:
  struct ActivityState {
    std::atomic<uint64_t> SessionCount{0};
    std::atomic<uint64_t> CallbackCount{0};
  };

  RegistryActivityLease(std::shared_ptr<ActivityState> StateValue,
                        Kind LeaseKind);

  std::shared_ptr<ActivityState> State;
  Kind LeaseKind = Kind::Session;
  friend class PluginRegistry;
};

class PluginRegistry {
public:
  explicit PluginRegistry(std::string HostBuildID, uint32_t LLVMMajor,
                          const PluginInterfaceRegistry *Interfaces = nullptr,
                          const NevercCoreAPI *CoreAPI = nullptr,
                          PluginOptionRegistry *Options = nullptr);
  ~PluginRegistry();

  PluginRegistry(const PluginRegistry &) = delete;
  PluginRegistry &operator=(const PluginRegistry &) = delete;

  llvm::Expected<std::shared_ptr<const PluginModule>>
  load(llvm::StringRef Path);
  llvm::Error unload(llvm::StringRef PluginID);
  RegistrySnapshotLease acquireSnapshot() const;
  RegistryActivityLease acquireSessionLease() const;
  RegistryActivityLease acquireCallbackLease() const;
  llvm::Error shutdown();

  uint64_t generation() const;
  size_t moduleCount() const;
  uint64_t activeSnapshotLeases() const;
  uint64_t activeSessions() const;
  uint64_t activeCallbacks() const;

private:
  llvm::Error ensureQuiet(llvm::StringRef Operation) const;
  void publishSnapshot();

  std::string HostBuildID;
  uint32_t LLVMMajor;
  const PluginInterfaceRegistry *Interfaces;
  const NevercCoreAPI *CoreAPI;
  PluginOptionRegistry *Options;
  mutable std::mutex Mutex;
  mutable std::mutex LifecycleMutex;
  std::vector<std::shared_ptr<PluginModule>> Modules;
  std::vector<std::shared_ptr<PluginModule>> InitializedModules;
  std::shared_ptr<const RegistrySnapshot> CurrentSnapshot;
  std::shared_ptr<RegistrySnapshotLease::LeaseState> SnapshotLeaseState;
  std::shared_ptr<RegistryActivityLease::ActivityState> ActivityState;
  uint64_t Generation = 0;
  bool ShuttingDown = false;
  bool ShutDown = false;

  friend llvm::Error activatePluginPlan(PluginProcessServices &,
                                        PluginActivationPlan &);
};

bool isCanonicalPluginID(llvm::StringRef PluginID);

/// Normalizes and validates a caller-provided C plugin descriptor into the
/// host-owned record, applying the same ABI-header capacity/version negotiation,
/// canonical-ID, interface, dependency and callback-pairing checks the loader
/// runs.  Exposed for the descriptor/single-header ABI fuzzers; it never loads
/// or executes native plugin code.
llvm::Expected<PluginDescriptorRecord>
copyAndValidateDescriptor(const NevercPluginDescriptor &Source,
                          uint32_t HostLLVMMajor);

} // namespace neverc::plugin

#endif
