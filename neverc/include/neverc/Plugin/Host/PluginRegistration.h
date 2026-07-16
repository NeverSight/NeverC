#ifndef NEVERC_PLUGIN_HOST_PLUGINREGISTRATION_H
#define NEVERC_PLUGIN_HOST_PLUGINREGISTRATION_H

#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/PluginSource.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neverc::plugin {

struct OwnedPluginOption;

enum class PluginRegistrationKind : uint8_t {
  Interface,
  Option,
  Phase,
  Observer,
  Interceptor,
  Provider,
  VFSProvider,
};

struct PluginRegistrationRecord {
  PluginRegistrationRecord() = default;
  PluginRegistrationRecord(PluginRegistrationRecord &&Other) noexcept;
  PluginRegistrationRecord &
  operator=(PluginRegistrationRecord &&Other) noexcept;
  ~PluginRegistrationRecord();

  PluginRegistrationRecord(const PluginRegistrationRecord &) = delete;
  PluginRegistrationRecord &
  operator=(const PluginRegistrationRecord &) = delete;

  PluginRegistrationKind Kind = PluginRegistrationKind::Phase;
  NevercInterfaceID Interface{};
  NevercInterfaceStability Stability = NEVERC_INTERFACE_STABLE;
  const void *InterfaceTable = nullptr;
  OwnedCompatibilityKey Compatibility;
  std::unique_ptr<OwnedPluginOption> Option;
  NevercPhaseDescriptor Phase{};
  NevercObserverDescriptor Observer{};
  NevercInterceptorDescriptor Interceptor{};
  NevercProviderDescriptor Provider{};
  NevercVFSProviderDescriptor VFSProvider{};
  std::string CanonicalName;
  std::string ProviderID;
  std::string RoutePrefix;
  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;
  void *OwnedUserData = nullptr;
  NevercDestroyUserDataFn DestroyUserData = nullptr;
};

class PluginPublishedRegistration {
public:
  explicit PluginPublishedRegistration(
      std::vector<PluginRegistrationRecord> RecordsValue)
      : Registered(std::move(RecordsValue)) {}
  ~PluginPublishedRegistration();

  llvm::ArrayRef<PluginRegistrationRecord> records() const {
    return Registered;
  }

private:
  std::vector<PluginRegistrationRecord> Registered;
};

class PluginActivationPlan {
public:
  PluginActivationPlan(PluginActivationPlan &&) noexcept = default;
  PluginActivationPlan &operator=(PluginActivationPlan &&) noexcept = default;

  PluginActivationPlan(const PluginActivationPlan &) = delete;
  PluginActivationPlan &operator=(const PluginActivationPlan &) = delete;

  llvm::ArrayRef<std::shared_ptr<const PluginModule>> plugins() const {
    return OrderedPlugins;
  }
  uint64_t registryGeneration() const {
    return Snapshot ? Snapshot->generation() : 0;
  }

private:
  PluginActivationPlan(
      PluginRegistry *OwnerValue, RegistrySnapshotLease SnapshotValue,
      std::vector<std::shared_ptr<const PluginModule>> OrderedPluginsValue)
      : Owner(OwnerValue), Snapshot(std::move(SnapshotValue)),
        OrderedPlugins(std::move(OrderedPluginsValue)) {}

  PluginRegistry *Owner = nullptr;
  RegistrySnapshotLease Snapshot;
  std::vector<std::shared_ptr<const PluginModule>> OrderedPlugins;

  friend llvm::Expected<PluginActivationPlan>
  makePluginActivationPlan(PluginRegistry &Registry,
                           llvm::ArrayRef<llvm::StringRef> SelectedPluginIDs);
  friend llvm::Error activatePluginPlan(PluginProcessServices &ProcessServices,
                                        PluginActivationPlan &Plan);
};

llvm::Expected<PluginActivationPlan>
makePluginActivationPlan(PluginRegistry &Registry,
                         llvm::ArrayRef<llvm::StringRef> SelectedPluginIDs);

llvm::Error activatePluginPlan(PluginProcessServices &ProcessServices,
                               PluginActivationPlan &Plan);

llvm::Error validateDependencyRange(const OwnedPluginDependency &Dependency);
bool dependencyVersionMatches(const OwnedPluginDependency &Dependency,
                              const PluginDescriptorRecord &Candidate);
NevercStatus registerPluginVFSProvider(
    void *Registrar, const NevercVFSProviderDescriptor *Descriptor);

} // namespace neverc::plugin

#endif
