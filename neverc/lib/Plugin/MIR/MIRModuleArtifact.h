#ifndef NEVERC_LIB_PLUGIN_MIR_MIRMODULEARTIFACT_H
#define NEVERC_LIB_PLUGIN_MIR_MIRMODULEARTIFACT_H

#include "neverc/Plugin/PluginTarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {
class Function;
class LLVMTargetMachine;
class MachineFunction;
class MachineModuleInfo;
class MachineModuleInfoWrapperPass;
class Module;
}

namespace neverc::plugin {

class PluginArtifactRegistry;

struct MIRModuleCoveragePolicy {
  bool HandlesGlobals = false;
  bool HandlesConstructors = false;
  bool HandlesDebugInfo = false;
  bool HandlesUnwind = false;
};

class MIRModuleArtifact {
public:
  static llvm::Expected<std::unique_ptr<MIRModuleArtifact>>
  createOwned(llvm::Module &Module, llvm::LLVMTargetMachine &TargetMachine,
              NevercTargetID TargetID, llvm::StringRef CompatibilityKey,
              llvm::StringRef SchemaDigest);

  static std::unique_ptr<MIRModuleArtifact>
  borrow(llvm::Module &Module, llvm::MachineModuleInfoWrapperPass &MMI,
         NevercTargetID TargetID, llvm::StringRef CompatibilityKey,
         llvm::StringRef SchemaDigest);

  ~MIRModuleArtifact();

  MIRModuleArtifact(const MIRModuleArtifact &) = delete;
  MIRModuleArtifact &operator=(const MIRModuleArtifact &) = delete;

  llvm::Module &module() const { return *IRModule; }
  llvm::MachineModuleInfo &machineModuleInfo() const;
  llvm::MachineFunction &getOrCreateMachineFunction(llvm::Function &Function);
  llvm::MachineFunction *
  getMachineFunction(const llvm::Function &Function) const;

  NevercTargetID targetID() const { return TargetID; }
  llvm::StringRef compatibilityKey() const { return CompatibilityKey; }
  llvm::StringRef schemaDigest() const { return SchemaDigest; }
  uint64_t generation() const { return Generation; }
  void advanceGeneration() { ++Generation; }

  void setCoveragePolicy(MIRModuleCoveragePolicy Policy) {
    Coverage = Policy;
  }
  const MIRModuleCoveragePolicy &coveragePolicy() const { return Coverage; }

  llvm::Error verify(bool RunMachineVerifier = true) const;

private:
  MIRModuleArtifact(llvm::Module &Module,
                    llvm::MachineModuleInfoWrapperPass &MMI,
                    NevercTargetID TargetID,
                    llvm::StringRef CompatibilityKey,
                    llvm::StringRef SchemaDigest);

  llvm::Module *IRModule;
  llvm::MachineModuleInfoWrapperPass *MMI;
  std::unique_ptr<llvm::MachineModuleInfoWrapperPass> OwnedMMI;
  NevercTargetID TargetID{};
  std::string CompatibilityKey;
  std::string SchemaDigest;
  MIRModuleCoveragePolicy Coverage;
  uint64_t Generation = 1;
};

NevercInterfaceID mirModuleArtifactID();
llvm::Error
registerMIRModuleArtifactType(PluginArtifactRegistry &Artifacts);

} // namespace neverc::plugin

#endif
