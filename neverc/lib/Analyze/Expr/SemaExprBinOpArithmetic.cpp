//===- SemaExprBinOpArithmetic.cpp - Arithmetic/pointer binary-op checks ===//
//
// Extracted from SemaExprBinOp.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Arithmetic & pointer operators
// ===----------------------------------------------------------------------===

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseDivisionSizeofPointerOrArray(Sema &S, Expr *LHS, Expr *RHS,
                                          SourceLocation Loc) {
  const auto *LUE = dyn_cast<UnaryExprOrTypeTraitExpr>(LHS);
  const auto *RUE = dyn_cast<UnaryExprOrTypeTraitExpr>(RHS);
  if (!LUE || !RUE)
    return;
  if (LUE->getKind() != UETT_SizeOf || LUE->isArgumentType() ||
      RUE->getKind() != UETT_SizeOf)
    return;

  const Expr *LHSArg = LUE->getArgumentExpr()->IgnoreParens();
  QualType LHSTy = LHSArg->getType();
  QualType RHSTy;

  if (RUE->isArgumentType())
    RHSTy = RUE->getArgumentType();
  else
    RHSTy = RUE->getArgumentExpr()->IgnoreParens()->getType();

  if (LHSTy->isPointerType() && !RHSTy->isPointerType()) {
    if (!S.Context.hasSameUnqualifiedType(LHSTy->getPointeeType(), RHSTy))
      return;

    S.Diag(Loc, diag::warn_division_sizeof_ptr) << LHS << LHS->getSourceRange();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSArg)) {
      if (const ValueDecl *LHSArgDecl = DRE->getDecl())
        S.Diag(LHSArgDecl->getLocation(), diag::note_pointer_declared_here)
            << LHSArgDecl;
    }
  } else if (const auto *ArrayTy = S.Context.getAsArrayType(LHSTy)) {
    QualType ArrayElemTy = ArrayTy->getElementType();
    if (ArrayElemTy != S.Context.getBaseElementType(ArrayTy) ||
        ArrayElemTy->isCharType() ||
        S.Context.getTypeSize(ArrayElemTy) == S.Context.getTypeSize(RHSTy))
      return;
    S.Diag(Loc, diag::warn_division_sizeof_array)
        << LHSArg->getSourceRange() << ArrayElemTy << RHSTy;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSArg)) {
      if (const ValueDecl *LHSArgDecl = DRE->getDecl())
        S.Diag(LHSArgDecl->getLocation(), diag::note_array_declared_here)
            << LHSArgDecl;
    }

    S.Diag(Loc, diag::note_precedence_silence) << RHS;
  }
}
} // namespace

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool cannotBeIntegerConstant(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const ValueDecl *D = DRE->getDecl();
    if (isa<ParmVarDecl>(D))
      return true;
    if (const auto *VD = dyn_cast<VarDecl>(D))
      return !VD->getType().isConstQualified() && !isa<EnumConstantDecl>(D);
  }
  return false;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBadDivideOrRemainderValues(Sema &S, ExprResult &LHS,
                                        ExprResult &RHS, SourceLocation Loc,
                                        bool IsDiv) {
  if (cannotBeIntegerConstant(RHS.get()))
    return;
  Expr::EvalResult RHSValue;
  if (RHS.get()->EvaluateAsInt(RHSValue, S.Context) &&
      RHSValue.Val.getInt() == 0)
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_remainder_division_by_zero)
                              << IsDiv << RHS.get()->getSourceRange());
}
} // namespace

// ===----------------------------------------------------------------------===
// Arithmetic & pointer operators
// ===----------------------------------------------------------------------===

QualType Sema::CheckMultiplyDivideOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign, bool IsDiv) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();
  if (LLVM_UNLIKELY(LHSTy->isVectorType() || RHSTy->isVectorType()))
    return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                               /*AllowBothBool*/ false,
                               /*AllowBoolConversions*/ false,
                               /*AllowBooleanOperation*/ false,
                               /*ReportInvalid*/ true);
  if (LLVM_UNLIKELY(LHSTy->isSveVLSBuiltinType() ||
                    RHSTy->isSveVLSBuiltinType()))
    return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                       ACK_Arithmetic);
  if (LLVM_UNLIKELY(!IsDiv && (LHSTy->isConstantMatrixType() ||
                               RHSTy->isConstantMatrixType())))
    return CheckMatrixMultiplyOperands(LHS, RHS, Loc, IsCompAssign);
  if (LLVM_UNLIKELY(IsDiv && LHSTy->isConstantMatrixType() &&
                    RHSTy->isArithmeticType()))
    return CheckMatrixElementwiseOperands(LHS, RHS, Loc, IsCompAssign);

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, IsCompAssign ? ACK_CompAssign : ACK_Arithmetic);
  if (LLVM_UNLIKELY(LHS.isInvalid() || RHS.isInvalid()))
    return QualType();

  if (compType.isNull() || !compType->isArithmeticType())
    return InvalidOperands(Loc, LHS, RHS);
  if (IsDiv) {
    diagnoseBadDivideOrRemainderValues(*this, LHS, RHS, Loc, IsDiv);
    diagnoseDivisionSizeofPointerOrArray(*this, LHS.get(), RHS.get(), Loc);
  }
  return compType;
}

QualType Sema::CheckRemainderOperands(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc, bool IsCompAssign) {
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                 /*AllowBothBool*/ false,
                                 /*AllowBoolConversions*/ false,
                                 /*AllowBooleanOperation*/ false,
                                 /*ReportInvalid*/ true);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_Arithmetic);

    return InvalidOperands(Loc, LHS, RHS);
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, IsCompAssign ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  if (compType.isNull() || !compType->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);
  diagnoseBadDivideOrRemainderValues(*this, LHS, RHS, Loc, false /* IsDiv */);
  return compType;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnTwoVoidPtrs(Sema &S, SourceLocation Loc, Expr *LHSExpr,
                                 Expr *RHSExpr) {
  S.Diag(Loc, diag::ext_gnu_void_ptr)
      << 1 /* two pointers */ << LHSExpr->getSourceRange()
      << RHSExpr->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnVoidPtr(Sema &S, SourceLocation Loc, Expr *Pointer) {
  S.Diag(Loc, diag::ext_gnu_void_ptr)
      << 0 /* one pointer */ << Pointer->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnNullPtr(Sema &S, SourceLocation Loc, Expr *Pointer,
                             bool IsGNUIdiom) {
  if (IsGNUIdiom)
    S.Diag(Loc, diag::warn_gnu_null_ptr_arith) << Pointer->getSourceRange();
  else
    S.Diag(Loc, diag::warn_pointer_arith_null_ptr)
        << false << Pointer->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnSubtractionOnNullPtr(Sema &S, SourceLocation Loc, Expr *Pointer,
                              bool BothNull) {
  // Is this a macro from a system header?
  if (S.Diags.getSuppressSystemWarnings() && S.SourceMgr.isInSystemMacro(Loc))
    return;

  S.DiagRuntimeBehavior(Loc, Pointer,
                        S.PDiag(diag::warn_pointer_sub_null_ptr)
                            << false << Pointer->getSourceRange());
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnTwoFnPtrs(Sema &S, SourceLocation Loc, Expr *LHS,
                               Expr *RHS) {
  assert(LHS->getType()->isAnyPointerType());
  assert(RHS->getType()->isAnyPointerType());
  S.Diag(Loc, diag::ext_gnu_ptr_func_arith)
      << 1 /* two pointers */
      << LHS->getType()->getPointeeType()
      // We only show the second type if it differs from the first.
      << (unsigned)!S.Context.hasSameUnqualifiedType(LHS->getType(),
                                                     RHS->getType())
      << RHS->getType()->getPointeeType() << LHS->getSourceRange()
      << RHS->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnFnPtr(Sema &S, SourceLocation Loc, Expr *Pointer) {
  assert(Pointer->getType()->isAnyPointerType());
  S.Diag(Loc, diag::ext_gnu_ptr_func_arith)
      << 0 /* one pointer */ << Pointer->getType()->getPointeeType()
      << 0 /* one pointer, so only one type */
      << Pointer->getSourceRange();
}
} // namespace

namespace {
bool verifyArithmeticPointerComplete(Sema &S, SourceLocation Loc,
                                     Expr *Operand) {
  QualType ResType = Operand->getType();
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  assert(ResType->isAnyPointerType());
  QualType PointeeTy = ResType->getPointeeType();
  return S.RequireCompleteSizedType(
      Loc, PointeeTy,
      diag::err_typecheck_arithmetic_incomplete_or_sizeless_type,
      Operand->getSourceRange());
}
} // namespace

namespace neverc {
bool verifyArithmeticPointerOp(Sema &S, SourceLocation Loc, Expr *Operand) {
  QualType ResType = Operand->getType();
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  if (!ResType->isAnyPointerType())
    return true;

  QualType PointeeTy = ResType->getPointeeType();
  if (PointeeTy->isVoidType()) {
    warnArithmeticOnVoidPtr(S, Loc, Operand);
    return true;
  }
  if (PointeeTy->isFunctionType()) {
    warnArithmeticOnFnPtr(S, Loc, Operand);
    return true;
  }

  if (verifyArithmeticPointerComplete(S, Loc, Operand))
    return false;

  return true;
}
} // namespace

namespace {
bool verifyArithmeticBinPtrOps(Sema &S, SourceLocation Loc, Expr *LHSExpr,
                               Expr *RHSExpr) {
  bool isLHSPointer = LHSExpr->getType()->isAnyPointerType();
  bool isRHSPointer = RHSExpr->getType()->isAnyPointerType();
  if (!isLHSPointer && !isRHSPointer)
    return true;

  QualType LHSPointeeTy, RHSPointeeTy;
  if (isLHSPointer)
    LHSPointeeTy = LHSExpr->getType()->getPointeeType();
  if (isRHSPointer)
    RHSPointeeTy = RHSExpr->getType()->getPointeeType();

  // if both are pointers check if operation is valid wrt address spaces
  if (isLHSPointer && isRHSPointer) {
    if (!LHSPointeeTy.isAddressSpaceOverlapping(RHSPointeeTy)) {
      S.Diag(Loc,
             diag::err_typecheck_op_on_nonoverlapping_address_space_pointers)
          << LHSExpr->getType() << RHSExpr->getType() << 1 /*arithmetic op*/
          << LHSExpr->getSourceRange() << RHSExpr->getSourceRange();
      return false;
    }
  }

  // Check for arithmetic on pointers to incomplete types.
  bool isLHSVoidPtr = isLHSPointer && LHSPointeeTy->isVoidType();
  bool isRHSVoidPtr = isRHSPointer && RHSPointeeTy->isVoidType();
  if (isLHSVoidPtr || isRHSVoidPtr) {
    if (!isRHSVoidPtr)
      warnArithmeticOnVoidPtr(S, Loc, LHSExpr);
    else if (!isLHSVoidPtr)
      warnArithmeticOnVoidPtr(S, Loc, RHSExpr);
    else
      warnArithmeticOnTwoVoidPtrs(S, Loc, LHSExpr, RHSExpr);

    return true;
  }

  bool isLHSFuncPtr = isLHSPointer && LHSPointeeTy->isFunctionType();
  bool isRHSFuncPtr = isRHSPointer && RHSPointeeTy->isFunctionType();
  if (isLHSFuncPtr || isRHSFuncPtr) {
    if (!isRHSFuncPtr)
      warnArithmeticOnFnPtr(S, Loc, LHSExpr);
    else if (!isLHSFuncPtr)
      warnArithmeticOnFnPtr(S, Loc, RHSExpr);
    else
      warnArithmeticOnTwoFnPtrs(S, Loc, LHSExpr, RHSExpr);

    return true;
  }

  if (isLHSPointer && verifyArithmeticPointerComplete(S, Loc, LHSExpr))
    return false;
  if (isRHSPointer && verifyArithmeticPointerComplete(S, Loc, RHSExpr))
    return false;

  return true;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnStringPlusInt(Sema &Self, SourceLocation OpLoc, Expr *LHSExpr,
                       Expr *RHSExpr) {
  StringLiteral *StrExpr = dyn_cast<StringLiteral>(LHSExpr->IgnoreImpCasts());
  Expr *IndexExpr = RHSExpr;
  if (!StrExpr) {
    StrExpr = dyn_cast<StringLiteral>(RHSExpr->IgnoreImpCasts());
    IndexExpr = LHSExpr;
  }

  bool IsStringPlusInt =
      StrExpr && IndexExpr->getType()->isIntegralOrUnscopedEnumerationType();
  if (!IsStringPlusInt)
    return;

  SourceRange DiagRange(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());
  Self.Diag(OpLoc, diag::warn_string_plus_int)
      << DiagRange << IndexExpr->IgnoreImpCasts()->getType();

  // Only print a fixit for "str" + int, not for int + "str".
  if (IndexExpr == RHSExpr) {
    SourceLocation EndLoc = Self.getLocForEndOfToken(RHSExpr->getEndLoc());
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence)
        << FixItHint::CreateInsertion(LHSExpr->getBeginLoc(), "&")
        << FixItHint::CreateReplacement(SourceRange(OpLoc), "[")
        << FixItHint::CreateInsertion(EndLoc, "]");
  } else
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence);
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnStringPlusChar(Sema &Self, SourceLocation OpLoc, Expr *LHSExpr,
                        Expr *RHSExpr) {
  const Expr *StringRefExpr = LHSExpr;
  const CharacterLiteral *CharExpr =
      dyn_cast<CharacterLiteral>(RHSExpr->IgnoreImpCasts());

  if (!CharExpr) {
    CharExpr = dyn_cast<CharacterLiteral>(LHSExpr->IgnoreImpCasts());
    StringRefExpr = RHSExpr;
  }

  if (!CharExpr || !StringRefExpr)
    return;

  const QualType StringType = StringRefExpr->getType();
  if (!StringType->isAnyPointerType())
    return;

  if (!StringType->getPointeeType()->isAnyCharacterType())
    return;

  TreeContext &Ctx = Self.getTreeContext();
  SourceRange DiagRange(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());

  const QualType CharType = CharExpr->getType();
  if (!CharType->isAnyCharacterType() && CharType->isIntegerType() &&
      llvm::isUIntN(Ctx.getCharWidth(), CharExpr->getValue())) {
    Self.Diag(OpLoc, diag::warn_string_plus_char) << DiagRange << Ctx.CharTy;
  } else {
    Self.Diag(OpLoc, diag::warn_string_plus_char)
        << DiagRange << CharExpr->getType();
  }

  // Only print a fixit for str + char, not for char + str.
  if (isa<CharacterLiteral>(RHSExpr->IgnoreImpCasts())) {
    SourceLocation EndLoc = Self.getLocForEndOfToken(RHSExpr->getEndLoc());
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence)
        << FixItHint::CreateInsertion(LHSExpr->getBeginLoc(), "&")
        << FixItHint::CreateReplacement(SourceRange(OpLoc), "[")
        << FixItHint::CreateInsertion(EndLoc, "]");
  } else {
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence);
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnPointerIncompatibility(Sema &S, SourceLocation Loc, Expr *LHSExpr,
                                Expr *RHSExpr) {
  assert(LHSExpr->getType()->isAnyPointerType());
  assert(RHSExpr->getType()->isAnyPointerType());
  S.Diag(Loc, diag::err_typecheck_sub_ptr_compatible)
      << LHSExpr->getType() << RHSExpr->getType() << LHSExpr->getSourceRange()
      << RHSExpr->getSourceRange();
}
} // namespace

// C99 6.5.6
QualType Sema::CheckAdditionOperands(ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc, BinaryOperatorKind Opc,
                                     QualType *CompLHSTy) {
  QualType LTy = LHS.get()->getType(), RTy = RHS.get()->getType();

  if (LLVM_UNLIKELY(LTy->isVectorType() || RTy->isVectorType())) {
    QualType compType = CheckVectorOperands(LHS, RHS, Loc, CompLHSTy,
                                            /*AllowBothBool*/ false,
                                            /*AllowBoolConversions*/ false,
                                            /*AllowBooleanOperation*/ false,
                                            /*ReportInvalid*/ true);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isSveVLSBuiltinType() || RTy->isSveVLSBuiltinType())) {
    QualType compType =
        CheckSizelessVectorOperands(LHS, RHS, Loc, CompLHSTy, ACK_Arithmetic);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isConstantMatrixType() ||
                    RTy->isConstantMatrixType())) {
    QualType compType =
        CheckMatrixElementwiseOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, CompLHSTy ? ACK_CompAssign : ACK_Arithmetic);
  if (LLVM_UNLIKELY(LHS.isInvalid() || RHS.isInvalid()))
    return QualType();

  if (LLVM_LIKELY(!compType.isNull() && compType->isArithmeticType())) {
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (Opc == BO_Add) {
    warnStringPlusInt(*this, Loc, LHS.get(), RHS.get());
    warnStringPlusChar(*this, Loc, LHS.get(), RHS.get());
  }

  // Type-checking.  Ultimately the pointer's going to be in PExp;
  // note that we bias towards the LHS being the pointer.
  Expr *PExp = LHS.get(), *IExp = RHS.get();
  if (PExp->getType()->isPointerType()) {
  } else {
    std::swap(PExp, IExp);
    if (!PExp->getType()->isPointerType()) {
      return InvalidOperands(Loc, LHS, RHS);
    }
  }
  assert(PExp->getType()->isPointerType());

  if (!IExp->getType()->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);

  // Adding to a null pointer results in undefined behavior.
  if (PExp->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull)) {
    bool IsGNUIdiom = BinaryOperator::isNullPointerArithmeticExtension(
        Context, BO_Add, PExp, IExp);
    warnArithmeticOnNullPtr(*this, Loc, PExp, IsGNUIdiom);
  }

  if (!verifyArithmeticPointerOp(*this, Loc, PExp))
    return QualType();
  CheckArrayAccess(PExp, IExp);

  if (CompLHSTy) {
    QualType LHSTy = Context.isPromotableBitField(LHS.get());
    if (LHSTy.isNull()) {
      LHSTy = LHS.get()->getType();
      if (Context.isPromotableIntegerType(LHSTy))
        LHSTy = Context.getPromotedIntegerType(LHSTy);
    }
    *CompLHSTy = LHSTy;
  }

  return PExp->getType();
}

// C99 6.5.6
QualType Sema::CheckSubtractionOperands(ExprResult &LHS, ExprResult &RHS,
                                        SourceLocation Loc,
                                        QualType *CompLHSTy) {
  QualType LTy = LHS.get()->getType(), RTy = RHS.get()->getType();

  if (LLVM_UNLIKELY(LTy->isVectorType() || RTy->isVectorType())) {
    QualType compType = CheckVectorOperands(LHS, RHS, Loc, CompLHSTy,
                                            /*AllowBothBool*/ false,
                                            /*AllowBoolConversions*/ false,
                                            /*AllowBooleanOperation*/ false,
                                            /*ReportInvalid*/ true);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isSveVLSBuiltinType() || RTy->isSveVLSBuiltinType())) {
    QualType compType =
        CheckSizelessVectorOperands(LHS, RHS, Loc, CompLHSTy, ACK_Arithmetic);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isConstantMatrixType() ||
                    RTy->isConstantMatrixType())) {
    QualType compType =
        CheckMatrixElementwiseOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, CompLHSTy ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Enforce type constraints: C99 6.5.6p3.

  // Handle the common case first (both operands are arithmetic).
  if (!compType.isNull() && compType->isArithmeticType()) {
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  // Either ptr - int   or   ptr - ptr.
  if (LHS.get()->getType()->isAnyPointerType()) {
    QualType lpointee = LHS.get()->getType()->getPointeeType();

    // The result type of a pointer-int computation is the pointer type.
    if (RHS.get()->getType()->isIntegerType()) {
      // Subtracting from a null pointer should produce a warning.
      // The last argument to the diagnose call says this doesn't match the
      // GNU int-to-pointer idiom.
      if (LHS.get()->IgnoreParenCasts()->isNullPointerConstant(
              Context, Expr::NPC_ValueDependentIsNotNull)) {
        warnArithmeticOnNullPtr(*this, Loc, LHS.get(), false);
      }

      if (!verifyArithmeticPointerOp(*this, Loc, LHS.get()))
        return QualType();
      CheckArrayAccess(LHS.get(), RHS.get(), /*ArraySubscriptExpr*/ nullptr,
                       /*AllowOnePastEnd*/ true, /*IndexNegated*/ true);

      if (CompLHSTy)
        *CompLHSTy = LHS.get()->getType();
      return LHS.get()->getType();
    }

    // Handle pointer-pointer subtractions.
    if (const PointerType *RHSPTy =
            RHS.get()->getType()->getAs<PointerType>()) {
      QualType rpointee = RHSPTy->getPointeeType();

      if (!Context.typesAreCompatible(
              Context.getCanonicalType(lpointee).getUnqualifiedType(),
              Context.getCanonicalType(rpointee).getUnqualifiedType())) {
        warnPointerIncompatibility(*this, Loc, LHS.get(), RHS.get());
        return QualType();
      }

      if (!verifyArithmeticBinPtrOps(*this, Loc, LHS.get(), RHS.get()))
        return QualType();

      bool LHSIsNullPtr = LHS.get()->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull);
      bool RHSIsNullPtr = RHS.get()->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull);

      // Subtracting nullptr or from nullptr is suspect
      if (LHSIsNullPtr)
        warnSubtractionOnNullPtr(*this, Loc, LHS.get(), RHSIsNullPtr);
      if (RHSIsNullPtr)
        warnSubtractionOnNullPtr(*this, Loc, RHS.get(), LHSIsNullPtr);

      // The pointee type may have zero size.  As an extension, a structure or
      // union may have zero size or an array may have zero length.  In this
      // case subtraction does not make sense.
      if (!rpointee->isVoidType() && !rpointee->isFunctionType()) {
        CharUnits ElementSize = Context.getTypeSizeInChars(rpointee);
        if (ElementSize.isZero()) {
          Diag(Loc, diag::warn_sub_ptr_zero_size_types)
              << rpointee.getUnqualifiedType() << LHS.get()->getSourceRange()
              << RHS.get()->getSourceRange();
        }
      }

      if (CompLHSTy)
        *CompLHSTy = LHS.get()->getType();
      return Context.getPointerDiffType();
    }
  }

  return InvalidOperands(Loc, LHS, RHS);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBadShiftValues(Sema &S, ExprResult &LHS, ExprResult &RHS,
                            SourceLocation Loc, BinaryOperatorKind Opc,
                            QualType LHSType) {
  if (cannotBeIntegerConstant(RHS.get()))
    return;
  Expr::EvalResult RHSResult;
  if (!RHS.get()->EvaluateAsInt(RHSResult, S.Context))
    return;
  llvm::APSInt Right = RHSResult.Val.getInt();

  if (Right.isNegative()) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_negative)
                              << RHS.get()->getSourceRange());
    return;
  }

  QualType LHSExprType = LHS.get()->getType();
  uint64_t LeftSize = S.Context.getTypeSize(LHSExprType);
  if (LHSExprType->isBitIntType())
    LeftSize = S.Context.getIntWidth(LHSExprType);
  else if (LHSExprType->isFixedPointType()) {
    auto FXSema = S.Context.getFixedPointSemantics(LHSExprType);
    LeftSize = FXSema.getWidth() - (unsigned)FXSema.hasUnsignedPadding();
  }
  if (Right.uge(LeftSize)) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_gt_typewidth)
                              << RHS.get()->getSourceRange());
    return;
  }

  if (Opc != BO_Shl || LHSExprType->isFixedPointType())
    return;

  // Signed left-shift overflow: undefined in older C; unsigned shifts wrap.
  Expr::EvalResult LHSResult;
  if (LHSType->hasUnsignedIntegerRepresentation() ||
      !LHS.get()->EvaluateAsInt(LHSResult, S.Context))
    return;
  llvm::APSInt Left = LHSResult.Val.getInt();

  // Skip when signed overflow is defined (GCC/NeverC) or when signed integers
  // use two's-complement (C23).
  if (S.getLangOpts().isSignedOverflowDefined())
    return;

  // Negative LHS is undefined for signed left shift in C.
  if (Left.isNegative()) {
    S.DiagRuntimeBehavior(Loc, LHS.get(),
                          S.PDiag(diag::warn_shift_lhs_negative)
                              << LHS.get()->getSourceRange());
    return;
  }

  llvm::APInt ResultBits =
      static_cast<llvm::APInt &>(Right) + Left.getSignificantBits();
  if (ResultBits.ule(LeftSize))
    return;
  llvm::APSInt Result = Left.extend(ResultBits.getLimitedValue());
  Result = Result.shl(Right);

  // Print the bit representation of the signed integer as an unsigned
  // hexadecimal number.
  llvm::SmallString<40> HexResult;
  Result.toString(HexResult, 16, /*Signed =*/false, /*Literal =*/true);

  // If we are only missing a sign bit, this is less likely to result in actual
  // bugs -- if the result is cast back to an unsigned type, it will have the
  // expected value. Thus we place this behind a different warning that can be
  // turned off separately if needed.
  if (ResultBits - 1 == LeftSize) {
    S.Diag(Loc, diag::warn_shift_result_sets_sign_bit)
        << HexResult << LHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return;
  }

  S.Diag(Loc, diag::warn_shift_result_gt_typewidth)
      << HexResult.str() << Result.getSignificantBits() << LHSType
      << Left.getBitWidth() << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
}
} // namespace

namespace {
QualType validateVectorShift(Sema &S, ExprResult &LHS, ExprResult &RHS,
                             SourceLocation Loc, bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = S.UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = S.UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType();
  // Note that LHS might be a scalar because the routine calls not only in
  const VectorType *LHSVecTy = LHSType->getAs<VectorType>();
  QualType LHSEleType = LHSVecTy ? LHSVecTy->getElementType() : LHSType;

  // Note that RHS might not be a vector.
  QualType RHSType = RHS.get()->getType();
  const VectorType *RHSVecTy = RHSType->getAs<VectorType>();
  QualType RHSEleType = RHSVecTy ? RHSVecTy->getElementType() : RHSType;

  // Do not allow shifts for boolean vectors.
  if ((LHSVecTy && LHSVecTy->isExtVectorBoolType()) ||
      (RHSVecTy && RHSVecTy->isExtVectorBoolType())) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHS.get()->getType() << RHS.get()->getType()
        << LHS.get()->getSourceRange();
    return QualType();
  }

  // The operands need to be integers.
  if (!LHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << LHS.get()->getType() << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << RHS.get()->getType() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSVecTy) {
    assert(RHSVecTy);
    if (IsCompAssign)
      return RHSType;
    if (LHSEleType != RHSEleType) {
      LHS = S.ImpCastExprToType(LHS.get(), RHSEleType, CK_IntegralCast);
      LHSEleType = RHSEleType;
    }
    QualType VecTy =
        S.Context.getExtVectorType(LHSEleType, RHSVecTy->getNumElements());
    LHS = S.ImpCastExprToType(LHS.get(), VecTy, CK_VectorSplat);
    LHSType = VecTy;
  } else if (RHSVecTy) {
    // For vector types, operators are applied component-wise. Ensure
    // the number of elements is the same as LHS.
    if (RHSVecTy->getNumElements() != LHSVecTy->getNumElements()) {
      S.Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
          << LHS.get()->getType() << RHS.get()->getType()
          << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      return QualType();
    }
    const BuiltinType *LHSBT = LHSEleType->getAs<neverc::BuiltinType>();
    const BuiltinType *RHSBT = RHSEleType->getAs<neverc::BuiltinType>();
    if (LHSBT != RHSBT &&
        S.Context.getTypeSize(LHSBT) != S.Context.getTypeSize(RHSBT)) {
      S.Diag(Loc, diag::warn_typecheck_vector_element_sizes_not_equal)
          << LHS.get()->getType() << RHS.get()->getType()
          << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    }
  } else {
    // ...else expand RHS to match the number of elements in LHS.
    QualType VecTy =
        S.Context.getExtVectorType(RHSEleType, LHSVecTy->getNumElements());
    RHS = S.ImpCastExprToType(RHS.get(), VecTy, CK_VectorSplat);
  }

  return LHSType;
}
} // namespace

namespace {
QualType validateSizelessVectorShift(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc, bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = S.UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = S.UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType();
  const BuiltinType *LHSBuiltinTy = LHSType->castAs<BuiltinType>();
  QualType LHSEleType = LHSType->isSveVLSBuiltinType()
                            ? LHSBuiltinTy->getSveEltType(S.getTreeContext())
                            : LHSType;

  // Note that RHS might not be a vector
  QualType RHSType = RHS.get()->getType();
  const BuiltinType *RHSBuiltinTy = RHSType->castAs<BuiltinType>();
  QualType RHSEleType = RHSType->isSveVLSBuiltinType()
                            ? RHSBuiltinTy->getSveEltType(S.getTreeContext())
                            : RHSType;

  if ((LHSBuiltinTy && LHSBuiltinTy->isSVEBool()) ||
      (RHSBuiltinTy && RHSBuiltinTy->isSVEBool())) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHSType << RHSType << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << LHS.get()->getType() << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << RHS.get()->getType() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType() &&
      (S.Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC !=
       S.Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC)) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSType->isSveVLSBuiltinType()) {
    assert(RHSType->isSveVLSBuiltinType());
    if (IsCompAssign)
      return RHSType;
    if (LHSEleType != RHSEleType) {
      LHS = S.ImpCastExprToType(LHS.get(), RHSEleType, neverc::CK_IntegralCast);
      LHSEleType = RHSEleType;
    }
    const llvm::ElementCount VecSize =
        S.Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC;
    QualType VecTy =
        S.Context.getScalableVectorType(LHSEleType, VecSize.getKnownMinValue());
    LHS = S.ImpCastExprToType(LHS.get(), VecTy, neverc::CK_VectorSplat);
    LHSType = VecTy;
  } else if (RHSBuiltinTy && RHSBuiltinTy->isSveVLSBuiltinType()) {
    if (S.Context.getTypeSize(RHSBuiltinTy) !=
        S.Context.getTypeSize(LHSBuiltinTy)) {
      S.Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
          << LHSType << RHSType << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
      return QualType();
    }
  } else {
    const llvm::ElementCount VecSize =
        S.Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC;
    if (LHSEleType != RHSEleType) {
      RHS = S.ImpCastExprToType(RHS.get(), LHSEleType, neverc::CK_IntegralCast);
      RHSEleType = LHSEleType;
    }
    QualType VecTy =
        S.Context.getScalableVectorType(RHSEleType, VecSize.getKnownMinValue());
    RHS = S.ImpCastExprToType(RHS.get(), VecTy, CK_VectorSplat);
  }

  return LHSType;
}
} // namespace

// C99 6.5.7
QualType Sema::CheckShiftOperands(ExprResult &LHS, ExprResult &RHS,
                                  SourceLocation Loc, BinaryOperatorKind Opc,
                                  bool IsCompAssign) {
  // Vector shifts promote their scalar inputs to vector type.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    return validateVectorShift(*this, LHS, RHS, Loc, IsCompAssign);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType())
    return validateSizelessVectorShift(*this, LHS, RHS, Loc, IsCompAssign);

  // Shifts don't perform usual arithmetic conversions, they just do integer
  // promotions on each operand. C99 6.5.7p3

  // For the LHS, do usual unary conversions, but then reset them away
  // if this is a compound assignment.
  ExprResult OldLHS = LHS;
  LHS = UsualUnaryConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();
  QualType LHSType = LHS.get()->getType();
  if (IsCompAssign)
    LHS = OldLHS;

  // The RHS is simpler.
  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();
  QualType RHSType = RHS.get()->getType();

  // C99 6.5.7p2: Each of the operands shall have integer type.
  // Embedded-C 4.1.6.2.2: The LHS may also be fixed-point.
  if ((!LHSType->isFixedPointOrIntegerType() &&
       !LHSType->hasIntegerRepresentation()) ||
      !RHSType->hasIntegerRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  diagnoseBadShiftValues(*this, LHS, RHS, Loc, Opc, LHSType);

  // "The type of the result is that of the promoted left operand."
  return LHSType;
}
