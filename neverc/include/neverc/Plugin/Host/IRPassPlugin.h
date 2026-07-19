#ifndef NEVERC_PLUGIN_HOST_IRPASSPLUGIN_H
#define NEVERC_PLUGIN_HOST_IRPASSPLUGIN_H

#include "neverc/Plugin/PluginIR.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

class IRPassPlan {
public:
  struct Impl;

  static llvm::Expected<std::unique_ptr<IRPassPlan>>
  create(PluginTaskContext &Task);
  ~IRPassPlan();

  IRPassPlan(const IRPassPlan &) = delete;
  IRPassPlan &operator=(const IRPassPlan &) = delete;

  bool empty() const;
  void addPasses(llvm::ModulePassManager &Manager, NevercInterfaceID Phase,
                 NevercIROptimizationLevel OptimizationLevel) const;

private:
  explicit IRPassPlan(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

llvm::Error registerPluginIRPassInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
