//===--- CXXCtorInitializer.h - C++ ctor mem-initializers --------*- C++ -*-===//
#ifndef NEVERC_TREE_DECL_CXXCTORINITIALIZER_H
#define NEVERC_TREE_DECL_CXXCTORINITIALIZER_H

#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"

namespace neverc {

/// One base or member initializer in a ctor-initializer list.
class CXXCtorInitializer {
  /// Either a base type (TypeSourceInfo*) or a FieldDecl* / IndirectField.
  llvm::PointerUnion<TypeSourceInfo *, FieldDecl *> Initializee;
  Expr *Init = nullptr;
  SourceLocation MemberOrEllipsisLoc;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
  bool IsBase : 1;
  bool IsVirtualBase : 1;
  bool IsDelegating : 1;

public:
  /// Member initializer: T(args) or T{args} represented as Init expr.
  CXXCtorInitializer(FieldDecl *Member, SourceLocation MemberLoc,
                     SourceLocation L, Expr *InitExpr, SourceLocation R)
      : Initializee(Member), Init(InitExpr), MemberOrEllipsisLoc(MemberLoc),
        LParenLoc(L), RParenLoc(R), IsBase(false), IsVirtualBase(false),
        IsDelegating(false) {}

  /// Base class initializer.
  CXXCtorInitializer(TypeSourceInfo *TInfo, bool IsVirtual, SourceLocation L,
                     Expr *InitExpr, SourceLocation R)
      : Initializee(TInfo), Init(InitExpr), LParenLoc(L), RParenLoc(R),
        IsBase(true), IsVirtualBase(IsVirtual), IsDelegating(false) {}

  /// Delegating constructor initializer.
  CXXCtorInitializer(TypeSourceInfo *TInfo, SourceLocation L, Expr *InitExpr,
                     SourceLocation R, bool Delegating)
      : Initializee(TInfo), Init(InitExpr), LParenLoc(L), RParenLoc(R),
        IsBase(false), IsVirtualBase(false), IsDelegating(Delegating) {}

  bool isBaseInitializer() const { return IsBase; }
  bool isMemberInitializer() const {
    return !IsBase && !IsDelegating && Initializee.is<FieldDecl *>();
  }
  bool isDelegatingInitializer() const { return IsDelegating; }
  bool isVirtualBaseInitializer() const { return IsBase && IsVirtualBase; }

  FieldDecl *getMember() const {
    return Initializee.dyn_cast<FieldDecl *>();
  }
  TypeSourceInfo *getTypeSourceInfo() const {
    return Initializee.dyn_cast<TypeSourceInfo *>();
  }
  QualType getBaseClass() const {
    if (TypeSourceInfo *TSI = getTypeSourceInfo())
      return TSI->getType();
    return QualType();
  }
  Expr *getInit() const { return Init; }
  void setInit(Expr *E) { Init = E; }
  SourceLocation getSourceLocation() const { return LParenLoc; }
  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceLocation getMemberLocation() const { return MemberOrEllipsisLoc; }
};

} // namespace neverc

#endif
