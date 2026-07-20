#ifndef NEVERC_PLUGIN_HOST_PLUGINCODEGENPROVIDER_H
#define NEVERC_PLUGIN_HOST_PLUGINCODEGENPROVIDER_H

#include "neverc/Plugin/Host/CodeGenRoutePlanner.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <string>

namespace neverc::plugin {

class PluginTaskContext;

struct CodeGenExecutionRequest {
  PluginTaskContext *TaskContext = nullptr;
  NevercTaskHandle Task{};
  NevercTargetKey Target{};
  NevercArtifactHandle Input{};
  NevercCodeGenProductKind InputKind = 0;
  NevercCodeGenProductKind OutputKind = 0;
  NevercCodeGenOptimizationLevel OptimizationLevel =
      NEVERC_CODEGEN_OPT_NONE;
  std::string CompatibilityKey;
  bool HasFinalIRProof = false;
};

struct VerifiedCodeGenProduct {
  NevercCodeGenProductCandidate Candidate{};
  bool HostVerified = false;
};

class PluginCodeGenProviderRuntime {
public:
  using BuiltinLower = std::function<
      llvm::Expected<NevercCodeGenProductCandidate>(
          const PluginTargetSnapshot::CodeGenEdgeRecord &,
          const CodeGenExecutionRequest &)>;

  static llvm::Expected<VerifiedCodeGenProduct>
  execute(const PlannedCodeGenRoute &Route,
          const CodeGenExecutionRequest &Request,
          BuiltinLower Builtin);
};

} // namespace neverc::plugin

#endif
