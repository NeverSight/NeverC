#ifndef NEVERC_TREE_ASTMUTATIONLISTENER_H
#define NEVERC_TREE_ASTMUTATIONLISTENER_H

namespace neverc {
class Attr;
class Decl;
class DeclContext;
class Expr;
class RecordDecl;
class TagDecl;
class VarDecl;

class TreeMutationListener {
public:
  virtual ~TreeMutationListener();

  virtual void CompletedTagDefinition(const TagDecl *D) {}

  virtual void AddedVisibleDecl(const DeclContext *DC, const Decl *D) {}

  virtual void DeclarationMarkedUsed(const Decl *D) {}

  virtual void AddedAttributeToRecord(const Attr *Attr,
                                      const RecordDecl *Record) {}

  virtual void ReplacedDeclarationInitializer(const VarDecl *Declaration,
                                              const Expr *Previous,
                                              const Expr *Replacement) {}

  // NOTE: If new methods are added they should also be added to
  // MultiplexASTMutationListener.
};

} // end namespace neverc

#endif
