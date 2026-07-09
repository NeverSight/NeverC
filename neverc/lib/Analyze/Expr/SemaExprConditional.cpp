//===- SemaExprConditional.cpp - Conditional operator semantic analysis ---===//
//
// Extracted from SemaExpr.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Conditional operator
// ===----------------------------------------------------------------------===

bool Sema::DiagnoseConditionalForNull(Expr *LHSExpr, Expr *RHSExpr,
                                      SourceLocation QuestionLoc) {
  Expr *NullExpr = LHSExpr;
  Expr *NonPointerExpr = RHSExpr;
  Expr::NullPointerConstantKind NullKind = NullExpr->isNullPointerConstant(
      Context, Expr::NPC_ValueDependentIsNotNull);

  if (NullKind == Expr::NPCK_NotNull) {
    NullExpr = RHSExpr;
    NonPointerExpr = LHSExpr;
    NullKind = NullExpr->isNullPointerConstant(
        Context, Expr::NPC_ValueDependentIsNotNull);
  }

  if (NullKind == Expr::NPCK_NotNull)
    return false;

  if (NullKind == Expr::NPCK_ZeroExpression)
    return false;

  if (NullKind == Expr::NPCK_ZeroLiteral) {
    // In this case, check to make sure that we got here from a "NULL"
    // string in the source code.
    NullExpr = NullExpr->IgnoreParenImpCasts();
    SourceLocation loc = NullExpr->getExprLoc();
    if (!locateMacroSpelling(loc, "NULL"))
      return false;
  }

  int DiagType = (NullKind == Expr::NPCK_nullptr);
  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands_null)
      << NonPointerExpr->getType() << DiagType
      << NonPointerExpr->getSourceRange();
  return true;
}

namespace {
bool validateCondition(Sema &S, Expr *Cond, SourceLocation QuestionLoc) {
  QualType CondTy = Cond->getType();

  // C99 6.5.15p2
  if (CondTy->isScalarType())
    return false;

  S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_scalar)
      << CondTy << Cond->getSourceRange();
  return true;
}
} // namespace

namespace {
bool validateConditionalNull(Sema &S, ExprResult &NullExpr,
                             QualType PointerTy) {
  if (!PointerTy->isAnyPointerType() ||
      !NullExpr.get()->isNullPointerConstant(S.Context,
                                             Expr::NPC_ValueDependentIsNull))
    return true;

  NullExpr = S.ImpCastExprToType(NullExpr.get(), PointerTy, CK_NullToPointer);
  return false;
}
} // namespace

namespace {
QualType validateConditionalPointers(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  if (S.Context.hasSameType(LHSTy, RHSTy)) {
    // Two identical pointers types are always compatible.
    return S.Context.getCommonSugaredType(LHSTy, RHSTy);
  }

  QualType lhptee, rhptee;
  lhptee = LHSTy->castAs<PointerType>()->getPointeeType();
  rhptee = RHSTy->castAs<PointerType>()->getPointeeType();

  // C99 6.5.15p6: If both operands are pointers to compatible types or to
  // differently qualified versions of compatible types, the result type is
  // a pointer to an appropriately qualified version of the composite
  // type.

  // Only CVR-qualifiers exist in the standard, and the differently-qualified
  // clause doesn't make sense for our extensions. E.g. address space 2 should
  // be incompatible with address space 3: they may live on different devices or
  // anything.
  Qualifiers lhQual = lhptee.getQualifiers();
  Qualifiers rhQual = rhptee.getQualifiers();

  LangAS ResultAddrSpace = LangAS::Default;
  LangAS LAddrSpace = lhQual.getAddressSpace();
  LangAS RAddrSpace = rhQual.getAddressSpace();

  // Conversion between pointers to distinct address spaces is disallowed.
  if (lhQual.isAddressSpaceSupersetOf(rhQual))
    ResultAddrSpace = LAddrSpace;
  else if (rhQual.isAddressSpaceSupersetOf(lhQual))
    ResultAddrSpace = RAddrSpace;
  else {
    S.Diag(Loc, diag::err_typecheck_op_on_nonoverlapping_address_space_pointers)
        << LHSTy << RHSTy << 2 << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  unsigned MergedCVRQual =
      lhQual.getCVRQualifiers() | rhQual.getCVRQualifiers();
  auto LHSCastKind = CK_BitCast, RHSCastKind = CK_BitCast;
  lhQual.removeCVRQualifiers();
  rhQual.removeCVRQualifiers();

  // Merge CVR and address space qualifiers for the conditional result.
  LHSCastKind =
      LAddrSpace == ResultAddrSpace ? CK_BitCast : CK_AddressSpaceConversion;
  RHSCastKind =
      RAddrSpace == ResultAddrSpace ? CK_BitCast : CK_AddressSpaceConversion;
  lhQual.removeAddressSpace();
  rhQual.removeAddressSpace();

  lhptee = S.Context.getQualifiedType(lhptee.getUnqualifiedType(), lhQual);
  rhptee = S.Context.getQualifiedType(rhptee.getUnqualifiedType(), rhQual);

  QualType CompositeTy =
      S.Context.mergeTypes(lhptee, rhptee, /*Unqualified=*/false,
                           /*IsConditionalOperator=*/true);

  if (CompositeTy.isNull()) {
    // In this situation, we assume void* type. No especially good
    // reason, but this is what gcc does, and we do have to pick
    // to get a consistent AST.
    QualType incompatTy;
    incompatTy = S.Context.getPointerType(
        S.Context.getAddrSpaceQualType(S.Context.VoidTy, ResultAddrSpace));
    LHS = S.ImpCastExprToType(LHS.get(), incompatTy, LHSCastKind);
    RHS = S.ImpCastExprToType(RHS.get(), incompatTy, RHSCastKind);

    // The warning emission and cast to void* leaves room
    // for casts between types with incompatible address space qualifiers:
    // local int *global *a;
    // global int *global *b;
    // a = (0 ? a : b); // see C99 6.5.16.1.p1.
    S.Diag(Loc, diag::ext_typecheck_cond_incompatible_pointers)
        << LHSTy << RHSTy << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();

    return incompatTy;
  }

  // The pointer types are compatible. ResultTy should have the address space
  // qualifier that is a superset of the 2nd and 3rd operands.
  QualType ResultTy =
      S.Context.getPointerType(CompositeTy.withCVRQualifiers(MergedCVRQual));

  LHS = S.ImpCastExprToType(LHS.get(), ResultTy, LHSCastKind);
  RHS = S.ImpCastExprToType(RHS.get(), ResultTy, RHSCastKind);
  return ResultTy;
}
} // namespace

namespace {
QualType validateConditionalObjPtrs(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                    SourceLocation Loc) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  QualType lhptee = LHSTy->castAs<PointerType>()->getPointeeType();
  QualType rhptee = RHSTy->castAs<PointerType>()->getPointeeType();

  // ignore qualifiers on void (C99 6.5.15p3, clause 6)
  if (lhptee->isVoidType() && rhptee->isIncompleteOrObjectType()) {
    // Figure out necessary qualifiers (C99 6.5.15p6)
    QualType destPointee =
        S.Context.getQualifiedType(lhptee, rhptee.getQualifiers());
    QualType destType = S.Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    LHS = S.ImpCastExprToType(LHS.get(), destType, CK_NoOp);
    // Promote to void*.
    RHS = S.ImpCastExprToType(RHS.get(), destType, CK_BitCast);
    return destType;
  }
  if (rhptee->isVoidType() && lhptee->isIncompleteOrObjectType()) {
    QualType destPointee =
        S.Context.getQualifiedType(rhptee, lhptee.getQualifiers());
    QualType destType = S.Context.getPointerType(destPointee);
    // Add qualifiers if necessary.
    RHS = S.ImpCastExprToType(RHS.get(), destType, CK_NoOp);
    // Promote to void*.
    LHS = S.ImpCastExprToType(LHS.get(), destType, CK_BitCast);
    return destType;
  }

  return validateConditionalPointers(S, LHS, RHS, Loc);
}
} // namespace

namespace {
bool validatePointerIntMismatch(Sema &S, ExprResult &Int, Expr *PointerExpr,
                                SourceLocation Loc, bool IsIntFirstExpr) {
  if (!PointerExpr->getType()->isPointerType() ||
      !Int.get()->getType()->isIntegerType())
    return false;

  Expr *Expr1 = IsIntFirstExpr ? Int.get() : PointerExpr;
  Expr *Expr2 = IsIntFirstExpr ? PointerExpr : Int.get();

  S.Diag(Loc, diag::ext_typecheck_cond_pointer_integer_mismatch)
      << Expr1->getType() << Expr2->getType() << Expr1->getSourceRange()
      << Expr2->getSourceRange();
  Int = S.ImpCastExprToType(Int.get(), PointerExpr->getType(),
                            CK_IntegralToPointer);
  return true;
}
} // namespace

namespace {
QualType vectorArithmeticConversions(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation QuestionLoc) {
  LHS = S.DefaultFunctionArrayLvalueConversion(LHS.get());
  if (LHS.isInvalid())
    return QualType();
  RHS = S.DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType =
      S.Context.getCanonicalType(LHS.get()->getType()).getUnqualifiedType();
  QualType RHSType =
      S.Context.getCanonicalType(RHS.get()->getType()).getUnqualifiedType();

  if (!LHSType->isIntegerType() && !LHSType->isRealFloatingType()) {
    S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_int_float)
        << LHSType << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSType->isIntegerType() && !RHSType->isRealFloatingType()) {
    S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_int_float)
        << RHSType << RHS.get()->getSourceRange();
    return QualType();
  }

  // If both types are identical, no conversion is needed.
  if (LHSType == RHSType)
    return LHSType;

  // Now handle "real" floating types (i.e. float, double, long double).
  if (LHSType->isRealFloatingType() || RHSType->isRealFloatingType())
    return balanceFloatTypes(S, LHS, RHS, LHSType, RHSType,
                             /*IsCompAssign = */ false);

  // Finally, we have two differing integer types.
  return balanceIntegerTypes<doIntegralCast, doIntegralCast>(
      S, LHS, RHS, LHSType, RHSType, /*IsCompAssign = */ false);
}
} // namespace

namespace {
QualType convertScalarsToVectors(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                 QualType CondTy, SourceLocation QuestionLoc) {
  QualType ResTy = vectorArithmeticConversions(S, LHS, RHS, QuestionLoc);
  if (ResTy.isNull())
    return QualType();

  const VectorType *CV = CondTy->getAs<VectorType>();
  assert(CV);

  unsigned NumElements = CV->getNumElements();
  QualType VectorTy = S.Context.getExtVectorType(ResTy, NumElements);

  // Ensure that all types have the same number of bits
  if (S.Context.getTypeSize(CV->getElementType()) !=
      S.Context.getTypeSize(ResTy)) {
    // Since VectorTy is created internally, it does not pretty print
    // with a type name. Instead, we just print a description.
    std::string EleTyName = ResTy.getUnqualifiedType().getAsString();
    llvm::SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << "(vector of " << NumElements << " '" << EleTyName << "' values)";
    S.Diag(QuestionLoc, diag::err_conditional_vector_element_size)
        << CondTy << OS.str();
    return QualType();
  }

  // Convert operands to the vector result type
  LHS = S.ImpCastExprToType(LHS.get(), VectorTy, CK_VectorSplat);
  RHS = S.ImpCastExprToType(RHS.get(), VectorTy, CK_VectorSplat);

  return VectorTy;
}
} // namespace

namespace {
bool validateConditionVector(Sema &S, Expr *Cond, SourceLocation QuestionLoc) {
  const VectorType *CondTy = Cond->getType()->getAs<VectorType>();
  assert(CondTy);
  QualType EleTy = CondTy->getElementType();
  if (EleTy->isIntegerType())
    return false;

  S.Diag(QuestionLoc, diag::err_typecheck_cond_expect_nonfloat)
      << Cond->getType() << Cond->getSourceRange();
  return true;
}
} // namespace

namespace {
bool validateVectorResult(Sema &S, QualType CondTy, QualType VecResTy,
                          SourceLocation QuestionLoc) {
  const VectorType *CV = CondTy->getAs<VectorType>();
  const VectorType *RV = VecResTy->getAs<VectorType>();
  assert(CV && RV);

  if (CV->getNumElements() != RV->getNumElements()) {
    S.Diag(QuestionLoc, diag::err_conditional_vector_size)
        << CondTy << VecResTy;
    return true;
  }

  QualType CVE = CV->getElementType();
  QualType RVE = RV->getElementType();

  if (S.Context.getTypeSize(CVE) != S.Context.getTypeSize(RVE)) {
    S.Diag(QuestionLoc, diag::err_conditional_vector_element_size)
        << CondTy << VecResTy;
    return true;
  }

  return false;
}
} // namespace

namespace {
QualType checkVectorConditional(Sema &S, ExprResult &Cond, ExprResult &LHS,
                                ExprResult &RHS, SourceLocation QuestionLoc) {
  Cond = S.DefaultFunctionArrayLvalueConversion(Cond.get());
  if (Cond.isInvalid())
    return QualType();
  QualType CondTy = Cond.get()->getType();

  if (validateConditionVector(S, Cond.get(), QuestionLoc))
    return QualType();

  // If either operand is a vector then find the vector type of the
  // result type.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    bool IsBoolVecLang = true;
    QualType VecResTy =
        S.CheckVectorOperands(LHS, RHS, QuestionLoc,
                              /*isCompAssign*/ false,
                              /*AllowBothBool*/ true,
                              /*AllowBoolConversions*/ false,
                              /*AllowBooleanOperation*/ IsBoolVecLang,
                              /*ReportInvalid*/ true);
    if (VecResTy.isNull())
      return QualType();
    // The result type must match the condition type as specified in
    if (validateVectorResult(S, CondTy, VecResTy, QuestionLoc))
      return QualType();
    return VecResTy;
  }

  // Both operands are scalar.
  return convertScalarsToVectors(S, LHS, RHS, CondTy, QuestionLoc);
}
} // namespace

// ===----------------------------------------------------------------------===
// Conditional operator
// ===----------------------------------------------------------------------===

QualType Sema::CheckConditionalOperands(ExprResult &Cond, ExprResult &LHS,
                                        ExprResult &RHS, ExprValueKind &VK,
                                        ExprObjectKind &OK,
                                        SourceLocation QuestionLoc) {

  ExprResult LHSResult = CheckPlaceholderExpr(LHS.get());
  if (!LHSResult.isUsable())
    return QualType();
  LHS = LHSResult;

  ExprResult RHSResult = CheckPlaceholderExpr(RHS.get());
  if (!RHSResult.isUsable())
    return QualType();
  RHS = RHSResult;

  VK = VK_PRValue;
  OK = OK_Ordinary;

  if (Context.isDependenceAllowed() &&
      (Cond.get()->isTypeDependent() || LHS.get()->isTypeDependent() ||
       RHS.get()->isTypeDependent())) {
    return Context.DependentTy;
  }

  // The ternary operator with an ext_vector condition is sufficiently
  // different to merit its own checker.
  if (Cond.get()->getType()->isExtVectorType())
    return checkVectorConditional(*this, Cond, LHS, RHS, QuestionLoc);

  // First, check the condition.
  Cond = UsualUnaryConversions(Cond.get());
  if (Cond.isInvalid())
    return QualType();
  if (validateCondition(*this, Cond.get(), QuestionLoc))
    return QualType();
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorOperands(LHS, RHS, QuestionLoc, /*isCompAssign*/ false,
                               /*AllowBothBool*/ true,
                               /*AllowBoolConversions*/ false,
                               /*AllowBooleanOperation*/ false,
                               /*ReportInvalid*/ true);

  QualType ResTy =
      UsualArithmeticConversions(LHS, RHS, QuestionLoc, ACK_Conditional);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();

  // If both operands have arithmetic type, do the usual arithmetic conversions
  // to find a common type: C99 6.5.15p3,5.
  if (LHSTy->isArithmeticType() && RHSTy->isArithmeticType()) {
    // Disallow invalid arithmetic conversions, such as those between bit-
    // precise integers types of different sizes, or between a bit-precise
    // integer and another type.
    if (ResTy.isNull() && (LHSTy->isBitIntType() || RHSTy->isBitIntType())) {
      Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
          << LHSTy << RHSTy << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
      return QualType();
    }

    LHS = ImpCastExprToType(LHS.get(), ResTy, PrepareScalarCast(LHS, ResTy));
    RHS = ImpCastExprToType(RHS.get(), ResTy, PrepareScalarCast(RHS, ResTy));

    return ResTy;
  }

  // If both operands are the same structure or union type, the result is that
  // type.
  if (const RecordType *LHSRT = LHSTy->getAs<RecordType>()) { // C99 6.5.15p3
    if (const RecordType *RHSRT = RHSTy->getAs<RecordType>())
      if (LHSRT->getDecl() == RHSRT->getDecl())
        // "If both the operands have structure or union type, the result has
        // that type."  This implies that CV qualifiers are dropped.
        return Context.getCommonSugaredType(LHSTy.getUnqualifiedType(),
                                            RHSTy.getUnqualifiedType());
  }

  // C99 6.5.15p5: "If both operands have void type, the result has void type."
  // The following || allows only one side to be void (a GCC-ism).
  if (LHSTy->isVoidType() || RHSTy->isVoidType()) {
    QualType ResTy;
    if (LHSTy->isVoidType() && RHSTy->isVoidType()) {
      ResTy = Context.getCommonSugaredType(LHSTy, RHSTy);
    } else if (RHSTy->isVoidType()) {
      ResTy = RHSTy;
      Diag(RHS.get()->getBeginLoc(), diag::ext_typecheck_cond_one_void)
          << RHS.get()->getSourceRange();
    } else {
      ResTy = LHSTy;
      Diag(LHS.get()->getBeginLoc(), diag::ext_typecheck_cond_one_void)
          << LHS.get()->getSourceRange();
    }
    LHS = ImpCastExprToType(LHS.get(), ResTy, CK_ToVoid);
    RHS = ImpCastExprToType(RHS.get(), ResTy, CK_ToVoid);
    return ResTy;
  }

  // C23 6.5.15p7:
  //   ... if both the second and third operands have nullptr_t type, the
  //   result also has that type.
  if (LHSTy->isNullPtrType() && Context.hasSameType(LHSTy, RHSTy))
    return ResTy;

  // C99 6.5.15p6 - "if one operand is a null pointer constant, the result has
  // the type of the other operand."
  if (!validateConditionalNull(*this, RHS, LHSTy))
    return LHSTy;
  if (!validateConditionalNull(*this, LHS, RHSTy))
    return RHSTy;

  // Check constraints for C object pointers types (C99 6.5.15p3,6).
  if (LHSTy->isPointerType() && RHSTy->isPointerType())
    return validateConditionalObjPtrs(*this, LHS, RHS, QuestionLoc);

  // GCC compatibility: soften pointer/integer mismatch.  Note that
  // null pointers have been filtered out by this point.
  if (validatePointerIntMismatch(*this, LHS, RHS.get(), QuestionLoc,
                                 /*IsIntFirstExpr=*/true))
    return RHSTy;
  if (validatePointerIntMismatch(*this, RHS, LHS.get(), QuestionLoc,
                                 /*IsIntFirstExpr=*/false))
    return LHSTy;

  // Emit a better diagnostic if one of the expressions is a null pointer
  // constant and the other is not a pointer type. In this case, the user most
  // likely forgot to take the address of the other expression.
  if (DiagnoseConditionalForNull(LHS.get(), RHS.get(), QuestionLoc))
    return QualType();

  // Finally, if the LHS and RHS types are canonically the same type, we can
  // use the common sugared type.
  if (Context.hasSameType(LHSTy, RHSTy))
    return Context.getCommonSugaredType(LHSTy, RHSTy);

  // Otherwise, the operands are not compatible.
  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
      << LHSTy << RHSTy << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
  return QualType();
}

namespace {
bool isArithmeticOp(BinaryOperatorKind Opc) {
  return BinaryOperator::isAdditiveOp(Opc) ||
         BinaryOperator::isMultiplicativeOp(Opc) ||
         BinaryOperator::isShiftOp(Opc) || Opc == BO_And || Opc == BO_Or;
  // This only checks for bitwise-or and bitwise-and, but not bitwise-xor and
  // not any of the logical operators.  Bitwise-xor is commonly used as a
  // logical-xor because there is no logical-xor operator.  The logical
  // operators, including uses of xor, have a high false positive rate for
  // precedence warnings.
}
} // namespace

namespace {
bool isArithmeticBinaryExpr(Expr *E, BinaryOperatorKind *Opcode,
                            Expr **RHSExprs) {
  // Don't strip parenthesis: we should not warn if E is in parenthesis.
  E = E->IgnoreImpCasts();

  // Built-in binary operator.
  if (BinaryOperator *OP = dyn_cast<BinaryOperator>(E)) {
    if (isArithmeticOp(OP->getOpcode())) {
      *Opcode = OP->getOpcode();
      *RHSExprs = OP->getRHS();
      return true;
    }
  }

  return false;
}
} // namespace

namespace {
bool exprLooksBoolean(Expr *E) {
  E = E->IgnoreParenImpCasts();

  if (E->getType()->isBooleanType())
    return true;
  if (BinaryOperator *OP = dyn_cast<BinaryOperator>(E))
    return OP->isComparisonOp() || OP->isLogicalOp();
  if (UnaryOperator *OP = dyn_cast<UnaryOperator>(E))
    return OP->getOpcode() == UO_LNot;
  if (E->getType()->isPointerType())
    return true;

  return false;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseConditionalPrecedence(Sema &Self, SourceLocation OpLoc,
                                   Expr *Condition, Expr *LHSExpr,
                                   Expr *RHSExpr) {
  BinaryOperatorKind CondOpcode;
  Expr *CondRHS;

  if (!isArithmeticBinaryExpr(Condition, &CondOpcode, &CondRHS))
    return;
  if (!exprLooksBoolean(CondRHS))
    return;

  // The condition is an arithmetic binary expression, with a right-
  // hand side that looks boolean, so warn.

  unsigned DiagID = BinaryOperator::isBitwiseOp(CondOpcode)
                        ? diag::warn_precedence_bitwise_conditional
                        : diag::warn_precedence_conditional;

  Self.Diag(OpLoc, DiagID) << Condition->getSourceRange()
                           << BinaryOperator::getOpcodeStr(CondOpcode);

  suggestParentheses(
      Self, OpLoc,
      Self.PDiag(diag::note_precedence_silence)
          << BinaryOperator::getOpcodeStr(CondOpcode),
      SourceRange(Condition->getBeginLoc(), Condition->getEndLoc()));

  suggestParentheses(Self, OpLoc,
                     Self.PDiag(diag::note_precedence_conditional_first),
                     SourceRange(CondRHS->getBeginLoc(), RHSExpr->getEndLoc()));
}
} // namespace

namespace {
QualType inferConditionalNullability(QualType ResTy, bool IsBin, QualType LHSTy,
                                     QualType RHSTy, TreeContext &Ctx) {
  if (!ResTy->isAnyPointerType())
    return ResTy;

  auto GetNullability = [](QualType Ty) {
    std::optional<NullabilityKind> Kind = Ty->getNullability();
    if (Kind)
      return *Kind;
    return NullabilityKind::Unspecified;
  };

  auto LHSKind = GetNullability(LHSTy), RHSKind = GetNullability(RHSTy);
  NullabilityKind MergedKind;

  // Compute nullability of a binary conditional expression.
  if (IsBin) {
    if (LHSKind == NullabilityKind::NonNull)
      MergedKind = NullabilityKind::NonNull;
    else
      MergedKind = RHSKind;
    // Compute nullability of a normal conditional expression.
  } else {
    if (LHSKind == NullabilityKind::Nullable ||
        RHSKind == NullabilityKind::Nullable)
      MergedKind = NullabilityKind::Nullable;
    else if (LHSKind == NullabilityKind::NonNull)
      MergedKind = RHSKind;
    else if (RHSKind == NullabilityKind::NonNull)
      MergedKind = LHSKind;
    else
      MergedKind = NullabilityKind::Unspecified;
  }
  if (GetNullability(ResTy) == MergedKind)
    return ResTy;

  // Strip all nullability from ResTy.
  while (ResTy->getNullability())
    ResTy = ResTy.getSingleStepDesugaredType(Ctx);
  auto NewAttr = AttributedType::getNullabilityAttrKind(MergedKind);
  return Ctx.getAttributedType(NewAttr, ResTy, ResTy);
}
} // namespace

ExprResult Sema::OnConditionalOp(SourceLocation QuestionLoc,
                                 SourceLocation ColonLoc, Expr *CondExpr,
                                 Expr *LHSExpr, Expr *RHSExpr) {
  // If this is the gnu "x ?: y" extension, analyze the types as though the LHS
  // was the condition.
  OpaqueValueExpr *opaqueValue = nullptr;
  Expr *commonExpr = nullptr;
  if (!LHSExpr) {
    commonExpr = CondExpr;
    // Lower out placeholder types first so we do not capture a placeholder.
    if (commonExpr->hasPlaceholderType()) {
      ExprResult result = CheckPlaceholderExpr(commonExpr);
      if (!result.isUsable())
        return ExprError();
      commonExpr = result.get();
    }
    {
      ExprResult commonRes = UsualUnaryConversions(commonExpr);
      if (commonRes.isInvalid())
        return ExprError();
      commonExpr = commonRes.get();
    }

    opaqueValue = new (Context) OpaqueValueExpr(
        commonExpr->getExprLoc(), commonExpr->getType(),
        commonExpr->getValueKind(), commonExpr->getObjectKind(), commonExpr);
    LHSExpr = CondExpr = opaqueValue;
  }

  QualType LHSTy = LHSExpr->getType(), RHSTy = RHSExpr->getType();
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  ExprResult Cond = CondExpr, LHS = LHSExpr, RHS = RHSExpr;
  QualType result =
      CheckConditionalOperands(Cond, LHS, RHS, VK, OK, QuestionLoc);
  if (result.isNull() || Cond.isInvalid() || LHS.isInvalid() || RHS.isInvalid())
    return ExprError();

  diagnoseConditionalPrecedence(*this, QuestionLoc, Cond.get(), LHS.get(),
                                RHS.get());

  CheckBoolLikeConversion(Cond.get(), QuestionLoc);

  result =
      inferConditionalNullability(result, commonExpr, LHSTy, RHSTy, Context);

  if (!commonExpr)
    return new (Context)
        ConditionalOperator(Cond.get(), QuestionLoc, LHS.get(), ColonLoc,
                            RHS.get(), result, VK, OK);

  return new (Context) BinaryConditionalOperator(
      commonExpr, opaqueValue, Cond.get(), LHS.get(), RHS.get(), QuestionLoc,
      ColonLoc, result, VK, OK);
}
