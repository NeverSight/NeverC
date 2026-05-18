#ifndef NEVERC_AST_MANGLE_H
#define NEVERC_AST_MANGLE_H

#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/GlobalDecl.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/Support/Casting.h"

namespace llvm {
class raw_ostream;
}

namespace neverc {
class TreeContext;

class MangleContext {
public:
  enum ManglerKind { MK_Itanium };

private:
  virtual void anchor();

  TreeContext &Context;
  DiagnosticsEngine &Diags;
  const ManglerKind Kind;

public:
  ManglerKind getKind() const { return Kind; }

  explicit MangleContext(TreeContext &Context, DiagnosticsEngine &Diags,
                         ManglerKind Kind)
      : Context(Context), Diags(Diags), Kind(Kind) {}

  virtual ~MangleContext() {}

  TreeContext &getTreeContext() const { return Context; }

  DiagnosticsEngine &getDiags() const { return Diags; }

  virtual void startNewFunction() {}

  virtual bool isUniqueInternalLinkageDecl(const NamedDecl *ND) {
    return false;
  }

  virtual void needsUniqueInternalLinkageNames() {}

  bool shouldMangleDeclName(const NamedDecl *D);

  void mangleName(GlobalDecl GD, llvm::raw_ostream &);

  virtual void mangleSEHFilterExpression(GlobalDecl EnclosingDecl,
                                         llvm::raw_ostream &Out) = 0;

  virtual void mangleSEHFinallyBlock(GlobalDecl EnclosingDecl,
                                     llvm::raw_ostream &Out) = 0;

  virtual void mangleCanonicalTypeName(QualType T, llvm::raw_ostream &) = 0;
};

class ItaniumMangleContext : public MangleContext {
public:
  explicit ItaniumMangleContext(TreeContext &C, DiagnosticsEngine &D)
      : MangleContext(C, D, MK_Itanium) {}

  static bool classof(const MangleContext *C) {
    return C->getKind() == MK_Itanium;
  }

  static ItaniumMangleContext *create(TreeContext &Context,
                                      DiagnosticsEngine &Diags);
};

} // namespace neverc

#endif
