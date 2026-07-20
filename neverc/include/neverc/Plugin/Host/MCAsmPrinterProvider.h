#ifndef NEVERC_PLUGIN_HOST_MCASMPRINTERPROVIDER_H
#define NEVERC_PLUGIN_HOST_MCASMPRINTERPROVIDER_H

#include "neverc/Plugin/Host/AssemblyArtifacts.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <string>

namespace llvm {
class Target;
class Triple;
class raw_ostream;
}

namespace neverc::plugin {

class MCPluginBridge;
class PluginMCUnit;
class PluginTargetSnapshot;
class PluginTaskContext;

class AssemblyOutputBuilder {
public:
  explicit AssemblyOutputBuilder(uint64_t MaximumBytes);

  llvm::Error write(llvm::StringRef Text);
  void rollback();

private:
  llvm::Expected<AssemblyOutputArtifact>
  finish(NevercTargetID Target, llvm::StringRef SchemaDigest,
         uint64_t UnitGeneration);

  uint64_t MaximumBytes = 0;
  std::string Staging;
  bool Closed = false;

  friend class MCAsmPrinterProviderRuntime;
  friend class PluginAssemblyPipelineRuntime;
};

struct AssemblyPrintExecutionRequest {
  PluginTaskContext *Task = nullptr;
  const PluginTargetSnapshot *Snapshot = nullptr;
  PluginMCUnit *Unit = nullptr;
  uint64_t MaximumOutputBytes = UINT64_C(16) * 1024 * 1024;
};

class MCAsmPrinterProviderRuntime {
public:
  using ReplacementProvider =
      std::function<llvm::Error(MCPluginBridge &, AssemblyOutputBuilder &)>;
  using BuiltinProvider = std::function<llvm::Expected<std::string>()>;

  static llvm::Expected<AssemblyOutputArtifact>
  execute(const AssemblyPrintExecutionRequest &Request,
          ReplacementProvider Replacement, BuiltinProvider Builtin);
};

class BuiltinLLVMAsmPrinter {
public:
  static llvm::Error print(const llvm::Target &Target,
                           const llvm::Triple &TargetTriple,
                           llvm::StringRef CPU,
                           llvm::StringRef Features,
                           const PluginMCUnit &Unit,
                           llvm::raw_ostream &Output);
};

} // namespace neverc::plugin

#endif
