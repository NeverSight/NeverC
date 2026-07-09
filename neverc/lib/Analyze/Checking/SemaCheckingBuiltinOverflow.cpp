#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringExtras.h"
#include <utility>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Alignment / overflow builtin semantic checks
// ===----------------------------------------------------------------------===

bool semaBuiltinAlignment(Sema &S, CallExpr *TheCall, unsigned ID) {
  if (checkArgCount(S, TheCall, 2))
    return true;

  neverc::Expr *Source = TheCall->getArg(0);
  bool IsBooleanAlignBuiltin = ID == Builtin::BI__builtin_is_aligned;

  auto IsValidIntegerType = [](QualType Ty) {
    return Ty->isIntegerType() && !Ty->isEnumeralType() && !Ty->isBooleanType();
  };
  QualType SrcTy = Source->getType();
  // We should also be able to use it with arrays (but not functions!).
  if (SrcTy->canDecayToPointerType() && SrcTy->isArrayType()) {
    SrcTy = S.Context.getDecayedType(SrcTy);
  }
  if ((!SrcTy->isPointerType() && !IsValidIntegerType(SrcTy)) ||
      SrcTy->isFunctionPointerType()) {
    S.Diag(Source->getExprLoc(), diag::err_typecheck_expect_scalar_operand)
        << SrcTy;
    return true;
  }

  neverc::Expr *AlignOp = TheCall->getArg(1);
  if (!IsValidIntegerType(AlignOp->getType())) {
    S.Diag(AlignOp->getExprLoc(), diag::err_typecheck_expect_int)
        << AlignOp->getType();
    return true;
  }
  Expr::EvalResult AlignResult;
  unsigned MaxAlignmentBits = S.Context.getIntWidth(SrcTy) - 1;
  if (AlignOp->EvaluateAsInt(AlignResult, S.Context,
                             Expr::SE_AllowSideEffects)) {
    llvm::APSInt AlignValue = AlignResult.Val.getInt();
    llvm::APSInt MaxValue(
        llvm::APInt::getOneBitSet(MaxAlignmentBits + 1, MaxAlignmentBits));
    if (AlignValue < 1) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_too_small) << 1;
      return true;
    }
    if (llvm::APSInt::compareValues(AlignValue, MaxValue) > 0) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_too_big)
          << toString(MaxValue, 10);
      return true;
    }
    if (!AlignValue.isPowerOf2()) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_not_power_of_two);
      return true;
    }
    if (AlignValue == 1) {
      S.Diag(AlignOp->getExprLoc(), diag::warn_alignment_builtin_useless)
          << IsBooleanAlignBuiltin;
    }
  }

  ExprResult SrcArg = S.PerformCopyInitialization(
      InitializedEntity::InitializeParameter(S.Context, SrcTy, false),
      SourceLocation(), Source);
  if (SrcArg.isInvalid())
    return true;
  TheCall->setArg(0, SrcArg.get());
  ExprResult AlignArg =
      S.PerformCopyInitialization(InitializedEntity::InitializeParameter(
                                      S.Context, AlignOp->getType(), false),
                                  SourceLocation(), AlignOp);
  if (AlignArg.isInvalid())
    return true;
  TheCall->setArg(1, AlignArg.get());
  // For align_up/align_down, the return type is the same as the (potentially
  // decayed) argument type including qualifiers. For is_aligned(), the result
  // is always bool.
  TheCall->setType(IsBooleanAlignBuiltin ? S.Context.BoolTy : SrcTy);
  return false;
}

bool semaBuiltinOverflow(Sema &S, CallExpr *TheCall, unsigned BuiltinID) {
  if (checkArgCount(S, TheCall, 3))
    return true;

  std::pair<unsigned, const char *> Builtins[] = {
      {Builtin::BI__builtin_add_overflow, "ckd_add"},
      {Builtin::BI__builtin_sub_overflow, "ckd_sub"},
      {Builtin::BI__builtin_mul_overflow, "ckd_mul"},
  };

  bool CkdOperation =
      llvm::any_of(Builtins, [&](const std::pair<unsigned, const char *> &P) {
        return BuiltinID == P.first && TheCall->getExprLoc().isMacroID() &&
               SourceScanner::getImmediateMacroName(
                   TheCall->getExprLoc(), S.getSourceManager(),
                   S.getLangOpts()) == P.second;
      });

  auto ValidCkdIntType = [](QualType QT) {
    // A valid checked integer type is an integer type other than a plain char,
    // bool, a bit-precise type, or an enumeration type.
    if (const auto *BT = QT.getCanonicalType()->getAs<BuiltinType>())
      return (BT->getKind() >= BuiltinType::Short &&
              BT->getKind() <= BuiltinType::Int128) ||
             (BT->getKind() >= BuiltinType::UShort &&
              BT->getKind() <= BuiltinType::UInt128) ||
             BT->getKind() == BuiltinType::UChar ||
             BT->getKind() == BuiltinType::SChar;
    return false;
  };

  // First two arguments should be integers.
  for (unsigned I = 0; I < 2; ++I) {
    ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(I));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(I, Arg.get());

    QualType Ty = Arg.get()->getType();
    bool IsValid = CkdOperation ? ValidCkdIntType(Ty) : Ty->isIntegerType();
    if (!IsValid) {
      S.Diag(Arg.get()->getBeginLoc(), diag::err_overflow_builtin_must_be_int)
          << CkdOperation << Ty << Arg.get()->getSourceRange();
      return true;
    }
  }

  // Third argument should be a pointer to a non-const integer.
  // IRGen correctly handles volatile, restrict, and address spaces, and
  // the other qualifiers aren't possible.
  {
    ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(2));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(2, Arg.get());

    QualType Ty = Arg.get()->getType();
    const auto *PtrTy = Ty->getAs<PointerType>();
    if (!PtrTy || !PtrTy->getPointeeType()->isIntegerType() ||
        (!ValidCkdIntType(PtrTy->getPointeeType()) && CkdOperation) ||
        PtrTy->getPointeeType().isConstQualified()) {
      S.Diag(Arg.get()->getBeginLoc(),
             diag::err_overflow_builtin_must_be_ptr_int)
          << CkdOperation << Ty << Arg.get()->getSourceRange();
      return true;
    }
  }

  // Disallow signed bit-precise integer args larger than 128 bits to mul
  // function until we improve backend support.
  if (BuiltinID == Builtin::BI__builtin_mul_overflow) {
    for (unsigned I = 0; I < 3; ++I) {
      const auto Arg = TheCall->getArg(I);
      // Third argument will be a pointer.
      auto Ty = I < 2 ? Arg->getType() : Arg->getType()->getPointeeType();
      if (Ty->isBitIntType() && Ty->isSignedIntegerType() &&
          S.getTreeContext().getIntWidth(Ty) > 128)
        return S.Diag(Arg->getBeginLoc(),
                      diag::err_overflow_builtin_bit_int_max_size)
               << 128;
    }
  }

  return false;
}
