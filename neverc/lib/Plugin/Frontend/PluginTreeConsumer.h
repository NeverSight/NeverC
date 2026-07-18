#ifndef NEVERC_PLUGIN_FRONTEND_PLUGINTREECONSUMER_H
#define NEVERC_PLUGIN_FRONTEND_PLUGINTREECONSUMER_H

#include "neverc/Analyze/SemaConsumer.h"
#include "neverc/Plugin/PluginAST.h"
#include <cstdint>

namespace neverc {
class Decl;

namespace plugin {
class PluginASTBridge;

class PluginTreeConsumer final : public SemaConsumer {
public:
  explicit PluginTreeConsumer(PluginASTBridge &AST);

  void Initialize(TreeContext &Context) override;
  bool ProcessTopLevelDecl(DeclGroupRef Declarations) override;
  void ProcessInlineFunctionDefinition(FunctionDecl *Declaration) override;
  void ProcessInterestingDecl(DeclGroupRef Declarations) override;
  void ProcessTranslationUnit(TreeContext &Context) override;
  void ProcessTagDeclDefinition(TagDecl *Declaration) override;
  void
  ProcessTagDeclRequiredDefinition(const TagDecl *Declaration) override;
  void FinalizeTentativeDefinition(VarDecl *Declaration) override;
  void FinalizeExternalDeclaration(VarDecl *Declaration) override;
  void InitializeSema(Sema &SemanticAnalyzer) override;
  void ForgetSema() override;

private:
  bool emit(uint32_t Kind, Decl *Declaration = nullptr,
            bool CleanupEvent = false);
  bool emitGroup(uint32_t Kind, DeclGroupRef Declarations);
  void reportFailure(NevercStatus Status);

  PluginASTBridge &AST;
  TreeContext *Context = nullptr;
  bool Failed = false;
  bool SemaActive = false;
};

} // namespace plugin
} // namespace neverc

#endif
