#ifndef NEVERC_TREE_IGNOREEXPR_H
#define NEVERC_TREE_IGNOREEXPR_H

#include "neverc/Tree/Expr/Expr.h"

namespace neverc {
namespace detail {
inline Expr *IgnoreExprNodesImpl(Expr *E) { return E; }
template <typename FnTy, typename... FnTys>
Expr *IgnoreExprNodesImpl(Expr *E, FnTy &&Fn, FnTys &&...Fns) {
  return IgnoreExprNodesImpl(std::forward<FnTy>(Fn)(E),
                             std::forward<FnTys>(Fns)...);
}
} // namespace detail

template <typename... FnTys> Expr *IgnoreExprNodes(Expr *E, FnTys &&...Fns) {
  Expr *LastE = nullptr;
  while (E != LastE) {
    LastE = E;
    E = detail::IgnoreExprNodesImpl(E, std::forward<FnTys>(Fns)...);
  }
  return E;
}

template <typename... FnTys>
const Expr *IgnoreExprNodes(const Expr *E, FnTys &&...Fns) {
  return IgnoreExprNodes(const_cast<Expr *>(E), std::forward<FnTys>(Fns)...);
}

inline Expr *IgnoreImplicitCastsSingleStep(Expr *E) {
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    return ICE->getSubExpr();

  if (auto *FE = dyn_cast<FullExpr>(E))
    return FE->getSubExpr();

  return E;
}

inline Expr *IgnoreImplicitCastsExtraSingleStep(Expr *E) {
  return IgnoreImplicitCastsSingleStep(E);
}

inline Expr *IgnoreCastsSingleStep(Expr *E) {
  if (auto *CE = dyn_cast<CastExpr>(E))
    return CE->getSubExpr();

  if (auto *FE = dyn_cast<FullExpr>(E))
    return FE->getSubExpr();

  return E;
}

inline Expr *IgnoreLValueCastsSingleStep(Expr *E) {
  // Skip what IgnoreCastsSingleStep skips, except that only
  // lvalue-to-rvalue casts are skipped.
  if (auto *CE = dyn_cast<CastExpr>(E))
    if (CE->getCastKind() != CK_LValueToRValue)
      return E;

  return IgnoreCastsSingleStep(E);
}

inline Expr *IgnoreBaseCastsSingleStep(Expr *E) {
  if (auto *CE = dyn_cast<CastExpr>(E))
    if (CE->getCastKind() == CK_NoOp)
      return CE->getSubExpr();

  return E;
}

inline Expr *IgnoreImplicitSingleStep(Expr *E) {
  return IgnoreImplicitCastsSingleStep(E);
}

inline Expr *IgnoreImplicitAsWrittenSingleStep(Expr *E) {
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    return ICE->getSubExprAsWritten();

  return IgnoreImplicitSingleStep(E);
}

inline Expr *IgnoreParensOnlySingleStep(Expr *E) {
  if (auto *PE = dyn_cast<ParenExpr>(E))
    return PE->getSubExpr();
  return E;
}

inline Expr *IgnoreParensSingleStep(Expr *E) {
  if (auto *PE = dyn_cast<ParenExpr>(E))
    return PE->getSubExpr();

  if (auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Extension)
      return UO->getSubExpr();
  }

  else if (auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
    if (!GSE->isResultDependent())
      return GSE->getResultExpr();
  }

  else if (auto *CE = dyn_cast<ChooseExpr>(E)) {
    if (!CE->isConditionDependent())
      return CE->getChosenSubExpr();
  }

  else if (auto *PE = dyn_cast<PredefinedExpr>(E)) {
    if (PE->isTransparent() && PE->getFunctionName())
      return PE->getFunctionName();
  }

  return E;
}

} // namespace neverc

#endif // NEVERC_TREE_IGNOREEXPR_H
