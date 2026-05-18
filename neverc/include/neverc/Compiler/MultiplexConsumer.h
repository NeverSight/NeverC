#ifndef NEVERC_FRONTEND_MULTIPLEXCONSUMER_H
#define NEVERC_FRONTEND_MULTIPLEXCONSUMER_H

#include "neverc/Analyze/SemaConsumer.h"
#include <memory>
#include <vector>

namespace neverc {

class MultiplexASTMutationListener;

class MultiplexConsumer : public SemaConsumer {
public:
  MultiplexConsumer(std::vector<std::unique_ptr<TreeConsumer>> C);
  ~MultiplexConsumer() override;

  void Initialize(TreeContext &Context) override;
  bool ProcessTopLevelDecl(DeclGroupRef D) override;
  void ProcessInlineFunctionDefinition(FunctionDecl *D) override;
  void ProcessInterestingDecl(DeclGroupRef D) override;
  void ProcessTranslationUnit(TreeContext &Ctx) override;
  void ProcessTagDeclDefinition(TagDecl *D) override;
  void ProcessTagDeclRequiredDefinition(const TagDecl *D) override;
  void FinalizeTentativeDefinition(VarDecl *D) override;
  void FinalizeExternalDeclaration(VarDecl *D) override;
  TreeMutationListener *GetTreeMutationListener() override;
  void PrintStats() override;

  void InitializeSema(Sema &S) override;
  void ForgetSema() override;

private:
  std::vector<std::unique_ptr<TreeConsumer>> Consumers;
  std::unique_ptr<MultiplexASTMutationListener> MutationListener;
};

} // end namespace neverc

#endif
