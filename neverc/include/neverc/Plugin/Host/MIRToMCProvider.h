#ifndef NEVERC_PLUGIN_HOST_MIRTOMCPROVIDER_H
#define NEVERC_PLUGIN_HOST_MIRTOMCPROVIDER_H

#include "llvm/Support/Error.h"
#include <functional>
#include <memory>

namespace neverc::plugin {

class MachineEmissionBridge;
class MIRModuleArtifact;
class PluginMCUnit;
class PluginTaskContext;
class PluginTargetSnapshot;

struct MIRToMCExecutionRequest {
  PluginTaskContext *Task = nullptr;
  MIRModuleArtifact *MIR = nullptr;
  const PluginTargetSnapshot *Snapshot = nullptr;
  bool HasFinalMIRProof = false;
  bool RunMachineVerifier = true;
};

class MIRToMCProviderRuntime {
public:
  using ReplacementProvider =
      std::function<llvm::Error(MachineEmissionBridge &)>;
  using BuiltinProvider =
      std::function<llvm::Expected<std::unique_ptr<PluginMCUnit>>()>;

  static llvm::Expected<std::unique_ptr<PluginMCUnit>>
  execute(const MIRToMCExecutionRequest &Request,
          ReplacementProvider Replacement, BuiltinProvider Builtin);
};

} // namespace neverc::plugin

#endif
