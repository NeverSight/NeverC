#include "ToolChains/Plugin.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/Tool.h"
#include "llvm/Support/ErrorHandling.h"

namespace neverc::driver::toolchains {
namespace {

class PluginLinkerUnavailable final : public Tool {
public:
  explicit PluginLinkerUnavailable(const ToolChain &TC)
      : Tool("neverc::plugin-link", "plugin linker unavailable", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  bool hasGoodDiagnostics() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &,
                    const InputInfo &, const InputInfoList &,
                    const llvm::opt::ArgList &,
                    const char *) const override {
    C.getDriver().Diag(diag::err_drv_plugin_phase)
        << "link requested for plugin target but no Linker Provider "
           "is registered";
  }
};

} // namespace

Plugin::Plugin(
    const Driver &D, const llvm::Triple &Triple,
    const llvm::opt::ArgList &Args,
    std::shared_ptr<const plugin::PluginTargetSnapshot> SnapshotValue,
    NevercTargetID TargetIDValue)
    : ToolChain(D, Triple, Args), Snapshot(std::move(SnapshotValue)),
      TargetID(TargetIDValue) {
  if (!Snapshot || !Snapshot->findTarget(TargetID))
    llvm::report_fatal_error(
        "plugin ToolChain constructed with an unknown target");
}

const plugin::PluginTargetSnapshot::TargetRecord &
Plugin::target() const {
  const auto *Record = Snapshot->findTarget(TargetID);
  if (!Record)
    llvm::report_fatal_error("plugin ToolChain target disappeared");
  return *Record;
}

Tool *Plugin::buildLinker() const {
  return new PluginLinkerUnavailable(*this);
}

} // namespace neverc::driver::toolchains
