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
// Declaration usage & implicit conversions
// ===----------------------------------------------------------------------===

bool Sema::CanUseDecl(NamedDecl *D, bool TreatUnavailableAsInvalid) {
  if (ParsingInitForAutoVars.contains(D))
    return false;

  if (TreatUnavailableAsInvalid && D->getAvailability() == AR_Unavailable &&
      cast<Decl>(CurContext)->getAvailability() != AR_Unavailable)
    return false;

  return true;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnIfMarkedUnused(Sema &S, NamedDecl *D, SourceLocation Loc) {
  if (const auto *A = D->getAttr<UnusedAttr>()) {
    if (A->getSemanticSpelling() != UnusedAttr::Bracket_maybe_unused &&
        A->getSemanticSpelling() != UnusedAttr::C23_maybe_unused) {
      const Decl *DC = cast_or_null<Decl>(S.getCurLexicalContext());
      if (DC && !DC->hasAttr<UnusedAttr>())
        S.Diag(Loc, diag::warn_used_but_marked_unused) << D;
    }
  }
}
} // namespace

namespace {
bool hasDeclaredStorage(const FunctionDecl *D) {
  for (auto *I : D->redecls()) {
    if (I->getStorageClass() != SC_None)
      return true;
  }
  return false;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnInternalInInline(Sema &S, const NamedDecl *D, SourceLocation Loc) {
  const llvm::Triple &Triple = S.Context.getTargetInfo().getTriple();
  if (Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
    return;
  FunctionDecl *Current = S.getCurFunctionDecl();
  if (!Current)
    return;
  if (!Current->isInlined())
    return;
  if (!Current->isExternallyVisible())
    return;

  if (D->getFormalLinkage() != Linkage::Internal)
    return;

  const FunctionDecl *UsedFn = dyn_cast<FunctionDecl>(D);
  bool DowngradeWarning = S.getSourceManager().isInMainFile(Loc);
  if (!DowngradeWarning && UsedFn)
    DowngradeWarning = UsedFn->isInlined() || UsedFn->hasAttr<ConstAttr>();

  S.Diag(Loc, DowngradeWarning ? diag::ext_internal_in_extern_inline_quiet
                               : diag::ext_internal_in_extern_inline)
      << /*IsVar=*/!UsedFn << D;

  S.MaybeSuggestAddingStaticToDecl(Current);

  S.Diag(D->getCanonicalDecl()->getLocation(), diag::note_entity_declared_at)
      << D;
}
} // namespace

void Sema::MaybeSuggestAddingStaticToDecl(const FunctionDecl *Cur) {
  const FunctionDecl *First = Cur->getFirstDecl();

  if (!hasDeclaredStorage(First)) {
    SourceLocation DeclBegin = First->getSourceRange().getBegin();
#ifndef _WIN32
    llvm::SmallString<16> StaticPrefix(tok::getKeywordSpelling(tok::kw_static));
    StaticPrefix += ' ';
    Diag(DeclBegin, diag::note_convert_inline_to_static)
        << Cur << FixItHint::CreateInsertion(DeclBegin, StaticPrefix);
#endif
  }
}

bool Sema::CheckDeclUsage(NamedDecl *D, llvm::ArrayRef<SourceLocation> Locs) {
  SourceLocation Loc = Locs.front();

  if (LLVM_UNLIKELY(!ParsingInitForAutoVars.empty() &&
                    ParsingInitForAutoVars.contains(D))) {
    Diag(Loc, diag::err_auto_variable_cannot_appear_in_own_initializer)
        << D->getDeclName() << cast<VarDecl>(D)->getType();
    return true;
  }

  if (LLVM_LIKELY(!D->hasAttrs())) {
    if (LLVM_LIKELY(!Diags.getIgnoreAllWarnings())) {
      FunctionDecl *CurFn = getCurFunctionDecl();
      if (LLVM_UNLIKELY(CurFn && CurFn->isInlined()))
        warnInternalInInline(*this, D, Loc);
    }
    if (auto *VD = dyn_cast<ValueDecl>(D)) {
      QualType Ty = VD->getType();
      if (LLVM_UNLIKELY(!Ty->isIntegerType() && !Ty->isPointerType() &&
                        !Ty->isRealFloatingType()))
        checkTypeSupport(Ty, Loc, VD);
    }
    return false;
  }

  if (diagnoseArgIndependentDiagnoseIfAttrs(D, Loc))
    return true;

  DiagnoseAvailabilityOfDecl(D, Locs);

  if (LLVM_LIKELY(!Diags.getIgnoreAllWarnings())) {
    warnIfMarkedUnused(*this, D, Loc);
    warnInternalInInline(*this, D, Loc);
  }

  if (D->hasAttr<AvailableOnlyInDefaultEvalMethodAttr>()) {
    if (getLangOpts().getFPEvalMethod() !=
            LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine &&
        PP.getLastFPEvalPragmaLocation().isValid() &&
        PP.getCurrentFPEvalMethod() != getLangOpts().getFPEvalMethod())
      Diag(D->getLocation(),
           diag::err_type_available_only_in_default_eval_method)
          << D->getName();
  }

  if (auto *VD = dyn_cast<ValueDecl>(D))
    checkTypeSupport(VD->getType(), Loc, VD);

  return false;
}

void Sema::CheckSentinelArgs(const NamedDecl *D, SourceLocation Loc,
                             llvm::ArrayRef<Expr *> Args) {
  const SentinelAttr *Attr = D->getAttr<SentinelAttr>();
  if (!Attr)
    return;

  // The number of formal parameters of the declaration.
  unsigned NumFormalParams;

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    NumFormalParams = FD->param_size();
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    QualType Ty = VD->getType();
    const FunctionType *Fn = nullptr;
    if (const auto *PtrTy = Ty->getAs<PointerType>()) {
      Fn = PtrTy->getPointeeType()->getAs<FunctionType>();
      if (!Fn)
        return;
    } else {
      return;
    }

    if (const auto *proto = dyn_cast<FunctionProtoType>(Fn))
      NumFormalParams = proto->getNumParams();
    else
      NumFormalParams = 0;
  } else {
    return;
  }

  unsigned NullPos = Attr->getNullPos();
  assert((NullPos == 0 || NullPos == 1) && "invalid null position on sentinel");
  NumFormalParams = (NullPos > NumFormalParams ? 0 : NumFormalParams - NullPos);

  unsigned NumArgsAfterSentinel = Attr->getSentinel();

  if (Args.size() < NumFormalParams + NumArgsAfterSentinel + 1) {
    Diag(Loc, diag::warn_not_enough_argument) << D->getDeclName();
    Diag(D->getLocation(), diag::note_sentinel_here);
    return;
  }

  const Expr *SentinelExpr = Args[Args.size() - NumArgsAfterSentinel - 1];
  if (!SentinelExpr)
    return;
  if (Context.isSentinelNullExpr(SentinelExpr))
    return;

  SourceLocation MissingNullLoc =
      getLocForEndOfToken(SentinelExpr->getEndLoc());
  std::string NullValue;
  if (PP.isMacroDefined("NULL"))
    NullValue = "NULL";
  else
    NullValue = "(void*) 0";

  if (MissingNullLoc.isInvalid())
    Diag(Loc, diag::warn_missing_sentinel);
  else
    Diag(MissingNullLoc, diag::warn_missing_sentinel)
        << FixItHint::CreateInsertion(MissingNullLoc, ", " + NullValue);
  Diag(D->getLocation(), diag::note_sentinel_here) << Attr->getRange();
}

SourceRange Sema::getExprRange(Expr *E) const {
  return E ? E->getSourceRange() : SourceRange();
}

ExprResult Sema::DefaultFunctionArrayConversion(Expr *E, bool Diagnose) {
  if (LLVM_UNLIKELY(E->hasPlaceholderType())) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid())
      return ExprError();
    E = result.get();
  }

  QualType Ty = E->getType();
  assert(!Ty.isNull() && "DefaultFunctionArrayConversion - missing type");

  if (LLVM_UNLIKELY(Ty->isFunctionType())) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts()))
      if (auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl()))
        if (!checkAddressOfFunctionIsAvailable(FD, Diagnose, E->getExprLoc()))
          return ExprError();

    E = ImpCastExprToType(E, Context.getPointerType(Ty),
                          CK_FunctionToPointerDecay)
            .get();
  } else if (LLVM_UNLIKELY(Ty->isArrayType())) {
    // C90 requires lvalue; C99+ allows any expression for array-to-pointer.
    if (getLangOpts().C99 || E->isLValue()) {
      ExprResult Res = ImpCastExprToType(E, Context.getArrayDecayedType(Ty),
                                         CK_ArrayToPointerDecay);
      if (Res.isInvalid())
        return ExprError();
      E = Res.get();
    }
  }
  return E;
}

ExprResult Sema::DefaultLvalueConversion(Expr *E) {
  if (LLVM_UNLIKELY(E->hasPlaceholderType())) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid())
      return ExprError();
    E = result.get();
  }

  if (LLVM_LIKELY(!E->isLValue()))
    return E;

  QualType T = E->getType();
  assert(!T.isNull() && "r-value conversion on typeless expression?");

  // Fast path: unqualified builtin type on
  // DeclRefExpr/MemberExpr/ArraySubscriptExpr. These are ~90% of all
  // lvalue-to-rvalue conversions. Safe to skip:
  //   - function/array/void type checks (impossible for builtin)
  //   - warnNullPtrDeref (no-op for these expression kinds)
  //   - CheckLValueToRValueConversionOperand (no-op for DeclRefExpr, trivial
  //   for others)
  //   - isDestructedType (always false for builtin)
  //   - isNullPtrType (CK_LValueToRValue)
  //   - AtomicType check (never atomic for plain builtin)
  if (LLVM_LIKELY(T->isBuiltinType() && !T.hasQualifiers()) &&
      isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(E)) {
    return ImplicitCastExpr::Create(Context, T, CK_LValueToRValue, E,
                                    VK_PRValue, CurFPFeatureOverrides());
  }

  if (LLVM_UNLIKELY(T->isFunctionType() || T->isArrayType()))
    return E;

  if (LLVM_UNLIKELY(T->isVoidType()))
    return E;

  if (LLVM_UNLIKELY((!isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(E))))
    warnNullPtrDeref(*this, E);

  if (T.hasQualifiers())
    T = T.getUnqualifiedType();

  ExprResult Res = CheckLValueToRValueConversionOperand(E);
  if (Res.isInvalid())
    return Res;
  E = Res.get();

  if (E->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
    Cleanup.setExprNeedsCleanups(true);

  CastKind CK = T->isNullPtrType() ? CK_NullToPointer : CK_LValueToRValue;
  Res = ImplicitCastExpr::Create(Context, T, CK, E, VK_PRValue,
                                 CurFPFeatureOverrides());

  if (const AtomicType *Atomic = T->getAs<AtomicType>()) {
    T = Atomic->getValueType().getUnqualifiedType();
    Res = ImplicitCastExpr::Create(Context, T, CK_AtomicToNonAtomic, Res.get(),
                                   VK_PRValue, FPOptionsOverride());
  }

  return Res;
}

ExprResult Sema::DefaultFunctionArrayLvalueConversion(Expr *E, bool Diagnose) {
  if (LLVM_LIKELY(!E->hasPlaceholderType())) {
    QualType Ty = E->getType();
    if (LLVM_LIKELY(!Ty->isFunctionType() && !Ty->isArrayType() &&
                    !E->isLValue()))
      return E;
  }
  ExprResult Res = DefaultFunctionArrayConversion(E, Diagnose);
  if (LLVM_UNLIKELY(Res.isInvalid()))
    return ExprError();
  Res = DefaultLvalueConversion(Res.get());
  if (LLVM_UNLIKELY(Res.isInvalid()))
    return ExprError();
  return Res;
}

ExprResult Sema::CallExprUnaryConversions(Expr *E) {
  QualType Ty = E->getType();
  ExprResult Res = E;
  if (LLVM_UNLIKELY(Ty->isFunctionType())) {
    Res = ImpCastExprToType(E, Context.getPointerType(Ty),
                            CK_FunctionToPointerDecay);
    if (LLVM_UNLIKELY(Res.isInvalid()))
      return ExprError();
  }
  Res = DefaultLvalueConversion(Res.get());
  if (LLVM_UNLIKELY(Res.isInvalid()))
    return ExprError();
  return Res.get();
}

ExprResult Sema::UsualUnaryConversions(Expr *E) {
  ExprResult Res = DefaultFunctionArrayLvalueConversion(E);
  if (LLVM_UNLIKELY(Res.isInvalid()))
    return ExprError();
  E = Res.get();

  QualType Ty = E->getType();
  assert(!Ty.isNull() && "UsualUnaryConversions - missing type");

  if (const auto *BT = Ty->getAs<BuiltinType>()) {
    unsigned K = BT->getKind();
    if (LLVM_LIKELY(K == BuiltinType::Int || K == BuiltinType::UInt ||
                    K == BuiltinType::Long || K == BuiltinType::ULong ||
                    K == BuiltinType::LongLong || K == BuiltinType::ULongLong))
      return E;
  }

  LangOptions::FPEvalMethodKind EvalMethod = CurFPFeatures.getFPEvalMethod();
  if (EvalMethod != LangOptions::FEM_Source && Ty->isFloatingType() &&
      (getLangOpts().getFPEvalMethod() !=
           LangOptions::FPEvalMethodKind::FEM_UnsetOnCommandLine ||
       PP.getLastFPEvalPragmaLocation().isValid())) {
    switch (EvalMethod) {
    default:
      llvm_unreachable("Unrecognized float evaluation method");
      break;
    case LangOptions::FEM_UnsetOnCommandLine:
      llvm_unreachable("Float evaluation method should be set by now");
      break;
    case LangOptions::FEM_Double:
      if (Context.getFloatingTypeOrder(Context.DoubleTy, Ty) > 0)
        // Widen the expression to double.
        return Ty->isComplexType()
                   ? ImpCastExprToType(E,
                                       Context.getComplexType(Context.DoubleTy),
                                       CK_FloatingComplexCast)
                   : ImpCastExprToType(E, Context.DoubleTy, CK_FloatingCast);
      break;
    case LangOptions::FEM_Extended:
      if (Context.getFloatingTypeOrder(Context.LongDoubleTy, Ty) > 0)
        // Widen the expression to long double.
        return Ty->isComplexType()
                   ? ImpCastExprToType(
                         E, Context.getComplexType(Context.LongDoubleTy),
                         CK_FloatingComplexCast)
                   : ImpCastExprToType(E, Context.LongDoubleTy,
                                       CK_FloatingCast);
      break;
    }
  }

  // Half FP have to be promoted to float unless it is natively supported
  if (Ty->isHalfType() && !getLangOpts().NativeHalfType)
    return ImpCastExprToType(Res.get(), Context.FloatTy, CK_FloatingCast);

  // Try to perform integral promotions if the object has a theoretically
  // promotable type.
  if (Ty->isIntegralOrUnscopedEnumerationType()) {
    // Integer promotions: promote sub-int types and bit-fields to int
    // (or unsigned int if int cannot represent all values).

    QualType PTy = Context.isPromotableBitField(E);
    if (!PTy.isNull()) {
      E = ImpCastExprToType(E, PTy, CK_IntegralCast).get();
      return E;
    }
    if (Context.isPromotableIntegerType(Ty)) {
      QualType PT = Context.getPromotedIntegerType(Ty);
      E = ImpCastExprToType(E, PT, CK_IntegralCast).get();
      return E;
    }
  }
  return E;
}

ExprResult Sema::DefaultArgumentPromotion(Expr *E) {
  QualType Ty = E->getType();
  assert(!Ty.isNull() && "DefaultArgumentPromotion - missing type");

  ExprResult Res = UsualUnaryConversions(E);
  if (LLVM_UNLIKELY(Res.isInvalid()))
    return ExprError();
  E = Res.get();

  // If this is a 'float'  or '__fp16' (CVR qualified or typedef)
  // promote to double.
  // Note that default argument promotion applies only to float (and
  // half/fp16); it does not apply to _Float16.
  const BuiltinType *BTy = Ty->getAs<BuiltinType>();
  if (BTy && (BTy->getKind() == BuiltinType::Half ||
              BTy->getKind() == BuiltinType::Float)) {
    E = ImpCastExprToType(E, Context.DoubleTy, CK_FloatingCast).get();
  }
  if (BTy &&
      getLangOpts().getExtendIntArgs() ==
          LangOptions::ExtendArgsKind::ExtendTo64 &&
      Context.getTargetInfo().supportsExtendIntArgs() && Ty->isIntegerType() &&
      Context.getTypeSizeInChars(BTy) <
          Context.getTypeSizeInChars(Context.LongLongTy)) {
    E = (Ty->isUnsignedIntegerType())
            ? ImpCastExprToType(E, Context.UnsignedLongLongTy, CK_IntegralCast)
                  .get()
            : ImpCastExprToType(E, Context.LongLongTy, CK_IntegralCast).get();
    assert(8 == Context.getTypeSizeInChars(Context.LongLongTy).getQuantity() &&
           "Unexpected typesize for LongLongTy");
  }

  return E;
}

Sema::VarArgKind Sema::isValidVarArgType(const QualType &Ty) {
  if (Ty->isIncompleteType()) {
    // After decay, `void` is invalid for a vararg; other incomplete types are
    // ruled out below.
    if (Ty->isVoidType())
      return VAK_Invalid;

    return VAK_Valid;
  }

  if (Ty.isDestructedType() == QualType::DK_nontrivial_c_struct)
    return VAK_Invalid;

  if (Ty.isPODType(Context))
    return VAK_Valid;

  if (getLangOpts().MSVCCompat)
    return VAK_MSVCUndefined;

  return VAK_Undefined;
}

void Sema::checkVariadicArgument(const Expr *E) {
  const QualType &Ty = E->getType();
  VarArgKind VAK = isValidVarArgType(Ty);

  switch (VAK) {
  case VAK_Valid:
    break;

  case VAK_Undefined:
  case VAK_MSVCUndefined:
    DiagRuntimeBehavior(E->getBeginLoc(), nullptr,
                        PDiag(diag::warn_cannot_pass_non_pod_arg_to_vararg)
                            << false << Ty);
    break;

  case VAK_Invalid:
    if (Ty.isDestructedType() == QualType::DK_nontrivial_c_struct)
      Diag(E->getBeginLoc(),
           diag::err_cannot_pass_non_trivial_c_struct_to_vararg)
          << Ty;
    else
      Diag(E->getBeginLoc(), diag::err_cannot_pass_to_vararg)
          << isa<InitListExpr>(E) << Ty;
    break;
  }
}

ExprResult Sema::DefaultVariadicArgumentPromotion(Expr *E) {
  if (LLVM_UNLIKELY(E->getType()->getAsPlaceholderType())) {
    ExprResult ExprRes = CheckPlaceholderExpr(E);
    if (ExprRes.isInvalid())
      return ExprError();
    E = ExprRes.get();
  }

  ExprResult ExprRes = DefaultArgumentPromotion(E);
  if (LLVM_UNLIKELY(ExprRes.isInvalid()))
    return ExprError();

  E = ExprRes.get();

  // Diagnostics for unsafe vararg argument types are emitted along with format
  // string checking in Sema::CheckFunctionCall().
  if (isValidVarArgType(E->getType()) == VAK_Undefined) {
    // Turn this into a trap.
    UnqualifiedId Name;
    Name.setIdentifier(PP.getIdentifierInfo("__builtin_trap"),
                       E->getBeginLoc());
    ExprResult TrapFn = OnIdExpression(TUScope, Name,
                                       /*HasTrailingLParen=*/true,
                                       /*IsAddressOfOperand=*/false);
    if (TrapFn.isInvalid())
      return ExprError();

    ExprResult Call = FormCallExpr(TUScope, TrapFn.get(), E->getBeginLoc(),
                                   std::nullopt, E->getEndLoc());
    if (Call.isInvalid())
      return ExprError();

    ExprResult Comma =
        OnBinOp(TUScope, E->getBeginLoc(), tok::comma, Call.get(), E);
    if (Comma.isInvalid())
      return ExprError();
    return Comma.get();
  }

  if (RequireCompleteType(E->getExprLoc(), E->getType(),
                          diag::err_call_incomplete_argument))
    return ExprError();

  return E;
}

namespace {
bool promoteIntToComplexFloat(Sema &S, ExprResult &IntExpr,
                              ExprResult &ComplexExpr, QualType IntTy,
                              QualType ComplexTy, bool SkipCast) {
  if (IntTy->isComplexType() || IntTy->isRealFloatingType())
    return true;
  if (SkipCast)
    return false;
  if (IntTy->isIntegerType()) {
    QualType fpTy = ComplexTy->castAs<ComplexType>()->getElementType();
    IntExpr = S.ImpCastExprToType(IntExpr.get(), fpTy, CK_IntegralToFloating);
    IntExpr =
        S.ImpCastExprToType(IntExpr.get(), ComplexTy, CK_FloatingRealToComplex);
  } else {
    assert(IntTy->isComplexIntegerType());
    IntExpr = S.ImpCastExprToType(IntExpr.get(), ComplexTy,
                                  CK_IntegralComplexToFloatingComplex);
  }
  return false;
}
} // namespace

// Promote the shorter operand to match the longer type's precision,
// preserving real/complex domain.
namespace {
QualType balanceComplexFloat(Sema &S, ExprResult &Shorter, QualType ShorterType,
                             QualType LongerType, bool PromotePrecision) {
  bool LongerIsComplex = isa<ComplexType>(LongerType.getCanonicalType());
  QualType Result =
      LongerIsComplex ? LongerType : S.Context.getComplexType(LongerType);

  if (PromotePrecision) {
    if (isa<ComplexType>(ShorterType.getCanonicalType())) {
      Shorter =
          S.ImpCastExprToType(Shorter.get(), Result, CK_FloatingComplexCast);
    } else {
      if (LongerIsComplex)
        LongerType = LongerType->castAs<ComplexType>()->getElementType();
      Shorter = S.ImpCastExprToType(Shorter.get(), LongerType, CK_FloatingCast);
    }
  }
  return Result;
}
} // namespace

namespace {
QualType balanceComplexTypes(Sema &S, ExprResult &LHS, ExprResult &RHS,
                             QualType LHSType, QualType RHSType,
                             bool IsCompAssign) {
  // if we have an integer operand, the result is the complex type.
  if (!promoteIntToComplexFloat(S, RHS, LHS, RHSType, LHSType,
                                /*SkipCast=*/false))
    return LHSType;
  if (!promoteIntToComplexFloat(S, LHS, RHS, LHSType, RHSType,
                                /*SkipCast=*/IsCompAssign))
    return RHSType;

  // Compute the rank of the two types, regardless of whether they are complex.
  int Order = S.Context.getFloatingTypeOrder(LHSType, RHSType);
  if (Order < 0)
    // Promote the precision of the LHS if not an assignment.
    return balanceComplexFloat(S, LHS, LHSType, RHSType,
                               /*PromotePrecision=*/!IsCompAssign);
  // Promote the precision of the RHS unless it is already the same as the LHS.
  return balanceComplexFloat(S, RHS, RHSType, LHSType,
                             /*PromotePrecision=*/Order > 0);
}
} // namespace

namespace neverc {
QualType promoteIntToFloat(Sema &S, ExprResult &FloatExpr, ExprResult &IntExpr,
                           QualType FloatTy, QualType IntTy, bool ConvertFloat,
                           bool ConvertInt) {
  if (IntTy->isIntegerType()) {
    if (ConvertInt)
      // Convert intExpr to the lhs floating point type.
      IntExpr =
          S.ImpCastExprToType(IntExpr.get(), FloatTy, CK_IntegralToFloating);
    return FloatTy;
  }

  // Convert both sides to the appropriate complex float.
  assert(IntTy->isComplexIntegerType());
  QualType result = S.Context.getComplexType(FloatTy);

  // _Complex int -> _Complex float
  if (ConvertInt)
    IntExpr = S.ImpCastExprToType(IntExpr.get(), result,
                                  CK_IntegralComplexToFloatingComplex);

  // float -> _Complex float
  if (ConvertFloat)
    FloatExpr =
        S.ImpCastExprToType(FloatExpr.get(), result, CK_FloatingRealToComplex);

  return result;
}

QualType balanceFloatTypes(Sema &S, ExprResult &LHS, ExprResult &RHS,
                           QualType LHSType, QualType RHSType,
                           bool IsCompAssign) {
  bool LHSFloat = LHSType->isRealFloatingType();
  bool RHSFloat = RHSType->isRealFloatingType();

  // N1169 4.1.4: If one of the operands has a floating type and the other
  //              operand has a fixed-point type, the fixed-point operand
  //              is converted to the floating type [...]
  if (LHSType->isFixedPointType() || RHSType->isFixedPointType()) {
    if (LHSFloat)
      RHS = S.ImpCastExprToType(RHS.get(), LHSType, CK_FixedPointToFloating);
    else if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.get(), RHSType, CK_FixedPointToFloating);
    return LHSFloat ? LHSType : RHSType;
  }

  // If we have two real floating types, convert the smaller operand
  // to the bigger result.
  if (LHSFloat && RHSFloat) {
    int order = S.Context.getFloatingTypeOrder(LHSType, RHSType);
    if (order > 0) {
      RHS = S.ImpCastExprToType(RHS.get(), LHSType, CK_FloatingCast);
      return LHSType;
    }

    assert(order < 0 && "illegal float comparison");
    if (!IsCompAssign)
      LHS = S.ImpCastExprToType(LHS.get(), RHSType, CK_FloatingCast);
    return RHSType;
  }

  if (LHSFloat) {
    // Half FP has to be promoted to float unless it is natively supported
    if (LHSType->isHalfType() && !S.getLangOpts().NativeHalfType)
      LHSType = S.Context.FloatTy;

    return promoteIntToFloat(S, LHS, RHS, LHSType, RHSType,
                             /*ConvertFloat=*/!IsCompAssign,
                             /*ConvertInt=*/true);
  }
  assert(RHSFloat);
  return promoteIntToFloat(S, RHS, LHS, RHSType, LHSType,
                           /*ConvertFloat=*/true,
                           /*ConvertInt=*/!IsCompAssign);
}
} // namespace neverc


namespace {
QualType balanceComplexIntTypes(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                QualType LHSType, QualType RHSType,
                                bool IsCompAssign) {
  const ComplexType *LHSComplexInt = LHSType->getAsComplexIntegerType();
  const ComplexType *RHSComplexInt = RHSType->getAsComplexIntegerType();

  if (LHSComplexInt && RHSComplexInt) {
    QualType LHSEltType = LHSComplexInt->getElementType();
    QualType RHSEltType = RHSComplexInt->getElementType();
    QualType ScalarType =
        balanceIntegerTypes<doComplexIntegralCast, doComplexIntegralCast>(
            S, LHS, RHS, LHSEltType, RHSEltType, IsCompAssign);

    return S.Context.getComplexType(ScalarType);
  }

  if (LHSComplexInt) {
    QualType LHSEltType = LHSComplexInt->getElementType();
    QualType ScalarType =
        balanceIntegerTypes<doComplexIntegralCast, doIntegralCast>(
            S, LHS, RHS, LHSEltType, RHSType, IsCompAssign);
    QualType ComplexType = S.Context.getComplexType(ScalarType);
    RHS = S.ImpCastExprToType(RHS.get(), ComplexType, CK_IntegralRealToComplex);

    return ComplexType;
  }

  assert(RHSComplexInt);

  QualType RHSEltType = RHSComplexInt->getElementType();
  QualType ScalarType =
      balanceIntegerTypes<doIntegralCast, doComplexIntegralCast>(
          S, LHS, RHS, LHSType, RHSEltType, IsCompAssign);
  QualType ComplexType = S.Context.getComplexType(ScalarType);

  if (!IsCompAssign)
    LHS = S.ImpCastExprToType(LHS.get(), ComplexType, CK_IntegralRealToComplex);
  return ComplexType;
}
} // namespace

namespace {
unsigned getFixedPointRank(QualType Ty) {
  const auto *BTy = Ty->getAs<BuiltinType>();
  assert(BTy && "Expected a builtin type.");

  switch (BTy->getKind()) {
  case BuiltinType::ShortFract:
  case BuiltinType::UShortFract:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatUShortFract:
    return 1;
  case BuiltinType::Fract:
  case BuiltinType::UFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatUFract:
    return 2;
  case BuiltinType::LongFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatULongFract:
    return 3;
  case BuiltinType::ShortAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatUShortAccum:
    return 4;
  case BuiltinType::Accum:
  case BuiltinType::UAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatUAccum:
    return 5;
  case BuiltinType::LongAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatULongAccum:
    return 6;
  default:
    if (BTy->isInteger())
      return 0;
    llvm_unreachable("Unexpected fixed point or integer type");
  }
}
} // namespace

namespace {
QualType balanceFixedPointTypes(Sema &S, QualType LHSTy, QualType RHSTy) {
  assert((LHSTy->isFixedPointType() || RHSTy->isFixedPointType()) &&
         "Expected at least one of the operands to be a fixed point type");
  assert((LHSTy->isFixedPointOrIntegerType() ||
          RHSTy->isFixedPointOrIntegerType()) &&
         "Special fixed point arithmetic operation conversions are only "
         "applied to ints or other fixed point types");

  // If one operand has signed fixed-point type and the other operand has
  // unsigned fixed-point type, then the unsigned fixed-point operand is
  // converted to its corresponding signed fixed-point type and the resulting
  // type is the type of the converted operand.
  if (RHSTy->isSignedFixedPointType() && LHSTy->isUnsignedFixedPointType())
    LHSTy = S.Context.getCorrespondingSignedFixedPointType(LHSTy);
  else if (RHSTy->isUnsignedFixedPointType() && LHSTy->isSignedFixedPointType())
    RHSTy = S.Context.getCorrespondingSignedFixedPointType(RHSTy);

  // The result type is the type with the highest rank, whereby a fixed-point
  // conversion rank is always greater than an integer conversion rank; if the
  // type of either of the operands is a saturating fixedpoint type, the result
  // type shall be the saturating fixed-point type corresponding to the type
  // with the highest rank; the resulting value is converted (taking into
  // account rounding and overflow) to the precision of the resulting type.
  // Same ranks between signed and unsigned types are resolved earlier, so both
  // types are either signed or both unsigned at this point.
  unsigned LHSTyRank = getFixedPointRank(LHSTy);
  unsigned RHSTyRank = getFixedPointRank(RHSTy);

  QualType ResultTy = LHSTyRank > RHSTyRank ? LHSTy : RHSTy;

  if (LHSTy->isSaturatedFixedPointType() || RHSTy->isSaturatedFixedPointType())
    ResultTy = S.Context.getCorrespondingSaturatedType(ResultTy);

  return ResultTy;
}
} // namespace

namespace {
void verifyEnumArithmetic(Sema &S, Expr *LHS, Expr *RHS, SourceLocation Loc,
                          Sema::ArithConvKind ACK) {
  // Enum mixed with float or with another enum: warn (stricter rules exist in
  // some other language modes).
  QualType L = LHS->getType(), R = RHS->getType();
  bool LEnum = L->isUnscopedEnumerationType(),
       REnum = R->isUnscopedEnumerationType();
  bool IsCompAssign = ACK == Sema::ACK_CompAssign;
  if ((!IsCompAssign && LEnum && R->isFloatingType()) ||
      (REnum && L->isFloatingType())) {
    S.Diag(Loc, diag::warn_arith_conv_enum_float)
        << LHS->getSourceRange() << RHS->getSourceRange() << (int)ACK << LEnum
        << L << R;
  } else if (!IsCompAssign && LEnum && REnum &&
             !S.Context.hasSameUnqualifiedType(L, R)) {
    unsigned DiagID;
    if (!L->castAs<EnumType>()->getDecl()->hasNameForLinkage() ||
        !R->castAs<EnumType>()->getDecl()->hasNameForLinkage()) {
      DiagID = diag::warn_arith_conv_mixed_anon_enum_types;
    } else if (ACK == Sema::ACK_Conditional) {
      DiagID = diag::warn_conditional_mixed_enum_types;
    } else if (ACK == Sema::ACK_Comparison) {
      DiagID = diag::warn_comparison_mixed_enum_types;
    } else {
      DiagID = diag::warn_arith_conv_mixed_enum_types;
    }
    S.Diag(Loc, DiagID) << LHS->getSourceRange() << RHS->getSourceRange()
                        << (int)ACK << L << R;
  }
}
} // namespace

QualType Sema::UsualArithmeticConversions(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          ArithConvKind ACK) {
  QualType LTy = LHS.get()->getType();
  QualType RTy = RHS.get()->getType();
  if (LLVM_LIKELY(LTy == RTy && !LTy.isNull())) {
    if (const auto *BT = LTy->getAs<BuiltinType>()) {
      unsigned K = BT->getKind();
      if (LLVM_LIKELY(K == BuiltinType::Int || K == BuiltinType::UInt ||
                      K == BuiltinType::Long || K == BuiltinType::ULong ||
                      K == BuiltinType::LongLong ||
                      K == BuiltinType::ULongLong || K == BuiltinType::Float ||
                      K == BuiltinType::Double)) {
        // Inline lvalue-to-rvalue: for builtin integer operands, the entire
        // DefaultFunctionArrayLvalueConversion → DefaultLvalueConversion →
        // CheckLValueToRValueConversionOperand call chain reduces to a single
        // ImplicitCastExpr allocation when the operand is an lvalue.
        QualType ResultTy = LTy.getUnqualifiedType();
        FPOptionsOverride FPO = CurFPFeatureOverrides();
        if (LLVM_LIKELY(ACK != ACK_CompAssign)) {
          Expr *L = LHS.get();
          if (L->isLValue())
            LHS = ImplicitCastExpr::Create(Context, ResultTy, CK_LValueToRValue,
                                           L, VK_PRValue, FPO);
        }
        Expr *R = RHS.get();
        if (R->isLValue())
          RHS = ImplicitCastExpr::Create(Context, ResultTy, CK_LValueToRValue,
                                         R, VK_PRValue, FPO);
        return ResultTy;
      }
    }
  }

  verifyEnumArithmetic(*this, LHS.get(), RHS.get(), Loc, ACK);

  if (ACK != ACK_CompAssign) {
    LHS = UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  if (const AtomicType *AtomicLHS = LHSType->getAs<AtomicType>())
    LHSType = AtomicLHS->getValueType();

  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // If either side is a non-arithmetic type (e.g. a pointer), we are done.
  // The caller can deal with this (e.g. pointer + int).
  if (!LHSType->isArithmeticType() || !RHSType->isArithmeticType())
    return QualType();

  // Apply unary and bitfield promotions to the LHS's type.
  QualType LHSUnpromotedType = LHSType;
  if (Context.isPromotableIntegerType(LHSType))
    LHSType = Context.getPromotedIntegerType(LHSType);
  QualType LHSBitfieldPromoteTy = Context.isPromotableBitField(LHS.get());
  if (!LHSBitfieldPromoteTy.isNull())
    LHSType = LHSBitfieldPromoteTy;
  if (LHSType != LHSUnpromotedType && ACK != ACK_CompAssign)
    LHS = ImpCastExprToType(LHS.get(), LHSType, CK_IntegralCast);

  // If both types are identical, no conversion is needed.
  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // At this point, we have two different arithmetic types.

  // Handle complex types first (C99 6.3.1.8p1).
  if (LHSType->isComplexType() || RHSType->isComplexType())
    return balanceComplexTypes(*this, LHS, RHS, LHSType, RHSType,
                               ACK == ACK_CompAssign);

  // Now handle "real" floating types (i.e. float, double, long double).
  if (LHSType->isRealFloatingType() || RHSType->isRealFloatingType())
    return balanceFloatTypes(*this, LHS, RHS, LHSType, RHSType,
                             ACK == ACK_CompAssign);

  // Handle GCC complex int extension.
  if (LHSType->isComplexIntegerType() || RHSType->isComplexIntegerType())
    return balanceComplexIntTypes(*this, LHS, RHS, LHSType, RHSType,
                                  ACK == ACK_CompAssign);

  if (LHSType->isFixedPointType() || RHSType->isFixedPointType())
    return balanceFixedPointTypes(*this, LHSType, RHSType);

  // Finally, we have two differing integer types.
  return balanceIntegerTypes<doIntegralCast, doIntegralCast>(
      *this, LHS, RHS, LHSType, RHSType, ACK == ACK_CompAssign);
}

