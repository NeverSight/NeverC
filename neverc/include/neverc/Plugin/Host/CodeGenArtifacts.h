#ifndef NEVERC_PLUGIN_HOST_CODEGENARTIFACTS_H
#define NEVERC_PLUGIN_HOST_CODEGENARTIFACTS_H

#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginTarget.h"
#include <memory>
#include <string>

namespace neverc::plugin {

struct TargetSelectionArtifact {
  NevercTargetID TargetID{};
  std::string CompatibilityKey;
};

struct CodeGenRequestArtifact {
  NevercCodeGenProductKind InputKind = 0;
  NevercCodeGenProductKind OutputKind = 0;
  std::string CompatibilityKey;
  bool HasFinalIRProof = false;
};

struct CodeGenProductArtifact {
  NevercCodeGenProductKind Kind = 0;
  NevercInterfaceID ProductID{};
  NevercArtifactHandle Payload{};
  std::string CompatibilityKey;
  bool HostVerified = false;
};

struct CodeGenArtifactTypes {
  std::shared_ptr<const PluginArtifactType> TargetSelection;
  std::shared_ptr<const PluginArtifactType> Request;
  std::shared_ptr<const PluginArtifactType> IRModule;
  std::shared_ptr<const PluginArtifactType> MIRModule;
  std::shared_ptr<const PluginArtifactType> MCUnit;
  std::shared_ptr<const PluginArtifactType> ObjectGraph;
  std::shared_ptr<const PluginArtifactType> ObjectImage;
};

NevercInterfaceID targetSelectionArtifactID();
NevercInterfaceID codeGenRequestArtifactID();
NevercInterfaceID codeGenIRModuleArtifactID();
NevercInterfaceID codeGenMIRModuleArtifactID();
NevercInterfaceID codeGenMCUnitArtifactID();
NevercInterfaceID codeGenObjectGraphArtifactID();
NevercInterfaceID codeGenObjectImageArtifactID();

llvm::Expected<CodeGenArtifactTypes>
registerCodeGenArtifactTypes(PluginArtifactRegistry &Registry);

} // namespace neverc::plugin

#endif
