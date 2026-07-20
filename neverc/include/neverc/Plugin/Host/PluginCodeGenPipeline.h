#ifndef NEVERC_PLUGIN_HOST_PLUGINCODEGENPIPELINE_H
#define NEVERC_PLUGIN_HOST_PLUGINCODEGENPIPELINE_H

#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {
class LLVMTargetMachine;
class MachineModuleInfoWrapperPass;
class Module;
}

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;
class PluginTargetSnapshot;

class PluginCodeGenPipelineRuntime final
    : public std::enable_shared_from_this<PluginCodeGenPipelineRuntime> {
public:
  struct Impl;

  static llvm::Expected<std::shared_ptr<PluginCodeGenPipelineRuntime>>
  create(PluginTaskContext &Task,
         std::shared_ptr<const PluginTargetSnapshot> Snapshot);
  ~PluginCodeGenPipelineRuntime();

  PluginCodeGenPipelineRuntime(const PluginCodeGenPipelineRuntime &) = delete;
  PluginCodeGenPipelineRuntime &
  operator=(const PluginCodeGenPipelineRuntime &) = delete;

  bool replacesIRToMIR() const;
  bool replacesMIRToMC() const;
  void install(llvm::LLVMTargetMachine &TargetMachine,
               bool RunMachineVerifier);

  llvm::Error runIRToMIR(llvm::Module &Module,
                         llvm::LLVMTargetMachine &TargetMachine,
                         llvm::MachineModuleInfoWrapperPass &MMI,
                         bool RunMachineVerifier);
  llvm::Error runMIRToMC(llvm::Module &Module,
                         llvm::LLVMTargetMachine &TargetMachine,
                         llvm::MachineModuleInfoWrapperPass &MMI,
                         bool RunMachineVerifier);

private:
  explicit PluginCodeGenPipelineRuntime(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

llvm::Error
registerPluginCodeGenProviderInterfaces(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
