#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Format/FormatString.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <ctime>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace neverc;
using namespace sema;

SourceLocation Sema::getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                    unsigned ByteNo) const {
  return SL->getLocationOfByte(ByteNo, getSourceManager(), LangOpts,
                               Context.getTargetInfo());
}


// ===----------------------------------------------------------------------===
// Argument validation helpers
// ===----------------------------------------------------------------------===

bool checkArgCountAtLeast(Sema &S, CallExpr *Call, unsigned MinArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount >= MinArgCount)
    return false;

  return S.Diag(Call->getEndLoc(), diag::err_typecheck_call_too_few_args)
         << MinArgCount << ArgCount << Call->getSourceRange();
}

bool checkArgCountAtMost(Sema &S, CallExpr *Call, unsigned MaxArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount <= MaxArgCount)
    return false;
  return S.Diag(Call->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
         << MaxArgCount << ArgCount << Call->getSourceRange();
}

bool checkArgCountRange(Sema &S, CallExpr *Call, unsigned MinArgCount,
                        unsigned MaxArgCount) {
  return checkArgCountAtLeast(S, Call, MinArgCount) ||
         checkArgCountAtMost(S, Call, MaxArgCount);
}

bool checkArgCount(Sema &S, CallExpr *Call, unsigned DesiredArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount == DesiredArgCount)
    return false;

  if (checkArgCountAtLeast(S, Call, DesiredArgCount))
    return true;
  assert(ArgCount > DesiredArgCount && "should have diagnosed this");

  // Highlight all the excess arguments.
  SourceRange Range(Call->getArg(DesiredArgCount)->getBeginLoc(),
                    Call->getArg(ArgCount - 1)->getEndLoc());

  return S.Diag(Range.getBegin(), diag::err_typecheck_call_too_many_args)
         << DesiredArgCount << ArgCount << Call->getArg(1)->getSourceRange();
}

bool convertArgumentToType(Sema &S, Expr *&Value, QualType Ty) {
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, Ty, false);
  ExprResult Result =
      S.PerformCopyInitialization(Entity, SourceLocation(), Value);
  if (Result.isInvalid())
    return true;
  Value = Result.get();
  return false;
}



bool semaBuiltinSEHScopeCheck(Sema &SemaRef, CallExpr *TheCall,
                              Scope::ScopeFlags NeededScopeFlags,
                              unsigned DiagID) {
  Scope *S = SemaRef.getCurScope();
  while (S && !S->isSEHExceptScope())
    S = S->getParent();
  if (!S || !(S->getFlags() & NeededScopeFlags)) {
    auto *DRE = cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
    SemaRef.Diag(TheCall->getExprLoc(), DiagID)
        << DRE->getDecl()->getIdentifier();
    return true;
  }

  return false;
}

// Emit an error and return true if the current architecture is not in the list
// of supported architectures.
bool checkBuiltinTargetInSupported(
    Sema &S, unsigned BuiltinID, CallExpr *TheCall,
    llvm::ArrayRef<llvm::Triple::ArchType> SupportedArchs) {
  llvm::Triple::ArchType CurArch =
      S.getTreeContext().getTargetInfo().getTriple().getArch();
  if (llvm::is_contained(SupportedArchs, CurArch))
    return false;
  S.Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
      << TheCall->getSourceRange();
  return true;
}

void checkNonNullArgument(Sema &S, const Expr *ArgExpr,
                          SourceLocation CallSiteLoc);

bool Sema::CheckTSBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                      CallExpr *TheCall) {
  switch (TI.getTriple().getArch()) {
  default:
    // Some builtins don't require additional checking, so just consider these
    // acceptable.
    return false;
  case llvm::Triple::aarch64:
    return CheckAArch64BuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::x86_64:
    return CheckX86BuiltinFunctionCall(TI, BuiltinID, TheCall);
  }
}

// Check if \p Ty is a valid type for the elementwise math builtins. If it is
// not a valid type, emit an error message and return true. Otherwise return
// false.
bool checkMathBuiltinElementType(Sema &S, SourceLocation Loc, QualType Ty) {
  if (!Ty->getAs<VectorType>() && !ConstantMatrixType::isValidElementType(Ty)) {
    return S.Diag(Loc, diag::err_builtin_invalid_arg_type)
           << 1 << /* vector, integer or float ty*/ 0 << Ty;
  }

  return false;
}

bool checkFPMathBuiltinElementType(Sema &S, SourceLocation Loc, QualType ArgTy,
                                   int ArgIndex) {
  QualType EltTy = ArgTy;
  if (auto *VecTy = EltTy->getAs<VectorType>())
    EltTy = VecTy->getElementType();

  if (!EltTy->isRealFloatingType()) {
    return S.Diag(Loc, diag::err_builtin_invalid_arg_type)
           << ArgIndex << /* vector or float ty*/ 5 << ArgTy;
  }

  return false;
}


// ===----------------------------------------------------------------------===
// Builtin function call dispatch
// ===----------------------------------------------------------------------===

ExprResult Sema::CheckBuiltinFunctionCall(FunctionDecl *FDecl,
                                          unsigned BuiltinID,
                                          CallExpr *TheCall) {
  ExprResult TheCallResult(TheCall);

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  TreeContext::GetBuiltinTypeError Error;
  Context.GetBuiltinType(BuiltinID, Error, &ICEArguments);
  if (Error != TreeContext::GE_None)
    ICEArguments = 0; // Don't diagnose previously diagnosed errors.

  // If any arguments are required to be ICE's, check and diagnose.
  for (unsigned ArgNo = 0; ICEArguments != 0; ++ArgNo) {
    // Skip arguments not required to be ICE's.
    if ((ICEArguments & (1 << ArgNo)) == 0)
      continue;

    llvm::APSInt Result;
    // If we don't have enough arguments, continue so we can issue better
    // diagnostic in checkArgCount(...)
    if (ArgNo < TheCall->getNumArgs() &&
        SemaBuiltinConstantArg(TheCall, ArgNo, Result))
      return true;
    ICEArguments &= ~(1 << ArgNo);
  }

  switch (BuiltinID) {
  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
    if (SemaBuiltinVAStart(BuiltinID, TheCall))
      return ExprError();
    break;
  case Builtin::BI__va_start: {
    if (Context.getTargetInfo().getTriple().getArch() ==
        llvm::Triple::aarch64) {
      if (SemaBuiltinVAStartARMMicrosoft(TheCall))
        return ExprError();
    } else if (SemaBuiltinVAStart(BuiltinID, TheCall)) {
      return ExprError();
    }
    break;
  }

  // The acquire, release, and no fence variants are AArch64 only.
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    if (checkBuiltinTargetInSupported(*this, BuiltinID, TheCall,
                                      {llvm::Triple::aarch64}))
      return ExprError();
    break;

  // The 64-bit bittest variants are x64 and AArch64 only.
  case Builtin::BI_bittest64:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
    if (checkBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::x86_64, llvm::Triple::aarch64}))
      return ExprError();
    break;

  case Builtin::BI__builtin_set_flt_rounds:
    if (checkBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::x86_64, llvm::Triple::aarch64}))
      return ExprError();
    break;

  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered:
    if (SemaBuiltinUnorderedCompare(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_fpclassify:
    if (SemaBuiltinFPClassification(TheCall, 6))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfpclass:
    if (SemaBuiltinFPClassification(TheCall, 2))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfinite:
  case Builtin::BI__builtin_isinf:
  case Builtin::BI__builtin_isinf_sign:
  case Builtin::BI__builtin_isnan:
  case Builtin::BI__builtin_issignaling:
  case Builtin::BI__builtin_isnormal:
  case Builtin::BI__builtin_issubnormal:
  case Builtin::BI__builtin_iszero:
  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl:
    if (SemaBuiltinFPClassification(TheCall, 1))
      return ExprError();
    break;
  case Builtin::BI__builtin_shufflevector:
    return SemaBuiltinShuffleVector(TheCall);
    // TheCall will be freed by the smart pointer here, but that's fine, since
    // SemaBuiltinShuffleVector guts it, but then doesn't release it.
  case Builtin::BI__builtin_prefetch:
    if (SemaBuiltinPrefetch(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_alloca_with_align:
  case Builtin::BI__builtin_alloca_with_align_uninitialized:
    if (SemaBuiltinAllocaWithAlign(TheCall))
      return ExprError();
    [[fallthrough]];
  case Builtin::BI__builtin_alloca:
  case Builtin::BI__builtin_alloca_uninitialized:
    Diag(TheCall->getBeginLoc(), diag::warn_alloca)
        << TheCall->getDirectCallee();
    break;
  case Builtin::BI__arithmetic_fence:
    if (SemaBuiltinArithmeticFence(TheCall))
      return ExprError();
    break;
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume:
    if (SemaBuiltinAssume(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_verbose_trap: {
    bool HasError = false;
    for (const Expr *Arg : TheCall->arguments()) {
      if (Arg->isValueDependent())
        continue;
      std::optional<std::string> ArgString =
          Arg->tryEvaluateString(Context);
      int DiagMsgKind = -1;
      if (!ArgString.has_value())
        DiagMsgKind = 0;
      else if (ArgString->find('$') != std::string::npos)
        DiagMsgKind = 1;
      if (DiagMsgKind >= 0) {
        Diag(Arg->getBeginLoc(), diag::err_builtin_verbose_trap_arg)
            << DiagMsgKind << Arg->getSourceRange();
        HasError = true;
      }
    }
    if (HasError)
      return ExprError();
    break;
  }
  case Builtin::BI__builtin_assume_aligned:
    if (SemaBuiltinAssumeAligned(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size:
    if (SemaBuiltinConstantArgRange(TheCall, 1, 0, 3))
      return ExprError();
    break;
  case Builtin::BI__builtin_longjmp:
    if (SemaBuiltinLongjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_setjmp:
    if (SemaBuiltinSetjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_classify_type:
    if (checkArgCount(*this, TheCall, 1))
      return true;
    TheCall->setType(Context.IntTy);
    break;
  case Builtin::BI__builtin_complex:
    if (SemaBuiltinComplex(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_constant_p: {
    if (checkArgCount(*this, TheCall, 1))
      return true;
    ExprResult Arg = DefaultFunctionArrayLvalueConversion(TheCall->getArg(0));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(0, Arg.get());
    TheCall->setType(Context.IntTy);
    break;
  }
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return SemaBuiltinAtomicOverloaded(TheCallResult);
  case Builtin::BI__sync_synchronize:
    Diag(TheCall->getBeginLoc(), diag::warn_atomic_implicit_seq_cst)
        << TheCall->getCallee()->getSourceRange();
    break;
  case Builtin::BI__builtin_nontemporal_load:
  case Builtin::BI__builtin_nontemporal_store:
    return SemaBuiltinNontemporalOverloaded(TheCallResult);
  case Builtin::BI__builtin_memcpy_inline: {
    neverc::Expr *SizeOp = TheCall->getArg(2);
    // We warn about copying to or from `nullptr` pointers when `size` is
    // greater than 0. When `size` is value dependent we cannot evaluate its
    // value so we bail out.
    if (!SizeOp->EvaluateKnownConstInt(Context).isZero()) {
      checkNonNullArgument(*this, TheCall->getArg(0), TheCall->getExprLoc());
      checkNonNullArgument(*this, TheCall->getArg(1), TheCall->getExprLoc());
    }
    break;
  }
  case Builtin::BI__builtin_memset_inline: {
    neverc::Expr *SizeOp = TheCall->getArg(2);
    if (!SizeOp->EvaluateKnownConstInt(Context).isZero())
      checkNonNullArgument(*this, TheCall->getArg(0), TheCall->getExprLoc());
    break;
  }
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS)                                        \
  case Builtin::BI##ID:                                                        \
    return SemaAtomicOpsOverloaded(TheCallResult, AtomicExpr::AO##ID);
#include "neverc/Foundation/Builtin/Builtins.def"
  case Builtin::BI__annotation:
    if (semaBuiltinMSVCAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_annotation:
    if (semaBuiltinAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_neverc_xorstr:
    return semaBuiltinNeverCXorstr(*this, TheCall);
  case Builtin::BI__builtin_neverc_strhash:
    return semaBuiltinNeverCStrHash(*this, TheCall);
  case Builtin::BI__builtin_neverc_random_u64:
    return semaBuiltinNeverCRandomU64(*this, TheCall);
  case Builtin::BI__builtin_function_start:
    if (semaBuiltinFunctionStart(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_is_aligned:
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down:
    if (semaBuiltinAlignment(*this, TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
    if (semaBuiltinOverflow(*this, TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_dump_struct:
    return semaBuiltinDumpStruct(*this, TheCall);
  case Builtin::BI__builtin_expect_with_probability: {
    // We first want to ensure we are called with 3 arguments
    if (checkArgCount(*this, TheCall, 3))
      return ExprError();
    // then check probability is constant float in range [0.0, 1.0]
    const Expr *ProbArg = TheCall->getArg(2);
    llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
    Expr::EvalResult Eval;
    Eval.Diag = &Notes;
    if ((!ProbArg->EvaluateAsConstantExpr(Eval, Context)) ||
        !Eval.Val.isFloat()) {
      Diag(ProbArg->getBeginLoc(), diag::err_probability_not_constant_float)
          << ProbArg->getSourceRange();
      for (const PartialDiagnosticAt &PDiag : Notes)
        Diag(PDiag.first, PDiag.second);
      return ExprError();
    }
    llvm::APFloat Probability = Eval.Val.getFloat();
    bool LoseInfo = false;
    Probability.convert(llvm::APFloat::IEEEdouble(),
                        llvm::RoundingMode::Dynamic, &LoseInfo);
    if (!(Probability >= llvm::APFloat(0.0) &&
          Probability <= llvm::APFloat(1.0))) {
      Diag(ProbArg->getBeginLoc(), diag::err_probability_out_of_range)
          << ProbArg->getSourceRange();
      return ExprError();
    }
    break;
  }
  case Builtin::BI__builtin_preserve_access_index:
    if (semaBuiltinPreserveAI(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_call_with_static_chain:
    if (semaBuiltinCallWithStaticChain(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    if (semaBuiltinSEHScopeCheck(*this, TheCall, Scope::SEHExceptScope,
                                 diag::err_seh___except_block))
      return ExprError();
    break;
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    if (semaBuiltinSEHScopeCheck(*this, TheCall, Scope::SEHFilterScope,
                                 diag::err_seh___except_filter))
      return ExprError();
    break;
  case Builtin::BI__builtin_os_log_format:
    Cleanup.setExprNeedsCleanups(true);
    [[fallthrough]];
  case Builtin::BI__builtin_os_log_format_buffer_size:
    if (SemaBuiltinOSLogFormat(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_frame_address:
  case Builtin::BI__builtin_return_address: {
    if (SemaBuiltinConstantArgRange(TheCall, 0, 0, 0xFFFF))
      return ExprError();

    // -Wframe-address warning if non-zero passed to builtin
    // return/frame address.
    Expr::EvalResult Result;
    if (TheCall->getArg(0)->EvaluateAsInt(Result, getTreeContext()) &&
        Result.Val.getInt() != 0)
      Diag(TheCall->getBeginLoc(), diag::warn_frame_address)
          << ((BuiltinID == Builtin::BI__builtin_return_address)
                  ? "__builtin_return_address"
                  : "__builtin_frame_address")
          << TheCall->getSourceRange();
    break;
  }

  case Builtin::BI__builtin_nondeterministic_value: {
    if (SemaBuiltinNonDeterministicValue(TheCall))
      return ExprError();
    break;
  }

  // __builtin_elementwise_abs restricts the element type to signed integers or
  // floating point types only.
  case Builtin::BI__builtin_elementwise_abs: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();
    if (EltTy->isUnsignedIntegerType()) {
      Diag(TheCall->getArg(0)->getBeginLoc(),
           diag::err_builtin_invalid_arg_type)
          << 1 << /* signed integer or float ty*/ 3 << ArgTy;
      return ExprError();
    }
    break;
  }

  // These builtins restrict the element type to floating point
  // types only.
  case Builtin::BI__builtin_elementwise_ceil:
  case Builtin::BI__builtin_elementwise_cos:
  case Builtin::BI__builtin_elementwise_exp:
  case Builtin::BI__builtin_elementwise_exp2:
  case Builtin::BI__builtin_elementwise_floor:
  case Builtin::BI__builtin_elementwise_log:
  case Builtin::BI__builtin_elementwise_log2:
  case Builtin::BI__builtin_elementwise_log10:
  case Builtin::BI__builtin_elementwise_roundeven:
  case Builtin::BI__builtin_elementwise_round:
  case Builtin::BI__builtin_elementwise_rint:
  case Builtin::BI__builtin_elementwise_nearbyint:
  case Builtin::BI__builtin_elementwise_sin:
  case Builtin::BI__builtin_elementwise_sqrt:
  case Builtin::BI__builtin_elementwise_trunc:
  case Builtin::BI__builtin_elementwise_canonicalize: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      ArgTy, 1))
      return ExprError();
    break;
  }
  case Builtin::BI__builtin_elementwise_fma: {
    if (SemaBuiltinElementwiseTernaryMath(TheCall))
      return ExprError();
    break;
  }

  // These builtins restrict the element type to floating point
  // types only, and take in two arguments.
  case Builtin::BI__builtin_elementwise_pow: {
    if (SemaBuiltinElementwiseMath(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      ArgTy, 1) ||
        checkFPMathBuiltinElementType(*this, TheCall->getArg(1)->getBeginLoc(),
                                      ArgTy, 2))
      return ExprError();
    break;
  }

  // These builtins restrict the element type to integer
  // types only.
  case Builtin::BI__builtin_elementwise_add_sat:
  case Builtin::BI__builtin_elementwise_sub_sat: {
    if (SemaBuiltinElementwiseMath(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    QualType ArgTy = Arg->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();

    if (!EltTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* integer ty */ 6 << ArgTy;
      return ExprError();
    }
    break;
  }

  case Builtin::BI__builtin_elementwise_min:
  case Builtin::BI__builtin_elementwise_max:
    if (SemaBuiltinElementwiseMath(TheCall))
      return ExprError();
    break;

  case Builtin::BI__builtin_elementwise_bitreverse: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    QualType ArgTy = Arg->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();

    if (!EltTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* integer ty */ 6 << ArgTy;
      return ExprError();
    }
    break;
  }

  case Builtin::BI__builtin_elementwise_copysign: {
    if (checkArgCount(*this, TheCall, 2))
      return ExprError();

    ExprResult Magnitude = UsualUnaryConversions(TheCall->getArg(0));
    ExprResult Sign = UsualUnaryConversions(TheCall->getArg(1));
    if (Magnitude.isInvalid() || Sign.isInvalid())
      return ExprError();

    QualType MagnitudeTy = Magnitude.get()->getType();
    QualType SignTy = Sign.get()->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      MagnitudeTy, 1) ||
        checkFPMathBuiltinElementType(*this, TheCall->getArg(1)->getBeginLoc(),
                                      SignTy, 2)) {
      return ExprError();
    }

    if (MagnitudeTy.getCanonicalType() != SignTy.getCanonicalType()) {
      return Diag(Sign.get()->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << MagnitudeTy << SignTy;
    }

    TheCall->setArg(0, Magnitude.get());
    TheCall->setArg(1, Sign.get());
    TheCall->setType(Magnitude.get()->getType());
    break;
  }
  case Builtin::BI__builtin_reduce_max:
  case Builtin::BI__builtin_reduce_min: {
    if (PrepareBuiltinReduceMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    const auto *TyA = Arg->getType()->getAs<VectorType>();
    if (!TyA) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* vector ty*/ 4 << Arg->getType();
      return ExprError();
    }

    TheCall->setType(TyA->getElementType());
    break;
  }

  // These builtins support vectors of integers only.
  case Builtin::BI__builtin_reduce_add:
  case Builtin::BI__builtin_reduce_mul:
  case Builtin::BI__builtin_reduce_xor:
  case Builtin::BI__builtin_reduce_or:
  case Builtin::BI__builtin_reduce_and: {
    if (PrepareBuiltinReduceMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    const auto *TyA = Arg->getType()->getAs<VectorType>();
    if (!TyA || !TyA->getElementType()->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* vector of integers */ 6 << Arg->getType();
      return ExprError();
    }
    TheCall->setType(TyA->getElementType());
    break;
  }

  case Builtin::BI__builtin_matrix_transpose:
    return SemaBuiltinMatrixTranspose(TheCall, TheCallResult);

  case Builtin::BI__builtin_matrix_column_major_load:
    return SemaBuiltinMatrixColumnMajorLoad(TheCall, TheCallResult);

  case Builtin::BI__builtin_matrix_column_major_store:
    return SemaBuiltinMatrixColumnMajorStore(TheCall, TheCallResult);
  }

  if (Context.BuiltinInfo.isTSBuiltin(BuiltinID)) {
    if (CheckTSBuiltinFunctionCall(Context.getTargetInfo(), BuiltinID, TheCall))
      return ExprError();
  }

  return TheCallResult;
}

// Get the valid immediate range for the specified NEON type code.
unsigned RFT(unsigned t, bool shift = false, bool ForceQuad = false) {
  NeonTypeFlags Type(t);
  int IsQuad = ForceQuad ? true : Type.isQuad();
  switch (Type.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
    return shift ? 7 : (8 << IsQuad) - 1;
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return shift ? 15 : (4 << IsQuad) - 1;
  case NeonTypeFlags::Int32:
    return shift ? 31 : (2 << IsQuad) - 1;
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return shift ? 63 : (1 << IsQuad) - 1;
  case NeonTypeFlags::Poly128:
    return shift ? 127 : (1 << IsQuad) - 1;
  case NeonTypeFlags::Float16:
    assert(!shift && "cannot shift float types!");
    return (4 << IsQuad) - 1;
  case NeonTypeFlags::Float32:
    assert(!shift && "cannot shift float types!");
    return (2 << IsQuad) - 1;
  case NeonTypeFlags::Float64:
    assert(!shift && "cannot shift float types!");
    return (1 << IsQuad) - 1;
  case NeonTypeFlags::BFloat16:
    assert(!shift && "cannot shift float types!");
    return (4 << IsQuad) - 1;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}

QualType getNeonEltType(NeonTypeFlags Flags, TreeContext &Context,
                        bool IsPolyUnsigned, bool IsInt64Long) {
  switch (Flags.getEltType()) {
  case NeonTypeFlags::Int8:
    return Flags.isUnsigned() ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Int16:
    return Flags.isUnsigned() ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Int32:
    return Flags.isUnsigned() ? Context.UnsignedIntTy : Context.IntTy;
  case NeonTypeFlags::Int64:
    if (IsInt64Long)
      return Flags.isUnsigned() ? Context.UnsignedLongTy : Context.LongTy;
    else
      return Flags.isUnsigned() ? Context.UnsignedLongLongTy
                                : Context.LongLongTy;
  case NeonTypeFlags::Poly8:
    return IsPolyUnsigned ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Poly16:
    return IsPolyUnsigned ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Poly64:
    if (IsInt64Long)
      return Context.UnsignedLongTy;
    else
      return Context.UnsignedLongLongTy;
  case NeonTypeFlags::Poly128:
    break;
  case NeonTypeFlags::Float16:
    return Context.HalfTy;
  case NeonTypeFlags::Float32:
    return Context.FloatTy;
  case NeonTypeFlags::Float64:
    return Context.DoubleTy;
  case NeonTypeFlags::BFloat16:
    return Context.BFloat16Ty;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}


// ===----------------------------------------------------------------------===
// Call validation, format strings & non-null checks
// ===----------------------------------------------------------------------===

bool Sema::getFormatStringInfo(const FormatAttr *Format, bool IsVariadic,
                               FormatStringInfo *FSI) {
  if (Format->getFirstArg() == 0)
    FSI->ArgPassingKind = FAPK_VAList;
  else if (IsVariadic)
    FSI->ArgPassingKind = FAPK_Variadic;
  else
    FSI->ArgPassingKind = FAPK_Fixed;
  FSI->FormatIdx = Format->getFormatIdx() - 1;
  FSI->FirstDataArg =
      FSI->ArgPassingKind == FAPK_VAList ? 0 : Format->getFirstArg() - 1;
  return true;
}

bool CheckNonNullExpr(Sema &S, const Expr *Expr) {
  // If the expression has non-null type, it doesn't evaluate to null.
  if (auto nullability = Expr->IgnoreImplicit()->getType()->getNullability()) {
    if (*nullability == NullabilityKind::NonNull)
      return false;
  }

  // As a special case, transparent unions initialized with zero are
  // considered null for the purposes of the nonnull attribute.
  if (const RecordType *UT = Expr->getType()->getAsUnionType()) {
    if (UT->getDecl()->hasAttr<TransparentUnionAttr>())
      if (const CompoundLiteralExpr *CLE = dyn_cast<CompoundLiteralExpr>(Expr))
        if (const InitListExpr *ILE =
                dyn_cast<InitListExpr>(CLE->getInitializer()))
          Expr = ILE->getInit(0);
  }

  bool Result;
  return (Expr->EvaluateAsBooleanCondition(Result, S.Context) && !Result);
}

void checkNonNullArgument(Sema &S, const Expr *ArgExpr,
                          SourceLocation CallSiteLoc) {
  if (CheckNonNullExpr(S, ArgExpr))
    S.DiagRuntimeBehavior(CallSiteLoc, ArgExpr,
                          S.PDiag(diag::warn_null_arg)
                              << ArgExpr->getSourceRange());
}

bool isNonNullType(QualType type) {
  if (auto nullability = type->getNullability())
    return *nullability == NullabilityKind::NonNull;

  return false;
}

void checkNonNullArguments(Sema &S, const NamedDecl *FDecl,
                           const FunctionProtoType *Proto,
                           llvm::ArrayRef<const Expr *> Args,
                           SourceLocation CallSiteLoc) {
  assert((FDecl || Proto) && "Need a function declaration or prototype");

  // Already checked by constant evaluator.
  if (S.isConstantEvaluatedContext())
    return;
  llvm::SmallBitVector NonNullArgs;
  if (FDecl) {
    for (const auto *NonNull : FDecl->specific_attrs<NonNullAttr>()) {
      if (!NonNull->args_size()) {
        // Easy case: all pointer arguments are nonnull.
        for (const auto *Arg : Args)
          if (S.isValidPointerAttrType(Arg->getType()))
            checkNonNullArgument(S, Arg, CallSiteLoc);
        return;
      }

      for (const ParamIdx &Idx : NonNull->args()) {
        unsigned IdxAST = Idx.getASTIndex();
        if (IdxAST >= Args.size())
          continue;
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());
        NonNullArgs.set(IdxAST);
      }
    }
  }

  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(FDecl)) {
    llvm::ArrayRef<ParmVarDecl *> parms;
    parms = FD->parameters();

    unsigned ParamIndex = 0;
    for (llvm::ArrayRef<ParmVarDecl *>::iterator I = parms.begin(),
                                                 E = parms.end();
         I != E; ++I, ++ParamIndex) {
      const ParmVarDecl *PVD = *I;
      if (PVD->hasAttr<NonNullAttr>() || isNonNullType(PVD->getType())) {
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());

        NonNullArgs.set(ParamIndex);
      }
    }
  } else {
    // If we have a non-function, non-method declaration but no
    // function prototype, try to dig out the function prototype.
    if (!Proto) {
      if (const ValueDecl *VD = dyn_cast<ValueDecl>(FDecl)) {
        QualType type = VD->getType();
        if (auto pointerType = type->getAs<PointerType>())
          type = pointerType->getPointeeType();

        // Dig out the function prototype, if there is one.
        Proto = type->getAs<FunctionProtoType>();
      }
    }

    // Fill in non-null argument information from the nullability
    // information on the parameter types (if we have them).
    if (Proto) {
      unsigned Index = 0;
      for (auto paramType : Proto->getParamTypes()) {
        if (isNonNullType(paramType)) {
          if (NonNullArgs.empty())
            NonNullArgs.resize(Args.size());

          NonNullArgs.set(Index);
        }

        ++Index;
      }
    }
  }
  for (unsigned ArgIndex = 0, ArgIndexEnd = NonNullArgs.size();
       ArgIndex != ArgIndexEnd; ++ArgIndex) {
    if (NonNullArgs[ArgIndex])
      checkNonNullArgument(S, Args[ArgIndex], Args[ArgIndex]->getExprLoc());
  }
}

void Sema::CheckArgAlignment(SourceLocation Loc, NamedDecl *FDecl,
                             llvm::StringRef ParamName, QualType ArgTy,
                             QualType ParamTy) {

  // If a function accepts a pointer type
  if (!ParamTy->isPointerType())
    return;

  // If the parameter is a pointer type, get the pointee type for the
  // argument too. If the parameter is a reference type, don't try to get
  // the pointee type for the argument.
  if (ParamTy->isPointerType())
    ArgTy = ArgTy->getPointeeType();

  // Remove reference or pointer
  ParamTy = ParamTy->getPointeeType();

  // Find expected alignment, and the actual alignment of the passed object.
  // getTypeAlignInChars requires complete types
  if (ArgTy.isNull() || ParamTy->isIncompleteType() ||
      ArgTy->isIncompleteType() || ParamTy->isUndeducedType() ||
      ArgTy->isUndeducedType())
    return;

  CharUnits ParamAlign = Context.getTypeAlignInChars(ParamTy);
  CharUnits ArgAlign = Context.getTypeAlignInChars(ArgTy);

  // If the argument is less aligned than the parameter, there is a
  // potential alignment issue.
  if (ArgAlign < ParamAlign)
    Diag(Loc, diag::warn_param_mismatched_alignment)
        << (int)ArgAlign.getQuantity() << (int)ParamAlign.getQuantity()
        << ParamName << (FDecl != nullptr) << FDecl;
}

void Sema::checkCall(NamedDecl *FDecl, const FunctionProtoType *Proto,
                     llvm::ArrayRef<const Expr *> Args, SourceLocation Loc,
                     SourceRange Range, VariadicCallType CallType) {
  // Printf and scanf checking.
  llvm::SmallBitVector CheckedVarArgs;
  if (FDecl) {
    for (const auto *I : FDecl->specific_attrs<FormatAttr>()) {
      // Only create vector if there are format attributes.
      CheckedVarArgs.resize(Args.size());

      CheckFormatArguments(I, Args, CallType, Loc, Range, CheckedVarArgs);
    }
  }

  // Refuse POD arguments that weren't caught by the format string
  // checks above.
  auto *FD = dyn_cast_or_null<FunctionDecl>(FDecl);
  if (CallType != VariadicDoesNotApply &&
      (!FD || FD->getBuiltinID() != Builtin::BI__noop)) {
    unsigned NumParams = Proto ? Proto->getNumParams()
                         : FD  ? FD->getNumParams()
                               : 0;

    for (unsigned ArgIdx = NumParams; ArgIdx < Args.size(); ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (CheckedVarArgs.empty() || !CheckedVarArgs[ArgIdx])
          checkVariadicArgument(Arg);
      }
    }
  }

  if (FDecl || Proto) {
    checkNonNullArguments(*this, FDecl, Proto, Args, Loc);

    // Type safety checking.
    if (FDecl) {
      for (const auto *I : FDecl->specific_attrs<ArgumentWithTypeTagAttr>())
        CheckArgumentWithTypeTag(I, Args, Loc);
    }
  }

  // Check that passed arguments match the alignment of original arguments.
  // Try to get the missing prototype from the declaration.
  if (!Proto && FDecl) {
    const auto *FT = FDecl->getFunctionType();
    if (isa_and_nonnull<FunctionProtoType>(FT))
      Proto = cast<FunctionProtoType>(FDecl->getFunctionType());
  }
  if (Proto) {
    // For variadic functions, we may have more args than parameters.
    // For some K&R functions, we may have less args than parameters.
    const auto N = std::min<unsigned>(Proto->getNumParams(), Args.size());
    for (unsigned ArgIdx = 0; ArgIdx < N; ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (Arg->containsErrors())
          continue;

        QualType ParamTy = Proto->getParamType(ArgIdx);
        QualType ArgTy = Arg->getType();
        CheckArgAlignment(Arg->getExprLoc(), FDecl, std::to_string(ArgIdx + 1),
                          ArgTy, ParamTy);
      }
    }

    // If the callee has an AArch64 SME attribute to indicate that it is an
    // __arm_streaming function, then the caller requires SME to be available.
    FunctionProtoType::ExtProtoInfo ExtInfo = Proto->getExtProtoInfo();
    if (ExtInfo.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask) {
      if (auto *CallerFD = dyn_cast<FunctionDecl>(CurContext)) {
        llvm::StringMap<bool> CallerFeatureMap;
        Context.getFunctionFeatureMap(CallerFeatureMap, CallerFD);
        if (!CallerFeatureMap.contains("sme"))
          Diag(Loc, diag::err_sme_call_in_non_sme_target);
      } else if (!Context.getTargetInfo().hasFeature("sme")) {
        Diag(Loc, diag::err_sme_call_in_non_sme_target);
      }
    }

    // If the callee uses AArch64 SME ZA state but the caller doesn't define
    // any, then this is an error.
    if (ExtInfo.AArch64SMEAttributes & FunctionType::SME_PStateZASharedMask) {
      bool CallerHasZAState = false;
      if (const auto *CallerFD = dyn_cast<FunctionDecl>(CurContext)) {
        if (CallerFD->hasAttr<ArmNewZAAttr>())
          CallerHasZAState = true;
        else if (const auto *FPT =
                     CallerFD->getType()->getAs<FunctionProtoType>())
          CallerHasZAState = FPT->getExtProtoInfo().AArch64SMEAttributes &
                             FunctionType::SME_PStateZASharedMask;
      }

      if (!CallerHasZAState)
        Diag(Loc, diag::err_sme_za_call_no_za_state);
    }
  }

  if (FDecl && FDecl->hasAttr<AllocAlignAttr>()) {
    auto *AA = FDecl->getAttr<AllocAlignAttr>();
    const Expr *Arg = Args[AA->getParamIndex().getASTIndex()];
    {
      Expr::EvalResult Align;
      if (Arg->EvaluateAsInt(Align, Context)) {
        const llvm::APSInt &I = Align.Val.getInt();
        if (!I.isPowerOf2())
          Diag(Arg->getExprLoc(), diag::warn_alignment_not_power_of_two)
              << Arg->getSourceRange();

        if (I > Sema::MaximumAlignment)
          Diag(Arg->getExprLoc(), diag::warn_assume_aligned_too_great)
              << Arg->getSourceRange() << Sema::MaximumAlignment;
      }
    }
  }

  if (FD)
    diagnoseArgDependentDiagnoseIfAttrs(FD, Args, Loc);
}

bool Sema::CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall,
                             const FunctionProtoType *Proto) {
  VariadicCallType CallType =
      (Proto && Proto->isVariadic()) ? VariadicFunction : VariadicDoesNotApply;
  Expr **Args = TheCall->getArgs();
  unsigned NumArgs = TheCall->getNumArgs();

  checkCall(FDecl, Proto, llvm::ArrayRef(Args, NumArgs),
            TheCall->getRParenLoc(), TheCall->getCallee()->getSourceRange(),
            CallType);

  IdentifierInfo *FnInfo = FDecl->getIdentifier();
  // None of the checks below are needed for functions without a simple name.
  if (!FnInfo)
    return false;

  // Enforce TCB except for builtin calls, which are always allowed.
  if (FDecl->getBuiltinID() == 0)
    CheckTCBEnforcement(TheCall->getExprLoc(), FDecl);

  CheckAbsoluteValueFunction(TheCall, FDecl);

  unsigned CMId = FDecl->getMemoryFunctionKind();
  switch (CMId) {
  case 0:
    return false;
  case Builtin::BIstrlcpy: // fallthrough
  case Builtin::BIstrlcat:
    CheckStrlcpycatArguments(TheCall, FnInfo);
    break;
  case Builtin::BIstrncat:
    CheckStrncatArguments(TheCall, FnInfo);
    break;
  case Builtin::BIfree:
    CheckFreeArguments(TheCall);
    break;
  default:
    CheckMemaccessArguments(TheCall, CMId, FnInfo);
  }

  return false;
}

bool Sema::CheckPointerCall(NamedDecl *NDecl, CallExpr *TheCall,
                            const FunctionProtoType *Proto) {
  QualType Ty;
  if (const auto *V = dyn_cast<VarDecl>(NDecl))
    Ty = V->getType();
  else if (const auto *F = dyn_cast<FieldDecl>(NDecl))
    Ty = F->getType();
  else
    return false;

  if (!Ty->isFunctionPointerType() && !Ty->isFunctionProtoType())
    return false;

  VariadicCallType CallType;
  if (!Proto || !Proto->isVariadic()) {
    CallType = VariadicDoesNotApply;
  } else {
    CallType = VariadicFunction;
  }

  checkCall(NDecl, Proto,
            llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            TheCall->getRParenLoc(), TheCall->getCallee()->getSourceRange(),
            CallType);

  return false;
}

bool Sema::CheckOtherCall(CallExpr *TheCall, const FunctionProtoType *Proto) {
  VariadicCallType CallType =
      (Proto && Proto->isVariadic()) ? VariadicFunction : VariadicDoesNotApply;
  checkCall(/*FDecl=*/nullptr, Proto,
            llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            TheCall->getRParenLoc(), TheCall->getCallee()->getSourceRange(),
            CallType);

  return false;
}

bool checkBuiltinArgument(Sema &S, CallExpr *E, unsigned ArgIndex) {
  FunctionDecl *Fn = E->getDirectCallee();
  assert(Fn && "builtin call without direct callee!");

  ParmVarDecl *Param = Fn->getParamDecl(ArgIndex);
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, Param);

  ExprResult Arg = E->getArg(ArgIndex);
  Arg = S.PerformCopyInitialization(Entity, SourceLocation(), Arg);
  if (Arg.isInvalid())
    return true;

  E->setArg(ArgIndex, Arg.get());
  return false;
}

