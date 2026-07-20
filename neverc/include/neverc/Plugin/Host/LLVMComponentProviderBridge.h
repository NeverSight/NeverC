#ifndef NEVERC_PLUGIN_HOST_LLVMCOMPONENTPROVIDERBRIDGE_H
#define NEVERC_PLUGIN_HOST_LLVMCOMPONENTPROVIDERBRIDGE_H

#include "llvm/MC/MCComponentProvider.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class PluginTargetSnapshot;
class PluginTaskContext;

class LLVMComponentProviderBridge final
    : public llvm::MCComponentProvider {
public:
  static llvm::Expected<
      std::unique_ptr<LLVMComponentProviderBridge>>
  create(PluginTaskContext &Task,
         std::shared_ptr<const PluginTargetSnapshot> Targets);
  ~LLVMComponentProviderBridge() override;

  bool hasReplacements() const;

  llvm::Expected<std::unique_ptr<llvm::MCCodeEmitter>>
  provideCodeEmitter(
      llvm::MCContext &Context,
      std::unique_ptr<llvm::MCCodeEmitter> Fallback) override;

  struct Impl;

private:
  explicit LLVMComponentProviderBridge(
      std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

} // namespace neverc::plugin

#endif
