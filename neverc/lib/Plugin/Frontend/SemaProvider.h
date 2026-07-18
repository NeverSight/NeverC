#ifndef NEVERC_PLUGIN_FRONTEND_SEMAPROVIDER_H
#define NEVERC_PLUGIN_FRONTEND_SEMAPROVIDER_H

#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include <string>

namespace neverc {
class Sema;

namespace plugin {
class PluginArtifactRegistry;
class PluginASTBridge;
class PluginPhaseExecutor;
class PluginTaskContext;

class PluginSemaProviderRuntime final : public PluginSemaPhaseAPI {
public:
  PluginSemaProviderRuntime(PluginTaskContext &Task,
                            PluginArtifactRegistry &Artifacts,
                            PluginPhaseExecutor &Executor);

  void attach(Sema &SemanticAnalyzer, PluginASTBridge &AST);
  void detach();

  NevercStatus builtinProvider(const NevercPhaseFrame *Frame,
                               NevercPhaseResult *Result);
  const std::string &failureMessage() const { return FailureMessage; }
  void clearFailureMessage() { FailureMessage.clear(); }

  NevercStatus getAnalyzePhaseInput(const NevercPhaseFrame *Frame,
                                    NevercArtifactHandle Input,
                                    NevercSemaPhaseInput *OutInput) override;
  NevercStatus createSemanticUnit(
      const NevercPhaseFrame *Frame,
      const NevercSemanticUnitDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput) override;
  NevercStatus getSemanticUnitInfo(const NevercPhaseFrame *Frame,
                                   NevercArtifactHandle Unit,
                                   NevercSemanticUnitInfo *OutInfo) override;

private:
  PluginTaskContext &Task;
  PluginArtifactRegistry &Artifacts;
  PluginPhaseExecutor &Executor;
  Sema *SemanticAnalyzer = nullptr;
  PluginASTBridge *AST = nullptr;
  std::string FailureMessage;
};

} // namespace plugin
} // namespace neverc

#endif
