#ifndef NEVERC_PLUGIN_HOST_LINKEXECUTIONHOOKSBRIDGE_H
#define NEVERC_PLUGIN_HOST_LINKEXECUTIONHOOKSBRIDGE_H

#include "neverc/Linker/Core/Driver/LinkExecutionHooks.h"
#include <memory>

namespace neverc {
class OutputCoordinator;
}

namespace neverc::plugin {

class PluginSession;

class LinkExecutionHooksBridge final
    : public linker::LinkExecutionHooks {
public:
  LinkExecutionHooksBridge(std::shared_ptr<PluginSession> Session,
                           OutputCoordinator &Outputs);
  ~LinkExecutionHooksBridge() override;

  llvm::Expected<linker::LinkHookResult>
  execute(const linker::LinkExecutionRequest &Request,
          const linker::LinkerDriverConfig &Config,
          llvm::raw_ostream &Stdout,
          llvm::raw_ostream &Stderr) override;
  void complete(bool Success) noexcept override;

private:
  std::shared_ptr<PluginSession> Session;
  OutputCoordinator &Outputs;
  bool Active = false;
  bool Completed = false;
};

} // namespace neverc::plugin

#endif
