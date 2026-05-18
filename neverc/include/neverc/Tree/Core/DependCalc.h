#ifndef NEVERC_AST_COMPUTEDEPENDENCE_H
#define NEVERC_AST_COMPUTEDEPENDENCE_H

#include "neverc/Tree/Type/DependenceFlags.h"
#include "llvm/ADT/ArrayRef.h"

namespace neverc {

class TreeContext;

class Expr;
class FullExpr;
class OpaqueValueExpr;
class ParenExpr;
class UnaryOperator;
class UnaryExprOrTypeTraitExpr;
class ArraySubscriptExpr;
class MatrixSubscriptExpr;
class CompoundLiteralExpr;
class ImplicitCastExpr;
class ExplicitCastExpr;
class BinaryOperator;
class ConditionalOperator;
class BinaryConditionalOperator;
class StmtExpr;
class ConvertVectorExpr;
class VAArgExpr;
class ChooseExpr;
class NoInitExpr;
class ArrayInitLoopExpr;
class ImplicitValueInitExpr;
class InitListExpr;
class ExtVectorElementExpr;
class DeclRefExpr;
class RecoveryExpr;
class PredefinedExpr;
class CallExpr;
class OffsetOfExpr;
class MemberExpr;
class ShuffleVectorExpr;
class GenericSelectionExpr;
class DesignatedInitExpr;
class ParenListExpr;
class PseudoObjectExpr;
class AtomicExpr;

// The following functions are called from constructors of `Expr`, so they
// should not access anything beyond basic
ExprDependence computeDependence(FullExpr *E);
ExprDependence computeDependence(OpaqueValueExpr *E);
ExprDependence computeDependence(ParenExpr *E);
ExprDependence computeDependence(UnaryOperator *E, const TreeContext &Ctx);
ExprDependence computeDependence(UnaryExprOrTypeTraitExpr *E);
ExprDependence computeDependence(ArraySubscriptExpr *E);
ExprDependence computeDependence(MatrixSubscriptExpr *E);
ExprDependence computeDependence(CompoundLiteralExpr *E);
ExprDependence computeDependence(ImplicitCastExpr *E);
ExprDependence computeDependence(ExplicitCastExpr *E);
ExprDependence computeDependence(BinaryOperator *E);
ExprDependence computeDependence(ConditionalOperator *E);
ExprDependence computeDependence(BinaryConditionalOperator *E);
ExprDependence computeDependence(StmtExpr *E);
ExprDependence computeDependence(ConvertVectorExpr *E);
ExprDependence computeDependence(VAArgExpr *E);
ExprDependence computeDependence(ChooseExpr *E);
ExprDependence computeDependence(NoInitExpr *E);
ExprDependence computeDependence(ArrayInitLoopExpr *E);
ExprDependence computeDependence(ImplicitValueInitExpr *E);
ExprDependence computeDependence(InitListExpr *E);
ExprDependence computeDependence(ExtVectorElementExpr *E);
ExprDependence computeDependence(DeclRefExpr *E, const TreeContext &Ctx);
ExprDependence computeDependence(RecoveryExpr *E);
ExprDependence computeDependence(PredefinedExpr *E);
ExprDependence computeDependence(CallExpr *E, llvm::ArrayRef<Expr *> PreArgs);
ExprDependence computeDependence(OffsetOfExpr *E);
ExprDependence computeDependence(MemberExpr *E);
ExprDependence computeDependence(ShuffleVectorExpr *E);
ExprDependence computeDependence(GenericSelectionExpr *E);
ExprDependence computeDependence(DesignatedInitExpr *E);
ExprDependence computeDependence(ParenListExpr *E);
ExprDependence computeDependence(PseudoObjectExpr *E);
ExprDependence computeDependence(AtomicExpr *E);

} // namespace neverc
#endif
