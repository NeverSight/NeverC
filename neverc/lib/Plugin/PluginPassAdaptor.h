#ifndef NEVERC_LIB_PLUGIN_PLUGINPASSADAPTOR_H
#define NEVERC_LIB_PLUGIN_PLUGINPASSADAPTOR_H

#include "neverc/Plugin/NevercPluginAPI.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc {
namespace plugin {

/// Wraps a C NevercModulePassFn callback into an LLVM new-PM ModulePass.
class PluginModulePassAdaptor
    : public llvm::PassInfoMixin<PluginModulePassAdaptor> {
public:
  PluginModulePassAdaptor(NevercModulePassFn Fn, const NevercHostAPI *API,
                          void *UserData, llvm::StringRef PassName,
                          llvm::StringRef PluginPath = "");

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  void printPipeline(llvm::raw_ostream &OS,
                     llvm::function_ref<llvm::StringRef(llvm::StringRef)>);

  static bool isRequired() { return true; }

private:
  NevercModulePassFn Callback;
  const NevercHostAPI *API;
  void *UserData;
  llvm::SmallString<64> Name;
  llvm::SmallString<128> Origin;
  llvm::SmallString<128> StackMsg;
};

/// Wraps a C NevercMachinePassFn callback into a legacy MachineFunctionPass
/// for use with TargetPassConfig-based MIR pipeline insertion.
class PluginMachineFunctionPassAdaptor final : public llvm::MachineFunctionPass {
public:
  static char ID;

  PluginMachineFunctionPassAdaptor(NevercMachinePassFn Fn,
                                   const NevercHostAPI *API, void *UserData,
                                   llvm::StringRef PassName,
                                   llvm::StringRef PluginPath = "");

  llvm::StringRef getPassName() const override { return Name; }

  bool runOnMachineFunction(llvm::MachineFunction &MF) override;

private:
  NevercMachinePassFn Callback;
  const NevercHostAPI *API;
  void *UserData;
  llvm::SmallString<64> Name;
  llvm::SmallString<128> Origin;
  llvm::SmallString<128> StackMsg;
};

} // namespace plugin
} // namespace neverc

#endif
