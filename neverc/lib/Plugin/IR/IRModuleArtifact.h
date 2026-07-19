#ifndef NEVERC_PLUGIN_IR_IRMODULEARTIFACT_H
#define NEVERC_PLUGIN_IR_IRMODULEARTIFACT_H

#include "neverc/Plugin/PluginIR.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {
class Module;
}

namespace neverc::plugin {

class IRPluginBridge;
class PluginArtifactRegistry;

struct IRModuleArtifact {
  std::shared_ptr<IRPluginBridge> Bridge;
  llvm::Module *BorrowedModule = nullptr;
  NevercInterfaceID Product{};
  std::string TargetTriple;
  std::string DataLayout;
  uint64_t Generation = 0;
  std::array<uint8_t, 32> DependencyDigest{};
  bool HasDependencyDigest = false;
};

NevercInterfaceID irGeneratePhaseID();
NevercInterfaceID irOptimizePhaseID();
NevercInterfaceID irModuleArtifactID();
NevercInterfaceID optimizedIRModuleArtifactID();
NevercInterfaceID standardIRModuleProductID();
llvm::Module *getIRModule(IRModuleArtifact &Artifact);
const llvm::Module *getIRModule(const IRModuleArtifact &Artifact);
llvm::Error registerIRModuleArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error
registerOptimizedIRModuleArtifactType(PluginArtifactRegistry &Artifacts);

} // namespace neverc::plugin

#endif
