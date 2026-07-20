#ifndef NEVERC_PLUGIN_HOST_PLUGINREGISTRATION_H
#define NEVERC_PLUGIN_HOST_PLUGINREGISTRATION_H

#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/PluginIR.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginMIR.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginSource.h"
#include "neverc/Plugin/PluginTarget.h"
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
  IRPass,
  IRAnalysis,
  MIRPass,
  Target,
  TargetABI,
  CallingConvention,
  MCSchema,
  MCEncoder,
  MCDecoder,
  MCAsmBackend,
  ObjectFormat,
  CodeGenEdge,
};

struct OwnedTargetTripleMatcher {
  std::string Architecture;
  std::string Vendor;
  std::string OperatingSystem;
  std::string Environment;
  uint32_t Priority = 0;
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
  NevercIRPassDescriptor IRPass{};
  NevercIRAnalysisDescriptor IRAnalysis{};
  NevercMIRPassDescriptor MIRPass{};
  NevercTargetDescriptor Target{};
  NevercTargetABIDescriptor TargetABI{};
  NevercCallingConventionDescriptor CallingConvention{};
  NevercMCSchemaDescriptor MCSchema{};
  NevercMCEncoderDescriptor MCEncoder{};
  NevercMCDecoderDescriptor MCDecoder{};
  NevercMCAsmBackendDescriptor MCAsmBackend{};
  NevercObjectFormatDescriptor ObjectFormatDescriptor{};
  NevercCodeGenEdgeDescriptor CodeGenEdge{};
  std::string CanonicalName;
  std::string ProviderID;
  std::string PassID;
  std::string AnalysisName;
  std::string RoutePrefix;
  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;
  std::string SchemaDigest;
  std::string CodeGenCompatibilityKey;
  std::string DefaultExtension;
  std::vector<std::string> Aliases;
  std::vector<OwnedTargetTripleMatcher> TargetMatchers;
  std::vector<NevercInterfaceID> TargetReferences;
  std::vector<NevercInterfaceID> RequiredAnalyses;
  std::vector<uint8_t> IRExternalDependencyDigest;
  std::vector<NevercMIRBuiltinAnalysis> MIRRequiredAnalyses;
  std::vector<NevercMIRBuiltinAnalysis> MIRPreservedAnalyses;
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
NevercStatus registerPluginIRPass(
    void *Registrar, const NevercIRPassDescriptor *Descriptor);
NevercStatus registerPluginIRAnalysis(
    void *Registrar, const NevercIRAnalysisDescriptor *Descriptor);
NevercStatus registerPluginMIRPass(
    void *Registrar, const NevercMIRPassDescriptor *Descriptor);
NevercStatus registerPluginTarget(
    void *Registrar, const NevercTargetDescriptor *Descriptor);
NevercStatus registerPluginTargetABI(
    void *Registrar, const NevercTargetABIDescriptor *Descriptor);
NevercStatus registerPluginCallingConvention(
    void *Registrar, const NevercCallingConventionDescriptor *Descriptor);
NevercStatus registerPluginMCSchema(
    void *Registrar, const NevercMCSchemaDescriptor *Descriptor);
NevercStatus registerPluginMCEncoder(
    void *Registrar, const NevercMCEncoderDescriptor *Descriptor);
NevercStatus registerPluginMCDecoder(
    void *Registrar, const NevercMCDecoderDescriptor *Descriptor);
NevercStatus registerPluginMCAsmBackend(
    void *Registrar, const NevercMCAsmBackendDescriptor *Descriptor);
NevercStatus registerPluginObjectFormat(
    void *Registrar, const NevercObjectFormatDescriptor *Descriptor);
NevercStatus registerPluginCodeGenEdge(
    void *Registrar, const NevercCodeGenEdgeDescriptor *Descriptor);

} // namespace neverc::plugin

#endif
