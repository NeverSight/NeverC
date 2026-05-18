#include "neverc/Tree/Core/DependCalc.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"

using namespace neverc;

ExprDependence neverc::computeDependence(FullExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence neverc::computeDependence(OpaqueValueExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (auto *S = E->getSourceExpr())
    D |= S->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(ParenExpr *E) {
  return E->getSubExpr()->getDependence();
}

ExprDependence neverc::computeDependence(UnaryOperator *E,
                                         const TreeContext &) {
  return toExprDependenceForImpliedType(E->getType()->getDependence()) |
         E->getSubExpr()->getDependence();
}

ExprDependence neverc::computeDependence(UnaryExprOrTypeTraitExpr *E) {
  // Not type-dependent. Value-dependent if the argument is type-dependent.
  if (E->isArgumentType())
    return turnTypeToValueDependence(
        toExprDependenceAsWritten(E->getArgumentType()->getDependence()));

  auto ArgDeps = E->getArgumentExpr()->getDependence();
  auto Deps = ArgDeps & ~ExprDependence::TypeValue;
  // Value-dependent if the argument is type-dependent.
  if (ArgDeps & ExprDependence::Type)
    Deps |= ExprDependence::Value;
  // Check to see if we are in the situation where alignof(decl) should be
  // dependent because decl's alignment is dependent.
  auto ExprKind = E->getKind();
  if (ExprKind != UETT_AlignOf && ExprKind != UETT_PreferredAlignOf)
    return Deps;
  if ((Deps & ExprDependence::Value) && (Deps & ExprDependence::Instantiation))
    return Deps;

  auto *NoParens = E->getArgumentExpr()->IgnoreParens();
  const ValueDecl *D = nullptr;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(NoParens))
    D = DRE->getDecl();
  else if (const auto *ME = dyn_cast<MemberExpr>(NoParens))
    D = ME->getMemberDecl();
  if (!D)
    return Deps;
  for (const auto *I : D->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentErrorDependent())
      Deps |= ExprDependence::Error;
    if (I->isAlignmentDependent())
      Deps |= ExprDependence::ValueInstantiation;
  }
  return Deps;
}

ExprDependence neverc::computeDependence(ArraySubscriptExpr *E) {
  return E->getLHS()->getDependence() | E->getRHS()->getDependence();
}

ExprDependence neverc::computeDependence(MatrixSubscriptExpr *E) {
  auto D = E->getBase()->getDependence() | E->getRowIdx()->getDependence();
  if (auto *Col = E->getColumnIdx())
    D |= Col->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(CompoundLiteralExpr *E) {
  return toExprDependenceAsWritten(
             E->getTypeSourceInfo()->getType()->getDependence()) |
         toExprDependenceForImpliedType(E->getType()->getDependence()) |
         turnTypeToValueDependence(E->getInitializer()->getDependence());
}

ExprDependence neverc::computeDependence(ImplicitCastExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (LLVM_LIKELY(E->getSubExpr() != nullptr))
    D |= E->getSubExpr()->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence neverc::computeDependence(ExplicitCastExpr *E) {
  auto D = toExprDependenceAsWritten(E->getTypeAsWritten()->getDependence()) |
           toExprDependenceForImpliedType(E->getType()->getDependence());
  if (LLVM_LIKELY(E->getSubExpr() != nullptr))
    D |= E->getSubExpr()->getDependence() & ~ExprDependence::Type;
  return D;
}

ExprDependence neverc::computeDependence(BinaryOperator *E) {
  return E->getLHS()->getDependence() | E->getRHS()->getDependence();
}

ExprDependence neverc::computeDependence(ConditionalOperator *E) {
  return E->getCond()->getDependence() | E->getLHS()->getDependence() |
         E->getRHS()->getDependence();
}

ExprDependence neverc::computeDependence(BinaryConditionalOperator *E) {
  return E->getCommon()->getDependence() | E->getFalseExpr()->getDependence();
}

ExprDependence neverc::computeDependence(StmtExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  if (const auto *CompoundExprResult =
          dyn_cast_or_null<ValueStmt>(E->getSubStmt()->getStmtExprResult()))
    if (const Expr *ResultExpr = CompoundExprResult->getExprStmt())
      D |= ResultExpr->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(ConvertVectorExpr *E) {
  auto D = toExprDependenceAsWritten(
               E->getTypeSourceInfo()->getType()->getDependence()) |
           E->getSrcExpr()->getDependence();
  if (!E->getType()->isDependentType())
    D &= ~ExprDependence::Type;
  return D;
}

ExprDependence neverc::computeDependence(ChooseExpr *E) {
  if (E->isConditionDependent())
    return ExprDependence::TypeValueInstantiation |
           E->getCond()->getDependence() | E->getLHS()->getDependence() |
           E->getRHS()->getDependence();

  auto Cond = E->getCond()->getDependence();
  auto Active = E->getLHS()->getDependence();
  auto Inactive = E->getRHS()->getDependence();
  if (!E->isConditionTrue())
    std::swap(Active, Inactive);
  // Take type- and value- dependency from the active branch. Propagate all
  // other flags from all branches.
  return (Active & ExprDependence::TypeValue) |
         ((Cond | Active | Inactive) & ~ExprDependence::TypeValue);
}

ExprDependence neverc::computeDependence(ParenListExpr *P) {
  auto D = ExprDependence::None;
  for (auto *E : P->exprs())
    D |= E->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(VAArgExpr *E) {
  auto D = toExprDependenceAsWritten(
               E->getWrittenTypeInfo()->getType()->getDependence()) |
           (E->getSubExpr()->getDependence() & ~ExprDependence::Type);
  return D;
}

ExprDependence neverc::computeDependence(NoInitExpr *E) {
  return toExprDependenceForImpliedType(E->getType()->getDependence()) &
         (ExprDependence::Instantiation | ExprDependence::Error);
}

ExprDependence neverc::computeDependence(ArrayInitLoopExpr *E) {
  auto D =
      E->getCommonExpr()->getDependence() | E->getSubExpr()->getDependence();
  return turnTypeToValueDependence(D);
}

ExprDependence neverc::computeDependence(ImplicitValueInitExpr *E) {
  return toExprDependenceForImpliedType(E->getType()->getDependence()) &
         ExprDependence::Instantiation;
}

ExprDependence neverc::computeDependence(ExtVectorElementExpr *E) {
  return E->getBase()->getDependence();
}

ExprDependence neverc::computeDependence(DeclRefExpr *E,
                                         const TreeContext &Ctx) {
  auto Deps = ExprDependence::None;

  auto *Decl = E->getDecl();
  auto Type = E->getType();

  Deps |= toExprDependenceForImpliedType(Type->getDependence()) &
          ExprDependence::Error;

  if (const auto *Var = dyn_cast<VarDecl>(Decl)) {
    const Expr *Init = LLVM_LIKELY(!Var->getPreviousDecl())
                           ? Var->getInit()
                           : Var->getAnyInitializer();
    if (Init && Init->containsErrors())
      Deps |= ExprDependence::Error;
    return Deps;
  }

  return Deps;
}

ExprDependence neverc::computeDependence(RecoveryExpr *E) {
  // RecoveryExpr is
  //   - always value-dependent, and therefore instantiation dependent
  //   - contains errors (ExprDependence::Error), by definition
  //   - type-dependent if we don't know the type (fallback to an opaque
  //     dependent type), or the type is known and dependent, or it has
  //     type-dependent subexpressions.
  auto D = toExprDependenceAsWritten(E->getType()->getDependence()) |
           ExprDependence::ErrorDependent;
  for (auto *S : E->subExpressions())
    D |= S->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(PredefinedExpr *E) {
  return toExprDependenceForImpliedType(E->getType()->getDependence());
}

__attribute__((hot)) ExprDependence
neverc::computeDependence(CallExpr *E, llvm::ArrayRef<Expr *> PreArgs) {
  auto D = E->getCallee()->getDependence();
  const unsigned NumArgs = E->getNumArgs();
  Expr *const *Args = E->getArgs();
  for (unsigned I = 0; I < NumArgs; ++I) {
    if (LLVM_LIKELY(I + 2 < NumArgs))
      __builtin_prefetch(Args[I + 2], 0, 1);
    if (LLVM_LIKELY(Args[I] != nullptr))
      D |= Args[I]->getDependence();
  }
  for (auto *A : PreArgs)
    D |= A->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(OffsetOfExpr *E) {
  auto D = turnTypeToValueDependence(toExprDependenceAsWritten(
      E->getTypeSourceInfo()->getType()->getDependence()));
  for (unsigned I = 0, N = E->getNumExpressions(); I < N; ++I)
    D |= turnTypeToValueDependence(E->getIndexExpr(I)->getDependence());
  return D;
}

ExprDependence neverc::computeDependence(MemberExpr *E) {
  return E->getBase()->getDependence();
}

__attribute__((hot)) ExprDependence neverc::computeDependence(InitListExpr *E) {
  auto D = ExprDependence::None;
  const unsigned N = E->getNumInits();
  Expr *const *Inits = E->getInits();
  unsigned I = 0;
  for (; I + 4 <= N; I += 4) {
    D |= Inits[I]->getDependence() | Inits[I + 1]->getDependence() |
         Inits[I + 2]->getDependence() | Inits[I + 3]->getDependence();
  }
  for (; I < N; ++I)
    D |= Inits[I]->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(ShuffleVectorExpr *E) {
  auto D = toExprDependenceForImpliedType(E->getType()->getDependence());
  for (auto *C : llvm::ArrayRef(E->getSubExprs(), E->getNumSubExprs()))
    D |= C->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(GenericSelectionExpr *E) {
  auto D = ExprDependence::None;
  for (auto *AE : E->getAssocExprs())
    D |= AE->getDependence() & ExprDependence::Error;

  if (E->isExprPredicate())
    D |= E->getControllingExpr()->getDependence() & ExprDependence::Error;
  else
    D |= toExprDependenceAsWritten(
        E->getControllingType()->getType()->getDependence());

  if (E->isResultDependent())
    return D | ExprDependence::TypeValueInstantiation;
  return D | E->getResultExpr()->getDependence();
}

ExprDependence neverc::computeDependence(DesignatedInitExpr *E) {
  auto Deps = E->getInit()->getDependence();
  for (const auto &D : E->designators()) {
    auto DesignatorDeps = ExprDependence::None;
    if (D.isArrayDesignator())
      DesignatorDeps |= E->getArrayIndex(D)->getDependence();
    else if (D.isArrayRangeDesignator())
      DesignatorDeps |= E->getArrayRangeStart(D)->getDependence() |
                        E->getArrayRangeEnd(D)->getDependence();
    Deps |= DesignatorDeps;
    if (DesignatorDeps & ExprDependence::TypeValue)
      Deps |= ExprDependence::TypeValueInstantiation;
  }
  return Deps;
}

ExprDependence neverc::computeDependence(PseudoObjectExpr *O) {
  auto D = O->getSyntacticForm()->getDependence();
  for (auto *E : O->semantics())
    D |= E->getDependence();
  return D;
}

ExprDependence neverc::computeDependence(AtomicExpr *A) {
  auto D = ExprDependence::None;
  for (auto *E : llvm::ArrayRef(A->getSubExprs(), A->getNumSubExprs()))
    D |= E->getDependence();
  return D;
}
