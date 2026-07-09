#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <limits>
#include <optional>
#include <utility>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Sema member builtin checks (va_start, FP, ConstantArg, longjmp, ...)
// ===----------------------------------------------------------------------===

ExprResult Sema::CheckOSLogFormatStringArg(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  auto *Literal = dyn_cast<StringLiteral>(Arg);
  if (!Literal || (!Literal->isOrdinary() && !Literal->isUTF8())) {
    return ExprError(
        Diag(Arg->getBeginLoc(), diag::err_os_log_format_not_string_constant)
        << Arg->getSourceRange());
  }

  ExprResult Result(Literal);
  QualType ResultTy = Context.getPointerType(Context.CharTy.withConst());
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(Context, ResultTy, false);
  Result = PerformCopyInitialization(Entity, SourceLocation(), Result);
  return Result;
}

// ===----------------------------------------------------------------------===
// Miscellaneous builtin checks (va_start, prefetch, assume, etc.)
// ===----------------------------------------------------------------------===

bool checkVAStartABI(Sema &S, unsigned BuiltinID, Expr *Fn) {
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  bool IsX64 = TT.getArch() == llvm::Triple::x86_64;
  bool IsAArch64 = TT.getArch() == llvm::Triple::aarch64;
  bool IsWindows = TT.isOSWindows();
  bool IsMSVAStart = BuiltinID == Builtin::BI__builtin_ms_va_start;
  if (IsX64 || IsAArch64) {
    CallingConv CC = CC_C;
    if (const FunctionDecl *FD = S.getCurFunctionDecl())
      CC = FD->getType()->castAs<FunctionType>()->getCallConv();
    if (IsMSVAStart) {
      // Don't allow this in System V ABI functions.
      if (CC == CC_X86_64SysV || (!IsWindows && CC != CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_ms_va_start_used_in_sysv_function);
    } else {
      // On x86-64/AArch64 Unix, don't allow this in Win64 ABI functions.
      // On x64 Windows, don't allow this in System V ABI functions.
      // (Yes, that means there's no corresponding way to support variadic
      // System V ABI functions on Windows.)
      if ((IsWindows && CC == CC_X86_64SysV) || (!IsWindows && CC == CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_va_start_used_in_wrong_abi_function)
               << !IsWindows;
    }
    return false;
  }

  if (IsMSVAStart)
    return S.Diag(Fn->getBeginLoc(), diag::err_builtin_x64_aarch64_only);
  return false;
}

bool checkVAStartIsInVariadicFunction(Sema &S, Expr *Fn,
                                      ParmVarDecl **LastParam = nullptr) {
  // Determine whether the current function or block is variadic
  // and get its parameter list.
  bool IsVariadic = false;
  llvm::ArrayRef<ParmVarDecl *> Params;
  DeclContext *Caller = S.CurContext;
  if (auto *FD = dyn_cast<FunctionDecl>(Caller)) {
    IsVariadic = FD->isVariadic();
    Params = FD->parameters();
  } else {
    // This must be some other declcontext that parses exprs.
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_outside_function);
    return true;
  }

  if (!IsVariadic) {
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_fixed_function);
    return true;
  }

  if (LastParam)
    *LastParam = Params.empty() ? nullptr : Params.back();

  return false;
}

bool Sema::SemaBuiltinVAStart(unsigned BuiltinID, CallExpr *TheCall) {
  Expr *Fn = TheCall->getCallee();

  if (checkVAStartABI(*this, BuiltinID, Fn))
    return true;

  // In C23 mode, va_start only needs one argument. However, the builtin still
  // requires two arguments (which matches the behavior of the GCC builtin),
  // <stdarg.h> passes `0` as the second argument in C23 mode.
  if (checkArgCount(*this, TheCall, 2))
    return true;

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, TheCall, 0))
    return true;

  ParmVarDecl *LastParam;
  if (checkVAStartIsInVariadicFunction(*this, Fn, &LastParam))
    return true;

  // Verify that the second argument to the builtin is the last argument of the
  // current function. In C23 mode, if the second argument is an
  // integer constant expression with value 0, then we don't bother with this
  // check.
  bool SecondArgIsLastNamedArgument = false;
  const Expr *Arg = TheCall->getArg(1)->IgnoreParenCasts();
  if (std::optional<llvm::APSInt> Val =
          TheCall->getArg(1)->getIntegerConstantExpr(Context);
      Val && LangOpts.C23 && *Val == 0)
    return false;

  // These are valid if SecondArgIsLastNamedArgument is false after the next
  // block.
  QualType Type;
  SourceLocation ParamLoc;
  bool IsCRegister = false;

  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Arg)) {
    if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(DR->getDecl())) {
      SecondArgIsLastNamedArgument = PV == LastParam;

      Type = PV->getType();
      ParamLoc = PV->getLocation();
      IsCRegister = PV->getStorageClass() == SC_Register;
    }
  }

  if (!SecondArgIsLastNamedArgument)
    Diag(TheCall->getArg(1)->getBeginLoc(),
         diag::warn_second_arg_of_va_start_not_last_named_param);
  else if (IsCRegister || Type->isSpecificBuiltinType(BuiltinType::Float) ||
           [=] {
             // Promotable integers are UB, but enumerations need a bit of
             // extra checking to see what their promotable type actually is.
             if (!Context.isPromotableIntegerType(Type))
               return false;
             if (!Type->isEnumeralType())
               return true;
             const EnumDecl *ED = Type->castAs<EnumType>()->getDecl();
             return !(ED &&
                      Context.typesAreCompatible(ED->getPromotionType(), Type));
           }()) {
    unsigned Reason = IsCRegister ? 1 : 0;
    Diag(Arg->getBeginLoc(), diag::warn_va_start_type_is_undefined) << Reason;
    Diag(ParamLoc, diag::note_parameter_type) << Type;
  }

  return false;
}

bool Sema::SemaBuiltinVAStartARMMicrosoft(CallExpr *Call) {
  auto IsSuitablyTypedFormatArgument =
      []([[maybe_unused]] const Expr *Arg) -> bool { return true; };

  // void __va_start(va_list *ap, const char *named_addr, size_t slot_size,
  //                 const char *named_addr);

  Expr *Func = Call->getCallee();

  if (Call->getNumArgs() < 3)
    return Diag(Call->getEndLoc(),
                diag::err_typecheck_call_too_few_args_at_least)
           << 3 << Call->getNumArgs();

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, Call, 0))
    return true;
  if (checkVAStartIsInVariadicFunction(*this, Func))
    return true;

  // __va_start on Windows does not validate the parameter qualifiers

  const Expr *Arg1 = Call->getArg(1)->IgnoreParens();
  const Type *Arg1Ty = Arg1->getType().getCanonicalType().getTypePtr();

  const Expr *Arg2 = Call->getArg(2)->IgnoreParens();
  const Type *Arg2Ty = Arg2->getType().getCanonicalType().getTypePtr();

  const QualType &ConstCharPtrTy =
      Context.getPointerType(Context.CharTy.withConst());
  if (!Arg1Ty->isPointerType() || !IsSuitablyTypedFormatArgument(Arg1))
    Diag(Arg1->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg1->getType() << ConstCharPtrTy << 1 /* different class */
        << 0                                      /* qualifier difference */
        << 3                                      /* parameter mismatch */
        << 2 << Arg1->getType() << ConstCharPtrTy;

  const QualType SizeTy = Context.getSizeType();
  if (Arg2Ty->getCanonicalTypeInternal().withoutLocalFastQualifiers() != SizeTy)
    Diag(Arg2->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg2->getType() << SizeTy << 1 /* different class */
        << 0                              /* qualifier difference */
        << 3                              /* parameter mismatch */
        << 3 << Arg2->getType() << SizeTy;

  return false;
}

bool Sema::SemaBuiltinUnorderedCompare(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  ExprResult OrigArg0 = TheCall->getArg(0);
  ExprResult OrigArg1 = TheCall->getArg(1);

  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res = UsualArithmeticConversions(
      OrigArg0, OrigArg1, TheCall->getExprLoc(), ACK_Comparison);
  if (OrigArg0.isInvalid() || OrigArg1.isInvalid())
    return true;

  // Make sure any conversions are pushed back into the call; this is
  // type safe since unordered compare builtins are declared as "_Bool
  // foo(...)".
  TheCall->setArg(0, OrigArg0.get());
  TheCall->setArg(1, OrigArg1.get());

  // If the common type isn't a real floating type, then the arguments were
  // invalid for this operation.
  if (Res.isNull() || !Res->isRealFloatingType())
    return Diag(OrigArg0.get()->getBeginLoc(),
                diag::err_typecheck_call_invalid_ordered_compare)
           << OrigArg0.get()->getType() << OrigArg1.get()->getType()
           << SourceRange(OrigArg0.get()->getBeginLoc(),
                          OrigArg1.get()->getEndLoc());

  return false;
}

bool Sema::SemaBuiltinFPClassification(CallExpr *TheCall, unsigned NumArgs) {
  if (checkArgCount(*this, TheCall, NumArgs))
    return true;

  bool IsFPClass = NumArgs == 2;

  // Find out position of floating-point argument.
  unsigned FPArgNo = IsFPClass ? 0 : NumArgs - 1;

  // We can count on all parameters preceding the floating-point just being int.
  // Try all of those.
  for (unsigned i = 0; i < FPArgNo; ++i) {
    Expr *Arg = TheCall->getArg(i);

    ExprResult Res = PerformImplicitConversion(Arg, Context.IntTy, AA_Passing);
    if (Res.isInvalid())
      return true;
    TheCall->setArg(i, Res.get());
  }

  Expr *OrigArg = TheCall->getArg(FPArgNo);

  // Usual Unary Conversions will convert half to float, which we want for
  // machines that use fp16 conversion intrinsics. Else, we wnat to leave the
  // type how it is, but do normal L->Rvalue conversions.
  if (Context.getTargetInfo().useFP16ConversionIntrinsics())
    OrigArg = UsualUnaryConversions(OrigArg).get();
  else
    OrigArg = DefaultFunctionArrayLvalueConversion(OrigArg).get();
  TheCall->setArg(FPArgNo, OrigArg);

  QualType VectorResultTy;
  QualType ElementTy = OrigArg->getType();
  if (ElementTy->isVectorType() && IsFPClass) {
    VectorResultTy = GetSignedVectorType(ElementTy);
    ElementTy = ElementTy->getAs<VectorType>()->getElementType();
  }

  // This operation requires a non-_Complex floating-point number.
  if (!ElementTy->isRealFloatingType())
    return Diag(OrigArg->getBeginLoc(),
                diag::err_typecheck_call_invalid_unary_fp)
           << OrigArg->getType() << OrigArg->getSourceRange();

  // __builtin_isfpclass has integer parameter that specify test mask. It is
  // passed in (...), so it should be analyzed completely here.
  if (IsFPClass)
    if (SemaBuiltinConstantArgRange(TheCall, 1, 0, llvm::fcAllFlags))
      return true;

  if (IsFPClass) {
    QualType ResultTy;
    if (!VectorResultTy.isNull())
      ResultTy = VectorResultTy;
    else
      ResultTy = Context.IntTy;
    TheCall->setType(ResultTy);
  }

  return false;
}

bool Sema::SemaBuiltinComplex(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  for (unsigned I = 0; I != 2; ++I) {
    Expr *Arg = TheCall->getArg(I);
    QualType T = Arg->getType();

    // Despite supporting _Complex int, GCC requires a real floating point type
    // for the operands of __builtin_complex.
    if (!T->isRealFloatingType()) {
      return Diag(Arg->getBeginLoc(), diag::err_typecheck_call_requires_real_fp)
             << Arg->getType() << Arg->getSourceRange();
    }

    ExprResult Converted = DefaultLvalueConversion(Arg);
    if (Converted.isInvalid())
      return true;
    TheCall->setArg(I, Converted.get());
  }

  Expr *Real = TheCall->getArg(0);
  Expr *Imag = TheCall->getArg(1);
  if (!Context.hasSameType(Real->getType(), Imag->getType())) {
    return Diag(Real->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << Real->getType() << Imag->getType() << Real->getSourceRange()
           << Imag->getSourceRange();
  }

  // We don't allow _Complex _Float16 nor _Complex __fp16 as type specifiers;
  // don't allow this builtin to form those types either.
  if (Real->getType()->isFloat16Type())
    return Diag(TheCall->getBeginLoc(), diag::err_invalid_complex_spec)
           << tok::getKeywordSpelling(tok::kw__Float16);
  if (Real->getType()->isHalfType())
    return Diag(TheCall->getBeginLoc(), diag::err_invalid_complex_spec)
           << "half";

  TheCall->setType(Context.getComplexType(Real->getType()));
  return false;
}

// Customized Sema Checking for VSX builtins that have the following signature:
// vector [...] builtinName(vector [...], vector [...], const int);
// Which takes the same type of vectors (any legal vector type) for the first
// two arguments and takes compile time constant for the third argument.
// Example builtins are :
// This is declared to take (...), so we have to check everything.
ExprResult Sema::SemaBuiltinShuffleVector(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 2)
    return ExprError(Diag(TheCall->getEndLoc(),
                          diag::err_typecheck_call_too_few_args_at_least)
                     << 2 << TheCall->getNumArgs()
                     << TheCall->getSourceRange());

  // Determine which of the following types of shufflevector we're checking:
  // 1) unary, vector mask: (lhs, mask)
  // 2) binary, scalar mask: (lhs, rhs, index, ..., index)
  QualType resType = TheCall->getArg(0)->getType();
  unsigned numElements = 0;

  {
    QualType LHSType = TheCall->getArg(0)->getType();
    QualType RHSType = TheCall->getArg(1)->getType();

    if (!LHSType->isVectorType() || !RHSType->isVectorType())
      return ExprError(
          Diag(TheCall->getBeginLoc(), diag::err_vec_builtin_non_vector)
          << TheCall->getDirectCallee()
          << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                         TheCall->getArg(1)->getEndLoc()));

    numElements = LHSType->castAs<VectorType>()->getNumElements();
    unsigned numResElements = TheCall->getNumArgs() - 2;

    // Check to see if we have a call with 2 vector arguments, the unary shuffle
    // with mask.  If so, verify that RHS is an integer vector type with the
    // same number of elts as lhs.
    if (TheCall->getNumArgs() == 2) {
      if (!RHSType->hasIntegerRepresentation() ||
          RHSType->castAs<VectorType>()->getNumElements() != numElements)
        return ExprError(Diag(TheCall->getBeginLoc(),
                              diag::err_vec_builtin_incompatible_vector)
                         << TheCall->getDirectCallee()
                         << SourceRange(TheCall->getArg(1)->getBeginLoc(),
                                        TheCall->getArg(1)->getEndLoc()));
    } else if (!Context.hasSameUnqualifiedType(LHSType, RHSType)) {
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_vec_builtin_incompatible_vector)
                       << TheCall->getDirectCallee()
                       << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                                      TheCall->getArg(1)->getEndLoc()));
    } else if (numElements != numResElements) {
      QualType eltType = LHSType->castAs<VectorType>()->getElementType();
      resType =
          Context.getVectorType(eltType, numResElements, VectorKind::Generic);
    }
  }

  for (unsigned i = 2; i < TheCall->getNumArgs(); i++) {

    std::optional<llvm::APSInt> Result;
    if (!(Result = TheCall->getArg(i)->getIntegerConstantExpr(Context)))
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_nonconstant_argument)
                       << TheCall->getArg(i)->getSourceRange());

    // Allow -1 which will be translated to undef in the IR.
    if (Result->isSigned() && Result->isAllOnes())
      continue;

    if (Result->getActiveBits() > 64 ||
        Result->getZExtValue() >= numElements * 2)
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_argument_too_large)
                       << TheCall->getArg(i)->getSourceRange());
  }

  llvm::SmallVector<Expr *, 32> exprs;

  for (unsigned i = 0, e = TheCall->getNumArgs(); i != e; i++) {
    exprs.push_back(TheCall->getArg(i));
    TheCall->setArg(i, nullptr);
  }

  return new (Context) ShuffleVectorExpr(Context, exprs, resType,
                                         TheCall->getCallee()->getBeginLoc(),
                                         TheCall->getRParenLoc());
}

ExprResult Sema::SemaConvertVectorExpr(Expr *E, TypeSourceInfo *TInfo,
                                       SourceLocation BuiltinLoc,
                                       SourceLocation RParenLoc) {
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType DstTy = TInfo->getType();
  QualType SrcTy = E->getType();

  if (!SrcTy->isVectorType())
    return ExprError(Diag(BuiltinLoc, diag::err_convertvector_non_vector)
                     << E->getSourceRange());
  if (!DstTy->isVectorType())
    return ExprError(Diag(BuiltinLoc, diag::err_builtin_non_vector_type)
                     << "second"
                     << "__builtin_convertvector");

  {
    unsigned SrcElts = SrcTy->castAs<VectorType>()->getNumElements();
    unsigned DstElts = DstTy->castAs<VectorType>()->getNumElements();
    if (SrcElts != DstElts)
      return ExprError(
          Diag(BuiltinLoc, diag::err_convertvector_incompatible_vector)
          << E->getSourceRange());
  }

  return new (Context)
      ConvertVectorExpr(E, TInfo, DstTy, VK, OK, BuiltinLoc, RParenLoc);
}

// This is declared to take (const void*, ...) and can take two
// optional constant int args.
bool Sema::SemaBuiltinPrefetch(CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs > 3)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 3 << NumArgs << TheCall->getSourceRange();

  // Argument 0 is checked for us and the remaining arguments must be
  // constant integers.
  for (unsigned i = 1; i != NumArgs; ++i)
    if (SemaBuiltinConstantArgRange(TheCall, i, 0, i == 1 ? 1 : 3))
      return true;

  return false;
}

bool Sema::SemaBuiltinArithmeticFence(CallExpr *TheCall) {
  if (!Context.getTargetInfo().checkArithmeticFenceSupported())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  if (checkArgCount(*this, TheCall, 1))
    return true;
  Expr *Arg = TheCall->getArg(0);
  QualType ArgTy = Arg->getType();
  if (!ArgTy->hasFloatingRepresentation())
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_expect_flt_or_vector)
           << ArgTy;
  if (Arg->isLValue()) {
    ExprResult FirstArg = DefaultLvalueConversion(Arg);
    TheCall->setArg(0, FirstArg.get());
  }
  TheCall->setType(TheCall->getArg(0)->getType());
  return false;
}

// __assume does not evaluate its arguments, and should warn if its argument
// has side effects.
bool Sema::SemaBuiltinAssume(CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);
  if (Arg->HasSideEffects(Context))
    Diag(Arg->getBeginLoc(), diag::warn_assume_side_effects)
        << Arg->getSourceRange()
        << cast<FunctionDecl>(TheCall->getCalleeDecl())->getIdentifier();

  return false;
}

bool Sema::SemaBuiltinAllocaWithAlign(CallExpr *TheCall) {
  // The alignment must be a constant integer.
  Expr *Arg = TheCall->getArg(1);

  {
    if (const auto *UE =
            dyn_cast<UnaryExprOrTypeTraitExpr>(Arg->IgnoreParenImpCasts()))
      if (UE->getKind() == UETT_AlignOf ||
          UE->getKind() == UETT_PreferredAlignOf)
        Diag(TheCall->getBeginLoc(), diag::warn_alloca_align_alignof)
            << Arg->getSourceRange();

    llvm::APSInt Result = Arg->EvaluateKnownConstInt(Context);

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << Arg->getSourceRange();

    if (Result < Context.getCharWidth())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_small)
             << (unsigned)Context.getCharWidth() << Arg->getSourceRange();

    if (Result > std::numeric_limits<int32_t>::max())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_big)
             << std::numeric_limits<int32_t>::max() << Arg->getSourceRange();
  }

  return false;
}

bool Sema::SemaBuiltinAssumeAligned(CallExpr *TheCall) {
  if (checkArgCountRange(*this, TheCall, 2, 3))
    return true;

  unsigned NumArgs = TheCall->getNumArgs();
  Expr *FirstArg = TheCall->getArg(0);

  {
    ExprResult FirstArgResult = DefaultFunctionArrayLvalueConversion(FirstArg);
    if (checkBuiltinArgument(*this, TheCall, 0))
      return true;
    /// In-place updation of FirstArg by checkBuiltinArgument is ignored.
    TheCall->setArg(0, FirstArgResult.get());
  }

  // The alignment must be a constant integer.
  Expr *SecondArg = TheCall->getArg(1);

  {
    llvm::APSInt Result;
    if (SemaBuiltinConstantArg(TheCall, 1, Result))
      return true;

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << SecondArg->getSourceRange();

    if (Result > Sema::MaximumAlignment)
      Diag(TheCall->getBeginLoc(), diag::warn_assume_aligned_too_great)
          << SecondArg->getSourceRange() << Sema::MaximumAlignment;
  }

  if (NumArgs > 2) {
    Expr *ThirdArg = TheCall->getArg(2);
    if (convertArgumentToType(*this, ThirdArg, Context.getSizeType()))
      return true;
    TheCall->setArg(2, ThirdArg);
  }

  return false;
}

bool Sema::SemaBuiltinOSLogFormat(CallExpr *TheCall) {
  unsigned BuiltinID =
      cast<FunctionDecl>(TheCall->getCalleeDecl())->getBuiltinID();
  bool IsSizeCall = BuiltinID == Builtin::BI__builtin_os_log_format_buffer_size;

  unsigned NumArgs = TheCall->getNumArgs();
  unsigned NumRequiredArgs = IsSizeCall ? 1 : 2;
  if (NumArgs < NumRequiredArgs) {
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << NumRequiredArgs << NumArgs << TheCall->getSourceRange();
  }
  if (NumArgs >= NumRequiredArgs + 0x100) {
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << (NumRequiredArgs + 0xff) << NumArgs << TheCall->getSourceRange();
  }
  unsigned i = 0;

  // For formatting call, check buffer arg.
  if (!IsSizeCall) {
    ExprResult Arg(TheCall->getArg(i));
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        Context, Context.VoidPtrTy, false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }
  unsigned FormatIdx = i;
  {
    ExprResult Arg = CheckOSLogFormatStringArg(TheCall->getArg(i));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Make sure variadic args are scalar.
  unsigned FirstDataArg = i;
  while (i < NumArgs) {
    ExprResult Arg = DefaultVariadicArgumentPromotion(TheCall->getArg(i));
    if (Arg.isInvalid())
      return true;
    CharUnits ArgSize = Context.getTypeSizeInChars(Arg.get()->getType());
    if (ArgSize.getQuantity() >= 0x100) {
      return Diag(Arg.get()->getEndLoc(), diag::err_os_log_argument_too_big)
             << i << (int)ArgSize.getQuantity() << 0xff
             << TheCall->getSourceRange();
    }
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Check formatting specifiers. NOTE: We're only doing this for the non-size
  // call to avoid duplicate diagnostics.
  if (!IsSizeCall) {
    llvm::SmallBitVector CheckedVarArgs(NumArgs, false);
    llvm::ArrayRef<const Expr *> Args(TheCall->getArgs(),
                                      TheCall->getNumArgs());
    bool Success = CheckFormatArguments(
        Args, FAPK_Variadic, FormatIdx, FirstDataArg, FST_OSLog,
        VariadicFunction, TheCall->getBeginLoc(), SourceRange(),
        CheckedVarArgs);
    if (!Success)
      return true;
  }

  if (IsSizeCall) {
    TheCall->setType(Context.getSizeType());
  } else {
    TheCall->setType(Context.VoidPtrTy);
  }
  return false;
}

bool Sema::SemaBuiltinConstantArg(CallExpr *TheCall, int ArgNum,
                                  llvm::APSInt &Result) {
  Expr *Arg = TheCall->getArg(ArgNum);
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  std::optional<llvm::APSInt> R;
  if (!(R = Arg->getIntegerConstantExpr(Context)))
    return Diag(TheCall->getBeginLoc(), diag::err_constant_integer_arg_type)
           << FDecl->getDeclName() << Arg->getSourceRange();
  Result = *R;
  return false;
}

bool Sema::SemaBuiltinConstantArgRange(CallExpr *TheCall, int ArgNum, int Low,
                                       int High, bool RangeIsError) {
  if (isConstantEvaluatedContext())
    return false;
  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() < Low || Result.getSExtValue() > High) {
    if (RangeIsError)
      return Diag(TheCall->getBeginLoc(), diag::err_argument_invalid_range)
             << toString(Result, 10) << Low << High << Arg->getSourceRange();
    else
      // Defer the warning until we know if the code will be emitted so that
      // dead code can ignore this.
      DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                          PDiag(diag::warn_argument_invalid_range)
                              << toString(Result, 10) << Low << High
                              << Arg->getSourceRange());
  }

  return false;
}

bool Sema::SemaBuiltinConstantArgMultiple(CallExpr *TheCall, int ArgNum,
                                          unsigned Num) {
  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() % Num != 0)
    return Diag(TheCall->getBeginLoc(), diag::err_argument_not_multiple)
           << Num << Arg->getSourceRange();

  return false;
}

bool Sema::SemaBuiltinConstantArgPower2(CallExpr *TheCall, int ArgNum) {
  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Bit-twiddling to test for a power of 2: for x > 0, x & (x-1) is zero if
  // and only if x is a power of 2.
  if (Result.isStrictlyPositive() && (Result & (Result - 1)) == 0)
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_argument_not_power_of_2)
         << Arg->getSourceRange();
}


bool Sema::SemaBuiltinLongjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());

  Expr *Arg = TheCall->getArg(1);
  llvm::APSInt Result;

  if (SemaBuiltinConstantArg(TheCall, 1, Result))
    return true;

  if (Result != 1)
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_invalid_val)
           << SourceRange(Arg->getBeginLoc(), Arg->getEndLoc());

  return false;
}

bool Sema::SemaBuiltinSetjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_setjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  return false;
}
