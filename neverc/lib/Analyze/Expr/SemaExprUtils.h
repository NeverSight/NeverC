//===- SemaExprUtils.h - Shared helpers for split SemaExpr TUs ------------===//
//
// Internal header that exposes helpers originally file-local inside
// SemaExpr*.cpp. Splitting expression checking into per-topic TUs
// (BinOp Vector/Arithmetic/Compare, Subscript, Conditional, UnaryOp)
// requires these symbols to be visible across translation units.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_ANALYZE_EXPR_SEMAEXPRUTILS_H
#define NEVERC_LIB_ANALYZE_EXPR_SEMAEXPRUTILS_H

#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Expr/Expr.h"

namespace neverc {

template <class Derived>
class ReferencedDeclWalker : public EvaluatedExprVisitor<Derived> {
protected:
  Sema &S;

public:
  typedef EvaluatedExprVisitor<Derived> Inherited;

  ReferencedDeclWalker(Sema &S) : Inherited(S.Context), S(S) {}

  Derived &asImpl() { return *static_cast<Derived *>(this); }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    auto *D = E->getDecl();
    if (isa<FunctionDecl>(D) || isa<VarDecl>(D)) {
      asImpl().visitUsedDecl(E->getLocation(), D);
    }
  }

  void VisitMemberExpr(MemberExpr *E) {
    auto *D = E->getMemberDecl();
    if (isa<FunctionDecl>(D) || isa<VarDecl>(D)) {
      asImpl().visitUsedDecl(E->getMemberLoc(), D);
    }
    asImpl().Visit(E->getBase());
  }

  void VisitInitListExpr(InitListExpr *ILE) {
    if (ILE->hasArrayFiller())
      asImpl().Visit(ILE->getArrayFiller());
    Inherited::VisitInitListExpr(ILE);
  }

  void visitUsedDecl(SourceLocation Loc, Decl *D) {}
};

// Defined in SemaExprBinOpVector.cpp
ExprResult castVectorElement(Expr *E, QualType ElementType, Sema &S);

// Defined in SemaExprBinOpArithmetic.cpp
bool verifyArithmeticPointerOp(Sema &S, SourceLocation Loc, Expr *Operand);

// Defined in SemaExprBinOpCompare.cpp
void warnLogicalNotOnLHS(Sema &S, ExprResult &LHS, ExprResult &RHS,
                         SourceLocation Loc, BinaryOperatorKind Opc);
void warnTautologicalCmp(Sema &S, SourceLocation Loc, Expr *LHS, Expr *RHS,
                         BinaryOperatorKind Opc);

// Defined in SemaExprBinOp.cpp
bool checkForModifiableLvalue(Expr *E, SourceLocation Loc, Sema &S);
void noteModifiableNonNullParam(Sema &S, const Expr *Exp);
QualType checkIndirectionOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                 SourceLocation OpLoc, bool IsAfterAmp = false);
bool requiresHalfVecConversion(bool OpRequiresConversion, TreeContext &Ctx,
                               Expr *E0, Expr *E1 = nullptr);
void suggestParentheses(Sema &Self, SourceLocation Loc,
                        const PartialDiagnostic &Note, SourceRange ParenRange);
bool couldBeNeverCStringOperand(Expr *E);
bool isNeverCStringOperand(Sema &S, Expr *E);
void warnNullPtrDeref(Sema &S, Expr *E);
bool isVector(QualType QT, QualType ElementType);
ExprResult buildNeverCStringConcat(Sema &S, SourceLocation OpLoc, Expr *LHS,
                                   Expr *RHS);
ExprResult buildNeverCStringCompare(Sema &S, SourceLocation OpLoc,
                                    BinaryOperatorKind Opc, Expr *LHS,
                                    Expr *RHS);

// --- Arithmetic conversion helpers (SemaExpr / Conditional) ---
typedef ExprResult PerformCastFn(Sema &S, Expr *operand, QualType toType);
inline ExprResult doIntegralCast(Sema &S, Expr *op, QualType toType) {
  return S.ImpCastExprToType(op, toType, CK_IntegralCast);
}

inline ExprResult doComplexIntegralCast(Sema &S, Expr *op, QualType toType) {
  return S.ImpCastExprToType(op, S.Context.getComplexType(toType),
                             CK_IntegralComplexCast);
}
template <PerformCastFn doLHSCast, PerformCastFn doRHSCast>
QualType balanceIntegerTypes(Sema &S, ExprResult &LHS, ExprResult &RHS,
                             QualType LHSType, QualType RHSType,
                             bool IsCompAssign) {
  // The rules for this case are in C99 6.3.1.8
  int order = S.Context.getIntegerTypeOrder(LHSType, RHSType);
  bool LHSSigned = LHSType->hasSignedIntegerRepresentation();
  bool RHSSigned = RHSType->hasSignedIntegerRepresentation();
  if (LHSSigned == RHSSigned) {
    // Same signedness; use the higher-ranked type
    if (order >= 0) {
      RHS = (*doRHSCast)(S, RHS.get(), LHSType);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), RHSType);
    return RHSType;
  } else if (order != (LHSSigned ? 1 : -1)) {
    // The unsigned type has greater than or equal rank to the
    // signed type, so use the unsigned type
    if (RHSSigned) {
      RHS = (*doRHSCast)(S, RHS.get(), LHSType);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), RHSType);
    return RHSType;
  } else if (S.Context.getIntWidth(LHSType) != S.Context.getIntWidth(RHSType)) {
    // The two types are different widths; if we are here, that
    // means the signed type is larger than the unsigned type, so
    // use the signed type.
    if (LHSSigned) {
      RHS = (*doRHSCast)(S, RHS.get(), LHSType);
      return LHSType;
    } else if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), RHSType);
    return RHSType;
  } else {
    // The signed type is higher-ranked than the unsigned type,
    // but isn't actually any bigger (like unsigned int and long
    // on most 32-bit systems).  Use the unsigned type corresponding
    // to the signed type.
    QualType result =
        S.Context.getCorrespondingUnsignedType(LHSSigned ? LHSType : RHSType);
    RHS = (*doRHSCast)(S, RHS.get(), result);
    if (!IsCompAssign)
      LHS = (*doLHSCast)(S, LHS.get(), result);
    return result;
  }
}
QualType promoteIntToFloat(Sema &S, ExprResult &FloatExpr, ExprResult &IntExpr,
                           QualType FloatTy, QualType IntTy, bool ConvertFloat,
                           bool ConvertInt);
QualType balanceFloatTypes(Sema &S, ExprResult &LHS, ExprResult &RHS,
                           QualType LHSType, QualType RHSType,
                           bool IsCompAssign);

// Defined in SemaExprCallArgs.cpp
bool resolveArgPlaceholders(Sema &S, MultiExprArg args);

// Defined in SemaExpr.cpp
ExprResult buildNeverCStringLiteral(Sema &S, QualType StringTy,
                                    Expr *Initializer, StringLiteral *SL);

// Defined in SemaExprCallArgs.cpp
FunctionDecl *rewriteBuiltinFunctionDecl(Sema *Sema, TreeContext &Context,
                                         FunctionDecl *FDecl,
                                         MultiExprArg ArgExprs);
void validateDirectCallTarget(Sema &S, const Expr *Fn, FunctionDecl *Callee,
                              MultiExprArg ArgExprs);


} // namespace neverc

#endif // NEVERC_LIB_ANALYZE_EXPR_SEMAEXPRUTILS_H
