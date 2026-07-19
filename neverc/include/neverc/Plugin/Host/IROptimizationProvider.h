#ifndef NEVERC_PLUGIN_HOST_IROPTIMIZATIONPROVIDER_H
#define NEVERC_PLUGIN_HOST_IROPTIMIZATIONPROVIDER_H

#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace llvm {
class Module;
}

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

class PluginIROptimizationProviderRuntime {
public:
  struct Impl;
  using BuiltinOptimizer = std::function<llvm::Error(llvm::Module &)>;

  static llvm::Expected<std::unique_ptr<PluginIROptimizationProviderRuntime>>
  create(PluginTaskContext &Task, llvm::Module &Module,
         uint32_t OptimizationLevel, bool DisableLLVMPasses,
         BuiltinOptimizer OptimizeBuiltin);
  ~PluginIROptimizationProviderRuntime();

  PluginIROptimizationProviderRuntime(
      const PluginIROptimizationProviderRuntime &) = delete;
  PluginIROptimizationProviderRuntime &
  operator=(const PluginIROptimizationProviderRuntime &) = delete;

  llvm::Error execute();
  bool ranBuiltinPipeline() const;
  llvm::Module *module() const;
  bool ownsModule() const;
  std::unique_ptr<llvm::Module> releaseOwnedModule();

private:
  explicit PluginIROptimizationProviderRuntime(std::unique_ptr<Impl> State);

  std::unique_ptr<Impl> State;
};

llvm::Error
registerPluginIROptimizationInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
