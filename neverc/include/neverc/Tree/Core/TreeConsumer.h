#ifndef NEVERC_AST_ASTCONSUMER_H
#define NEVERC_AST_ASTCONSUMER_H

namespace neverc {
class TreeContext;
class DeclGroupRef;
class TreeMutationListener;
class SemaConsumer; // layering violation required for safe SemaConsumer
class TagDecl;
class VarDecl;
class FunctionDecl;

class TreeConsumer {
  bool SemaConsumer = false;

  friend class SemaConsumer;

public:
  TreeConsumer() = default;

  virtual ~TreeConsumer() {}

  virtual void Initialize(TreeContext &Context) {}

  virtual bool ProcessTopLevelDecl(DeclGroupRef D);

  virtual void ProcessInlineFunctionDefinition(FunctionDecl *D) {}

  virtual void ProcessInterestingDecl(DeclGroupRef D);

  virtual void ProcessTranslationUnit(TreeContext &Ctx) {}

  virtual void ProcessTagDeclDefinition(TagDecl *D) {}

  virtual void ProcessTagDeclRequiredDefinition(const TagDecl *D) {}

  virtual void FinalizeTentativeDefinition(VarDecl *D) {}

  virtual void FinalizeExternalDeclaration(VarDecl *D) {}

  virtual TreeMutationListener *GetTreeMutationListener() { return nullptr; }

  virtual void PrintStats() {}
};

} // end namespace neverc.

#endif
