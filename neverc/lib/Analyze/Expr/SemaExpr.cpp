#include "TreeTransform.h"
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
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace {
ExprResult buildNeverCStringLiteral(Sema &S, QualType StringTy,
                                    Expr *Initializer, StringLiteral *SL);
} // namespace

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
} // namespace neverc

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

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnNullPtrDeref(Sema &S, Expr *E) {
  const auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParenCasts());
  if (UO && UO->getOpcode() == UO_Deref &&
      UO->getSubExpr()->getType()->isPointerType()) {
    const LangAS AS =
        UO->getSubExpr()->getType()->getPointeeType().getAddressSpace();
    if ((!isTargetAddressSpace(AS) ||
         (isTargetAddressSpace(AS) && toTargetAddressSpace(AS) == 0)) &&
        UO->getSubExpr()->IgnoreParenCasts()->isNullPointerConstant(
            S.Context, Expr::NPC_ValueDependentIsNotNull) &&
        !UO->getType().isVolatileQualified()) {
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::warn_indirection_through_null)
                                << UO->getSubExpr()->getSourceRange());
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::note_indirection_through_null));
    }
  }
}
} // namespace

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

namespace {
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
} // namespace

namespace {
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
} // namespace

typedef ExprResult PerformCastFn(Sema &S, Expr *operand, QualType toType);

namespace {
ExprResult doIntegralCast(Sema &S, Expr *op, QualType toType) {
  return S.ImpCastExprToType(op, toType, CK_IntegralCast);
}

ExprResult doComplexIntegralCast(Sema &S, Expr *op, QualType toType) {
  return S.ImpCastExprToType(op, S.Context.getComplexType(toType),
                             CK_IntegralComplexCast);
}
} // namespace

namespace {
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
} // namespace

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

// ===----------------------------------------------------------------------===
// Literals, identifiers & generic selection
// ===----------------------------------------------------------------------===

ExprResult Sema::OnGenericSelectionExpr(
    SourceLocation KeyLoc, SourceLocation DefaultLoc, SourceLocation RParenLoc,
    bool PredicateIsExpr, void *ControllingExprOrType,
    llvm::ArrayRef<ParsedType> ArgTypes, llvm::ArrayRef<Expr *> ArgExprs) {
  unsigned NumAssocs = ArgTypes.size();
  assert(NumAssocs == ArgExprs.size());

  TypeSourceInfo **Types = new TypeSourceInfo *[NumAssocs];
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (ArgTypes[i])
      (void)GetTypeFromParser(ArgTypes[i], &Types[i]);
    else
      Types[i] = nullptr;
  }

  // If we have a controlling type, we need to convert it from a parsed type
  // into a semantic type and then pass that along.
  if (!PredicateIsExpr) {
    TypeSourceInfo *ControllingType;
    (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(ControllingExprOrType),
                            &ControllingType);
    assert(ControllingType && "couldn't get the type out of the parser");
    ControllingExprOrType = ControllingType;
  }

  ExprResult ER = CreateGenericSelectionExpr(
      KeyLoc, DefaultLoc, RParenLoc, PredicateIsExpr, ControllingExprOrType,
      llvm::ArrayRef(Types, NumAssocs), ArgExprs);
  delete[] Types;
  return ER;
}

ExprResult Sema::CreateGenericSelectionExpr(
    SourceLocation KeyLoc, SourceLocation DefaultLoc, SourceLocation RParenLoc,
    bool PredicateIsExpr, void *ControllingExprOrType,
    llvm::ArrayRef<TypeSourceInfo *> Types, llvm::ArrayRef<Expr *> Exprs) {
  unsigned NumAssocs = Types.size();
  assert(NumAssocs == Exprs.size());
  assert(ControllingExprOrType &&
         "Must have either a controlling expression or a controlling type");

  Expr *ControllingExpr = nullptr;
  TypeSourceInfo *ControllingType = nullptr;
  if (PredicateIsExpr) {
    // Decay and strip qualifiers for the controlling expression type, and
    // handle placeholder type replacement. See committee discussion from WG14
    // DR423.
    EnterExpressionEvaluationContext Unevaluated(
        *this, Sema::ExpressionEvaluationContext::Unevaluated);
    ExprResult R = DefaultFunctionArrayLvalueConversion(
        reinterpret_cast<Expr *>(ControllingExprOrType));
    if (R.isInvalid())
      return ExprError();
    ControllingExpr = R.get();
  } else {
    // The extension form uses the type directly rather than converting it.
    ControllingType = reinterpret_cast<TypeSourceInfo *>(ControllingExprOrType);
    if (!ControllingType)
      return ExprError();
  }

  bool TypeErrorFound = false;

  if (ControllingExpr && ControllingExpr->HasSideEffects(Context, false))
    Diag(ControllingExpr->getExprLoc(),
         diag::warn_side_effects_unevaluated_context);

  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (Types[i]) {
      // Generic association type must be a complete, non-VLA object type.
      unsigned D = 0;
      if (ControllingExpr && Types[i]->getType()->isIncompleteType())
        D = diag::err_assoc_type_incomplete;
      else if (ControllingExpr && !Types[i]->getType()->isObjectType())
        D = diag::err_assoc_type_nonobject;
      else if (Types[i]->getType()->isVariablyModifiedType())
        D = diag::err_assoc_type_variably_modified;
      else if (ControllingExpr) {
        unsigned Reason = 0;
        QualType QT = Types[i]->getType();
        if (QT->isArrayType())
          Reason = 1;
        else if (QT.hasQualifiers())
          Reason = 2;

        if (Reason)
          Diag(Types[i]->getTypeLoc().getBeginLoc(),
               diag::warn_unreachable_association)
              << QT << (Reason - 1);
      }

      if (D != 0) {
        Diag(Types[i]->getTypeLoc().getBeginLoc(), D)
            << Types[i]->getTypeLoc().getSourceRange() << Types[i]->getType();
        TypeErrorFound = true;
      }

      // No two generic associations may have compatible types.
      for (unsigned j = i + 1; j < NumAssocs; ++j)
        if (Types[j] && Context.typesAreCompatible(Types[i]->getType(),
                                                   Types[j]->getType())) {
          Diag(Types[j]->getTypeLoc().getBeginLoc(),
               diag::err_assoc_compatible_types)
              << Types[j]->getTypeLoc().getSourceRange() << Types[j]->getType()
              << Types[i]->getType();
          Diag(Types[i]->getTypeLoc().getBeginLoc(), diag::note_compat_assoc)
              << Types[i]->getTypeLoc().getSourceRange() << Types[i]->getType();
          TypeErrorFound = true;
        }
    }
  }
  if (TypeErrorFound)
    return ExprError();

  llvm::SmallVector<unsigned, 1> CompatIndices;
  unsigned DefaultIndex = -1U;
  // Look at the canonical type of the controlling expression in case it was a
  // deduced type like __auto_type. However, when issuing diagnostics, use the
  // type the user wrote in source rather than the canonical one.
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (!Types[i])
      DefaultIndex = i;
    else if (ControllingExpr &&
             Context.typesAreCompatible(
                 ControllingExpr->getType().getCanonicalType(),
                 Types[i]->getType()))
      CompatIndices.push_back(i);
    else if (ControllingType &&
             Context.typesAreCompatible(
                 ControllingType->getType().getCanonicalType(),
                 Types[i]->getType()))
      CompatIndices.push_back(i);
  }

  auto GetControllingRangeAndType = [](Expr *ControllingExpr,
                                       TypeSourceInfo *ControllingType) {
    // We strip parens here because the controlling expression is typically
    // parenthesized in macro definitions.
    if (ControllingExpr)
      ControllingExpr = ControllingExpr->IgnoreParens();

    SourceRange SR = ControllingExpr
                         ? ControllingExpr->getSourceRange()
                         : ControllingType->getTypeLoc().getSourceRange();
    QualType QT = ControllingExpr ? ControllingExpr->getType()
                                  : ControllingType->getType();

    return std::make_pair(SR, QT);
  };

  // Controlling expression must match at most one generic association type.
  if (CompatIndices.size() > 1) {
    auto P = GetControllingRangeAndType(ControllingExpr, ControllingType);
    SourceRange SR = P.first;
    Diag(SR.getBegin(), diag::err_generic_sel_multi_match)
        << SR << P.second << (unsigned)CompatIndices.size();
    for (unsigned I : CompatIndices) {
      Diag(Types[I]->getTypeLoc().getBeginLoc(), diag::note_compat_assoc)
          << Types[I]->getTypeLoc().getSourceRange() << Types[I]->getType();
    }
    return ExprError();
  }

  // Without a default, the controlling expression must match exactly one type.
  if (DefaultIndex == -1U && CompatIndices.size() == 0) {
    if ((ControllingExpr && ControllingExpr->containsErrors()) ||
        (ControllingType && ControllingType->getType()->containsErrors()))
      return ExprError();
    auto P = GetControllingRangeAndType(ControllingExpr, ControllingType);
    SourceRange SR = P.first;
    Diag(SR.getBegin(), diag::err_generic_sel_no_match) << SR << P.second;
    return ExprError();
  }

  // Result is the matching association, or the default if none matches.
  unsigned ResultIndex = CompatIndices.size() ? CompatIndices[0] : DefaultIndex;

  if (ControllingExpr) {
    return GenericSelectionExpr::Create(Context, KeyLoc, ControllingExpr, Types,
                                        Exprs, DefaultLoc, RParenLoc,
                                        ResultIndex);
  }
  return GenericSelectionExpr::Create(Context, KeyLoc, ControllingType, Types,
                                      Exprs, DefaultLoc, RParenLoc,
                                      ResultIndex);
}

namespace {
PredefinedIdentKind getPredefinedExprKind(tok::TokenKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("unexpected TokenKind");
  case tok::kw___func__:
    return PredefinedIdentKind::Func; // [C99 6.4.2.2]
  case tok::kw___FUNCTION__:
    return PredefinedIdentKind::Function;
  case tok::kw___FUNCDNAME__:
    return PredefinedIdentKind::FuncDName; // [MS]
  case tok::kw___FUNCSIG__:
    return PredefinedIdentKind::FuncSig; // [MS]
  case tok::kw_L__FUNCTION__:
    return PredefinedIdentKind::LFunction; // [MS]
  case tok::kw_L__FUNCSIG__:
    return PredefinedIdentKind::LFuncSig; // [MS]
  case tok::kw___PRETTY_FUNCTION__:
    return PredefinedIdentKind::PrettyFunction; // [GNU]
  }
}
} // namespace

namespace {
Decl *getPredefinedExprDecl(DeclContext *DC) {
  while (DC && !isa<FunctionDecl>(DC))
    DC = DC->getParent();
  return cast_or_null<Decl>(DC);
}
} // namespace

ExprResult Sema::OnUnevaluatedStringLiteral(llvm::ArrayRef<Token> StringToks) {
  // StringToks needs backing storage as it doesn't hold array elements itself
  std::vector<Token> ExpandedToks;
  if (getLangOpts().MicrosoftExt)
    StringToks = ExpandedToks = ExpandFunctionLocalPredefinedMacros(StringToks);

  StringLiteralParser Literal(StringToks, PP,
                              StringLiteralEvalMethod::Unevaluated);
  if (Literal.hadError)
    return ExprError();

  llvm::SmallVector<SourceLocation, 4> StringTokLocs;
  for (const Token &Tok : StringToks)
    StringTokLocs.push_back(Tok.getLocation());

  StringLiteral *Lit = StringLiteral::Create(
      Context, Literal.getString(), StringLiteralKind::Unevaluated, {},
      &StringTokLocs[0], StringTokLocs.size());

  return Lit;
}

std::vector<Token>
Sema::ExpandFunctionLocalPredefinedMacros(llvm::ArrayRef<Token> Toks) {
  // MSVC treats some predefined identifiers (e.g. __FUNCTION__) as function
  // local macros that expand to string literals that may be concatenated.
  // These macros are expanded here (in Sema), because StringLiteralParser
  // (in Lex) doesn't know the enclosing function (because it hasn't been
  // parsed yet).
  assert(getLangOpts().MicrosoftExt);

  // Note: Although function local macros are defined only inside functions,
  // we ensure a valid `CurrentDecl` even outside of a function. This allows
  // expansion of macros into empty string literals without additional checks.
  Decl *CurrentDecl = getPredefinedExprDecl(CurContext);
  if (!CurrentDecl)
    CurrentDecl = Context.getTranslationUnitDecl();

  std::vector<Token> ExpandedToks;
  ExpandedToks.reserve(Toks.size());
  for (const Token &Tok : Toks) {
    if (!isFunctionLocalStringLiteralMacro(Tok.getKind(), getLangOpts())) {
      assert(tok::isStringLiteral(Tok.getKind()));
      ExpandedToks.emplace_back(Tok);
      continue;
    }
    if (isa<TranslationUnitDecl>(CurrentDecl))
      Diag(Tok.getLocation(), diag::ext_predef_outside_function);
    // Escape predefined expression to string literal.
    Diag(Tok.getLocation(), diag::ext_string_literal_from_predefined)
        << Tok.getKind();
    llvm::SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    Token &Exp = ExpandedToks.emplace_back();
    Exp.startToken();
    if (Tok.getKind() == tok::kw_L__FUNCTION__ ||
        Tok.getKind() == tok::kw_L__FUNCSIG__) {
      OS << 'L';
      Exp.setKind(tok::wide_string_literal);
    } else {
      Exp.setKind(tok::string_literal);
    }
    OS << '"'
       << SourceScanner::escapeStringLiteral(PredefinedExpr::ComputeName(
              getPredefinedExprKind(Tok.getKind()), CurrentDecl))
       << '"';
    PP.WriteScratch(OS.str(), Exp, Tok.getLocation(), Tok.getEndLoc());
  }
  return ExpandedToks;
}

ExprResult Sema::OnStringLiteral(llvm::ArrayRef<Token> StringToks) {
  assert(!StringToks.empty() && "Must have at least one string!");

  // StringToks needs backing storage as it doesn't hold array elements itself
  std::vector<Token> ExpandedToks;
  if (getLangOpts().MicrosoftExt)
    StringToks = ExpandedToks = ExpandFunctionLocalPredefinedMacros(StringToks);

  StringLiteralParser Literal(StringToks, PP);
  if (Literal.hadError)
    return ExprError();

  llvm::SmallVector<SourceLocation, 4> StringTokLocs;
  for (const Token &Tok : StringToks)
    StringTokLocs.push_back(Tok.getLocation());

  QualType CharTy = Context.CharTy;
  StringLiteralKind Kind = StringLiteralKind::Ordinary;
  if (Literal.isWide()) {
    CharTy = Context.getWideCharType();
    Kind = StringLiteralKind::Wide;
  } else if (Literal.isUTF8()) {
    if (getLangOpts().Char8)
      CharTy = Context.Char8Ty;
    Kind = StringLiteralKind::UTF8;
  } else if (Literal.isUTF16()) {
    CharTy = Context.Char16Ty;
    Kind = StringLiteralKind::UTF16;
  } else if (Literal.isUTF32()) {
    CharTy = Context.Char32Ty;
    Kind = StringLiteralKind::UTF32;
  }

  QualType StrTy =
      Context.getStringLiteralArrayType(CharTy, Literal.getNumStringChars());

  // Pass &StringTokLocs[0], StringTokLocs.size() to factory!
  StringLiteral *Lit =
      StringLiteral::Create(Context, Literal.getString(), Kind, StrTy,
                            &StringTokLocs[0], StringTokLocs.size());
  return Lit;
}

DeclRefExpr *Sema::MakeDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                                   SourceLocation Loc) {
  DeclarationNameInfo NameInfo(D->getDeclName(), Loc);
  return MakeDeclRefExpr(D, Ty, VK, NameInfo);
}

NonOdrUseReason Sema::getNonOdrUseReasonInCurrentContext(ValueDecl *D) {
  // A declaration named in an unevaluated operand never constitutes an odr-use.
  if (isUnevaluatedContext())
    return NOUR_Unevaluated;

  // All remaining non-variable cases constitute an odr-use. For variables, we
  // need to wait and see how the expression is used.
  return NOUR_None;
}

DeclRefExpr *Sema::MakeDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                                   const DeclarationNameInfo &NameInfo,
                                   NamedDecl *FoundD) {
  DeclRefExpr *E = DeclRefExpr::Create(Context, D, NameInfo, Ty, VK, FoundD,
                                       getNonOdrUseReasonInCurrentContext(D));
  MarkDeclRefReferenced(E);

  bool IsCommonDecl = isa<VarDecl, FunctionDecl, EnumConstantDecl>(D);
  if (LLVM_UNLIKELY(!IsCommonDecl)) {
    const auto *FD = dyn_cast<FieldDecl>(D);
    if (const auto *IFD = dyn_cast<IndirectFieldDecl>(D))
      FD = IFD->getAnonField();
    if (FD) {
      UnusedPrivateFields.remove(FD);
      if (FD->isBitField())
        E->setObjectKind(OK_BitField);
    }
  }

  return E;
}

namespace {
void reportEmptyLookupTypo(const TypoCorrection &TC, Sema &SemaRef,
                           DeclarationName Typo, SourceLocation TypoLoc,
                           unsigned DiagnosticID,
                           unsigned DiagnosticSuggestID) {
  if (!TC) {
    SemaRef.Diag(TypoLoc, DiagnosticID) << Typo;
    return;
  }

  unsigned NoteID = TC.getCorrectionDeclAs<ImplicitParamDecl>()
                        ? diag::note_implicit_param_decl
                        : diag::note_previous_decl;
  SemaRef.diagnoseTypo(TC, SemaRef.PDiag(DiagnosticSuggestID) << Typo,
                       SemaRef.PDiag(NoteID));
}
} // namespace

bool Sema::DiagnoseEmptyLookup(Scope *S, LookupResult &R,
                               CorrectionCandidateCallback &CCC,
                               TypoExpr **Out) {
  DeclarationName Name = R.getLookupName();

  const unsigned diagnostic = diag::err_undeclared_var_use;
  const unsigned diagnostic_suggest = diag::err_undeclared_var_use_suggest;

  // We didn't find anything, so try to correct for a typo.
  TypoCorrection Corrected;
  if (S && Out) {
    SourceLocation TypoLoc = R.getNameLoc();
    *Out = CorrectTypoDelayed(
        R.getLookupNameInfo(), R.getLookupKind(), S, CCC,
        [=](const TypoCorrection &TC) {
          reportEmptyLookupTypo(TC, *this, Name, TypoLoc, diagnostic,
                                diagnostic_suggest);
        },
        nullptr, CTK_ErrorRecovery);
    if (*Out)
      return true;
  } else if (S &&
             (Corrected = CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(),
                                      S, CCC, CTK_ErrorRecovery))) {
    R.setLookupName(Corrected.getCorrection());

    bool AcceptableWithRecovery = false;
    bool AcceptableWithoutRecovery = false;
    NamedDecl *ND = Corrected.getFoundDecl();
    if (ND) {
      R.addDecl(ND);

      auto *UnderlyingND = ND->getUnderlyingDecl();
      AcceptableWithRecovery = isa<ValueDecl>(UnderlyingND);
      AcceptableWithoutRecovery = isa<TypeDecl>(UnderlyingND);
    } else {
      AcceptableWithoutRecovery = true;
    }

    if (AcceptableWithRecovery || AcceptableWithoutRecovery) {
      unsigned NoteID = Corrected.getCorrectionDeclAs<ImplicitParamDecl>()
                            ? diag::note_implicit_param_decl
                            : diag::note_previous_decl;
      diagnoseTypo(Corrected, PDiag(diagnostic_suggest) << Name, PDiag(NoteID),
                   AcceptableWithRecovery);

      // Tell the callee whether to try to recover.
      return !AcceptableWithRecovery;
    }
  }
  R.clear();

  // Give up, we can't recover.
  Diag(R.getNameLoc(), diagnostic) << Name;
  return true;
}

__attribute__((hot)) ExprResult
Sema::OnIdExpression(Scope *S, UnqualifiedId &Id, bool HasTrailingLParen,
                     bool IsAddressOfOperand, CorrectionCandidateCallback *CCC,
                     bool IsInlineAsmIdentifier, Token *KeywordReplacement) {
  assert(!(IsAddressOfOperand && HasTrailingLParen) &&
         "cannot be direct & operand and have a trailing lparen");

  DeclarationNameInfo NameInfo = GetNameFromUnqualifiedId(Id);

  DeclarationName Name = NameInfo.getName();
  IdentifierInfo *II = Name.getAsIdentifierInfo();
  SourceLocation NameLoc = NameInfo.getLoc();

  LookupResult R(*this, NameInfo, ResolveOrdinary);
  if (CachedClassifyNameDecl && CachedClassifyNameLoc == NameLoc) {
    R.addDecl(CachedClassifyNameDecl);
    R.resolveKind();
    CachedClassifyNameDecl = nullptr;
  } else {
    CachedClassifyNameDecl = nullptr;
    LookupParsedName(R, S, /*AllowBuiltinCreation=*/true);
  }

  if (LLVM_UNLIKELY(R.isAmbiguous()))
    return ExprError();

  if (LLVM_UNLIKELY(R.empty()) && HasTrailingLParen && II &&
      getLangOpts().implicitFunctionsAllowed()) {
    NamedDecl *D = ImplicitlyDefineFunction(NameLoc, *II, S);
    if (D)
      R.addDecl(D);
  }

  if (R.empty()) {
    // Don't diagnose an empty lookup for inline assembly.
    if (IsInlineAsmIdentifier)
      return ExprError();

    // If this name wasn't predeclared and if this is not a function
    // call, diagnose the problem.
    TypoExpr *TE = nullptr;
    DefaultFilterCCC DefaultValidator(II);
    DefaultValidator.IsAddressOfOperand = IsAddressOfOperand;
    assert((!CCC || CCC->IsAddressOfOperand == IsAddressOfOperand) &&
           "Typo correction callback misconfigured");
    if (CCC) {
      // Make sure the callback knows what the typo being diagnosed is.
      CCC->setTypoName(II);
    }
    if (DiagnoseEmptyLookup(S, R, CCC ? *CCC : DefaultValidator, &TE)) {
      if (TE && KeywordReplacement) {
        auto &State = getTypoExprState(TE);
        auto BestTC = State.Consumer->getNextCorrection();
        if (BestTC.isKeyword()) {
          auto *II = BestTC.getCorrectionAsIdentifierInfo();
          if (State.DiagHandler)
            State.DiagHandler(BestTC);
          KeywordReplacement->startToken();
          KeywordReplacement->setKind(II->getTokenID());
          KeywordReplacement->setIdentifierInfo(II);
          KeywordReplacement->setLocation(
              BestTC.getCorrectionRange().getBegin());
          // Clean up the state associated with the TypoExpr, since it has
          // now been diagnosed (without a call to CorrectDelayedTyposInExpr).
          clearDelayedTypo(TE);
          // Signal that a correction to a keyword was performed by returning a
          // valid-but-null ExprResult.
          return (Expr *)nullptr;
        }
        State.Consumer->resetCorrectionStream();
      }
      return TE ? TE : ExprError();
    }

    assert(!R.empty() &&
           "DiagnoseEmptyLookup returned false but added no results");
  }

  assert(!R.empty());

  return FormDeclarationNameExpr(R);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool validateDeclForExpr(Sema &S, SourceLocation Loc, NamedDecl *D) {
  if (LLVM_UNLIKELY(D->isInvalidDecl()))
    return true;

  if (LLVM_UNLIKELY(isa<TypedefNameDecl>(D))) {
    S.Diag(Loc, diag::err_unexpected_typedef) << D->getDeclName();
    return true;
  }

  return false;
}
} // namespace

ExprResult Sema::FormDeclarationNameExpr(LookupResult &R) {
  return FormDeclarationNameExpr(R.getLookupNameInfo(), R.getFoundDecl(),
                                 R.getRepresentativeDecl());
}

__attribute__((hot)) ExprResult Sema::FormDeclarationNameExpr(
    const DeclarationNameInfo &NameInfo, NamedDecl *D, NamedDecl *FoundD) {
  assert(D && "Cannot refer to a NULL declaration");

  SourceLocation Loc = NameInfo.getLoc();
  if (validateDeclForExpr(*this, Loc, D)) {
    // Recovery from invalid cases (e.g. D is an invalid Decl).
    // We use the dependent type for the RecoveryExpr to prevent bogus follow-up
    // diagnostics, as invalid decls use int as a fallback type.
    return CreateRecoveryExpr(NameInfo.getBeginLoc(), NameInfo.getEndLoc(), {});
  }

  if (LLVM_UNLIKELY(!isa<ValueDecl>(D))) {
    Diag(Loc, diag::err_ref_non_value) << D << SourceRange();
    Diag(D->getLocation(), diag::note_declared_at);
    return ExprError();
  }

  if (LLVM_UNLIKELY(CheckDeclUsage(D, Loc)))
    return ExprError();

  auto *VD = cast<ValueDecl>(D);

  if (LLVM_UNLIKELY(VD->isInvalidDecl()))
    return ExprError();

  if (LLVM_UNLIKELY(isa<IndirectFieldDecl>(VD)))
    return FormAnonymousStructUnionMemberReference(NameInfo.getLoc(),
                                                   cast<IndirectFieldDecl>(VD));

  QualType type = VD->getType();
  if (type.isNull())
    return ExprError();
  ExprValueKind valueKind;

  auto Kind = D->getKind();
  if (LLVM_LIKELY(Kind == Decl::Var || Kind == Decl::ParmVar ||
                  Kind == Decl::ImplicitParam)) {
    valueKind =
        (Kind == Decl::Var && !type.hasQualifiers() && type->isVoidType())
            ? VK_PRValue
            : VK_LValue;
  } else {
    valueKind = VK_PRValue;
    switch (Kind) {
#define ABSTRACT_DECL(kind)
#define VALUE(type, base)
#define DECL(type, base) case Decl::type:
#include "neverc/Tree/DeclNodes.td.h"
      llvm_unreachable("invalid value decl kind");

    case Decl::EnumConstant:
      break;

    case Decl::Field:
    case Decl::IndirectField:
      valueKind = VK_LValue;
      break;

    case Decl::Var:
    case Decl::ImplicitParam:
    case Decl::ParmVar:
      llvm_unreachable("handled above");

    case Decl::Function: {
      if (unsigned BID = cast<FunctionDecl>(VD)->getBuiltinID()) {
        if (!Context.BuiltinInfo.isDirectlyAddressable(BID)) {
          type = Context.BuiltinFnTy;
          break;
        }
      }

      const FunctionType *fty = type->castAs<FunctionType>();
      if (!cast<FunctionDecl>(VD)->hasPrototype() &&
          isa<FunctionProtoType>(fty))
        type = Context.getFunctionNoProtoType(fty->getReturnType(),
                                              fty->getExtInfo());
      break;
    }
    }
  }

  auto *E = MakeDeclRefExpr(VD, type, valueKind, NameInfo, FoundD);
  // NeverC AST consumers assume a DeclRefExpr refers to a valid decl. We
  // wrap a DeclRefExpr referring to an invalid decl with a dependent-type
  // RecoveryExpr to avoid follow-up semantic analysis (thus prevent bogus
  // diagnostics).
  if (VD->isInvalidDecl() && E)
    return CreateRecoveryExpr(E->getBeginLoc(), E->getEndLoc(), {E});
  return E;
}

namespace {
void utf8ToWideString(unsigned CharByteWidth, llvm::StringRef Source,
                      llvm::SmallString<32> &Target) {
  Target.resize(CharByteWidth * (Source.size() + 1));
  char *ResultPtr = &Target[0];
  const llvm::UTF8 *ErrorPtr;
  bool success =
      llvm::ConvertUTF8toWide(CharByteWidth, Source, ResultPtr, ErrorPtr);
  (void)success;
  assert(success);
  Target.resize(ResultPtr - &Target[0]);
}
} // namespace

ExprResult Sema::FormPredefinedExpr(SourceLocation Loc,
                                    PredefinedIdentKind IK) {
  Decl *currentDecl = getPredefinedExprDecl(CurContext);
  if (!currentDecl) {
    Diag(Loc, diag::ext_predef_outside_function);
    currentDecl = Context.getTranslationUnitDecl();
  }

  auto Str = PredefinedExpr::ComputeName(IK, currentDecl);
  unsigned Length = Str.length();

  llvm::APInt LengthI(32, Length + 1);
  QualType ResTy;
  StringLiteral *SL = nullptr;
  if (IK == PredefinedIdentKind::LFunction ||
      IK == PredefinedIdentKind::LFuncSig) {
    ResTy = Context.adjustStringLiteralBaseType(Context.WideCharTy.withConst());
    llvm::SmallString<32> RawChars;
    utf8ToWideString(Context.getTypeSizeInChars(ResTy).getQuantity(), Str,
                     RawChars);
    ResTy = Context.getConstantArrayType(ResTy, LengthI, nullptr,
                                         ArraySizeModifier::Normal,
                                         /*IndexTypeQuals*/ 0);
    SL = StringLiteral::Create(Context, RawChars, StringLiteralKind::Wide,
                               ResTy, Loc);
  } else {
    ResTy = Context.adjustStringLiteralBaseType(Context.CharTy.withConst());
    ResTy = Context.getConstantArrayType(ResTy, LengthI, nullptr,
                                         ArraySizeModifier::Normal,
                                         /*IndexTypeQuals*/ 0);
    SL = StringLiteral::Create(Context, Str, StringLiteralKind::Ordinary, ResTy,
                               Loc);
  }

  return PredefinedExpr::Create(Context, Loc, ResTy, IK, LangOpts.MicrosoftExt,
                                SL);
}

ExprResult Sema::OnPredefinedExpr(SourceLocation Loc, tok::TokenKind Kind) {
  return FormPredefinedExpr(Loc, getPredefinedExprKind(Kind));
}

ExprResult Sema::OnCharacterConstant(const Token &Tok) {
  llvm::SmallString<16> CharBuffer;
  bool Invalid = false;
  llvm::StringRef ThisTok = PP.getSpelling(Tok, CharBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  CharLiteralParser Literal(ThisTok.begin(), ThisTok.end(), Tok.getLocation(),
                            PP, Tok.getKind());
  if (Literal.hadError())
    return ExprError();

  QualType Ty;
  if (Literal.isWide())
    Ty = Context.WideCharTy; // L'x' -> wchar_t
  else if (Literal.isUTF8() && getLangOpts().C23)
    Ty = Context.UnsignedCharTy; // u8'x' -> unsigned char in C23
  else if (Literal.isUTF8() && getLangOpts().Char8)
    Ty = Context.Char8Ty; // u8'x' -> char8_t when it exists.
  else if (Literal.isUTF16())
    Ty = Context.Char16Ty; // u'x' -> char16_t
  else if (Literal.isUTF32())
    Ty = Context.Char32Ty; // U'x' -> char32_t
  else
    Ty = Context.IntTy; // 'x' -> int in C.

  CharacterLiteralKind Kind = CharacterLiteralKind::Ascii;
  if (Literal.isWide())
    Kind = CharacterLiteralKind::Wide;
  else if (Literal.isUTF16())
    Kind = CharacterLiteralKind::UTF16;
  else if (Literal.isUTF32())
    Kind = CharacterLiteralKind::UTF32;
  else if (Literal.isUTF8())
    Kind = CharacterLiteralKind::UTF8;

  return new (Context)
      CharacterLiteral(Literal.getValue(), Kind, Ty, Tok.getLocation());
}

ExprResult Sema::OnIntegerConstant(SourceLocation Loc, uint64_t Val) {
  unsigned IntSize = Context.getTargetInfo().getIntWidth();
  return IntegerLiteral::Create(Context, llvm::APInt(IntSize, Val),
                                Context.IntTy, Loc);
}

namespace {
Expr *formFloatingLiteral(Sema &S, NumericLiteralParser &Literal, QualType Ty,
                          SourceLocation Loc) {
  const llvm::fltSemantics &Format = S.Context.getFloatTypeSemantics(Ty);

  using llvm::APFloat;
  APFloat Val(Format);

  APFloat::opStatus result = Literal.getFloatValue(Val);

  // Overflow is always an error, but underflow is only an error if
  // we underflowed to zero (APFloat reports denormals as underflow).
  if ((result & APFloat::opOverflow) ||
      ((result & APFloat::opUnderflow) && Val.isZero())) {
    unsigned diagnostic;
    llvm::SmallString<20> buffer;
    if (result & APFloat::opOverflow) {
      diagnostic = diag::warn_float_overflow;
      APFloat::getLargest(Format).toString(buffer);
    } else {
      diagnostic = diag::warn_float_underflow;
      APFloat::getSmallest(Format).toString(buffer);
    }

    S.Diag(Loc, diagnostic)
        << Ty << llvm::StringRef(buffer.data(), buffer.size());
  }

  bool isExact = (result == APFloat::opOK);
  return FloatingLiteral::Create(S.Context, Val, isExact, Ty, Loc);
}
} // namespace

__attribute__((hot)) ExprResult Sema::OnNumericConstant(const Token &Tok) {
  // Fast path: single digit is extremely common in real-world C code.
  if (Tok.getLength() == 1) {
    const char Val = PP.getSpellingOfSingleCharacterNumericConstant(Tok);
    return OnIntegerConstant(Tok.getLocation(), Val - '0');
  }

  // Extended fast path: small pure-decimal integers (2-9 digits, no suffix,
  // no cleaning) are extremely common (array sizes, bit widths, enum values).
  // Avoid constructing NumericLiteralParser + SmallString for these.
  if (unsigned Len = Tok.getLength();
      LLVM_LIKELY(Len <= 9 && !Tok.needsCleaning())) {
    if (const char *D = Tok.getLiteralData()) {
      bool AllDecimal = true;
      uint64_t Val = 0;
      for (unsigned I = 0; I < Len; ++I) {
        unsigned char C = static_cast<unsigned char>(D[I]);
        unsigned Digit = C - '0';
        if (LLVM_UNLIKELY(Digit > 9u)) {
          AllDecimal = false;
          break;
        }
        Val = Val * 10 + Digit;
      }
      if (LLVM_LIKELY(AllDecimal && D[0] != '0'))
        return OnIntegerConstant(Tok.getLocation(), Val);
    }
  }

  llvm::SmallString<128> SpellingBuffer;
  // NumericLiteralParser wants to overread by one character.  Add padding to
  // the buffer in case the token is copied to the buffer.  If getSpelling()
  // returns a llvm::StringRef to the memory buffer, it should have a null char
  // at the EOF, so it is also safe.
  SpellingBuffer.resize(Tok.getLength() + 1);
  bool Invalid = false;
  llvm::StringRef TokSpelling = PP.getSpelling(Tok, SpellingBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  NumericLiteralParser Literal(TokSpelling, Tok.getLocation(),
                               PP.getSourceManager(), PP.getLangOpts(),
                               PP.getTargetInfo(), PP.getDiagnostics());
  if (Literal.hadError)
    return ExprError();

  Expr *Res;

  if (Literal.isFixedPointLiteral()) {
    QualType Ty;

    if (Literal.isAccum) {
      if (Literal.isHalf) {
        Ty = Context.ShortAccumTy;
      } else if (Literal.isLong) {
        Ty = Context.LongAccumTy;
      } else {
        Ty = Context.AccumTy;
      }
    } else if (Literal.isFract) {
      if (Literal.isHalf) {
        Ty = Context.ShortFractTy;
      } else if (Literal.isLong) {
        Ty = Context.LongFractTy;
      } else {
        Ty = Context.FractTy;
      }
    }

    if (Literal.isUnsigned)
      Ty = Context.getCorrespondingUnsignedType(Ty);

    bool isSigned = !Literal.isUnsigned;
    unsigned scale = Context.getFixedPointScale(Ty);
    unsigned bit_width = Context.getTypeInfo(Ty).Width;

    llvm::APInt Val(bit_width, 0, isSigned);
    bool Overflowed = Literal.getFixedPointValue(Val, scale);
    bool ValIsZero = Val.isZero() && !Overflowed;

    auto MaxVal = Context.getFixedPointMax(Ty).getValue();
    if (Literal.isFract && Val == MaxVal + 1 && !ValIsZero)
      // Clause 6.4.4 - The value of a constant shall be in the range of
      // representable values for its type, with exception for constants of a
      // fract type with a value of exactly 1; such a constant shall denote
      // the maximal value for the type.
      --Val;
    else if (Val.ugt(MaxVal) || Overflowed)
      Diag(Tok.getLocation(), diag::err_too_large_for_fixed_point);

    Res = FixedPointLiteral::CreateFromRawInt(Context, Val, Ty,
                                              Tok.getLocation(), scale);
  } else if (Literal.isFloatingLiteral()) {
    QualType Ty;
    if (Literal.isHalf) {
      Diag(Tok.getLocation(), diag::err_half_const_requires_fp16);
      return ExprError();
    } else if (Literal.isFloat)
      Ty = Context.FloatTy;
    else if (Literal.isLong)
      Ty = Context.LongDoubleTy;
    else if (Literal.isFloat16)
      Ty = Context.Float16Ty;
    else if (Literal.isFloat128)
      Ty = Context.Float128Ty;
    else
      Ty = Context.DoubleTy;

    Res = formFloatingLiteral(*this, Literal, Ty, Tok.getLocation());

  } else if (!Literal.isIntegerLiteral()) {
    return ExprError();
  } else {
    QualType Ty;

    // size_t literal suffix (z/uz): rejected in this compiler.
    if (Literal.isSizeT)
      Diag(Tok.getLocation(), diag::err_size_t_suffix);

    if (Literal.isBitInt)
      PP.Diag(Tok.getLocation(), getLangOpts().C23
                                     ? diag::warn_c23_compat_bitint_suffix
                                     : diag::ext_c23_bitint_suffix);

    // Get the value in the widest-possible width. What is "widest" depends on
    // whether the literal is a bit-precise integer or not. For a bit-precise
    // integer type, try to scan the source to determine how many bits are
    // needed to represent the value. This may seem a bit expensive, but trying
    // to get the integer value from an overly-wide APInt is *extremely*
    // expensive, so the naive approach of assuming
    // llvm::IntegerType::MAX_INT_BITS is a big performance hit.
    unsigned BitsNeeded =
        Literal.isBitInt ? llvm::APInt::getSufficientBitsNeeded(
                               Literal.getLiteralDigits(), Literal.getRadix())
                         : Context.getTargetInfo().getIntMaxTWidth();
    llvm::APInt ResultVal(BitsNeeded, 0);

    if (Literal.getIntegerValue(ResultVal)) {
      // If this value didn't fit into uintmax_t, error and force to ull.
      Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
          << /* Unsigned */ 1;
      Ty = Context.UnsignedLongLongTy;
      assert(Context.getTypeSize(Ty) == ResultVal.getBitWidth() &&
             "long long is not intmax_t?");
    } else {
      // If this value fits into a ULL, try to figure out what else it fits into
      // according to the rules of C99 6.4.4.1p5.

      // Octal, Hexadecimal, and integers with a U suffix are allowed to
      // be an unsigned int.
      bool AllowUnsigned = Literal.isUnsigned || Literal.getRadix() != 10;

      // Check from smallest to largest, picking the smallest type we can.
      unsigned Width = 0;

      // Microsoft specific integer suffixes are explicitly sized.
      if (Literal.MicrosoftInteger) {
        if (Literal.MicrosoftInteger == 8 && !Literal.isUnsigned) {
          Width = 8;
          Ty = Context.CharTy;
        } else {
          Width = Literal.MicrosoftInteger;
          Ty = Context.getIntTypeForBitwidth(Width,
                                             /*Signed=*/!Literal.isUnsigned);
        }
      }

      // Bit-precise integer literals are automagically-sized based on the
      // width required by the literal.
      if (Literal.isBitInt) {
        // The signed version has one more bit for the sign value. There are no
        // zero-width bit-precise integers, even if the literal value is 0.
        Width = std::max(ResultVal.getActiveBits(), 1u) +
                (Literal.isUnsigned ? 0u : 1u);

        // Diagnose if the width of the constant is larger than BITINT_MAXWIDTH,
        // and reset the type to the largest supported width.
        unsigned int MaxBitIntWidth =
            Context.getTargetInfo().getMaxBitIntWidth();
        if (Width > MaxBitIntWidth) {
          Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
              << Literal.isUnsigned;
          Width = MaxBitIntWidth;
        }

        // Reset the result value to the smaller APInt and select the correct
        // type to be used. Note, we zext even for signed values because the
        // literal itself is always an unsigned value (a preceeding - is a
        // unary operator, not part of the literal).
        ResultVal = ResultVal.zextOrTrunc(Width);
        Ty = Context.getBitIntType(Literal.isUnsigned, Width);
      }

      // size_t literal suffix: pick size_t / ssize_t when in range.
      if (Literal.isSizeT) {
        assert(!Literal.MicrosoftInteger &&
               "size_t literals can't be Microsoft literals");
        unsigned SizeTSize = Context.getTargetInfo().getTypeWidth(
            Context.getTargetInfo().getSizeType());

        // Does it fit in size_t?
        if (ResultVal.isIntN(SizeTSize)) {
          // Does it fit in ssize_t?
          if (!Literal.isUnsigned && ResultVal[SizeTSize - 1] == 0)
            Ty = Context.getSignedSizeType();
          else if (AllowUnsigned)
            Ty = Context.getSizeType();
          Width = SizeTSize;
        }
      }

      if (Ty.isNull() && !Literal.isLong && !Literal.isLongLong &&
          !Literal.isSizeT) {
        // Are int/unsigned possibilities?
        unsigned IntSize = Context.getTargetInfo().getIntWidth();

        // Does it fit in a unsigned int?
        if (ResultVal.isIntN(IntSize)) {
          // Does it fit in a signed int?
          if (!Literal.isUnsigned && ResultVal[IntSize - 1] == 0)
            Ty = Context.IntTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedIntTy;
          Width = IntSize;
        }
      }

      // Are long/unsigned long possibilities?
      if (Ty.isNull() && !Literal.isLongLong && !Literal.isSizeT) {
        unsigned LongSize = Context.getTargetInfo().getLongWidth();

        // Does it fit in a unsigned long?
        if (ResultVal.isIntN(LongSize)) {
          // Does it fit in a signed long?
          if (!Literal.isUnsigned && ResultVal[LongSize - 1] == 0)
            Ty = Context.LongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongTy;
          // C90 6.1.3.2p5 (unsigned long for out-of-range unsigned decimal).
          else if (!getLangOpts().C99) {
            const unsigned LongLongSize =
                Context.getTargetInfo().getLongLongWidth();
            Diag(Tok.getLocation(), diag::warn_old_implicitly_unsigned_long)
                << (LongLongSize > LongSize ? /*will have type 'long long'*/ 0
                                            : /*will be ill-formed*/ 1);
            Ty = Context.UnsignedLongTy;
          }
          Width = LongSize;
        }
      }
      if (Ty.isNull() && !Literal.isSizeT) {
        unsigned LongLongSize = Context.getTargetInfo().getLongLongWidth();

        // Does it fit in a unsigned long long?
        if (ResultVal.isIntN(LongLongSize)) {
          // Does it fit in a signed long long?
          // To be compatible with MSVC, hex integer literals ending with the
          // LL or i64 suffix are always signed in Microsoft mode.
          if (!Literal.isUnsigned &&
              (ResultVal[LongLongSize - 1] == 0 ||
               (getLangOpts().MSVCCompat && Literal.isLongLong)))
            Ty = Context.LongLongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongLongTy;
          Width = LongLongSize;

          // Using long long (or needing its width) is a C99 extension in
          // pre-C99 modes.
          if (!getLangOpts().C99)
            Diag(Tok.getLocation(), diag::ext_c99_longlong);
        }
      }

      // If we still couldn't decide a type, we either have 'size_t' literal
      // that is out of range, or a decimal literal that does not fit in a
      // signed long long and has no U suffix.
      if (Ty.isNull()) {
        if (Literal.isSizeT)
          Diag(Tok.getLocation(), diag::err_size_t_literal_too_large)
              << Literal.isUnsigned;
        else
          Diag(Tok.getLocation(),
               diag::ext_integer_literal_too_large_for_signed);
        Ty = Context.UnsignedLongLongTy;
        Width = Context.getTargetInfo().getLongLongWidth();
      }

      if (ResultVal.getBitWidth() != Width)
        ResultVal = ResultVal.trunc(Width);
    }
    Res = IntegerLiteral::Create(Context, ResultVal, Ty, Tok.getLocation());
  }

  // If this is an imaginary literal, create the ImaginaryLiteral wrapper.
  if (Literal.isImaginary) {
    Res = new (Context)
        ImaginaryLiteral(Res, Context.getComplexType(Res->getType()));

    Diag(Tok.getLocation(), diag::ext_imaginary_constant);
  }
  return Res;
}

ExprResult Sema::OnParenExpr(SourceLocation L, SourceLocation R, Expr *E) {
  assert(E && "OnParenExpr() missing expr");
  QualType ExprTy = E->getType();
  if (getLangOpts().ProtectParens && CurFPFeatures.getAllowFPReassociate() &&
      !E->isLValue() && ExprTy->hasFloatingRepresentation())
    return FormBuiltinCallExpr(R, Builtin::BI__arithmetic_fence, E);
  return new (Context) ParenExpr(L, R, E);
}

namespace {
bool checkVectorElementsTraitOperandType(Sema &S, QualType T,
                                         SourceLocation Loc,
                                         SourceRange ArgRange) {
  // builtin_vectorelements supports both fixed-sized and scalable vectors.
  if (!T->isVectorType() && !T->isSizelessVectorType())
    return S.Diag(Loc, diag::err_builtin_non_vector_type)
           << ""
           << "__builtin_vectorelements" << T << ArgRange;

  return false;
}
} // namespace

namespace {
bool checkExtensionTraitOperandType(Sema &S, QualType T, SourceLocation Loc,
                                    SourceRange ArgRange,
                                    UnaryExprOrTypeTrait TraitKind) {
  // sizeof/alignof on function or void type is an extension.
  if (T->isFunctionType() &&
      (TraitKind == UETT_SizeOf || TraitKind == UETT_AlignOf ||
       TraitKind == UETT_PreferredAlignOf)) {
    // sizeof(function)/alignof(function) is allowed as an extension.
    S.Diag(Loc, diag::ext_sizeof_alignof_function_type)
        << getTraitSpelling(TraitKind) << ArgRange;
    return false;
  }

  if (T->isVoidType()) {
    S.Diag(Loc, diag::ext_sizeof_alignof_void_type)
        << getTraitSpelling(TraitKind) << ArgRange;
    return false;
  }

  return true;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnSizeofOnArrayDecay(Sema &S, SourceLocation Loc, QualType T,
                            const Expr *E) {
  // Don't warn if the operation changed the type.
  if (T != E->getType())
    return;

  // Now look for array decays.
  const auto *ICE = dyn_cast<ImplicitCastExpr>(E);
  if (!ICE || ICE->getCastKind() != CK_ArrayToPointerDecay)
    return;

  S.Diag(Loc, diag::warn_sizeof_array_decay)
      << ICE->getSourceRange() << ICE->getType()
      << ICE->getSubExpr()->getType();
}
} // namespace

bool Sema::CheckUnaryExprOrTypeTraitOperand(Expr *E,
                                            UnaryExprOrTypeTrait ExprKind) {
  QualType ExprTy = E->getType();

  bool IsUnevaluatedOperand =
      (ExprKind == UETT_SizeOf || ExprKind == UETT_AlignOf ||
       ExprKind == UETT_PreferredAlignOf);

  // The operand for sizeof and alignof is in an unevaluated expression context,
  // so side effects could result in unintended consequences.
  if (IsUnevaluatedOperand && !E->getType()->isVariableArrayType() &&
      E->HasSideEffects(Context, false))
    Diag(E->getExprLoc(), diag::warn_side_effects_unevaluated_context);

  if (ExprKind == UETT_VectorElements)
    return checkVectorElementsTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                               E->getSourceRange());

  // Explicitly list some types as extensions.
  if (!checkExtensionTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                      E->getSourceRange(), ExprKind))
    return false;

  // 'alignof' applied to an expression only requires the base element type of
  // the expression to be complete. 'sizeof' requires the expression's type to
  // be complete (and will attempt to complete it if it's an array of unknown
  // bound).
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    if (RequireCompleteSizedType(
            E->getExprLoc(), Context.getBaseElementType(E->getType()),
            diag::err_sizeof_alignof_incomplete_or_sizeless_type,
            getTraitSpelling(ExprKind), E->getSourceRange()))
      return true;
  } else {
    if (RequireCompleteSizedExprType(
            E, diag::err_sizeof_alignof_incomplete_or_sizeless_type,
            getTraitSpelling(ExprKind), E->getSourceRange()))
      return true;
  }

  ExprTy = E->getType();

  if (ExprTy->isFunctionType()) {
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_function_type)
        << getTraitSpelling(ExprKind) << E->getSourceRange();
    return true;
  }

  if (ExprKind == UETT_SizeOf) {
    if (const auto *DeclRef = dyn_cast<DeclRefExpr>(E->IgnoreParens())) {
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DeclRef->getFoundDecl())) {
        QualType OType = PVD->getOriginalType();
        QualType Type = PVD->getType();
        if (Type->isPointerType() && OType->isArrayType()) {
          Diag(E->getExprLoc(), diag::warn_sizeof_array_param) << Type << OType;
          Diag(PVD->getLocation(), diag::note_declared_at);
        }
      }
    }

    // Warn on "sizeof(array op x)" and "sizeof(x op array)", where the array
    // decays into a pointer and returns an unintended result. This is most
    // likely a typo for "sizeof(array) op x".
    if (const auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParens())) {
      warnSizeofOnArrayDecay(*this, BO->getOperatorLoc(), BO->getType(),
                             BO->getLHS());
      warnSizeofOnArrayDecay(*this, BO->getOperatorLoc(), BO->getType(),
                             BO->getRHS());
    }
  }

  return false;
}

namespace {
bool checkAlignOfExpr(Sema &S, Expr *E, UnaryExprOrTypeTrait ExprKind) {
  if (E->getObjectKind() == OK_BitField) {
    S.Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield)
        << 1 << E->getSourceRange();
    return true;
  }

  ValueDecl *D = nullptr;
  Expr *Inner = E->IgnoreParens();
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Inner)) {
    D = DRE->getDecl();
  } else if (MemberExpr *ME = dyn_cast<MemberExpr>(Inner)) {
    D = ME->getMemberDecl();
  }

  // If it's a field, require the containing struct to have a
  // complete definition so that we can compute the layout.
  //
  // Member can appear outside a member expression (unevaluated contexts,
  // trailing return type, etc.).
  //
  // For the record, since __alignof__ on expressions is a GCC
  // extension, GCC seems to permit this but always gives the
  // nonsensical answer 0.
  //
  // We don't really need the layout here --- we could instead just
  // directly check for all the appropriate alignment-lowing
  // attributes --- but that would require duplicating a lot of
  // logic that just isn't worth duplicating for such a marginal
  // use-case.
  if (FieldDecl *FD = dyn_cast_or_null<FieldDecl>(D)) {
    // Fast path this check, since we at least know the record has a
    // definition if we can find a member of it.
    if (!FD->getParent()->isCompleteDefinition()) {
      S.Diag(E->getExprLoc(), diag::err_alignof_member_of_incomplete_type)
          << E->getSourceRange();
      return true;
    }

    // Otherwise the field must have a complete type (or be a flexible array
    // member, which we explicitly want to white-list anyway), which makes the
    // following checks trivial.
    return false;
  }

  return S.CheckUnaryExprOrTypeTraitOperand(E, ExprKind);
}
} // namespace

bool Sema::CheckUnaryExprOrTypeTraitOperand(QualType ExprType,
                                            SourceLocation OpLoc,
                                            SourceRange ExprRange,
                                            UnaryExprOrTypeTrait ExprKind,
                                            llvm::StringRef KWName) {
  // alignof/_Alignof on an array: alignment of the element type
  // (C11 6.5.3.4/3).
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf)
    ExprType = Context.getBaseElementType(ExprType);

  if (ExprKind == UETT_VectorElements)
    return checkVectorElementsTraitOperandType(*this, ExprType, OpLoc,
                                               ExprRange);

  // Explicitly list some types as extensions.
  if (!checkExtensionTraitOperandType(*this, ExprType, OpLoc, ExprRange,
                                      ExprKind))
    return false;

  if (RequireCompleteSizedType(
          OpLoc, ExprType, diag::err_sizeof_alignof_incomplete_or_sizeless_type,
          KWName, ExprRange))
    return true;

  if (ExprType->isFunctionType()) {
    Diag(OpLoc, diag::err_sizeof_alignof_function_type) << KWName << ExprRange;
    return true;
  }

  return false;
}

ExprResult Sema::CreateUnaryExprOrTypeTraitExpr(TypeSourceInfo *TInfo,
                                                SourceLocation OpLoc,
                                                UnaryExprOrTypeTrait ExprKind,
                                                SourceRange R) {
  if (!TInfo)
    return ExprError();

  QualType T = TInfo->getType();

  if (CheckUnaryExprOrTypeTraitOperand(T, OpLoc, R, ExprKind,
                                       getTraitSpelling(ExprKind)))
    return ExprError();

  // Adds overload of TransformToPotentiallyEvaluated for TypeSourceInfo to
  // properly deal with VLAs in nested calls of sizeof and typeof.
  if (isUnevaluatedContext() && ExprKind == UETT_SizeOf &&
      TInfo->getType()->isVariablyModifiedType())
    TInfo = TransformToPotentiallyEvaluated(TInfo);

  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, TInfo, Context.getSizeType(), OpLoc, R.getEnd());
}

ExprResult Sema::CreateUnaryExprOrTypeTraitExpr(Expr *E, SourceLocation OpLoc,
                                                UnaryExprOrTypeTrait ExprKind) {
  ExprResult PE = CheckPlaceholderExpr(E);
  if (PE.isInvalid())
    return ExprError();

  E = PE.get();

  // Verify that the operand is valid.
  bool isInvalid = false;
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    isInvalid = checkAlignOfExpr(*this, E, ExprKind);
  } else if (E->refersToBitField()) {
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield) << 0;
    isInvalid = true;
  } else if (ExprKind == UETT_VectorElements) {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_VectorElements);
  } else {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_SizeOf);
  }

  if (isInvalid)
    return ExprError();

  if (ExprKind == UETT_SizeOf && E->getType()->isVariableArrayType()) {
    PE = TransformToPotentiallyEvaluated(E);
    if (PE.isInvalid())
      return ExprError();
    E = PE.get();
  }

  // Result type is size_t.
  return new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, E, Context.getSizeType(), OpLoc, E->getSourceRange().getEnd());
}

ExprResult Sema::OnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc,
                                            UnaryExprOrTypeTrait ExprKind,
                                            bool IsType, void *TyOrEx,
                                            SourceRange ArgRange) {
  // If error parsing type, ignore.
  if (!TyOrEx)
    return ExprError();

  if (IsType) {
    TypeSourceInfo *TInfo;
    (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(TyOrEx), &TInfo);
    return CreateUnaryExprOrTypeTraitExpr(TInfo, OpLoc, ExprKind, ArgRange);
  }

  Expr *ArgEx = (Expr *)TyOrEx;
  ExprResult Result = CreateUnaryExprOrTypeTraitExpr(ArgEx, OpLoc, ExprKind);
  return Result;
}

bool Sema::CheckAlignasTypeArgument(llvm::StringRef KWName,
                                    TypeSourceInfo *TInfo, SourceLocation OpLoc,
                                    SourceRange R) {
  if (!TInfo)
    return true;
  return CheckUnaryExprOrTypeTraitOperand(TInfo->getType(), OpLoc, R,
                                          UETT_AlignOf, KWName);
}

bool Sema::OnAlignasTypeArgument(llvm::StringRef KWName, ParsedType Ty,
                                 SourceLocation OpLoc, SourceRange R) {
  TypeSourceInfo *TInfo;
  (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(Ty.getAsOpaquePtr()),
                          &TInfo);
  return CheckAlignasTypeArgument(KWName, TInfo, OpLoc, R);
}

namespace {
QualType checkRealImagOperand(Sema &S, ExprResult &V, SourceLocation Loc,
                              bool IsReal) {
  if (V.get()->isTypeDependent())
    return S.Context.DependentTy;

  // _Real and _Imag are only l-values for normal l-values.
  if (V.get()->getObjectKind() != OK_Ordinary) {
    V = S.DefaultLvalueConversion(V.get());
    if (V.isInvalid())
      return QualType();
  }

  // These operators return the element type of a complex type.
  if (const ComplexType *CT = V.get()->getType()->getAs<ComplexType>())
    return CT->getElementType();

  // Otherwise they pass through real integer and floating point types here.
  if (V.get()->getType()->isArithmeticType())
    return V.get()->getType();

  // Test for placeholders.
  ExprResult PR = S.CheckPlaceholderExpr(V.get());
  if (PR.isInvalid())
    return QualType();
  if (PR.get() != V.get()) {
    V = PR;
    return checkRealImagOperand(S, V, Loc, IsReal);
  }

  // Reject anything else.
  S.Diag(Loc, diag::err_realimag_invalid_type)
      << V.get()->getType() << (IsReal ? "__real" : "__imag");
  return QualType();
}
} // namespace

ExprResult Sema::OnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                                  tok::TokenKind Kind, Expr *Input) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default:
    llvm_unreachable("Unknown unary op!");
  case tok::plusplus:
    Opc = UO_PostInc;
    break;
  case tok::minusminus:
    Opc = UO_PostDec;
    break;
  }

  // Since this might is a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Input);
  if (Result.isInvalid())
    return ExprError();
  Input = Result.get();

  return FormUnaryOp(S, OpLoc, Opc, Input);
}

// Returns the type used for LHS[RHS], given one of LHS, RHS is type-dependent.
// Typically this is DependentTy, but can sometimes be more precise.
// ===----------------------------------------------------------------------===
// Array subscript & matrix subscript
// ===----------------------------------------------------------------------===

namespace {
bool resolveArgPlaceholders(Sema &S, MultiExprArg args);
} // namespace

ExprResult Sema::OnArraySubscriptExpr(Scope *S, Expr *base,
                                      SourceLocation lbLoc,
                                      MultiExprArg ArgExprs,
                                      SourceLocation rbLoc) {

  // Since this might be a postfix expression, get rid of ParenListExprs.
  if (isa<ParenListExpr>(base)) {
    ExprResult result = MaybeConvertParenListExprToParenExpr(S, base);
    if (result.isInvalid())
      return ExprError();
    base = result.get();
  }

  // Check if base and idx form a MatrixSubscriptExpr.
  //
  // Helper to check for comma expressions, which are not allowed as indices for
  // matrix subscript expressions.
  auto CheckAndReportCommaError = [this, base, rbLoc](Expr *E) {
    if (isa<BinaryOperator>(E) && cast<BinaryOperator>(E)->isCommaOp()) {
      Diag(E->getExprLoc(), diag::err_matrix_subscript_comma)
          << SourceRange(base->getBeginLoc(), rbLoc);
      return true;
    }
    return false;
  };
  // The matrix subscript operator ([][])is considered a single operator.
  // Separating the index expressions by parenthesis is not allowed.
  if (base && !base->getType().isNull() &&
      base->hasPlaceholderType(BuiltinType::IncompleteMatrixIdx) &&
      !isa<MatrixSubscriptExpr>(base)) {
    Diag(base->getExprLoc(), diag::err_matrix_separate_incomplete_index)
        << SourceRange(base->getBeginLoc(), rbLoc);
    return ExprError();
  }
  // If the base is a MatrixSubscriptExpr, try to create a new
  // MatrixSubscriptExpr.
  auto *matSubscriptE = dyn_cast<MatrixSubscriptExpr>(base);
  if (matSubscriptE) {
    assert(ArgExprs.size() == 1);
    if (CheckAndReportCommaError(ArgExprs.front()))
      return ExprError();

    assert(matSubscriptE->isIncomplete() &&
           "base has to be an incomplete matrix subscript");
    return CreateBuiltinMatrixSubscriptExpr(matSubscriptE->getBase(),
                                            matSubscriptE->getRowIdx(),
                                            ArgExprs.front(), rbLoc);
  }
  // Handle any non-overload placeholder types in the base and index
  // expressions.  We can't handle overloads here because the other
  // operand might be an overloadable type, in which case the overload
  // resolution for the operator overload should get the first crack
  // at the overload.
  if (base->getType()->isNonOverloadPlaceholderType()) {
    {
      ExprResult result = CheckPlaceholderExpr(base);
      if (result.isInvalid())
        return ExprError();
      base = result.get();
    }
  }

  // If the base is a matrix type, try to create a new MatrixSubscriptExpr.
  if (base->getType()->isMatrixType()) {
    assert(ArgExprs.size() == 1);
    if (CheckAndReportCommaError(ArgExprs.front()))
      return ExprError();

    return CreateBuiltinMatrixSubscriptExpr(base, ArgExprs.front(), nullptr,
                                            rbLoc);
  }

  if (ArgExprs.size() == 1 &&
      ArgExprs[0]->getType()->isNonOverloadPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(ArgExprs[0]);
    if (result.isInvalid())
      return ExprError();
    ArgExprs[0] = result.get();
  } else {
    if (resolveArgPlaceholders(*this, ArgExprs))
      return ExprError();
  }

  // NeverC builtin string `s[i]` rewrite -> `neverc_string_at(s, i)`.
  // The dotted-call `s.at(i)` already reaches the same runtime helper
  // through `BuiltinStringMethodNames.def`, but std::string users
  // expect `s[i]` to work too.  Plain `char buf[N]; buf[i]` keeps
  // reaching the regular array path because `buf`'s type is
  // `char[N]`, not the NeverC `string` record -- only base expressions
  // whose evaluated type IS the builtin string type take this branch.
  // The receiver is consumed by the runtime helper per the by-value
  // contract; an lvalue receiver gets the implicit retain copy Sema
  // inserts in `GatherArgumentsForCall` so writing `s[i]` does not
  // dangle the caller's owned buffer.  Read-only by construction: the
  // helper returns `char` (prvalue), so `s[i] = ch` is rejected at
  // ordinary assignment-expression typing time -- mutation has to go
  // through `s.replace_char(i, 1, ch)` / `s = ...` instead, matching
  // the value-typed mutation contract the rest of the surface uses.
  if (ArgExprs.size() == 1 && this->isNeverCStringType(base->getType())) {
    Expr *IdxExpr = ArgExprs[0];
    QualType IdxTy = IdxExpr->getType();
    if (!IdxTy->isIntegerType() || IdxTy->isBooleanType()) {
      Diag(IdxExpr->getBeginLoc(), diag::err_typecheck_subscript_not_integer)
          << IdxExpr->getSourceRange();
      return ExprError();
    }
    ExprResult IdxRes = DefaultLvalueConversion(IdxExpr);
    if (IdxRes.isInvalid())
      return ExprError();
    QualType SizeTy = Context.getSizeType();
    if (IdxRes.get()->getType() != SizeTy) {
      IdxRes = ImpCastExprToType(IdxRes.get(), SizeTy, CK_IntegralCast);
      if (IdxRes.isInvalid())
        return ExprError();
    }
    Expr *Args[] = {base, IdxRes.get()};
    return buildNeverCStringRuntimeCall(
        *this, S, lbLoc, BuiltinStringNames::AtFunctionName, Args, rbLoc);
  }

  ExprResult Res =
      CreateBuiltinArraySubscriptExpr(base, lbLoc, ArgExprs.front(), rbLoc);

  if (!Res.isInvalid() && isa<ArraySubscriptExpr>(Res.get()))
    CheckSubscriptAccessOfNoDeref(cast<ArraySubscriptExpr>(Res.get()));

  return Res;
}

ExprResult Sema::tryConvertExprToType(Expr *E, QualType Ty) {
  InitializedEntity Entity = InitializedEntity::InitializeTemporary(Ty);
  InitializationKind Kind =
      InitializationKind::CreateCopy(E->getBeginLoc(), SourceLocation());
  InitializationSequence InitSeq(*this, Entity, Kind, E);
  return InitSeq.Perform(*this, Entity, Kind, E);
}

ExprResult Sema::CreateBuiltinMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                                  Expr *ColumnIdx,
                                                  SourceLocation RBLoc) {
  ExprResult BaseR = CheckPlaceholderExpr(Base);
  if (BaseR.isInvalid())
    return BaseR;
  Base = BaseR.get();

  ExprResult RowR = CheckPlaceholderExpr(RowIdx);
  if (RowR.isInvalid())
    return RowR;
  RowIdx = RowR.get();

  if (!ColumnIdx)
    return new (Context) MatrixSubscriptExpr(
        Base, RowIdx, ColumnIdx, Context.IncompleteMatrixIdxTy, RBLoc);

  // Build an unanalyzed expression if any of the operands is type-dependent.

  ExprResult ColumnR = CheckPlaceholderExpr(ColumnIdx);
  if (ColumnR.isInvalid())
    return ColumnR;
  ColumnIdx = ColumnR.get();

  // Check that IndexExpr is an integer expression. If it is a constant
  // expression, check that it is less than Dim (= the number of elements in the
  // corresponding dimension).
  auto IsIndexValid = [&](Expr *IndexExpr, unsigned Dim,
                          bool IsColumnIdx) -> Expr * {
    if (!IndexExpr->getType()->isIntegerType()) {
      Diag(IndexExpr->getBeginLoc(), diag::err_matrix_index_not_integer)
          << IsColumnIdx;
      return nullptr;
    }

    if (std::optional<llvm::APSInt> Idx =
            IndexExpr->getIntegerConstantExpr(Context)) {
      if ((*Idx < 0 || *Idx >= Dim)) {
        Diag(IndexExpr->getBeginLoc(), diag::err_matrix_index_outside_range)
            << IsColumnIdx << Dim;
        return nullptr;
      }
    }

    ExprResult ConvExpr =
        tryConvertExprToType(IndexExpr, Context.getSizeType());
    assert(!ConvExpr.isInvalid() &&
           "should be able to convert any integer type to size type");
    return ConvExpr.get();
  };

  auto *MTy = Base->getType()->getAs<ConstantMatrixType>();
  RowIdx = IsIndexValid(RowIdx, MTy->getNumRows(), false);
  ColumnIdx = IsIndexValid(ColumnIdx, MTy->getNumColumns(), true);
  if (!RowIdx || !ColumnIdx)
    return ExprError();

  return new (Context) MatrixSubscriptExpr(Base, RowIdx, ColumnIdx,
                                           MTy->getElementType(), RBLoc);
}

void Sema::CheckAddressOfNoDeref(const Expr *E) {
  ExpressionEvaluationContextRecord &LastRecord = ExprEvalContexts.back();
  const Expr *StrippedExpr = E->IgnoreParenImpCasts();

  // For expressions like `&(*s).b`, the base is recorded and what should be
  // checked.
  const MemberExpr *Member = nullptr;
  while ((Member = dyn_cast<MemberExpr>(StrippedExpr)) && !Member->isArrow())
    StrippedExpr = Member->getBase()->IgnoreParenImpCasts();

  LastRecord.PossibleDerefs.erase(StrippedExpr);
}

void Sema::CheckSubscriptAccessOfNoDeref(const ArraySubscriptExpr *E) {
  if (isUnevaluatedContext())
    return;

  QualType ResultTy = E->getType();
  ExpressionEvaluationContextRecord &LastRecord = ExprEvalContexts.back();

  // Bail if the element is an array since it is not memory access.
  if (isa<ArrayType>(ResultTy))
    return;

  if (ResultTy->hasAttr(attr::NoDeref)) {
    LastRecord.PossibleDerefs.insert(E);
    return;
  }

  // Check if the base type is a pointer to a member access of a struct
  // marked with noderef.
  const Expr *Base = E->getBase();
  QualType BaseTy = Base->getType();
  if (!(isa<ArrayType>(BaseTy) || isa<PointerType>(BaseTy)))
    // Not a pointer access
    return;

  const MemberExpr *Member = nullptr;
  while ((Member = dyn_cast<MemberExpr>(Base->IgnoreParenCasts())) &&
         Member->isArrow())
    Base = Member->getBase();

  if (const auto *Ptr = dyn_cast<PointerType>(Base->getType())) {
    if (Ptr->getPointeeType()->hasAttr(attr::NoDeref))
      LastRecord.PossibleDerefs.insert(E);
  }
}

ExprResult Sema::CreateBuiltinArraySubscriptExpr(Expr *Base,
                                                 SourceLocation LLoc, Expr *Idx,
                                                 SourceLocation RLoc) {
  Expr *LHSExp = Base;
  Expr *RHSExp = Idx;

  ExprValueKind VK = VK_LValue;
  ExprObjectKind OK = OK_Ordinary;

  if (LLVM_LIKELY(!LHSExp->getType()->getAs<VectorType>())) {
    ExprResult Result = DefaultFunctionArrayLvalueConversion(LHSExp);
    if (LLVM_UNLIKELY(Result.isInvalid()))
      return ExprError();
    LHSExp = Result.get();
  }
  ExprResult Result = DefaultFunctionArrayLvalueConversion(RHSExp);
  if (LLVM_UNLIKELY(Result.isInvalid()))
    return ExprError();
  RHSExp = Result.get();

  QualType LHSTy = LHSExp->getType(), RHSTy = RHSExp->getType();

  // C99 6.5.2.1p2: the expression e1[e2] is by definition precisely equivalent
  // to the expression *((e1)+(e2)). This means the array "Base" may actually be
  // in the subscript position. As a result, we need to derive the array base
  // and index from the expression types.
  Expr *BaseExpr, *IndexExpr;
  QualType ResultType;
  if (const PointerType *PTy = LHSTy->getAs<PointerType>()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const PointerType *PTy = RHSTy->getAs<PointerType>()) {
    // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const VectorType *VTy = LHSTy->getAs<VectorType>()) {
    BaseExpr = LHSExp; // vectors: V[123]
    IndexExpr = RHSExp;
    VK = LHSExp->getValueKind();
    if (VK != VK_PRValue)
      OK = OK_VectorComponent;

    ResultType = VTy->getElementType();
    QualType BaseType = BaseExpr->getType();
    Qualifiers BaseQuals = BaseType.getQualifiers();
    Qualifiers MemberQuals = ResultType.getQualifiers();
    Qualifiers Combined = BaseQuals + MemberQuals;
    if (Combined != MemberQuals)
      ResultType = Context.getQualifiedType(ResultType, Combined);
  } else if (LHSTy->isBuiltinType() &&
             LHSTy->getAs<BuiltinType>()->isSveVLSBuiltinType()) {
    const BuiltinType *BTy = LHSTy->getAs<BuiltinType>();
    if (BTy->isSVEBool())
      return ExprError(Diag(LLoc, diag::err_subscript_svbool_t)
                       << LHSExp->getSourceRange() << RHSExp->getSourceRange());

    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    VK = LHSExp->getValueKind();
    if (VK != VK_PRValue)
      OK = OK_VectorComponent;

    ResultType = BTy->getSveEltType(Context);

    QualType BaseType = BaseExpr->getType();
    Qualifiers BaseQuals = BaseType.getQualifiers();
    Qualifiers MemberQuals = ResultType.getQualifiers();
    Qualifiers Combined = BaseQuals + MemberQuals;
    if (Combined != MemberQuals)
      ResultType = Context.getQualifiedType(ResultType, Combined);
  } else if (LHSTy->isArrayType()) {
    // If we see an array that wasn't promoted by
    // DefaultFunctionArrayLvalueConversion, it must be an array that
    // wasn't promoted because of the C90 rule that doesn't
    // allow promoting non-lvalue arrays.  Warn, then
    // force the promotion here.
    Diag(LHSExp->getBeginLoc(), diag::ext_subscript_non_lvalue)
        << LHSExp->getSourceRange();
    LHSExp = ImpCastExprToType(LHSExp, Context.getArrayDecayedType(LHSTy),
                               CK_ArrayToPointerDecay)
                 .get();
    LHSTy = LHSExp->getType();

    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = LHSTy->castAs<PointerType>()->getPointeeType();
  } else if (RHSTy->isArrayType()) {
    // Same as previous, except for 123[f().a] case
    Diag(RHSExp->getBeginLoc(), diag::ext_subscript_non_lvalue)
        << RHSExp->getSourceRange();
    RHSExp = ImpCastExprToType(RHSExp, Context.getArrayDecayedType(RHSTy),
                               CK_ArrayToPointerDecay)
                 .get();
    RHSTy = RHSExp->getType();

    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = RHSTy->castAs<PointerType>()->getPointeeType();
  } else {
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_value)
                     << LHSExp->getSourceRange() << RHSExp->getSourceRange());
  }
  // C99 6.5.2.1p1
  if (!IndexExpr->getType()->isIntegerType())
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_not_integer)
                     << IndexExpr->getSourceRange());

  if (IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
      IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_U)) {
    std::optional<llvm::APSInt> IntegerContantExpr =
        IndexExpr->getIntegerConstantExpr(getTreeContext());
    if (!IntegerContantExpr.has_value() ||
        IntegerContantExpr.value().isNegative())
      Diag(LLoc, diag::warn_subscript_is_char) << IndexExpr->getSourceRange();
  }

  // Subscript base must be pointer to object type (not function); complete
  // object type required except where dependent.
  if (ResultType->isFunctionType()) {
    Diag(BaseExpr->getBeginLoc(), diag::err_subscript_function_type)
        << ResultType << BaseExpr->getSourceRange();
    return ExprError();
  }

  if (ResultType->isVoidType()) {
    // GNU extension: subscripting on pointer to void
    Diag(LLoc, diag::ext_gnu_subscript_void_type) << BaseExpr->getSourceRange();

    // C forbids expressions of unqualified void type from being l-values.
    // See IsCForbiddenLValueType.
    if (!ResultType.hasQualifiers())
      VK = VK_PRValue;
  } else if (RequireCompleteSizedType(
                 LLoc, ResultType,
                 diag::err_subscript_incomplete_or_sizeless_type, BaseExpr))
    return ExprError();

  assert(VK == VK_PRValue || !ResultType.isCForbiddenLValueType());

  return new (Context)
      ArraySubscriptExpr(LHSExp, RHSExp, ResultType, VK, OK, RLoc);
}

// ===----------------------------------------------------------------------===
// Call expressions & argument conversion
// ===----------------------------------------------------------------------===

bool Sema::ConvertArgumentsForCall(CallExpr *Call, Expr *Fn,
                                   FunctionDecl *FDecl,
                                   const FunctionProtoType *Proto,
                                   llvm::ArrayRef<Expr *> Args,
                                   SourceLocation RParenLoc) {
  // Bail out early if calling a builtin with custom typechecking.
  if (FDecl)
    if (unsigned ID = FDecl->getBuiltinID())
      if (Context.BuiltinInfo.hasCustomTypechecking(ID))
        return false;

  // C99 6.5.2.2p7 - the arguments are implicitly converted, as if by
  // assignment, to the types of the corresponding parameter, ...
  unsigned NumParams = Proto->getNumParams();
  bool Invalid = false;
  unsigned MinArgs = FDecl ? FDecl->getMinRequiredArguments() : NumParams;

  // If too few arguments are available (and we don't have default
  // arguments for the remaining parameters), don't make the call.
  if (Args.size() < NumParams) {
    if (Args.size() < MinArgs) {
      if (MinArgs == 1 && FDecl && FDecl->getParamDecl(0)->getDeclName())
        Diag(RParenLoc,
             MinArgs == NumParams && !Proto->isVariadic()
                 ? diag::err_typecheck_call_too_few_args_one
                 : diag::err_typecheck_call_too_few_args_at_least_one)
            << FDecl->getParamDecl(0) << Fn->getSourceRange();
      else
        Diag(RParenLoc, MinArgs == NumParams && !Proto->isVariadic()
                            ? diag::err_typecheck_call_too_few_args
                            : diag::err_typecheck_call_too_few_args_at_least)
            << MinArgs << static_cast<unsigned>(Args.size())
            << Fn->getSourceRange();

      if (FDecl && !FDecl->getBuiltinID())
        Diag(FDecl->getLocation(), diag::note_callee_decl)
            << FDecl << FDecl->getParametersSourceRange();

      return true;
    }
    // We reserve space for the default arguments when we create
    // the call expression, before calling ConvertArgumentsForCall.
    assert((Call->getNumArgs() == NumParams) &&
           "We should have reserved space for the default arguments before!");
  }

  // If too many are passed and not variadic, error on the extras and drop
  // them.
  if (Args.size() > NumParams) {
    if (!Proto->isVariadic()) {
      if (NumParams == 1 && FDecl && FDecl->getParamDecl(0)->getDeclName())
        Diag(Args[NumParams]->getBeginLoc(),
             MinArgs == NumParams
                 ? diag::err_typecheck_call_too_many_args_one
                 : diag::err_typecheck_call_too_many_args_at_most_one)
            << FDecl->getParamDecl(0) << static_cast<unsigned>(Args.size())
            << Fn->getSourceRange()
            << SourceRange(Args[NumParams]->getBeginLoc(),
                           Args.back()->getEndLoc());
      else
        Diag(Args[NumParams]->getBeginLoc(),
             MinArgs == NumParams
                 ? diag::err_typecheck_call_too_many_args
                 : diag::err_typecheck_call_too_many_args_at_most)
            << NumParams << static_cast<unsigned>(Args.size())
            << Fn->getSourceRange()
            << SourceRange(Args[NumParams]->getBeginLoc(),
                           Args.back()->getEndLoc());

      if (FDecl && !FDecl->getBuiltinID())
        Diag(FDecl->getLocation(), diag::note_callee_decl)
            << FDecl << FDecl->getParametersSourceRange();

      // This deletes the extra arguments.
      Call->shrinkNumArgs(NumParams);
      return true;
    }
  }
  llvm::SmallVector<Expr *, 8> AllArgs;
  VariadicCallType CallType =
      (Proto && Proto->isVariadic()) ? VariadicFunction : VariadicDoesNotApply;

  Invalid = GatherArgumentsForCall(Call->getBeginLoc(), FDecl, Proto, 0, Args,
                                   AllArgs, CallType);
  if (Invalid)
    return true;
  unsigned TotalNumArgs = AllArgs.size();
  for (unsigned i = 0; i < TotalNumArgs; ++i)
    Call->setArg(i, AllArgs[i]);

  Call->computeDependence();
  return false;
}

bool Sema::GatherArgumentsForCall(SourceLocation CallLoc, FunctionDecl *FDecl,
                                  const FunctionProtoType *Proto,
                                  unsigned FirstParam,
                                  llvm::ArrayRef<Expr *> Args,
                                  llvm::SmallVectorImpl<Expr *> &AllArgs,
                                  VariadicCallType CallType) {
  unsigned NumParams = Proto->getNumParams();
  bool Invalid = false;
  size_t ArgIx = 0;

  // Hoist loop-invariant FDecl / context queries out of the per-param loop.
  bool IsNeverCStringRuntimeFn = FDecl && isNeverCStringRuntimeFD(FDecl);
  bool IsNeverCStringCStrFn = FDecl && isNeverCStringBorrowedViewFD(FDecl);
  bool InsideStringRuntime =
      IsNeverCStringRuntimeFn && isInsideNeverCStringRuntime();

  // Continue to check argument types (even if we have too few/many args).
  for (unsigned i = FirstParam; i < NumParams; i++) {
    QualType ProtoArgType = Proto->getParamType(i);

    Expr *Arg;
    ParmVarDecl *Param = FDecl ? FDecl->getParamDecl(i) : nullptr;
    if (ArgIx < Args.size()) {
      Arg = Args[ArgIx++];

      if (RequireCompleteType(Arg->getBeginLoc(), ProtoArgType,
                              diag::err_call_incomplete_argument, Arg))
        return true;

      bool IsNeverCStringParam =
          (IsNeverCStringRuntimeFn || IsNeverCStringCStrFn) &&
          this->isNeverCStringType(ProtoArgType);
      bool IsNeverCStringRuntimeArg =
          IsNeverCStringParam && IsNeverCStringRuntimeFn;
      bool IsNeverCStringCStrArg = IsNeverCStringParam && IsNeverCStringCStrFn;

      if (this->isNeverCStringType(ProtoArgType)) {
        if (StringLiteral *SL = getNeverCStringLiteral(Arg)) {
          ExprResult LiteralView =
              buildNeverCStringLiteral(*this, ProtoArgType, Arg, SL);
          if (LiteralView.isInvalid())
            return true;
          Arg = LiteralView.get();
        }
      }

      bool ArgIsNeverCString =
          (IsNeverCStringCStrArg || IsNeverCStringRuntimeArg)
              ? this->isNeverCStringType(Arg->getType())
              : false;

      if (IsNeverCStringCStrArg && ArgIsNeverCString &&
          Arg->getValueKind() == VK_PRValue) {
        Diag(Arg->getExprLoc(), diag::err_neverc_string_cstr_temporary)
            << Arg->getSourceRange();
        return true;
      }

      ExprResult ArgE;
      bool PassNeverCStringLValueDirect =
          IsNeverCStringRuntimeArg && ArgIsNeverCString &&
          Arg->getValueKind() == VK_LValue &&
          BuiltinString::isLValueDirectHelper(FDecl->getName(),
                                              InsideStringRuntime);
      if (PassNeverCStringLValueDirect) {
        ArgE = DefaultFunctionArrayLvalueConversion(Arg, /*Diagnose=*/false);
      } else {
        InitializedEntity Entity =
            Param ? InitializedEntity::InitializeParameter(Context, Param,
                                                           ProtoArgType)
                  : InitializedEntity::InitializeParameter(Context,
                                                           ProtoArgType, false);
        ArgE = PerformCopyInitialization(Entity, SourceLocation(), Arg);
      }
      if (ArgE.isInvalid())
        return true;

      Arg = ArgE.getAs<Expr>();
    } else {
      return true;
    }

    // Check for array bounds violations for each argument to the call. This
    // check only triggers warnings when the argument isn't a more complex Expr
    // with its own checking, such as a BinaryOperator.
    CheckArrayAccess(Arg);

    // Check for violations of C99 static array rules (C99 6.7.5.3p7).
    CheckStaticArrayArgument(CallLoc, Param, Arg);

    AllArgs.push_back(Arg);
  }

  // If this is a variadic call, handle args passed through "...".
  if (CallType != VariadicDoesNotApply) {
    // NeverC string variadic args: insert an implicit
    // `__neverc_string_retain` wrapper for every lvalue NeverC `string`
    // argument the caller hands to a runtime helper through the `...`
    // tail.  C's `DefaultVariadicArgumentPromotion` cannot do this on
    // its own (the standard exposes no per-arg conversion hook for
    // variadic), so without the wrapper the helper's by-value consume
    // contract would double-free with the caller's scope cleanup.
    // Mirrors the non-variadic copy-init path's PerformCopyInitialization
    // which yields the same retain copy through Sema's value-init logic.
    // Only fires when the called helper is a NeverC string runtime
    // function (`Sema::isNeverCStringRuntimeFD`); plain C variadic
    // calls (printf, ...) are untouched.
    // Argument promotion for variadic arguments (C99 6.5.2.2p7).
    for (Expr *A : Args.slice(ArgIx)) {
      Expr *ArgExpr = A;
      if (IsNeverCStringRuntimeFn &&
          this->isNeverCStringType(ArgExpr->getType()) &&
          ArgExpr->getValueKind() == VK_LValue) {
        Expr *RetainArgs[] = {ArgExpr};
        ExprResult Retained = buildNeverCStringRuntimeCall(
            *this, /*Scope=*/nullptr, ArgExpr->getExprLoc(),
            BuiltinStringNames::RetainFunctionName, RetainArgs,
            ArgExpr->getExprLoc());
        if (Retained.isInvalid()) {
          Invalid = true;
        } else {
          ArgExpr = Retained.get();
        }
      }
      ExprResult Arg = DefaultVariadicArgumentPromotion(ArgExpr);
      Invalid |= Arg.isInvalid();
      AllArgs.push_back(Arg.get());
    }

    for (Expr *A : Args.slice(ArgIx))
      CheckArrayAccess(A);
  }
  return Invalid;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnStaticArrayParam(Sema &S, ParmVarDecl *PVD) {
  TypeLoc TL = PVD->getTypeSourceInfo()->getTypeLoc();
  if (DecayedTypeLoc DTL = TL.getAs<DecayedTypeLoc>())
    TL = DTL.getOriginalLoc();
  if (ArrayTypeLoc ATL = TL.getAs<ArrayTypeLoc>())
    S.Diag(PVD->getLocation(), diag::note_callee_static_array)
        << ATL.getLocalSourceRange();
}
} // namespace

void Sema::CheckStaticArrayArgument(SourceLocation CallLoc, ParmVarDecl *Param,
                                    const Expr *ArgExpr) {
  if (!Param)
    return;

  QualType OrigTy = Param->getOriginalType();

  const ArrayType *AT = Context.getAsArrayType(OrigTy);
  if (!AT || AT->getSizeModifier() != ArraySizeModifier::Static)
    return;

  if (ArgExpr->isNullPointerConstant(Context, Expr::NPC_NeverValueDependent)) {
    Diag(CallLoc, diag::warn_null_arg) << ArgExpr->getSourceRange();
    warnStaticArrayParam(*this, Param);
    return;
  }

  const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT);
  if (!CAT)
    return;

  const ConstantArrayType *ArgCAT =
      Context.getAsConstantArrayType(ArgExpr->IgnoreParenCasts()->getType());
  if (!ArgCAT)
    return;

  if (getTreeContext().hasSameUnqualifiedType(CAT->getElementType(),
                                              ArgCAT->getElementType())) {
    if (ArgCAT->getSize().ult(CAT->getSize())) {
      Diag(CallLoc, diag::warn_static_array_too_small)
          << ArgExpr->getSourceRange()
          << (unsigned)ArgCAT->getSize().getZExtValue()
          << (unsigned)CAT->getSize().getZExtValue() << 0;
      warnStaticArrayParam(*this, Param);
    }
    return;
  }

  std::optional<CharUnits> ArgSize =
      getTreeContext().getTypeSizeInCharsIfKnown(ArgCAT);
  std::optional<CharUnits> ParmSize =
      getTreeContext().getTypeSizeInCharsIfKnown(CAT);
  if (ArgSize && ParmSize && *ArgSize < *ParmSize) {
    Diag(CallLoc, diag::warn_static_array_too_small)
        << ArgExpr->getSourceRange() << (unsigned)ArgSize->getQuantity()
        << (unsigned)ParmSize->getQuantity() << 1;
    warnStaticArrayParam(*this, Param);
  }
}

namespace {
bool isStrippablePlaceholderArg(QualType type) {
  // Placeholders are never sugared.
  const BuiltinType *placeholder = dyn_cast<BuiltinType>(type);
  if (!placeholder)
    return false;

  switch (placeholder->getKind()) {
    // Ignore all the non-placeholder types.
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
#define PLACEHOLDER_TYPE(ID, SINGLETON_ID)
#define BUILTIN_TYPE(ID, SINGLETON_ID) case BuiltinType::ID:
#include "neverc/Tree/Type/BuiltinTypes.def"
    return false;

  // We cannot lower out overload sets; they might validly be resolved
  // by the call machinery.
  case BuiltinType::Overload:
    return false;

  case BuiltinType::PseudoObject:
  case BuiltinType::BuiltinFn:
  case BuiltinType::IncompleteMatrixIdx:
    return true;
  }
  llvm_unreachable("bad builtin type kind");
}
} // namespace

namespace {
bool resolveArgPlaceholders(Sema &S, MultiExprArg args) {
  // Apply this processing to all the arguments at once instead of
  // dying at the first failure.
  bool hasInvalid = false;
  for (size_t i = 0, e = args.size(); i != e; i++) {
    if (isStrippablePlaceholderArg(args[i]->getType())) {
      ExprResult result = S.CheckPlaceholderExpr(args[i]);
      if (result.isInvalid())
        hasInvalid = true;
      else
        args[i] = result.get();
    }
  }
  return hasInvalid;
}
} // namespace

namespace {
FunctionDecl *rewriteBuiltinFunctionDecl(Sema *Sema, TreeContext &Context,
                                         FunctionDecl *FDecl,
                                         MultiExprArg ArgExprs) {

  QualType DeclType = FDecl->getType();
  const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(DeclType);

  if (!Context.BuiltinInfo.hasPtrArgsOrResult(FDecl->getBuiltinID()) || !FT ||
      ArgExprs.size() < FT->getNumParams())
    return nullptr;

  bool NeedsNewDecl = false;
  unsigned i = 0;
  llvm::SmallVector<QualType, 8> OverloadParams;

  for (QualType ParamType : FT->param_types()) {

    // Convert array arguments to pointer to simplify type lookup.
    ExprResult ArgRes =
        Sema->DefaultFunctionArrayLvalueConversion(ArgExprs[i++]);
    if (ArgRes.isInvalid())
      return nullptr;
    Expr *Arg = ArgRes.get();
    QualType ArgType = Arg->getType();
    if (!ParamType->isPointerType() || ParamType.hasAddressSpace() ||
        !ArgType->isPointerType() ||
        !ArgType->getPointeeType().hasAddressSpace() ||
        isPtrSizeAddressSpace(ArgType->getPointeeType().getAddressSpace())) {
      OverloadParams.push_back(ParamType);
      continue;
    }

    QualType PointeeType = ParamType->getPointeeType();
    if (PointeeType.hasAddressSpace())
      continue;

    NeedsNewDecl = true;
    LangAS AS = ArgType->getPointeeType().getAddressSpace();

    PointeeType = Context.getAddrSpaceQualType(PointeeType, AS);
    OverloadParams.push_back(Context.getPointerType(PointeeType));
  }

  if (!NeedsNewDecl)
    return nullptr;

  FunctionProtoType::ExtProtoInfo EPI;
  EPI.Variadic = FT->isVariadic();
  QualType OverloadTy =
      Context.getFunctionType(FT->getReturnType(), OverloadParams, EPI);
  DeclContext *Parent = FDecl->getParent();
  FunctionDecl *OverloadDecl = FunctionDecl::Create(
      Context, Parent, FDecl->getLocation(), FDecl->getLocation(),
      FDecl->getIdentifier(), OverloadTy,
      /*TInfo=*/nullptr, SC_Extern, Sema->getCurFPFeatures().isFPConstrained(),
      false,
      /*hasPrototype=*/true);
  llvm::SmallVector<ParmVarDecl *, 16> Params;
  FT = cast<FunctionProtoType>(OverloadTy);
  for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
    QualType ParamType = FT->getParamType(i);
    ParmVarDecl *Parm =
        ParmVarDecl::Create(Context, OverloadDecl, SourceLocation(),
                            SourceLocation(), nullptr, ParamType,
                            /*TInfo=*/nullptr, SC_None, nullptr);
    Parm->setScopeInfo(0, i);
    Params.push_back(Parm);
  }
  OverloadDecl->setParams(Params);
  Sema->mergeDeclAttributes(OverloadDecl, FDecl);
  return OverloadDecl;
}
} // namespace

namespace {
void validateDirectCallTarget(Sema &S, const Expr *Fn, FunctionDecl *Callee,
                              MultiExprArg ArgExprs) {
  // `Callee` (when called with ArgExprs) may be ill-formed. enable_if (and
  // similar attributes) really don't like it when functions are called with an
  // invalid number of args.
  if (S.TooManyArguments(Callee->getNumParams(), ArgExprs.size()) &&
      !Callee->isVariadic())
    return;
  if (Callee->getMinRequiredArguments() > ArgExprs.size())
    return;
}
} // namespace

// ===----------------------------------------------------------------------===
// Call expression construction
// ===----------------------------------------------------------------------===

ExprResult Sema::OnCallExpr(Scope *Scope, Expr *Fn, SourceLocation LParenLoc,
                            MultiExprArg ArgExprs, SourceLocation RParenLoc) {
  return FormCallExpr(Scope, Fn, LParenLoc, ArgExprs, RParenLoc,
                      /*AllowRecovery=*/true);
}

__attribute__((hot)) ExprResult Sema::FormCallExpr(Scope *Scope, Expr *Fn,
                                                   SourceLocation LParenLoc,
                                                   MultiExprArg ArgExprs,
                                                   SourceLocation RParenLoc,
                                                   bool AllowRecovery) {
  ExprResult Result = MaybeConvertParenListExprToParenExpr(Scope, Fn);
  if (LLVM_UNLIKELY(Result.isInvalid()))
    return ExprError();
  Fn = Result.get();

  if (LLVM_UNLIKELY(resolveArgPlaceholders(*this, ArgExprs)))
    return ExprError();

  Expr *NakedFn = Fn->IgnoreParens();

  bool CallingNDeclIndirectly = false;
  NamedDecl *NDecl = nullptr;
  if (LLVM_UNLIKELY(isa<UnaryOperator>(NakedFn))) {
    auto *UnOp = cast<UnaryOperator>(NakedFn);
    if (UnOp->getOpcode() == UO_AddrOf) {
      CallingNDeclIndirectly = true;
      NakedFn = UnOp->getSubExpr()->IgnoreParens();
    }
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(NakedFn)) {
    NDecl = DRE->getDecl();

    FunctionDecl *FDecl = dyn_cast<FunctionDecl>(NDecl);
    if (LLVM_UNLIKELY(FDecl && FDecl->getBuiltinID())) {
      if ((FDecl =
               rewriteBuiltinFunctionDecl(this, Context, FDecl, ArgExprs))) {
        NDecl = FDecl;
        Fn = DeclRefExpr::Create(Context, FDecl, SourceLocation(),
                                 FDecl->getType(), Fn->getValueKind(), FDecl,
                                 DRE->isNonOdrUse());
      }
    }
  } else if (auto *ME = dyn_cast<MemberExpr>(NakedFn))
    NDecl = ME->getMemberDecl();

  if (FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(NDecl)) {
    if (CallingNDeclIndirectly && !checkAddressOfFunctionIsAvailable(
                                      FD, /*Complain=*/true, Fn->getBeginLoc()))
      return ExprError();

    validateDirectCallTarget(*this, Fn, FD, ArgExprs);
  }

  if (Context.isDependenceAllowed() && Fn->isTypeDependent()) {
    return CallExpr::Create(Context, Fn, ArgExprs, Context.DependentTy,
                            VK_PRValue, RParenLoc, CurFPFeatureOverrides());
  }
  return MakeResolvedCallExpr(Fn, NDecl, LParenLoc, ArgExprs, RParenLoc);
}

//  with the specified CallArgs
Expr *Sema::FormBuiltinCallExpr(SourceLocation Loc, Builtin::ID Id,
                                MultiExprArg CallArgs) {
  llvm::StringRef Name = Context.BuiltinInfo.getName(Id);
  LookupResult R(*this, &Context.Idents.get(Name), Loc,
                 neverc::ResolveOrdinary);
  ResolveName(R, TUScope, /*AllowBuiltinCreation=*/true);

  auto *BuiltInDecl = R.getAsSingle<FunctionDecl>();
  assert(BuiltInDecl && "failed to find builtin declaration");

  ExprResult DeclRef =
      MakeDeclRefExpr(BuiltInDecl, BuiltInDecl->getType(), VK_LValue, Loc);
  assert(DeclRef.isUsable() && "Builtin reference cannot fail");

  ExprResult Call =
      FormCallExpr(/*Scope=*/nullptr, DeclRef.get(), Loc, CallArgs, Loc);

  assert(!Call.isInvalid() && "Call to builtin cannot fail!");
  return Call.get();
}

ExprResult Sema::OnConvertVectorExpr(Expr *E, ParsedType ParsedDestTy,
                                     SourceLocation BuiltinLoc,
                                     SourceLocation RParenLoc) {
  TypeSourceInfo *TInfo;
  GetTypeFromParser(ParsedDestTy, &TInfo);
  return SemaConvertVectorExpr(E, TInfo, BuiltinLoc, RParenLoc);
}

ExprResult Sema::MakeResolvedCallExpr(Expr *Fn, NamedDecl *NDecl,
                                      SourceLocation LParenLoc,
                                      llvm::ArrayRef<Expr *> Args,
                                      SourceLocation RParenLoc) {
  FunctionDecl *FDecl = dyn_cast_or_null<FunctionDecl>(NDecl);
  unsigned BuiltinID = (FDecl ? FDecl->getBuiltinID() : 0);

  // Functions with 'interrupt' attribute cannot be called directly.
  if (FDecl && FDecl->hasAttr<AnyX86InterruptAttr>()) {
    Diag(Fn->getExprLoc(), diag::err_anyx86_interrupt_called);
    return ExprError();
  }

  // Interrupt handlers don't save off the VFP regs automatically on AArch64,
  // so there's some risk when calling out to non-interrupt handler functions
  // that the callee might not preserve them. This is easy to diagnose here,
  // but can be very challenging to debug.
  // Likewise, X86 interrupt handlers may only call routines with attribute
  // no_caller_saved_registers since there is no efficient way to
  // save and restore the non-GPR state.
  if (auto *Caller = getCurFunctionDecl()) {
    if (Caller->hasAttr<AnyX86InterruptAttr>() ||
        Caller->hasAttr<AnyX86NoCallerSavedRegistersAttr>()) {
      const TargetInfo &TI = Context.getTargetInfo();
      bool HasNonGPRRegisters =
          TI.hasFeature("sse") || TI.hasFeature("x87") || TI.hasFeature("mmx");
      if (HasNonGPRRegisters &&
          (!FDecl || !FDecl->hasAttr<AnyX86NoCallerSavedRegistersAttr>())) {
        Diag(Fn->getExprLoc(), diag::warn_anyx86_excessive_regsave)
            << (Caller->hasAttr<AnyX86InterruptAttr>() ? 0 : 1);
        if (FDecl)
          Diag(FDecl->getLocation(), diag::note_callee_decl) << FDecl;
      }
    }
  }

  // Promote the function operand.
  // We special-case function promotion here because we only allow promoting
  // builtin functions to function pointers in the callee of a call.
  ExprResult Result;
  QualType ResultTy;
  if (BuiltinID &&
      Fn->getType()->isSpecificBuiltinType(BuiltinType::BuiltinFn)) {
    // Extract the return type from the (builtin) function pointer type.
    // Several builtins still have setType in
    // Sema::CheckBuiltinFunctionCall. One should review their definitions in
    // Builtins.def to ensure they are correct before removing setType calls.
    QualType FnPtrTy = Context.getPointerType(FDecl->getType());
    Result = ImpCastExprToType(Fn, FnPtrTy, CK_BuiltinFnToFnPtr).get();
    ResultTy = FDecl->getCallResultType();
  } else {
    Result = CallExprUnaryConversions(Fn);
    ResultTy = Context.BoolTy;
  }
  if (Result.isInvalid())
    return ExprError();
  Fn = Result.get();

  // Check for a valid function type, but only if it is not a builtin which
  // requires custom type checking. These will be handled by
  // CheckBuiltinFunctionCall below just after creation of the call expression.
  const FunctionType *FuncT = nullptr;
  if (!BuiltinID || !Context.BuiltinInfo.hasCustomTypechecking(BuiltinID)) {
    if (const PointerType *PT = Fn->getType()->getAs<PointerType>()) {
      // C99 6.5.2.2p1 - "The expression that denotes the called function shall
      // have type pointer to function".
      FuncT = PT->getPointeeType()->getAs<FunctionType>();
      if (!FuncT)
        return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                         << Fn->getType() << Fn->getSourceRange());
    } else {
      return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                       << Fn->getType() << Fn->getSourceRange());
    }
  }

  const auto *Proto = dyn_cast_or_null<FunctionProtoType>(FuncT);
  unsigned NumParams = Proto ? Proto->getNumParams() : 0;

  CallExpr *TheCall;
  TheCall = CallExpr::Create(Context, Fn, Args, ResultTy, VK_PRValue, RParenLoc,
                             CurFPFeatureOverrides(), NumParams);

  if (!Context.isDependenceAllowed()) {
    // Forget about the nulled arguments since typo correction
    // do not handle them well.
    TheCall->shrinkNumArgs(Args.size());
    Args = llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs());
    TheCall->setNumArgsUnsafe(std::max<unsigned>(Args.size(), NumParams));
  }

  // Bail out early if calling a builtin with custom type checking.
  if (BuiltinID && Context.BuiltinInfo.hasCustomTypechecking(BuiltinID))
    return CheckBuiltinFunctionCall(FDecl, BuiltinID, TheCall);
  if (CheckCallReturnType(FuncT->getReturnType(), Fn->getBeginLoc(), TheCall,
                          FDecl))
    return ExprError();

  // We know the result type of the call, set it.
  TheCall->setType(FuncT->getCallResultType(Context));
  TheCall->setValueKind(VK_PRValue);

  if (Proto) {
    if (ConvertArgumentsForCall(TheCall, Fn, FDecl, Proto, Args, RParenLoc))
      return ExprError();
  } else {
    assert(isa<FunctionNoProtoType>(FuncT) && "Unknown FunctionType!");

    if (FDecl) {
      const FunctionDecl *Def = nullptr;
      if (FDecl->hasBody(Def) && Args.size() != Def->param_size()) {
        Proto = Def->getType()->getAs<FunctionProtoType>();
        if (!Proto ||
            !(Proto->isVariadic() && Args.size() >= Def->param_size()))
          Diag(RParenLoc, diag::warn_call_wrong_number_of_arguments)
              << (Args.size() > Def->param_size()) << FDecl
              << Fn->getSourceRange();
      }

      // If the function we're calling isn't a function prototype, but we have
      // a function prototype from a prior declaratiom, use that prototype.
      if (!FDecl->hasPrototype())
        Proto = FDecl->getType()->getAs<FunctionProtoType>();
    }

    // If we still haven't found a prototype to use but there are arguments to
    // the call, diagnose this as calling a function without a prototype.
    // However, if we found a function declaration, check to see if
    // -Wdeprecated-non-prototype was disabled where the function was declared.
    // If so, we will silence the diagnostic here on the assumption that this
    // interface is intentional and the user knows what they're doing. We will
    // also silence the diagnostic if there is a function declaration but it
    // was implicitly defined (the user already gets diagnostics about the
    // creation of the implicit function declaration, so the additional warning
    // is not helpful).
    if (!Proto && !Args.empty() &&
        (!FDecl || (!FDecl->isImplicit() &&
                    !Diags.isIgnored(diag::warn_strict_uses_without_prototype,
                                     FDecl->getLocation()))))
      Diag(LParenLoc, diag::warn_strict_uses_without_prototype)
          << (FDecl != nullptr) << FDecl;

    // Promote the arguments (C99 6.5.2.2p6).
    for (unsigned i = 0, e = Args.size(); i != e; i++) {
      Expr *Arg = Args[i];

      if (Proto && i < Proto->getNumParams()) {
        InitializedEntity Entity = InitializedEntity::InitializeParameter(
            Context, Proto->getParamType(i), false);
        ExprResult ArgE =
            PerformCopyInitialization(Entity, SourceLocation(), Arg);
        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.getAs<Expr>();

      } else {
        if (FDecl) {
          const FunctionDecl *Def = nullptr;
          if (FDecl->hasBody(Def) || FDecl->isDefined(Def)) {
            if (const auto *DefProto =
                    Def->getType()->getAs<FunctionProtoType>()) {
              if (i < DefProto->getNumParams() &&
                  this->isNeverCStringType(DefProto->getParamType(i))) {
                if (StringLiteral *SL = getNeverCStringLiteral(Arg)) {
                  ExprResult LV = buildNeverCStringLiteral(
                      *this, DefProto->getParamType(i), Arg, SL);
                  if (!LV.isInvalid())
                    Arg = LV.get();
                }
              }
            }
          }
        }
        ExprResult ArgE = DefaultArgumentPromotion(Arg);

        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.getAs<Expr>();
      }

      if (RequireCompleteType(Arg->getBeginLoc(), Arg->getType(),
                              diag::err_call_incomplete_argument, Arg))
        return ExprError();

      TheCall->setArg(i, Arg);
    }
    TheCall->computeDependence();
  }
  if (NDecl)
    CheckSentinelArgs(NDecl, LParenLoc, Args);

  // Do special checking on direct calls to functions.
  if (FDecl) {
    if (CheckFunctionCall(FDecl, TheCall, Proto))
      return ExprError();

    checkFortifiedBuiltinMemoryFunction(FDecl, TheCall);

    if (BuiltinID)
      return CheckBuiltinFunctionCall(FDecl, BuiltinID, TheCall);
  } else if (NDecl) {
    if (CheckPointerCall(NDecl, TheCall, Proto))
      return ExprError();
  } else {
    if (CheckOtherCall(TheCall, Proto))
      return ExprError();
  }

  return MaybeBindToTemporary(TheCall);
}

ExprResult Sema::OnCompoundLiteral(SourceLocation LParenLoc, ParsedType Ty,
                                   SourceLocation RParenLoc, Expr *InitExpr) {
  assert(Ty && "OnCompoundLiteral(): missing type");
  assert(InitExpr && "OnCompoundLiteral(): missing expression");

  TypeSourceInfo *TInfo;
  QualType literalType = GetTypeFromParser(Ty, &TInfo);
  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(literalType);

  return FormCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, InitExpr);
}

ExprResult Sema::FormCompoundLiteralExpr(SourceLocation LParenLoc,
                                         TypeSourceInfo *TInfo,
                                         SourceLocation RParenLoc,
                                         Expr *LiteralExpr) {
  QualType literalType = TInfo->getType();

  if (literalType->isArrayType()) {
    if (RequireCompleteSizedType(
            LParenLoc, Context.getBaseElementType(literalType),
            diag::err_array_incomplete_or_sizeless_type,
            SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd())))
      return ExprError();
    if (literalType->isVariableArrayType()) {
      // C23 6.7.10p4: An entity of variable length array type shall not be
      // initialized except by an empty initializer.
      //
      // Brace-init diagnostics for VLAs are handled in ParseBraceInitializer();
      // here we reject a non-empty initializer on a VLA compound literal.
      std::optional<unsigned> NumInits;
      if (const auto *ILE = dyn_cast<InitListExpr>(LiteralExpr))
        NumInits = ILE->getNumInits();
      if (NumInits.value_or(0) &&
          !tryToFixVariablyModifiedVarType(TInfo, literalType, LParenLoc,
                                           diag::err_variable_object_no_init))
        return ExprError();
    }
  } else if (RequireCompleteType(
                 LParenLoc, literalType,
                 diag::err_typecheck_decl_incomplete_type,
                 SourceRange(LParenLoc,
                             LiteralExpr->getSourceRange().getEnd())))
    return ExprError();

  InitializedEntity Entity =
      InitializedEntity::InitializeCompoundLiteralInit(TInfo);
  InitializationKind Kind = InitializationKind::CreateCStyleCast(
      LParenLoc, SourceRange(LParenLoc, RParenLoc),
      /*InitList=*/true);
  InitializationSequence InitSeq(*this, Entity, Kind, LiteralExpr);
  ExprResult Result =
      InitSeq.Perform(*this, Entity, Kind, LiteralExpr, &literalType);
  if (Result.isInvalid())
    return ExprError();
  LiteralExpr = Result.get();

  bool isFileScope = !CurContext->isFunctionOrMethod();

  // In C, compound literals are l-values.
  ExprValueKind VK = VK_LValue;

  if (isFileScope)
    if (auto ILE = dyn_cast<InitListExpr>(LiteralExpr))
      for (unsigned i = 0, j = ILE->getNumInits(); i != j; i++) {
        Expr *Init = ILE->getInit(i);
        ILE->setInit(i, ConstantExpr::Create(Context, Init));
      }

  auto *E = new (Context) CompoundLiteralExpr(LParenLoc, TInfo, literalType, VK,
                                              LiteralExpr, isFileScope);
  if (isFileScope) {
    if (CheckForConstantInitializer(LiteralExpr, literalType)) // C99 6.5.2.5p3
      return ExprError();
  } else if (literalType.getAddressSpace() != LangAS::Default) {
    // Embedded-C extensions to C99 6.5.2.5:
    //   "If the compound literal occurs inside the body of a function, the
    //   type name shall not be qualified by an address-space qualifier."
    Diag(LParenLoc, diag::err_compound_literal_with_address_space)
        << SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd());
    return ExprError();
  }

  if (!isFileScope) {
    // Block-scope compound literals have automatic storage duration (C99
    // 6.5.2.5).

    // Diagnose jumps that enter or exit the lifetime of the compound literal.
    if (literalType.isDestructedType()) {
      Cleanup.setExprNeedsCleanups(true);
      ExprCleanupObjects.push_back(E);
      getCurFunction()->setHasBranchProtectedScope();
    }
  }

  return MaybeBindToTemporary(E);
}

ExprResult Sema::OnInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                            SourceLocation RBraceLoc) {
  // Only produce each kind of designated initialization diagnostic once.
  SourceLocation FirstDesignator;
  for (unsigned I = 0, E = InitArgList.size(); I != E; ++I) {
    if (auto *DIE = dyn_cast<DesignatedInitExpr>(InitArgList[I])) {
      if (FirstDesignator.isInvalid())
        FirstDesignator = DIE->getBeginLoc();
      break;
    }
  }

  if (FirstDesignator.isValid()) {
    if (!getLangOpts().C99)
      Diag(FirstDesignator, diag::ext_designated_init);
  }

  return FormInitList(LBraceLoc, InitArgList, RBraceLoc);
}

ExprResult Sema::FormInitList(SourceLocation LBraceLoc,
                              MultiExprArg InitArgList,
                              SourceLocation RBraceLoc) {
  // Semantic analysis for initializers is done by OnDeclarator() and
  // CheckInitializer() - it requires knowledge of the object being initialized.

  // Immediately handle non-overload placeholders.  Overloads can be
  // resolved contextually, but everything else here can't.
  for (unsigned I = 0, E = InitArgList.size(); I != E; ++I) {
    if (InitArgList[I]->getType()->isNonOverloadPlaceholderType()) {
      ExprResult result = CheckPlaceholderExpr(InitArgList[I]);

      // Ignore failures; dropping the entire initializer list because
      // of one failure would be terrible for indexing/etc.
      if (result.isInvalid())
        continue;

      InitArgList[I] = result.get();
    }
  }

  InitListExpr *E =
      new (Context) InitListExpr(Context, LBraceLoc, InitArgList, RBraceLoc);
  E->setType(Context.VoidTy);
  return E;
}

CastKind Sema::PrepareScalarCast(ExprResult &Src, QualType DestTy) {
  // Both Src and Dest are scalar types, i.e. arithmetic or pointer.
  // Also, callers should have filtered out the invalid cases with
  // pointers.  Everything else should be possible.

  QualType SrcTy = Src.get()->getType();
  if (Context.hasSameUnqualifiedType(SrcTy, DestTy))
    return CK_NoOp;

  switch (SrcTy->getScalarTypeKind()) {
  case Type::STK_CPointer:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer: {
      LangAS SrcAS = SrcTy->getPointeeType().getAddressSpace();
      LangAS DestAS = DestTy->getPointeeType().getAddressSpace();
      if (SrcAS != DestAS)
        return CK_AddressSpaceConversion;
      if (Context.hasCvrSimilarType(SrcTy, DestTy))
        return CK_NoOp;
      return CK_BitCast;
    }
    case Type::STK_Bool:
      return CK_PointerToBoolean;
    case Type::STK_Integral:
      return CK_PointerToIntegral;
    case Type::STK_Floating:
    case Type::STK_FloatingComplex:
    case Type::STK_IntegralComplex:
    case Type::STK_FixedPoint:
      llvm_unreachable("illegal cast from pointer");
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_FixedPoint:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FixedPoint:
      return CK_FixedPointCast;
    case Type::STK_Bool:
      return CK_FixedPointToBoolean;
    case Type::STK_Integral:
      return CK_FixedPointToIntegral;
    case Type::STK_Floating:
      return CK_FixedPointToFloating;
    case Type::STK_IntegralComplex:
    case Type::STK_FloatingComplex:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << DestTy;
      return CK_IntegralCast;
    case Type::STK_CPointer:
      llvm_unreachable("illegal cast to pointer type");
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_Bool: // casting from bool is like casting from an integer
  case Type::STK_Integral:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer:
      if (Src.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull))
        return CK_NullToPointer;
      return CK_IntegralToPointer;
    case Type::STK_Bool:
      return CK_IntegralToBoolean;
    case Type::STK_Integral:
      return CK_IntegralCast;
    case Type::STK_Floating:
      return CK_IntegralToFloating;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralCast);
      return CK_IntegralRealToComplex;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralToFloating);
      return CK_FloatingRealToComplex;
    case Type::STK_FixedPoint:
      return CK_IntegralToFixedPoint;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_Floating:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_Floating:
      return CK_FloatingCast;
    case Type::STK_Bool:
      return CK_FloatingToBoolean;
    case Type::STK_Integral:
      return CK_FloatingToIntegral;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingCast);
      return CK_FloatingRealToComplex;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingToIntegral);
      return CK_IntegralRealToComplex;
    case Type::STK_CPointer:
      llvm_unreachable("valid float->pointer cast?");
    case Type::STK_FixedPoint:
      return CK_FloatingToFixedPoint;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_FloatingComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_FloatingComplexCast;
    case Type::STK_IntegralComplex:
      return CK_FloatingComplexToIntegralComplex;
    case Type::STK_Floating: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_FloatingComplexToReal;
      Src = ImpCastExprToType(Src.get(), ET, CK_FloatingComplexToReal);
      return CK_FloatingCast;
    }
    case Type::STK_Bool:
      return CK_FloatingComplexToBoolean;
    case Type::STK_Integral:
      Src = ImpCastExprToType(Src.get(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingComplexToReal);
      return CK_FloatingToIntegral;
    case Type::STK_CPointer:
      llvm_unreachable("valid complex float->pointer cast?");
    case Type::STK_FixedPoint:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << SrcTy;
      return CK_IntegralCast;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_IntegralComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_IntegralComplexToFloatingComplex;
    case Type::STK_IntegralComplex:
      return CK_IntegralComplexCast;
    case Type::STK_Integral: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_IntegralComplexToReal;
      Src = ImpCastExprToType(Src.get(), ET, CK_IntegralComplexToReal);
      return CK_IntegralCast;
    }
    case Type::STK_Bool:
      return CK_IntegralComplexToBoolean;
    case Type::STK_Floating:
      Src = ImpCastExprToType(Src.get(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralComplexToReal);
      return CK_IntegralToFloating;
    case Type::STK_CPointer:
      llvm_unreachable("valid complex int->pointer cast?");
    case Type::STK_FixedPoint:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << SrcTy;
      return CK_IntegralCast;
    }
    llvm_unreachable("Should have returned before this");
  }

  llvm_unreachable("Unhandled scalar cast");
}

namespace {
bool decomposeVectorType(QualType type, uint64_t &len, QualType &eltType) {
  // Vectors are simple.
  if (const VectorType *vecType = type->getAs<VectorType>()) {
    len = vecType->getNumElements();
    eltType = vecType->getElementType();
    assert(eltType->isScalarType());
    return true;
  }

  // We allow lax conversion to and from non-vector types, but only if
  // they're real types (i.e. non-complex, non-pointer scalar types).
  if (!type->isRealType())
    return false;

  len = 1;
  eltType = type;
  return true;
}
} // namespace

// ===----------------------------------------------------------------------===
// Type compatibility checks & casts
// ===----------------------------------------------------------------------===

bool Sema::isValidSveBitcast(QualType srcTy, QualType destTy) {
  assert(srcTy->isVectorType() || destTy->isVectorType());

  auto ValidScalableConversion = [](QualType FirstType, QualType SecondType) {
    if (!FirstType->isSVESizelessBuiltinType())
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    return VecTy && VecTy->getVectorKind() == VectorKind::SveFixedLengthData;
  };

  return ValidScalableConversion(srcTy, destTy) ||
         ValidScalableConversion(destTy, srcTy);
}

bool Sema::areMatrixTypesOfTheSameDimension(QualType srcTy, QualType destTy) {
  if (!destTy->isMatrixType() || !srcTy->isMatrixType())
    return false;

  const ConstantMatrixType *matSrcType = srcTy->getAs<ConstantMatrixType>();
  const ConstantMatrixType *matDestType = destTy->getAs<ConstantMatrixType>();

  return matSrcType->getNumRows() == matDestType->getNumRows() &&
         matSrcType->getNumColumns() == matDestType->getNumColumns();
}

bool Sema::areVectorTypesSameSize(QualType SrcTy, QualType DestTy) {
  assert(DestTy->isVectorType() || SrcTy->isVectorType());

  uint64_t SrcLen, DestLen;
  QualType SrcEltTy, DestEltTy;
  if (!decomposeVectorType(SrcTy, SrcLen, SrcEltTy))
    return false;
  if (!decomposeVectorType(DestTy, DestLen, DestEltTy))
    return false;

  // TreeContext::getTypeSize will return the size rounded up to a
  // power of 2, so instead of using that, we need to use the raw
  // element size multiplied by the element count.
  uint64_t SrcEltSize = Context.getTypeSize(SrcEltTy);
  uint64_t DestEltSize = Context.getTypeSize(DestEltTy);

  return (SrcLen * SrcEltSize == DestLen * DestEltSize);
}

bool Sema::areLaxCompatibleVectorTypes(QualType srcTy, QualType destTy) {
  assert(destTy->isVectorType() || srcTy->isVectorType());

  // Disallow lax conversions between scalars and ExtVectors (these
  // conversions are allowed for other vector types because common headers
  // depend on them).  Most scalar OP ExtVector cases are handled by the
  // splat path anyway, which does what we want (convert, not bitcast).
  // What this rules out for ExtVectors is crazy things like char4*float.
  if (srcTy->isScalarType() && destTy->isExtVectorType())
    return false;
  if (destTy->isScalarType() && srcTy->isExtVectorType())
    return false;

  return areVectorTypesSameSize(srcTy, destTy);
}

bool Sema::isLaxVectorConversion(QualType srcTy, QualType destTy) {
  assert(destTy->isVectorType() || srcTy->isVectorType());

  switch (Context.getLangOpts().getLaxVectorConversions()) {
  case LangOptions::LaxVectorConversionKind::None:
    return false;

  case LangOptions::LaxVectorConversionKind::Integer:
    if (!srcTy->isIntegralOrEnumerationType()) {
      auto *Vec = srcTy->getAs<VectorType>();
      if (!Vec || !Vec->getElementType()->isIntegralOrEnumerationType())
        return false;
    }
    if (!destTy->isIntegralOrEnumerationType()) {
      auto *Vec = destTy->getAs<VectorType>();
      if (!Vec || !Vec->getElementType()->isIntegralOrEnumerationType())
        return false;
    }
    // OK, integer (vector) -> integer (vector) bitcast.
    break;

  case LangOptions::LaxVectorConversionKind::All:
    break;
  }

  return areLaxCompatibleVectorTypes(srcTy, destTy);
}

bool Sema::CheckMatrixCast(SourceRange R, QualType DestTy, QualType SrcTy,
                           CastKind &Kind) {
  if (SrcTy->isMatrixType() && DestTy->isMatrixType()) {
    if (!areMatrixTypesOfTheSameDimension(SrcTy, DestTy)) {
      return Diag(R.getBegin(), diag::err_invalid_conversion_between_matrixes)
             << DestTy << SrcTy << R;
    }
  } else if (SrcTy->isMatrixType()) {
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_matrix_and_type)
           << SrcTy << DestTy << R;
  } else if (DestTy->isMatrixType()) {
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_matrix_and_type)
           << DestTy << SrcTy << R;
  }

  Kind = CK_MatrixCast;
  return false;
}

bool Sema::CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty,
                           CastKind &Kind) {
  assert(VectorTy->isVectorType() && "Not a vector type!");

  if (Ty->isVectorType() || Ty->isIntegralType(Context)) {
    if (!areLaxCompatibleVectorTypes(Ty, VectorTy))
      return Diag(R.getBegin(),
                  Ty->isVectorType()
                      ? diag::err_invalid_conversion_between_vectors
                      : diag::err_invalid_conversion_between_vector_and_integer)
             << VectorTy << Ty << R;
  } else
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
           << VectorTy << Ty << R;

  Kind = CK_BitCast;
  return false;
}

ExprResult Sema::prepareVectorSplat(QualType VectorTy, Expr *SplattedExpr) {
  QualType DestElemTy = VectorTy->castAs<VectorType>()->getElementType();

  if (DestElemTy == SplattedExpr->getType())
    return SplattedExpr;

  assert(DestElemTy->isFloatingType() ||
         DestElemTy->isIntegralOrEnumerationType());

  CastKind CK;
  if (VectorTy->isExtVectorType() && SplattedExpr->getType()->isBooleanType()) {
    // Convert `true` boolean expressions to -1 when splatting vectors.
    if (DestElemTy->isFloatingType()) {
      // To avoid having to have a CK_BooleanToSignedFloating cast kind, we cast
      // in two steps: boolean to signed integral, then to floating.
      ExprResult CastExprRes = ImpCastExprToType(SplattedExpr, Context.IntTy,
                                                 CK_BooleanToSignedIntegral);
      SplattedExpr = CastExprRes.get();
      CK = CK_IntegralToFloating;
    } else {
      CK = CK_BooleanToSignedIntegral;
    }
  } else {
    ExprResult CastExprRes = SplattedExpr;
    CK = PrepareScalarCast(CastExprRes, DestElemTy);
    if (CastExprRes.isInvalid())
      return ExprError();
    SplattedExpr = CastExprRes.get();
  }
  return ImpCastExprToType(SplattedExpr, DestElemTy, CK);
}

ExprResult Sema::CheckExtVectorCast(SourceRange R, QualType DestTy,
                                    Expr *CastExpr, CastKind &Kind) {
  assert(DestTy->isExtVectorType() && "Not an extended vector type!");

  QualType SrcTy = CastExpr->getType();

  // If SrcTy is a VectorType, the total size must match to explicitly cast to
  // an ExtVectorType.
  if (SrcTy->isVectorType()) {
    if (!areLaxCompatibleVectorTypes(SrcTy, DestTy)) {
      Diag(R.getBegin(), diag::err_invalid_conversion_between_ext_vectors)
          << DestTy << SrcTy << R;
      return ExprError();
    }
    Kind = CK_BitCast;
    return CastExpr;
  }

  // All non-pointer scalars can be cast to ExtVector type.  The appropriate
  // conversion will take place first from scalar to elt type, and then
  // splat from elt type to vector.
  if (SrcTy->isPointerType())
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
           << DestTy << SrcTy << R;

  Kind = CK_VectorSplat;
  return prepareVectorSplat(DestTy, CastExpr);
}

ExprResult Sema::OnCastExpr(Scope *S, SourceLocation LParenLoc, Declarator &D,
                            ParsedType &Ty, SourceLocation RParenLoc,
                            Expr *CastExpr) {
  assert(!D.isInvalidType() && (CastExpr != nullptr) &&
         "OnCastExpr(): missing type or expr");

  TypeSourceInfo *castTInfo = ResolveDeclaratorTypeCast(D, CastExpr->getType());
  if (D.isInvalidType())
    return ExprError();

  checkUnusedDeclAttributes(D);

  QualType castType = castTInfo->getType();
  Ty = CreateParsedType(castType, castTInfo);

  // If the Expr being casted is a ParenListExpr, handle it specially.
  // Turn the ParenListExpr into a sequence of BinOp comma operators.
  if (isa<ParenListExpr>(CastExpr)) {
    ExprResult Result = MaybeConvertParenListExprToParenExpr(S, CastExpr);
    if (Result.isInvalid())
      return ExprError();
    CastExpr = Result.get();
  }
  DiscardMisalignedMemberAddress(castType.getTypePtr(), CastExpr);

  return FormCStyleCastExpr(LParenLoc, castTInfo, RParenLoc, CastExpr);
}

ExprResult Sema::MaybeConvertParenListExprToParenExpr(Scope *S,
                                                      Expr *OrigExpr) {
  ParenListExpr *E = dyn_cast<ParenListExpr>(OrigExpr);
  if (!E)
    return OrigExpr;

  ExprResult Result(E->getExpr(0));

  for (unsigned i = 1, e = E->getNumExprs(); i != e && !Result.isInvalid(); ++i)
    Result =
        OnBinOp(S, E->getExprLoc(), tok::comma, Result.get(), E->getExpr(i));

  if (Result.isInvalid())
    return ExprError();

  return OnParenExpr(E->getLParenLoc(), E->getRParenLoc(), Result.get());
}

ExprResult Sema::OnParenListExpr(SourceLocation L, SourceLocation R,
                                 MultiExprArg Val) {
  return ParenListExpr::Create(Context, L, Val, R);
}

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
void suggestParentheses(Sema &Self, SourceLocation Loc,
                        const PartialDiagnostic &Note, SourceRange ParenRange) {
  SourceLocation EndLoc = Self.getLocForEndOfToken(ParenRange.getEnd());
  if (ParenRange.getBegin().isFileID() && ParenRange.getEnd().isFileID() &&
      EndLoc.isValid()) {
    Self.Diag(Loc, Note) << FixItHint::CreateInsertion(ParenRange.getBegin(),
                                                       "(")
                         << FixItHint::CreateInsertion(EndLoc, ")");
  } else {
    // We can't display the parentheses, so just show the bare note.
    Self.Diag(Loc, Note) << ParenRange;
  }
}
} // namespace

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

// Check that the SME attributes for PSTATE.ZA and PSTATE.SM are compatible.
bool Sema::IsInvalidSMECallConversion(QualType FromType, QualType ToType,
                                      AArch64SMECallConversionKind C) {
  unsigned FromAttributes = 0, ToAttributes = 0;
  if (const auto *FromFn =
          dyn_cast<FunctionProtoType>(Context.getCanonicalType(FromType)))
    FromAttributes =
        FromFn->getAArch64SMEAttributes() & FunctionType::SME_AttributeMask;
  if (const auto *ToFn =
          dyn_cast<FunctionProtoType>(Context.getCanonicalType(ToType)))
    ToAttributes =
        ToFn->getAArch64SMEAttributes() & FunctionType::SME_AttributeMask;

  if (FromAttributes == ToAttributes)
    return false;

  // If the '__arm_preserves_za' is the only difference between the types,
  // check whether we're allowed to add or remove it.
  if ((FromAttributes ^ ToAttributes) ==
      FunctionType::SME_PStateZAPreservedMask) {
    switch (C) {
    case AArch64SMECallConversionKind::MatchExactly:
      return true;
    case AArch64SMECallConversionKind::MayAddPreservesZA:
      return !(ToAttributes & FunctionType::SME_PStateZAPreservedMask);
    case AArch64SMECallConversionKind::MayDropPreservesZA:
      return !(FromAttributes & FunctionType::SME_PStateZAPreservedMask);
    }
  }

  // There has been a mismatch of attributes
  return true;
}

// checkPointerTypesForAssignment - This is a very tricky routine (despite
// being closely modeled after the C99 spec:-). The odd characteristic of this
// routine is it effectively iqnores the qualifiers on the top level pointee.
// This circumvents the usual type rules specified in 6.2.7p1 & 6.7.5.[1-3].
namespace {
Sema::AssignConvertType checkPointerTypesForAssignment(Sema &S,
                                                       QualType LHSType,
                                                       QualType RHSType,
                                                       SourceLocation Loc) {
  assert(LHSType.isCanonical() && "LHS not canonicalized!");
  assert(RHSType.isCanonical() && "RHS not canonicalized!");

  // get the "pointed to" type (ignoring qualifiers at the top level)
  const Type *lhptee, *rhptee;
  Qualifiers lhq, rhq;
  std::tie(lhptee, lhq) =
      cast<PointerType>(LHSType)->getPointeeType().split().asPair();
  std::tie(rhptee, rhq) =
      cast<PointerType>(RHSType)->getPointeeType().split().asPair();

  Sema::AssignConvertType ConvTy = Sema::Compatible;

  // C99 6.5.16.1p1: This following citation is common to constraints
  // 3 & 4 (below). ...and the type *pointed to* by the left has all the
  // qualifiers of the type *pointed to* by the right;

  if (!lhq.compatiblyIncludes(rhq)) {
    // Treat address-space mismatches as fatal.
    if (!lhq.isAddressSpaceSupersetOf(rhq))
      return Sema::IncompatiblePointerDiscardsQualifiers;
    // For GCC/MS compatibility, other qualifier mismatches are treated
    // as still compatible in C.
    else
      ConvTy = Sema::CompatiblePointerDiscardsQualifiers;
  }

  // C99 6.5.16.1p1 (constraint 4): If one operand is a pointer to an object or
  // incomplete type and the other is a pointer to a qualified or unqualified
  // version of void...
  if (lhptee->isVoidType()) {
    if (rhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(rhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (rhptee->isVoidType()) {
    if (lhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(lhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (!S.Diags.isIgnored(
          diag::warn_typecheck_convert_incompatible_function_pointer_strict,
          Loc) &&
      RHSType->isFunctionPointerType() && LHSType->isFunctionPointerType() &&
      !S.IsFunctionConversion(RHSType, LHSType, RHSType))
    return Sema::IncompatibleFunctionPointerStrict;

  // C99 6.5.16.1p1 (constraint 3): both operands are pointers to qualified or
  // unqualified versions of compatible types, ...
  QualType ltrans = QualType(lhptee, 0), rtrans = QualType(rhptee, 0);
  if (!S.Context.typesAreCompatible(ltrans, rtrans)) {
    // Check if the pointee types are compatible ignoring the sign.
    // We explicitly check for char so that we catch "char" vs
    // "unsigned char" on systems where "char" is unsigned.
    if (lhptee->isCharType())
      ltrans = S.Context.UnsignedCharTy;
    else if (lhptee->hasSignedIntegerRepresentation())
      ltrans = S.Context.getCorrespondingUnsignedType(ltrans);

    if (rhptee->isCharType())
      rtrans = S.Context.UnsignedCharTy;
    else if (rhptee->hasSignedIntegerRepresentation())
      rtrans = S.Context.getCorrespondingUnsignedType(rtrans);

    if (ltrans == rtrans) {
      // Types are compatible ignoring the sign. Qualifier incompatibility
      // takes priority over sign incompatibility because the sign
      // warning can be disabled.
      if (ConvTy != Sema::Compatible)
        return ConvTy;

      return Sema::IncompatiblePointerSign;
    }

    // If we are a multi-level pointer, it's possible that our issue is simply
    // one of qualification - e.g. char ** -> const char ** is not allowed. If
    // the eventual target type is the same and the pointers have the same
    // level of indirection, this must be the issue.
    if (isa<PointerType>(lhptee) && isa<PointerType>(rhptee)) {
      do {
        std::tie(lhptee, lhq) =
            cast<PointerType>(lhptee)->getPointeeType().split().asPair();
        std::tie(rhptee, rhq) =
            cast<PointerType>(rhptee)->getPointeeType().split().asPair();

        // Inconsistent address spaces at this point is invalid, even if the
        // address spaces would be compatible.
        // This doesn't catch address space mismatches for pointers of
        // different nesting levels, like:
        //   __local int *** a;
        //   int ** b = a;
        // It's not clear how to actually determine when such pointers are
        // invalidly incompatible.
        if (lhq.getAddressSpace() != rhq.getAddressSpace())
          return Sema::IncompatibleNestedPointerAddressSpaceMismatch;

      } while (isa<PointerType>(lhptee) && isa<PointerType>(rhptee));

      if (lhptee == rhptee)
        return Sema::IncompatibleNestedPointerQualifiers;
    }

    // General pointer incompatibility takes priority over qualifiers.
    if (RHSType->isFunctionPointerType() && LHSType->isFunctionPointerType())
      return Sema::IncompatibleFunctionPointer;
    return Sema::IncompatiblePointer;
  }
  if (S.IsFunctionConversion(ltrans, rtrans, ltrans))
    return Sema::IncompatibleFunctionPointer;
  if (S.IsInvalidSMECallConversion(
          rtrans, ltrans,
          Sema::AArch64SMECallConversionKind::MayDropPreservesZA))
    return Sema::IncompatibleFunctionPointer;
  return ConvTy;
}
} // namespace

Sema::AssignConvertType Sema::CheckAssignmentConstraints(SourceLocation Loc,
                                                         QualType LHSType,
                                                         QualType RHSType) {
  // Fake up an opaque expression.  We don't actually care about what
  // cast operations are required, so if CheckAssignmentConstraints
  // adds casts to this they'll be wasted, but fortunately that doesn't
  // usually happen on valid code.
  OpaqueValueExpr RHSExpr(Loc, RHSType, VK_PRValue);
  ExprResult RHSPtr = &RHSExpr;
  CastKind K;

  return CheckAssignmentConstraints(LHSType, RHSPtr, K, /*ConvertRHS=*/false);
}

Sema::AssignConvertType Sema::CheckAssignmentConstraints(QualType LHSType,
                                                         ExprResult &RHS,
                                                         CastKind &Kind,
                                                         bool ConvertRHS) {
  QualType RHSType = RHS.get()->getType();

  // Get canonical types.  We're not formatting these types, just comparing
  // them.
  LHSType = Context.getCanonicalType(LHSType).getUnqualifiedType();
  RHSType = Context.getCanonicalType(RHSType).getUnqualifiedType();

  // Common case: no conversion required.
  if (LHSType == RHSType) {
    Kind = CK_NoOp;
    return Compatible;
  }

  // If the LHS has an __auto_type, there are no additional type constraints
  // to be worried about.
  if (const auto *AT = dyn_cast<AutoType>(LHSType)) {
    if (AT->isGNUAutoType()) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  // If we have an atomic type, try a non-atomic assignment, then just add an
  // atomic qualification step.
  if (const AtomicType *AtomicTy = dyn_cast<AtomicType>(LHSType)) {
    Sema::AssignConvertType result =
        CheckAssignmentConstraints(AtomicTy->getValueType(), RHS, Kind);
    if (result != Compatible)
      return result;
    if (Kind != CK_NoOp && ConvertRHS)
      RHS = ImpCastExprToType(RHS.get(), AtomicTy->getValueType(), Kind);
    Kind = CK_NonAtomicToAtomic;
    return Compatible;
  }

  // Allow scalar to ExtVector assignments, and assignments of an ExtVector type
  // to the same ExtVector type.
  if (LHSType->isExtVectorType()) {
    if (RHSType->isExtVectorType())
      return Incompatible;
    if (RHSType->isArithmeticType()) {
      // CK_VectorSplat does T -> vector T, so first cast to the element type.
      if (ConvertRHS)
        RHS = prepareVectorSplat(LHSType, RHS.get());
      Kind = CK_VectorSplat;
      return Compatible;
    }
  }

  // Conversions to or from vector type.
  if (LHSType->isVectorType() || RHSType->isVectorType()) {
    if (LHSType->isVectorType() && RHSType->isVectorType()) {
      // Allow assignments between compatible vector types
      if (Context.areCompatibleVectorTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

      // If we are allowing lax vector conversions, and LHS and RHS are both
      // vectors, the total size only needs to be the same. This is a bitcast;
      // no bits are changed but the result type is different.
      if (isLaxVectorConversion(RHSType, LHSType)) {
        Kind = CK_BitCast;
        return IncompatibleVectors;
      }
    }

    // When the RHS comes from another lax conversion (e.g. binops between
    // scalars and vectors) the result is canonicalized as a vector. When the
    // LHS is also a vector, the lax is allowed by the condition above. Handle
    // the case where LHS is a scalar.
    if (LHSType->isScalarType()) {
      const VectorType *VecType = RHSType->getAs<VectorType>();
      if (VecType && VecType->getNumElements() == 1 &&
          isLaxVectorConversion(RHSType, LHSType)) {
        ExprResult *VecExpr = &RHS;
        *VecExpr = ImpCastExprToType(VecExpr->get(), LHSType, CK_BitCast);
        Kind = CK_BitCast;
        return Compatible;
      }
    }

    // Allow assignments between fixed-length and sizeless SVE vectors.
    if ((LHSType->isSVESizelessBuiltinType() && RHSType->isVectorType()) ||
        (LHSType->isVectorType() && RHSType->isSVESizelessBuiltinType()))
      if (Context.areCompatibleSveTypes(LHSType, RHSType) ||
          Context.areLaxCompatibleSveTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

    return Incompatible;
  }

  // Arithmetic conversions.
  if (LHSType->isArithmeticType() && RHSType->isArithmeticType()) {
    if (ConvertRHS)
      Kind = PrepareScalarCast(RHS, LHSType);
    return Compatible;
  }

  // Conversions to normal pointers.
  if (const PointerType *LHSPointer = dyn_cast<PointerType>(LHSType)) {
    // U* -> T*
    if (isa<PointerType>(RHSType)) {
      LangAS AddrSpaceL = LHSPointer->getPointeeType().getAddressSpace();
      LangAS AddrSpaceR = RHSType->getPointeeType().getAddressSpace();
      if (AddrSpaceL != AddrSpaceR)
        Kind = CK_AddressSpaceConversion;
      else if (Context.hasCvrSimilarType(RHSType, LHSType))
        Kind = CK_NoOp;
      else
        Kind = CK_BitCast;
      return checkPointerTypesForAssignment(*this, LHSType, RHSType,
                                            RHS.get()->getBeginLoc());
    }

    // int -> T*
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer;
      return IntToPointer;
    }

    return Incompatible;
  }

  // Conversion to nullptr_t (C23 only)
  if (getLangOpts().C23 && LHSType->isNullPtrType() &&
      RHS.get()->isNullPointerConstant(Context,
                                       Expr::NPC_ValueDependentIsNull)) {
    // null -> nullptr_t
    Kind = CK_NullToPointer;
    return Compatible;
  }

  // Conversions from pointers that are not covered by the above.
  if (isa<PointerType>(RHSType)) {
    // T* -> _Bool
    if (LHSType == Context.BoolTy) {
      Kind = CK_PointerToBoolean;
      return Compatible;
    }

    // T* -> int
    if (LHSType->isIntegerType()) {
      Kind = CK_PointerToIntegral;
      return PointerToInt;
    }

    return Incompatible;
  }

  // struct A -> struct B
  if (isa<TagType>(LHSType) && isa<TagType>(RHSType)) {
    if (Context.typesAreCompatible(LHSType, RHSType)) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  return Incompatible;
}

namespace {
void constructTransparentUnion(Sema &S, TreeContext &C, ExprResult &EResult,
                               QualType UnionType, FieldDecl *Field) {
  Expr *E = EResult.get();
  InitListExpr *Initializer =
      new (C) InitListExpr(C, SourceLocation(), E, SourceLocation());
  Initializer->setType(UnionType);
  Initializer->setInitializedFieldInUnion(Field);

  TypeSourceInfo *unionTInfo = C.getTrivialTypeSourceInfo(UnionType);
  EResult = new (C) CompoundLiteralExpr(SourceLocation(), unionTInfo, UnionType,
                                        VK_PRValue, Initializer, false);
}
} // namespace

Sema::AssignConvertType
Sema::CheckTransparentUnionArgumentConstraints(QualType ArgType,
                                               ExprResult &RHS) {
  QualType RHSType = RHS.get()->getType();

  // If the ArgType is a Union type, we want to handle a potential
  // transparent_union GCC extension.
  const RecordType *UT = ArgType->getAsUnionType();
  if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
    return Incompatible;

  // The field to initialize within the transparent union.
  RecordDecl *UD = UT->getDecl();
  FieldDecl *InitField = nullptr;
  // It's compatible if the expression matches any of the fields.
  for (auto *it : UD->fields()) {
    if (it->getType()->isPointerType()) {
      // If the transparent union contains a pointer type, we allow:
      // 1) void pointer
      // 2) null pointer constant
      if (RHSType->isPointerType())
        if (RHSType->castAs<PointerType>()->getPointeeType()->isVoidType()) {
          RHS = ImpCastExprToType(RHS.get(), it->getType(), CK_BitCast);
          InitField = it;
          break;
        }

      if (RHS.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull)) {
        RHS = ImpCastExprToType(RHS.get(), it->getType(), CK_NullToPointer);
        InitField = it;
        break;
      }
    }

    CastKind Kind;
    if (CheckAssignmentConstraints(it->getType(), RHS, Kind) == Compatible) {
      RHS = ImpCastExprToType(RHS.get(), it->getType(), Kind);
      InitField = it;
      break;
    }
  }

  if (!InitField)
    return Incompatible;

  constructTransparentUnion(*this, Context, RHS, ArgType, InitField);
  return Compatible;
}

namespace {
bool couldBeNeverCStringOperand(Expr *E) {
  QualType T = E->getType();
  if (T.isNull())
    return false;
  const Type *TP = T.getTypePtr();
  return TP->isRecordType() || TP->isArrayType() || TP->isPointerType();
}
} // namespace

namespace {
bool isNeverCStringOperand(Sema &S, Expr *E) {
  if (!couldBeNeverCStringOperand(E))
    return false;
  return S.isNeverCStringType(E->getType()) || getNeverCStringLiteral(E);
}
} // namespace

namespace {
ExprResult buildNeverCStringLiteral(Sema &S, QualType StringTy,
                                    Expr *Initializer, StringLiteral *SL) {
  TreeContext &Context = S.Context;

  // Fold wide / UTF-16 / UTF-32 source bytes into a compile-time UTF-8
  // byte stream so the byte-shaped `string` value type splices the
  // bytes directly.  When folding occurs we also adopt the synthesised
  // ordinary literal as the `Initializer`, so the array decay below
  // sees `char[N]` (decaying to `const char *`) rather than the source
  // wide-character array.  Ordinary / UTF-8 literals pass through
  // unchanged.
  StringLiteral *FoldedSL = foldNeverCStringWideLiteralToUtf8(S, SL);
  if (!FoldedSL)
    return ExprError();
  if (FoldedSL != SL) {
    SL = FoldedSL;
    Initializer = FoldedSL;
  }

  ExprResult Data = S.DefaultFunctionArrayLvalueConversion(Initializer,
                                                           /*Diagnose=*/false);
  if (Data.isInvalid())
    return ExprError();

  QualType DataTy = Context.getPointerType(Context.CharTy.withConst());
  if (Data.get()->getType() != DataTy)
    Data = S.ImpCastExprToType(Data.get(), DataTy, CK_NoOp);
  if (Data.isInvalid())
    return ExprError();

  QualType SizeTy = Context.getSizeType();
  unsigned SizeBits = Context.getTypeSize(SizeTy);

  Expr *Len =
      IntegerLiteral::Create(Context, llvm::APInt(SizeBits, SL->getLength()),
                             SizeTy, Initializer->getExprLoc());
  Expr *Cap = IntegerLiteral::Create(Context, llvm::APInt(SizeBits, 0), SizeTy,
                                     Initializer->getExprLoc());

  Expr *Inits[] = {Data.get(), Len, Cap};
  auto *ILE = new (Context) InitListExpr(Context, Initializer->getBeginLoc(),
                                         Inits, Initializer->getEndLoc());
  ILE->setType(StringTy);

  bool IsFileScope = !S.CurContext->isFunctionOrMethod();
  if (IsFileScope) {
    for (unsigned I = 0, E = ILE->getNumInits(); I != E; ++I)
      ILE->setInit(I, ConstantExpr::Create(Context, ILE->getInit(I)));
  }

  TypeSourceInfo *TInfo =
      Context.getTrivialTypeSourceInfo(StringTy, Initializer->getBeginLoc());
  return new (Context) CompoundLiteralExpr(SourceLocation(), TInfo, StringTy,
                                           VK_LValue, ILE, IsFileScope);
}
} // namespace

namespace {
ExprResult buildNeverCStringRetain(Sema &S, SourceLocation Loc, Expr *Value) {
  Expr *Args[] = {Value};
  return buildNeverCStringRuntimeCall(S, /*Scope=*/nullptr, Loc,
                                      BuiltinStringNames::RetainFunctionName,
                                      Args, Loc);
}
} // namespace


Sema::AssignConvertType
Sema::CheckSingleAssignmentConstraints(QualType LHSType, ExprResult &CallerRHS,
                                       bool Diagnose, bool ConvertRHS) {
  // We need to be able to tell the caller whether we diagnosed a problem, if
  // they ask us to issue diagnostics.
  assert((ConvertRHS || !Diagnose) && "can't indicate whether we diagnosed");

  // If ConvertRHS is false, we want to leave the caller's RHS untouched. Sadly,
  // we can't avoid *all* modifications at the moment, so we need some somewhere
  // to put the updated value.
  ExprResult LocalRHS = CallerRHS;
  ExprResult &RHS = ConvertRHS ? CallerRHS : LocalRHS;

  if (const auto *LHSPtrType = LHSType->getAs<PointerType>()) {
    if (const auto *RHSPtrType = RHS.get()->getType()->getAs<PointerType>()) {
      if (RHSPtrType->getPointeeType()->hasAttr(attr::NoDeref) &&
          !LHSPtrType->getPointeeType()->hasAttr(attr::NoDeref)) {
        Diag(RHS.get()->getExprLoc(),
             diag::warn_noderef_to_dereferenceable_pointer)
            << RHS.get()->getSourceRange();
      }
    }
  }

  if (RHS.get()->getType() == Context.OverloadTy)
    return Incompatible;

  if (this->isNeverCStringType(LHSType) &&
      !isInsideNeverCStringRuntime()) {
    if (StringLiteral *SL = getNeverCStringLiteral(RHS.get())) {
      if (ConvertRHS) {
        ExprResult Converted =
            buildNeverCStringLiteral(*this, LHSType, RHS.get(), SL);
        if (Converted.isInvalid())
          return Incompatible;
        if (CurContext->isFunctionOrMethod()) {
          Converted = buildNeverCStringRetain(*this, RHS.get()->getExprLoc(),
                                              Converted.get());
          if (Converted.isInvalid())
            return Incompatible;
        }
        RHS = Converted;
      }
      return Compatible;
    }

    if (this->isNeverCStringType(RHS.get()->getType()) &&
        RHS.get()->getValueKind() == VK_LValue &&
        !isa<CompoundLiteralExpr>(RHS.get()->IgnoreParens())) {
      if (ConvertRHS) {
        ExprResult Converted =
            buildNeverCStringRetain(*this, RHS.get()->getExprLoc(), RHS.get());
        if (Converted.isInvalid())
          return Incompatible;
        RHS = Converted;
      }
      return Compatible;
    }
  }

  RHS = DefaultFunctionArrayLvalueConversion(RHS.get(), Diagnose);
  if (RHS.isInvalid())
    return Incompatible;

  // The constraints are expressed in terms of the atomic, qualified, or
  // unqualified type of the LHS.
  QualType LHSTypeAfterConversion = LHSType.getAtomicUnqualifiedType();

  // C99 6.5.16.1p1: the left operand is a pointer and the right is
  // a null pointer constant <C23>or its type is nullptr_t;</C23>.
  if (LHSTypeAfterConversion->isPointerType() &&
      ((getLangOpts().C23 && RHS.get()->getType()->isNullPtrType()) ||
       RHS.get()->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNull))) {
    if (Diagnose || ConvertRHS) {
      CastKind Kind;
      CheckPointerConversion(RHS.get(), LHSType, Kind,
                             /*IgnoreBaseAccess=*/false, Diagnose);
      if (ConvertRHS)
        RHS = ImpCastExprToType(RHS.get(), LHSType, Kind, VK_PRValue);
    }
    return Compatible;
  }
  // C23 6.5.16.1p1: the left operand has type atomic, qualified, or
  // unqualified bool, and the right operand is a pointer or its type is
  // nullptr_t.
  if (getLangOpts().C23 && LHSType->isBooleanType() &&
      RHS.get()->getType()->isNullPtrType()) {
    // NB: T* -> _Bool is handled in CheckAssignmentConstraints, this only
    // only handles nullptr -> _Bool due to needing an extra conversion
    // step.
    // We model this by converting from nullptr -> void * and then let the
    // conversion from void * -> _Bool happen naturally.
    if (Diagnose || ConvertRHS) {
      CastKind Kind;
      CheckPointerConversion(RHS.get(), Context.VoidPtrTy, Kind,
                             /*IgnoreBaseAccess=*/false, Diagnose);
      if (ConvertRHS)
        RHS = ImpCastExprToType(RHS.get(), Context.VoidPtrTy, Kind, VK_PRValue);
    }
  }

  CastKind Kind;
  Sema::AssignConvertType result =
      CheckAssignmentConstraints(LHSType, RHS, Kind, ConvertRHS);

  // C99 6.5.16.1p2: The value of the right operand is converted to the
  if (result != Incompatible && RHS.get()->getType() != LHSType) {
    QualType Ty = LHSType.getNonLValueExprType(Context);
    Expr *E = RHS.get();

    if (ConvertRHS)
      RHS = ImpCastExprToType(E, Ty, Kind);
  }

  return result;
}
