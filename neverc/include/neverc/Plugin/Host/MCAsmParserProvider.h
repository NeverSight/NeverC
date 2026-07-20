#ifndef NEVERC_PLUGIN_HOST_MCASMPARSERPROVIDER_H
#define NEVERC_PLUGIN_HOST_MCASMPARSERPROVIDER_H

#include "neverc/Plugin/Host/AssemblyArtifacts.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>

namespace neverc::plugin {

class MCPluginBridge;
class PluginMCUnit;
class PluginTargetSnapshot;
class PluginTaskContext;

struct AssemblyParseExecutionRequest {
  PluginTaskContext *Task = nullptr;
  const PluginTargetSnapshot *Snapshot = nullptr;
  const AssemblySourceArtifact *Source = nullptr;
  NevercTargetID TargetID{};
};

class MCAsmParserProviderRuntime {
public:
  using ReplacementProvider = std::function<llvm::Error(
      const AssemblySourceArtifact &, MCPluginBridge &)>;
  using BuiltinProvider =
      std::function<llvm::Expected<std::unique_ptr<PluginMCUnit>>()>;

  static llvm::Expected<std::unique_ptr<PluginMCUnit>>
  execute(const AssemblyParseExecutionRequest &Request,
          ReplacementProvider Replacement, BuiltinProvider Builtin);
};

} // namespace neverc::plugin

#endif
