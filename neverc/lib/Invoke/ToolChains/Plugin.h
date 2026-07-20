#ifndef NEVERC_LIB_INVOKE_TOOLCHAINS_PLUGIN_H
#define NEVERC_LIB_INVOKE_TOOLCHAINS_PLUGIN_H

#include "neverc/Invoke/ToolChain.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include <memory>

namespace neverc::driver::toolchains {

class Plugin final : public ToolChain {
public:
  Plugin(const Driver &D, const llvm::Triple &Triple,
         const llvm::opt::ArgList &Args,
         std::shared_ptr<const plugin::PluginTargetSnapshot> Snapshot,
         NevercTargetID TargetID);

  const plugin::PluginTargetSnapshot::TargetRecord &target() const;
  bool IsIntegratedAssemblerDefault() const override { return true; }
  bool IsIntegratedBackendDefault() const override { return true; }
  bool IsNonIntegratedBackendSupported() const override { return false; }

protected:
  Tool *buildLinker() const override;

private:
  std::shared_ptr<const plugin::PluginTargetSnapshot> Snapshot;
  NevercTargetID TargetID{};
};

} // namespace neverc::driver::toolchains

#endif
