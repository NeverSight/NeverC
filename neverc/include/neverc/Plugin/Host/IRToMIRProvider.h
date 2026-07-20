#ifndef NEVERC_PLUGIN_HOST_IRTOMIRPROVIDER_H
#define NEVERC_PLUGIN_HOST_IRTOMIRPROVIDER_H

#include "neverc/Plugin/PluginTarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>

namespace llvm {
class LLVMTargetMachine;
class MachineModuleInfoWrapperPass;
class Module;
}

namespace neverc::plugin {

class MIRModuleArtifact;
struct MIRModuleCoveragePolicy;

struct IRToMIRExecutionRequest {
  llvm::Module *Module = nullptr;
  llvm::LLVMTargetMachine *TargetMachine = nullptr;
  /// Pipeline-owned MMI used by replacement lowering. The task-local
  /// artifact borrows it and must not outlive the owning codegen pipeline.
  llvm::MachineModuleInfoWrapperPass *PipelineMMI = nullptr;
  NevercTargetID TargetID{};
  llvm::StringRef CompatibilityKey;
  llvm::StringRef SchemaDigest;
  const MIRModuleCoveragePolicy *Coverage = nullptr;
  bool HasFinalIRProof = false;
  bool RunMachineVerifier = true;
};

class IRToMIRProviderRuntime {
public:
  using ReplacementProvider =
      std::function<llvm::Error(MIRModuleArtifact &)>;
  using BuiltinProvider = std::function<
      llvm::Expected<std::unique_ptr<MIRModuleArtifact>>()>;

  static llvm::Expected<std::unique_ptr<MIRModuleArtifact>>
  execute(const IRToMIRExecutionRequest &Request,
          ReplacementProvider Replacement, BuiltinProvider Builtin);
};

} // namespace neverc::plugin

#endif
