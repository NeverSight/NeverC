#ifndef NEVERC_INVOKE_PLUGIN_TOOLCHAINPLUGINBRIDGE_H
#define NEVERC_INVOKE_PLUGIN_TOOLCHAINPLUGINBRIDGE_H

#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::driver {

struct DriverToolChainRequestData {
  std::string RequestedTriple;
  std::string ComputedTriple;
  std::string SysRoot;
  std::string ResourceDir;
  std::string CPU;
  std::vector<std::string> Features;
  NevercExecutionLevel ExecutionLevel = NEVERC_EXECUTION_LEVEL_UNSPECIFIED;
  bool DynamicCodeProfile = false;
};

class DriverToolChainMutation;

class DriverToolChainRequestArtifact {
public:
  explicit DriverToolChainRequestArtifact(DriverToolChainRequestData Request);
  DriverToolChainRequestArtifact(const DriverToolChainRequestArtifact &Other);

  llvm::Expected<std::unique_ptr<DriverToolChainMutation>> beginMutation();
  DriverToolChainRequestData snapshot() const;
  void describe(NevercToolChainRequest &OutRequest) const;
  llvm::Error verify() const;

private:
  llvm::Error commit(DriverToolChainRequestData Request);
  void finishMutation();
  void rebuildFeatureViews();

  mutable std::mutex Mutex;
  DriverToolChainRequestData Request;
  std::vector<NevercStringView> FeatureViews;
  bool MutationActive = false;

  friend class DriverToolChainMutation;
};

class DriverToolChainMutation {
public:
  DriverToolChainMutation(DriverToolChainRequestArtifact &Owner,
                          DriverToolChainRequestData Request);
  ~DriverToolChainMutation();

  llvm::Error setTriple(llvm::StringRef Triple);
  llvm::Error setCPU(llvm::StringRef CPU);
  llvm::Error setFeatures(llvm::ArrayRef<llvm::StringRef> Features);
  llvm::Error commit();
  void abort();

private:
  DriverToolChainRequestArtifact *Owner;
  DriverToolChainRequestData Request;
  bool Finished = false;
};

class DriverToolChainSelectionArtifact {
public:
  DriverToolChainSelectionArtifact() = default;
  DriverToolChainSelectionArtifact(
      const DriverToolChainSelectionArtifact &Other);
  DriverToolChainSelectionArtifact &
  operator=(const DriverToolChainSelectionArtifact &Other);
  DriverToolChainSelectionArtifact(
      DriverToolChainSelectionArtifact &&Other) noexcept;
  DriverToolChainSelectionArtifact &
  operator=(DriverToolChainSelectionArtifact &&Other) noexcept;

  void set(std::string ToolChainID, std::string TargetKey,
           std::string TargetTriple, std::string CPU,
           std::vector<std::string> Features,
           NevercToolChainProviderHandle Provider, bool BuiltinProviderUsed);
  llvm::Error verify() const;
  void rebuildFeatureViews();

  std::string ToolChainID;
  std::string TargetKey;
  std::string TargetTriple;
  std::string CPU;
  std::vector<std::string> Features;
  std::vector<NevercStringView> FeatureViews;
  NevercToolChainProviderHandle Provider{};
  bool BuiltinProviderUsed = false;
};

struct DriverToolChainArtifactTypes {
  std::shared_ptr<const plugin::PluginArtifactType> Request;
  std::shared_ptr<const plugin::PluginArtifactType> Selection;
};

using DriverToolChainSelectionVerifier =
    std::function<llvm::Error(const DriverToolChainSelectionArtifact &)>;

NevercInterfaceID driverToolChainRequestArtifactID();
NevercInterfaceID driverToolChainSelectionArtifactID();
NevercInterfaceID driverSelectToolChainPhaseID();

llvm::Expected<DriverToolChainArtifactTypes> registerDriverToolChainArtifacts(
    plugin::PluginArtifactRegistry &Registry,
    DriverToolChainSelectionVerifier SelectionVerifier);

} // namespace neverc::driver

#endif
