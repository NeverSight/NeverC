#ifndef NEVERC_PLUGIN_HOST_MIRPASSPLUGIN_H
#define NEVERC_PLUGIN_HOST_MIRPASSPLUGIN_H

#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

class MIRPassPlan final : public llvm::MachinePipelineHooks {
public:
  struct Impl;

  static llvm::Expected<std::shared_ptr<MIRPassPlan>>
  create(PluginTaskContext &Task);
  ~MIRPassPlan() override;

  MIRPassPlan(const MIRPassPlan &) = delete;
  MIRPassPlan &operator=(const MIRPassPlan &) = delete;

  bool empty() const;
  bool requiresSerialCodeGen() const;
  void addPasses(llvm::TargetPassConfig &TPC,
                 llvm::MachinePipelineHookPoint Point) override;

private:
  explicit MIRPassPlan(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

llvm::Error registerPluginMIRPassInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
