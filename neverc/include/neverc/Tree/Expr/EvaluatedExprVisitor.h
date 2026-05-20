#ifndef NEVERC_TREE_EVALUATEDEXPRVISITOR_H
#define NEVERC_TREE_EVALUATEDEXPRVISITOR_H

#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/STLExtras.h"

namespace neverc {

class TreeContext;

template <template <typename> class Ptr, typename ImplClass>
class EvaluatedExprVisitorBase : public StmtVisitorBase<Ptr, ImplClass, void> {
protected:
  const TreeContext &Context;

public:
#define PTR(CLASS) typename Ptr<CLASS>::type

  explicit EvaluatedExprVisitorBase(const TreeContext &Context)
      : Context(Context) {}

  // Expressions that have no potentially-evaluated subexpressions (but may have
  // other sub-expressions).
  void VisitDeclRefExpr(PTR(DeclRefExpr) E) {}
  void VisitOffsetOfExpr(PTR(OffsetOfExpr) E) {}
  void VisitUnaryExprOrTypeTraitExpr(PTR(UnaryExprOrTypeTraitExpr) E) {}
  void VisitMemberExpr(PTR(MemberExpr) E) {
    // Only the base matters.
    return this->Visit(E->getBase());
  }

  void VisitChooseExpr(PTR(ChooseExpr) E) {
    // Don't visit either child expression if the condition is dependent.
    if (E->getCond()->isValueDependent())
      return;
    // Only the selected subexpression matters; the other one is not evaluated.
    return this->Visit(E->getChosenSubExpr());
  }

  void VisitGenericSelectionExpr(PTR(GenericSelectionExpr) E) {
    // The controlling expression of a generic selection is not evaluated.

    // Don't visit either child expression if the condition is type-dependent.
    if (E->isResultDependent())
      return;
    // Only the selected subexpression matters; the other subexpressions and the
    // controlling expression are not evaluated.
    return this->Visit(E->getResultExpr());
  }

  void VisitDesignatedInitExpr(PTR(DesignatedInitExpr) E) {
    // Only the actual initializer matters; the designators are all constant
    // expressions.
    return this->Visit(E->getInit());
  }

  void VisitCallExpr(PTR(CallExpr) CE) {
    if (!CE->isUnevaluatedBuiltinCall(Context))
      return getDerived().VisitExpr(CE);
  }

  void VisitStmt(PTR(Stmt) S) {
    for (auto *SubStmt : S->children())
      if (SubStmt)
        this->Visit(SubStmt);
  }

  ImplClass &getDerived() { return *static_cast<ImplClass *>(this); }

#undef PTR
};

template <typename ImplClass>
class EvaluatedExprVisitor
    : public EvaluatedExprVisitorBase<std::add_pointer, ImplClass> {
public:
  explicit EvaluatedExprVisitor(const TreeContext &Context)
      : EvaluatedExprVisitorBase<std::add_pointer, ImplClass>(Context) {}
};

template <typename ImplClass>
class ConstEvaluatedExprVisitor
    : public EvaluatedExprVisitorBase<llvm::make_const_ptr, ImplClass> {
public:
  explicit ConstEvaluatedExprVisitor(const TreeContext &Context)
      : EvaluatedExprVisitorBase<llvm::make_const_ptr, ImplClass>(Context) {}
};
} // namespace neverc

#endif // NEVERC_TREE_EVALUATEDEXPRVISITOR_H
