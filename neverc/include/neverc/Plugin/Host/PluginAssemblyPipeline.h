#ifndef NEVERC_PLUGIN_HOST_PLUGINASSEMBLYPIPELINE_H
#define NEVERC_PLUGIN_HOST_PLUGINASSEMBLYPIPELINE_H

#include "neverc/Plugin/Host/MCAsmParserProvider.h"
#include "neverc/Plugin/Host/MCAsmPrinterProvider.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTargetSnapshot;
class PluginTaskContext;

class PluginAssemblyPipelineRuntime {
public:
  static llvm::Expected<std::unique_ptr<PluginAssemblyPipelineRuntime>>
  create(PluginTaskContext &Task,
         std::shared_ptr<const PluginTargetSnapshot> Snapshot);
  ~PluginAssemblyPipelineRuntime();

  bool replacesParser() const;
  bool replacesPrinter() const;

  llvm::Expected<std::unique_ptr<PluginMCUnit>>
  parse(const AssemblySourceArtifact &Source, NevercTargetID Target,
        MCAsmParserProviderRuntime::BuiltinProvider Builtin);

  llvm::Expected<AssemblyOutputArtifact>
  print(PluginMCUnit &Unit,
        MCAsmPrinterProviderRuntime::BuiltinProvider Builtin);

  struct Impl;

private:
  explicit PluginAssemblyPipelineRuntime(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

llvm::Error
registerPluginAssemblyProviderInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
