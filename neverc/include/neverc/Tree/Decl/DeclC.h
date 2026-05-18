#ifndef NEVERC_AST_DECLC_H
#define NEVERC_AST_DECLC_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclBase.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Decl/Redeclarable.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>

namespace neverc {

using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;

class TreeContext;

class StaticAssertDecl : public Decl {
  llvm::PointerIntPair<Expr *, 1, bool> AssertExprAndFailed;
  Expr *Message;
  SourceLocation RParenLoc;

  StaticAssertDecl(DeclContext *DC, SourceLocation StaticAssertLoc,
                   Expr *AssertExpr, Expr *Message, SourceLocation RParenLoc,
                   bool Failed)
      : Decl(StaticAssert, DC, StaticAssertLoc),
        AssertExprAndFailed(AssertExpr, Failed), Message(Message),
        RParenLoc(RParenLoc) {}

  virtual void anchor();

public:
  static StaticAssertDecl *Create(TreeContext &C, DeclContext *DC,
                                  SourceLocation StaticAssertLoc,
                                  Expr *AssertExpr, Expr *Message,
                                  SourceLocation RParenLoc, bool Failed);
  Expr *getAssertExpr() { return AssertExprAndFailed.getPointer(); }
  const Expr *getAssertExpr() const { return AssertExprAndFailed.getPointer(); }

  Expr *getMessage() { return Message; }
  const Expr *getMessage() const { return Message; }

  bool isFailed() const { return AssertExprAndFailed.getInt(); }

  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getLocation(), getRParenLoc());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == StaticAssert; }
};

} // namespace neverc

#endif // NEVERC_AST_DECLC_H
