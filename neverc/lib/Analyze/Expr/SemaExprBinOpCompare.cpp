//===- SemaExprBinOpCompare.cpp - Comparison binary-op checking ---------===//
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
// Comparison operators
// ===----------------------------------------------------------------------===

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnDistinctPointerCmp(Sema &S, SourceLocation Loc, ExprResult &LHS,
                            ExprResult &RHS, bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_distinct_pointers
                      : diag::ext_typecheck_comparison_of_distinct_pointers)
      << LHS.get()->getType() << RHS.get()->getType()
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnFnPtrToVoidCmp(Sema &S, SourceLocation Loc, ExprResult &LHS,
                        ExprResult &RHS, bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_fptr_to_void
                      : diag::ext_typecheck_comparison_of_fptr_to_void)
      << LHS.get()->getType() << RHS.get()->getType()
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}
} // namespace

namespace neverc {
LLVM_ATTRIBUTE_NOINLINE
void warnLogicalNotOnLHS(Sema &S, ExprResult &LHS, ExprResult &RHS,
                         SourceLocation Loc, BinaryOperatorKind Opc) {
  UnaryOperator *UO = dyn_cast<UnaryOperator>(LHS.get()->IgnoreImpCasts());
  if (!UO || UO->getOpcode() != UO_LNot)
    return;

  // Only check if the right hand side is non-bool arithmetic type.
  if (RHS.get()->isKnownToHaveBooleanValue())
    return;

  // Make sure that the something in !something is not bool.
  Expr *SubExpr = UO->getSubExpr()->IgnoreImpCasts();
  if (SubExpr->isKnownToHaveBooleanValue())
    return;
  bool IsBitwiseOp = Opc == BO_And || Opc == BO_Or || Opc == BO_Xor;
  S.Diag(UO->getOperatorLoc(), diag::warn_logical_not_on_lhs_of_check)
      << Loc << IsBitwiseOp;

  // First note suggest !(x < y)
  SourceLocation FirstOpen = SubExpr->getBeginLoc();
  SourceLocation FirstClose = RHS.get()->getEndLoc();
  FirstClose = S.getLocForEndOfToken(FirstClose);
  if (FirstClose.isInvalid())
    FirstOpen = SourceLocation();
  S.Diag(UO->getOperatorLoc(), diag::note_logical_not_fix)
      << IsBitwiseOp << FixItHint::CreateInsertion(FirstOpen, "(")
      << FixItHint::CreateInsertion(FirstClose, ")");

  // Second note suggests (!x) < y
  SourceLocation SecondOpen = LHS.get()->getBeginLoc();
  SourceLocation SecondClose = LHS.get()->getEndLoc();
  SecondClose = S.getLocForEndOfToken(SecondClose);
  if (SecondClose.isInvalid())
    SecondOpen = SourceLocation();
  S.Diag(UO->getOperatorLoc(), diag::note_logical_not_silence_with_parens)
      << FixItHint::CreateInsertion(SecondOpen, "(")
      << FixItHint::CreateInsertion(SecondClose, ")");
}
} // namespace neverc

// Returns true if E refers to a non-weak array.
namespace {
bool checkForArray(const Expr *E) {
  const ValueDecl *D = nullptr;
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E)) {
    D = DR->getDecl();
  }
  if (!D)
    return false;
  return D->getType()->isArrayType() && !D->isWeak();
}
} // namespace

namespace neverc {
LLVM_ATTRIBUTE_NOINLINE
void warnTautologicalCmp(Sema &S, SourceLocation Loc, Expr *LHS, Expr *RHS,
                         BinaryOperatorKind Opc) {
  Expr *LHSStripped = LHS->IgnoreParenImpCasts();
  Expr *RHSStripped = RHS->IgnoreParenImpCasts();

  QualType LHSType = LHS->getType();
  if (LHSType->hasFloatingRepresentation())
    return;

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  //
  // NOTE: Don't warn about comparison expressions resulting from macro
  // expansion. The idea is to warn when the comparison operator will always
  // evaluate to the same result.

  // Used for indexing into %select in warn_comparison_always
  enum {
    AlwaysConstant,
    AlwaysTrue,
    AlwaysFalse,
  };

  if (!LHS->getBeginLoc().isMacroID() && !RHS->getBeginLoc().isMacroID()) {
    if (Expr::isSameComparisonOperand(LHS, RHS)) {
      unsigned Result;
      switch (Opc) {
      case BO_EQ:
      case BO_LE:
      case BO_GE:
        Result = AlwaysTrue;
        break;
      case BO_NE:
      case BO_LT:
      case BO_GT:
        Result = AlwaysFalse;
        break;
      default:
        Result = AlwaysConstant;
        break;
      }
      S.DiagRuntimeBehavior(Loc, nullptr,
                            S.PDiag(diag::warn_comparison_always)
                                << 0 /*self-comparison*/
                                << Result);
    } else if (checkForArray(LHSStripped) && checkForArray(RHSStripped)) {
      // What is it always going to evaluate to?
      unsigned Result;
      switch (Opc) {
      case BO_EQ: // e.g. array1 == array2
        Result = AlwaysFalse;
        break;
      case BO_NE: // e.g. array1 != array2
        Result = AlwaysTrue;
        break;
      default: // e.g. array1 <= array2
        // The best we can say is 'a constant'
        Result = AlwaysConstant;
        break;
      }
      S.DiagRuntimeBehavior(Loc, nullptr,
                            S.PDiag(diag::warn_comparison_always)
                                << 1 /*array comparison*/
                                << Result);
    }
  }

  if (isa<CastExpr>(LHSStripped))
    LHSStripped = LHSStripped->IgnoreParenCasts();
  if (isa<CastExpr>(RHSStripped))
    RHSStripped = RHSStripped->IgnoreParenCasts();

  // Warn about comparisons against a string constant (unless the other
  // operand is null); the user probably wants string comparison function.
  Expr *LiteralString = nullptr;
  if (isa<StringLiteral>(LHSStripped) &&
      !RHSStripped->isNullPointerConstant(S.Context,
                                          Expr::NPC_ValueDependentIsNull)) {
    LiteralString = LHS;
  } else if (isa<StringLiteral>(RHSStripped) &&
             !LHSStripped->isNullPointerConstant(
                 S.Context, Expr::NPC_ValueDependentIsNull)) {
    LiteralString = RHS;
  }

  if (LiteralString) {
    S.DiagRuntimeBehavior(Loc, nullptr,
                          S.PDiag(diag::warn_stringcompare)
                              << LiteralString->getSourceRange());
  }
}
} // namespace neverc

namespace {
QualType validateArithmeticOrEnumCmp(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc,
                                     BinaryOperatorKind Opc) {
  // C99 6.5.8p3 / C99 6.5.9p4
  QualType Type =
      S.UsualArithmeticConversions(LHS, RHS, Loc, Sema::ACK_Comparison);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (Type.isNull())
    return S.InvalidOperands(Loc, LHS, RHS);
  assert(Type->isArithmeticType() || Type->isEnumeralType());

  if (Type->isAnyComplexType() && BinaryOperator::isRelationalOp(Opc))
    return S.InvalidOperands(Loc, LHS, RHS);
  if (Type->hasFloatingRepresentation())
    S.CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);

  // Result type is the language's "logical" type (typically `int` in C).
  return S.Context.getLogicalOperationType();
}
} // namespace

void Sema::CheckPtrComparisonWithNullChar(ExprResult &E, ExprResult &NullE) {
  if (!NullE.get()->getType()->isAnyPointerType())
    return;
  int NullValue = PP.isMacroDefined("NULL") ? 0 : 1;
  if (!E.get()->getType()->isAnyPointerType() &&
      E.get()->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull) ==
          Expr::NPCK_ZeroExpression) {
    if (const auto *CL = dyn_cast<CharacterLiteral>(E.get())) {
      if (CL->getValue() == 0)
        Diag(E.get()->getExprLoc(), diag::warn_pointer_compare)
            << NullValue
            << FixItHint::CreateReplacement(E.get()->getExprLoc(),
                                            NullValue ? "NULL" : "(void *)0");
    } else if (const auto *CE = dyn_cast<CStyleCastExpr>(E.get())) {
      TypeSourceInfo *TI = CE->getTypeInfoAsWritten();
      QualType T = Context.getCanonicalType(TI->getType()).getUnqualifiedType();
      if (T == Context.CharTy)
        Diag(E.get()->getExprLoc(), diag::warn_pointer_compare)
            << NullValue
            << FixItHint::CreateReplacement(E.get()->getExprLoc(),
                                            NullValue ? "NULL" : "(void *)0");
    }
  }
}

// Relational / equality operands (C99 6.5.8, 6.5.9).
// ===----------------------------------------------------------------------===
// Comparison operators
// ===----------------------------------------------------------------------===

QualType Sema::CheckCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                    SourceLocation Loc,
                                    BinaryOperatorKind Opc) {
  bool IsRelational = BinaryOperator::isRelationalOp(Opc);
  bool IsOrdered = IsRelational;

  LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
  if (LLVM_UNLIKELY(LHS.isInvalid()))
    return QualType();
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (LLVM_UNLIKELY(RHS.isInvalid()))
    return QualType();

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();

  if (LLVM_LIKELY((LHSType->isArithmeticType() || LHSType->isEnumeralType()) &&
                  (RHSType->isArithmeticType() || RHSType->isEnumeralType()))) {
    warnLogicalNotOnLHS(*this, LHS, RHS, Loc, Opc);
    warnTautologicalCmp(*this, Loc, LHS.get(), RHS.get(), Opc);
    return validateArithmeticOrEnumCmp(*this, LHS, RHS, Loc, Opc);
  }

  if (LLVM_UNLIKELY(BinaryOperator::isEqualityOp(Opc))) {
    CheckPtrComparisonWithNullChar(LHS, RHS);
    CheckPtrComparisonWithNullChar(RHS, LHS);
  }

  if (LLVM_UNLIKELY(LHSType->isVectorType() || RHSType->isVectorType()))
    return CheckVectorCompareOperands(LHS, RHS, Loc, Opc);

  if (LLVM_UNLIKELY(LHSType->isSveVLSBuiltinType() ||
                    RHSType->isSveVLSBuiltinType()))
    return CheckSizelessVectorCompareOperands(LHS, RHS, Loc, Opc);

  warnLogicalNotOnLHS(*this, LHS, RHS, Loc, Opc);
  warnTautologicalCmp(*this, Loc, LHS.get(), RHS.get(), Opc);

  const Expr::NullPointerConstantKind LHSNullKind =
      LHS.get()->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
  const Expr::NullPointerConstantKind RHSNullKind =
      RHS.get()->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
  bool LHSIsNull = LHSNullKind != Expr::NPCK_NotNull;
  bool RHSIsNull = RHSNullKind != Expr::NPCK_NotNull;

  auto computeResultTy = [&]() { return Context.getLogicalOperationType(); };

  if (!IsOrdered && LHSIsNull != RHSIsNull) {
    bool IsEquality = Opc == BO_EQ;
    if (RHSIsNull)
      DiagnoseAlwaysNonNullPointer(LHS.get(), RHSNullKind, IsEquality,
                                   RHS.get()->getSourceRange());
    else
      DiagnoseAlwaysNonNullPointer(RHS.get(), LHSNullKind, IsEquality,
                                   LHS.get()->getSourceRange());
  }

  if (IsOrdered && LHSType->isFunctionPointerType() &&
      RHSType->isFunctionPointerType()) {
    Diag(Loc, diag::ext_typecheck_ordered_comparison_of_function_pointers)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
  }

  if ((LHSType->isIntegerType() && !LHSIsNull) ||
      (RHSType->isIntegerType() && !RHSIsNull)) {
    // Skip normal pointer conversion checks in this case; we have better
    // diagnostics for this below.
  } else if (LHSType->isPointerType() &&
             RHSType->isPointerType()) { // C99 6.5.8p2
    // All of the following pointer-related warnings are GCC extensions, except
    // when handling null pointer constants.
    QualType LCanPointeeTy =
        LHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();
    QualType RCanPointeeTy =
        RHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();

    // C99 6.5.9p2 and C99 6.5.8p2
    if (Context.typesAreCompatible(LCanPointeeTy.getUnqualifiedType(),
                                   RCanPointeeTy.getUnqualifiedType())) {
      if (IsRelational) {
        // Pointers both need to point to complete or incomplete types
        if ((LCanPointeeTy->isIncompleteType() !=
             RCanPointeeTy->isIncompleteType()) &&
            !getLangOpts().C11) {
          Diag(Loc, diag::ext_typecheck_compare_complete_incomplete_pointers)
              << LHS.get()->getSourceRange() << RHS.get()->getSourceRange()
              << LHSType << RHSType << LCanPointeeTy->isIncompleteType()
              << RCanPointeeTy->isIncompleteType();
        }
      }
    } else if (!IsRelational &&
               (LCanPointeeTy->isVoidType() || RCanPointeeTy->isVoidType())) {
      // Valid unless comparison between non-null pointer and function pointer
      if ((LCanPointeeTy->isFunctionType() ||
           RCanPointeeTy->isFunctionType()) &&
          !LHSIsNull && !RHSIsNull)
        warnFnPtrToVoidCmp(*this, Loc, LHS, RHS,
                           /*isError*/ false);
    } else {
      // Invalid
      warnDistinctPointerCmp(*this, Loc, LHS, RHS,
                             /*isError*/ false);
    }
    if (LCanPointeeTy != RCanPointeeTy) {
      LangAS AddrSpaceL = LCanPointeeTy.getAddressSpace();
      LangAS AddrSpaceR = RCanPointeeTy.getAddressSpace();
      CastKind Kind =
          AddrSpaceL != AddrSpaceR ? CK_AddressSpaceConversion : CK_BitCast;
      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.get(), RHSType, Kind);
      else
        RHS = ImpCastExprToType(RHS.get(), LHSType, Kind);
    }
    return computeResultTy();
  }

  // C23 6.5.9p5: nullptr_t vs null pointer constant compares equal when
  // allowed.
  if (!IsOrdered && LHSIsNull && RHSIsNull) {
    if (LHSType->isNullPtrType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
    if (RHSType->isNullPtrType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  if (!IsOrdered && (LHSIsNull || RHSIsNull)) {
    // C23 6.5.9p6:
    //   Otherwise, at least one operand is a pointer. If one is a pointer and
    //   the other is a null pointer constant or has type nullptr_t, they
    //   compare equal
    if (LHSIsNull && RHSType->isPointerType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }
    if (RHSIsNull && LHSType->isPointerType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  if ((LHSType->isAnyPointerType() && RHSType->isIntegerType()) ||
      (LHSType->isIntegerType() && RHSType->isAnyPointerType())) {
    unsigned DiagID = 0;
    if ((LHSIsNull && LHSType->isIntegerType()) ||
        (RHSIsNull && RHSType->isIntegerType())) {
    } else if (IsOrdered)
      DiagID = diag::ext_typecheck_ordered_comparison_of_pointer_integer;
    else
      DiagID = diag::ext_typecheck_comparison_of_pointer_integer;

    if (DiagID) {
      Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                        << RHS.get()->getSourceRange();
    }

    if (LHSType->isIntegerType())
      LHS = ImpCastExprToType(LHS.get(), RHSType,
                              LHSIsNull ? CK_NullToPointer
                                        : CK_IntegralToPointer);
    else
      RHS = ImpCastExprToType(RHS.get(), LHSType,
                              RHSIsNull ? CK_NullToPointer
                                        : CK_IntegralToPointer);
    return computeResultTy();
  }

  return InvalidOperands(Loc, LHS, RHS);
}
