//===- SemaExprEval.cpp - Expr evaluation context & diagnostics ---===//
//
// Extracted from SemaExprUnaryOp.cpp (mechanical move, no logic change).
// Covers assignment diagnosis, ICE verification, evaluation contexts,
// declaration referencing, and related diagnostics.
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
// Assignment diagnosis, ICE, evaluation context & referenced decls
// ===----------------------------------------------------------------------===

namespace {
bool maybeDiagnoseAssignmentToFunction(Sema &S, QualType DstType,
                                       const Expr *SrcExpr) {
  if (!DstType->isFunctionPointerType() ||
      !SrcExpr->getType()->isFunctionType())
    return false;

  auto *DRE = dyn_cast<DeclRefExpr>(SrcExpr->IgnoreParenImpCasts());
  if (!DRE)
    return false;

  auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
  if (!FD)
    return false;

  return !S.checkAddressOfFunctionIsAvailable(FD,
                                              /*Complain=*/true,
                                              SrcExpr->getBeginLoc());
}
} // namespace

bool Sema::DiagnoseAssignmentResult(AssignConvertType ConvTy,
                                    SourceLocation Loc, QualType DstType,
                                    QualType SrcType, Expr *SrcExpr,
                                    AssignmentAction Action, bool *Complained) {
  if (Complained)
    *Complained = false;

  if (SrcExpr && SrcExpr->getType()->isDependentType())
    return ConvTy != Compatible;

  // Decode the result (notice that AST's are still created for extensions).
  bool isInvalid = false;
  unsigned DiagKind = 0;
  ConversionFixItGenerator ConvHints;
  bool MayHaveConvFixit = false;
  bool MayHaveFunctionDiff = false;

  switch (ConvTy) {
  case Compatible:
    DiagnoseAssignmentEnum(DstType, SrcType, SrcExpr);
    return false;

  case PointerToInt:
    DiagKind = diag::ext_typecheck_convert_pointer_int;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IntToPointer:
    DiagKind = diag::ext_typecheck_convert_int_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatibleFunctionPointerStrict:
    DiagKind =
        diag::warn_typecheck_convert_incompatible_function_pointer_strict;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatibleFunctionPointer:
    DiagKind = diag::ext_typecheck_convert_incompatible_function_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointer:
    DiagKind = diag::ext_typecheck_convert_incompatible_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointerSign:
    DiagKind = diag::ext_typecheck_convert_incompatible_pointer_sign;
    break;
  case FunctionVoidPointer:
    DiagKind = diag::ext_typecheck_convert_pointer_void_func;
    break;
  case IncompatiblePointerDiscardsQualifiers: {
    // Perform array-to-pointer decay if necessary.
    if (SrcType->isArrayType())
      SrcType = Context.getArrayDecayedType(SrcType);

    isInvalid = true;

    Qualifiers lhq = SrcType->getPointeeType().getQualifiers();
    Qualifiers rhq = DstType->getPointeeType().getQualifiers();
    if (lhq.getAddressSpace() != rhq.getAddressSpace()) {
      DiagKind = diag::err_typecheck_incompatible_address_space;
      break;
    }

    llvm_unreachable("unknown error case for discarding qualifiers!");
    // fallthrough
  }
  case CompatiblePointerDiscardsQualifiers:
    // If the qualifiers lost were because we were applying the
    DiagKind = diag::ext_typecheck_convert_discards_qualifiers;
    break;
  case IncompatibleNestedPointerQualifiers:
    DiagKind = diag::ext_nested_pointer_qualifier_mismatch;
    break;
  case IncompatibleNestedPointerAddressSpaceMismatch:
    DiagKind = diag::err_typecheck_incompatible_nested_address_space;
    isInvalid = true;
    break;
  case IncompatibleVectors:
    DiagKind = diag::warn_incompatible_vectors;
    break;
  case Incompatible:
    if (maybeDiagnoseAssignmentToFunction(*this, DstType, SrcExpr)) {
      if (Complained)
        *Complained = true;
      return true;
    }

    DiagKind = diag::err_typecheck_convert_incompatible;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    isInvalid = true;
    MayHaveFunctionDiff = true;
    break;
  }

  QualType FirstType, SecondType;
  switch (Action) {
  case AA_Assigning:
  case AA_Initializing:
    // The destination type comes first.
    FirstType = DstType;
    SecondType = SrcType;
    break;

  case AA_Returning:
  case AA_Passing:
  case AA_Converting:
  case AA_Sending:
  case AA_Casting:
    FirstType = SrcType;
    SecondType = DstType;
    break;
  }

  PartialDiagnostic FDiag = PDiag(DiagKind);

  FDiag << FirstType << SecondType << Action << SrcExpr->getSourceRange();

  if (DiagKind == diag::ext_typecheck_convert_incompatible_pointer_sign ||
      DiagKind == diag::err_typecheck_convert_incompatible_pointer_sign) {
    auto isPlainChar = [](const neverc::Type *Type) {
      return Type->isSpecificBuiltinType(BuiltinType::Char_S) ||
             Type->isSpecificBuiltinType(BuiltinType::Char_U);
    };
    FDiag << (isPlainChar(FirstType->getPointeeOrArrayElementType()) ||
              isPlainChar(SecondType->getPointeeOrArrayElementType()));
  }

  // If we can fix the conversion, suggest the FixIts.
  if (!ConvHints.isNull()) {
    for (FixItHint &H : ConvHints.Hints)
      FDiag << H;
  }

  if (MayHaveConvFixit) {
    FDiag << (unsigned)(ConvHints.Kind);
  }

  if (MayHaveFunctionDiff)
    DiagnoseFunctionTypeMismatch(FDiag, SecondType, FirstType);

  Diag(Loc, FDiag);

  if (Complained)
    *Complained = true;
  return isInvalid;
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                                 AllowFoldKind CanFold) {
  class SimpleICEDiagnoser : public VerifyICEDiagnoser {
  public:
    SemaDiagnosticBuilder diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                             QualType T) override {
      return S.Diag(Loc, diag::err_ice_not_integral) << T << /*C=*/false;
    }
    SemaDiagnosticBuilder diagnoseNotICE(Sema &S, SourceLocation Loc) override {
      return S.Diag(Loc, diag::err_expr_not_ice) << /*C=*/false;
    }
  } Diagnoser;

  return VerifyIntegerConstantExpression(E, Result, Diagnoser, CanFold);
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                                 unsigned DiagID,
                                                 AllowFoldKind CanFold) {
  class IDDiagnoser : public VerifyICEDiagnoser {
    unsigned DiagID;

  public:
    IDDiagnoser(unsigned DiagID)
        : VerifyICEDiagnoser(DiagID == 0), DiagID(DiagID) {}

    SemaDiagnosticBuilder diagnoseNotICE(Sema &S, SourceLocation Loc) override {
      return S.Diag(Loc, DiagID);
    }
  } Diagnoser(DiagID);

  return VerifyIntegerConstantExpression(E, Result, Diagnoser, CanFold);
}

Sema::SemaDiagnosticBuilder
Sema::VerifyICEDiagnoser::diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                             QualType T) {
  return diagnoseNotICE(S, Loc);
}

Sema::SemaDiagnosticBuilder
Sema::VerifyICEDiagnoser::diagnoseFold(Sema &S, SourceLocation Loc) {
  return S.Diag(Loc, diag::ext_expr_not_ice) << /*C=*/false;
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                                 VerifyICEDiagnoser &Diagnoser,
                                                 AllowFoldKind CanFold) {
  SourceLocation DiagLoc = E->getBeginLoc();

  if (!E->getType()->isIntegralOrUnscopedEnumerationType()) {
    if (!Diagnoser.Suppress)
      Diagnoser.diagnoseNotICEType(*this, DiagLoc, E->getType())
          << E->getSourceRange();
    return ExprError();
  }

  ExprResult RValueExpr = DefaultLvalueConversion(E);
  if (RValueExpr.isInvalid())
    return ExprError();

  E = RValueExpr.get();

  if (E->isIntegerConstantExpr(Context)) {
    if (Result)
      *Result = E->EvaluateKnownConstIntCheckOverflow(Context);
    if (!isa<ConstantExpr>(E))
      E = Result ? ConstantExpr::Create(Context, E, APValue(*Result))
                 : ConstantExpr::Create(Context, E);
    return E;
  }

  Expr::EvalResult EvalResult;
  llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
  EvalResult.Diag = &Notes;

  // Try to evaluate the expression, and produce diagnostics explaining why it's
  // not a constant expression as a side-effect.
  bool Folded =
      E->EvaluateAsRValue(EvalResult, Context, /*isConstantContext*/ true) &&
      EvalResult.Val.isInt() && !EvalResult.HasSideEffects;

  if (!isa<ConstantExpr>(E))
    E = ConstantExpr::Create(Context, E, EvalResult.Val);

  // If our only note is the usual "invalid subexpression" note, just point
  // the caret at its location rather than producing an essentially
  // redundant note.
  if (Notes.size() == 1 &&
      Notes[0].second.getDiagID() == diag::note_invalid_subexpr_in_const_expr) {
    DiagLoc = Notes[0].first;
    Notes.clear();
  }

  if (!Folded || !CanFold) {
    if (!Diagnoser.Suppress) {
      Diagnoser.diagnoseNotICE(*this, DiagLoc) << E->getSourceRange();
      for (const PartialDiagnosticAt &Note : Notes)
        Diag(Note.first, Note.second);
    }

    return ExprError();
  }

  Diagnoser.diagnoseFold(*this, DiagLoc) << E->getSourceRange();
  for (const PartialDiagnosticAt &Note : Notes)
    Diag(Note.first, Note.second);

  if (Result)
    *Result = EvalResult.Val.getInt();
  return E;
}

namespace {
class TransformToPE : public TreeTransform<TransformToPE> {
public:
  TransformToPE(Sema &SemaRef) : TreeTransform<TransformToPE>(SemaRef) {}

  bool AlwaysRebuild() { return true; }
};
} // namespace

ExprResult Sema::TransformToPotentiallyEvaluated(Expr *E) {
  assert(isUnevaluatedContext() &&
         "Should only transform unevaluated expressions");
  ExprEvalContexts.back().Context =
      ExprEvalContexts[ExprEvalContexts.size() - 2].Context;
  if (isUnevaluatedContext())
    return E;
  return TransformToPE(*this).TransformExpr(E);
}

TypeSourceInfo *Sema::TransformToPotentiallyEvaluated(TypeSourceInfo *TInfo) {
  assert(isUnevaluatedContext() &&
         "Should only transform unevaluated expressions");
  ExprEvalContexts.back().Context =
      ExprEvalContexts[ExprEvalContexts.size() - 2].Context;
  if (isUnevaluatedContext())
    return TInfo;
  return TransformToPE(*this).TransformType(TInfo);
}

void Sema::PushExpressionEvaluationContext(
    ExpressionEvaluationContext NewContext) {
  ExprEvalContexts.emplace_back(NewContext, ExprCleanupObjects.size(), Cleanup);
  Cleanup.reset();
}

namespace {

const DeclRefExpr *CheckPossibleDeref(Sema &S, const Expr *PossibleDeref) {
  PossibleDeref = PossibleDeref->IgnoreParenImpCasts();
  if (const auto *E = dyn_cast<UnaryOperator>(PossibleDeref)) {
    if (E->getOpcode() == UO_Deref)
      return CheckPossibleDeref(S, E->getSubExpr());
  } else if (const auto *E = dyn_cast<ArraySubscriptExpr>(PossibleDeref)) {
    return CheckPossibleDeref(S, E->getBase());
  } else if (const auto *E = dyn_cast<MemberExpr>(PossibleDeref)) {
    return CheckPossibleDeref(S, E->getBase());
  } else if (const auto E = dyn_cast<DeclRefExpr>(PossibleDeref)) {
    QualType Inner;
    QualType Ty = E->getType();
    if (const auto *Ptr = Ty->getAs<PointerType>())
      Inner = Ptr->getPointeeType();
    else if (const auto *Arr = S.Context.getAsArrayType(Ty))
      Inner = Arr->getElementType();
    else
      return nullptr;

    if (Inner->hasAttr(attr::NoDeref))
      return E;
  }
  return nullptr;
}

} // namespace

void Sema::WarnOnPendingNoDerefs(ExpressionEvaluationContextRecord &Rec) {
  for (const Expr *E : Rec.PossibleDerefs) {
    const DeclRefExpr *DeclRef = CheckPossibleDeref(*this, E);
    if (DeclRef) {
      const ValueDecl *Decl = DeclRef->getDecl();
      Diag(E->getExprLoc(), diag::warn_dereference_of_noderef_type)
          << Decl->getName() << E->getSourceRange();
      Diag(Decl->getLocation(), diag::note_previous_decl) << Decl->getName();
    } else {
      Diag(E->getExprLoc(), diag::warn_dereference_of_noderef_type_no_decl)
          << E->getSourceRange();
    }
  }
  Rec.PossibleDerefs.clear();
}

void Sema::PopExpressionEvaluationContext() {
  ExpressionEvaluationContextRecord &Rec = ExprEvalContexts.back();
  unsigned NumTypos = Rec.NumTypos;

  WarnOnPendingNoDerefs(Rec);

  // When are coming out of an unevaluated context, clear out any
  // temporaries that we may have created as part of the evaluation of
  // the expression in that context: they aren't relevant because they
  // will never be constructed.
  if (Rec.isUnevaluated() || Rec.isConstantEvaluated()) {
    ExprCleanupObjects.erase(ExprCleanupObjects.begin() + Rec.NumCleanupObjects,
                             ExprCleanupObjects.end());
    Cleanup = Rec.ParentCleanup;
  } else {
    Cleanup.mergeFrom(Rec.ParentCleanup);
  }

  // Pop the current expression evaluation context off the stack.
  ExprEvalContexts.pop_back();

  // The global expression evaluation context record is never popped.
  ExprEvalContexts.back().NumTypos += NumTypos;
}

void Sema::DiscardCleanupsInEvaluationContext() {
  ExprCleanupObjects.erase(ExprCleanupObjects.begin() +
                               ExprEvalContexts.back().NumCleanupObjects,
                           ExprCleanupObjects.end());
  Cleanup.reset();
}

ExprResult Sema::ResolveExprEvaluationContextForTypeof(Expr *E) {
  ExprResult Result = CheckPlaceholderExpr(E);
  if (Result.isInvalid())
    return ExprError();
  E = Result.get();
  if (!E->getType()->isVariablyModifiedType())
    return E;
  return TransformToPotentiallyEvaluated(E);
}

namespace {
bool funcHasParameterSizeMangling(Sema &S, FunctionDecl *FD) {
  // These manglings don't do anything on non-Windows or non-x86 platforms, so
  // we don't need parameter type sizes.
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  if (!TT.isOSWindows() || !TT.isX86())
    return false;

  // Stdcall, fastcall, and vectorcall need this special treatment.
  CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
  switch (CC) {
  case CC_X86StdCall:
  case CC_X86FastCall:
  case CC_X86VectorCall:
    return true;
  default:
    break;
  }
  return false;
}
} // namespace

namespace {
void checkCompleteParameterTypesForMangler(Sema &S, FunctionDecl *FD,
                                           SourceLocation Loc) {
  class ParamIncompleteTypeDiagnoser : public Sema::TypeDiagnoser {
    FunctionDecl *FD;
    ParmVarDecl *Param;

  public:
    ParamIncompleteTypeDiagnoser(FunctionDecl *FD, ParmVarDecl *Param)
        : FD(FD), Param(Param) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
      llvm::StringRef CCName;
      switch (CC) {
      case CC_X86StdCall:
        CCName = "stdcall";
        break;
      case CC_X86FastCall:
        CCName = "fastcall";
        break;
      case CC_X86VectorCall:
        CCName = "vectorcall";
        break;
      default:
        llvm_unreachable("CC does not need mangling");
      }

      S.Diag(Loc, diag::err_cconv_incomplete_param_type)
          << Param->getDeclName() << FD->getDeclName() << CCName;
    }
  };

  for (ParmVarDecl *Param : FD->parameters()) {
    ParamIncompleteTypeDiagnoser Diagnoser(FD, Param);
    S.RequireCompleteType(Loc, Param->getType(), Diagnoser);
  }
}
} // namespace

namespace {
enum class OdrUseContext { None, FormallyOdrUsed, Used };
}

namespace {
OdrUseContext isOdrUseContext(Sema &SemaRef) {
  OdrUseContext Result;

  switch (SemaRef.ExprEvalContexts.back().Context) {
  case Sema::ExpressionEvaluationContext::Unevaluated:
  case Sema::ExpressionEvaluationContext::UnevaluatedAbstract:
    return OdrUseContext::None;

  case Sema::ExpressionEvaluationContext::ConstantEvaluated:
  case Sema::ExpressionEvaluationContext::PotentiallyEvaluated:
    Result = OdrUseContext::Used;
    break;
  }

  return Result;
}
} // namespace

void Sema::MarkFunctionReferenced(SourceLocation Loc, FunctionDecl *Func,
                                  bool MightBeOdrUse) {
  assert(Func && "No function?");

  Func->setReferenced();

  // Recursive functions aren't really used until they're used from some other
  // context.
  bool IsRecursiveCall = CurContext == Func;

  // Potentially-evaluated function name => odr-use (C99 6.9p3); see
  // isOdrUseContext.
  OdrUseContext OdrUse =
      MightBeOdrUse ? isOdrUseContext(*this) : OdrUseContext::None;
  if (IsRecursiveCall && OdrUse == OdrUseContext::Used)
    OdrUse = OdrUseContext::FormallyOdrUsed;

  // If this is the first "real" use, act on that.
  if (OdrUse == OdrUseContext::Used && !Func->isUsed(/*CheckUsedAttr=*/false)) {
    // Keep track of used but undefined functions.
    if (!Func->isDefined()) {
      if (mightHaveNonExternalLinkage(Func))
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
      else if (Func->getMostRecentDecl()->isInlined() && !LangOpts.GNUInline &&
               !Func->getMostRecentDecl()->hasAttr<GNUInlineAttr>()) {
        const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
        if (Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
          Func->getMostRecentDecl()->setImplicitlyInline(false);
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
      }
    }

    // Some x86 Windows calling conventions mangle the size of the parameter
    // pack into the name. Computing the size of the parameters requires the
    // parameter types to be complete. Check that now.
    if (funcHasParameterSizeMangling(*this, Func))
      checkCompleteParameterTypesForMangler(*this, Func, Loc);

    Func->markUsed(Context);
  }
  // If the function is inlined in the most recent declaration but not in the
  // first declaration, then the function is implicitly inlined. This can happen
  // when a function is not declared inline in a header file and then defined in
  // a source file with the inline keyword. In this case, we need to set the
  // `ImplicitlyInline` flag to false so that the function is not treated as
  // explicitly inlined.
  const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
  if ((Triple.isOSWindows() || Triple.isOSBinFormatCOFF()) &&
      Func->getMostRecentDecl() && Func->getFirstDecl() &&
      Func->getMostRecentDecl()->isInlined() &&
      !Func->getFirstDecl()->isInlined()) {
    Func->getMostRecentDecl()->setImplicitlyInline(false);
  }
}

namespace {
void markVarDeclODRUsed(ValueDecl *V, SourceLocation Loc, Sema &SemaRef) {
  auto *Var = cast<VarDecl>(V);

  if (LLVM_UNLIKELY(isa<ParmVarDecl>(Var)
                        ? false
                        : Var->hasExternalStorage() || Var->isInline()) &&
      Var->hasDefinition(SemaRef.Context) == VarDecl::DeclarationOnly &&
      (!Var->isExternallyVisible() || Var->isInline())) {
    SourceLocation &old = SemaRef.UndefinedButUsed[Var->getCanonicalDecl()];
    if (old.isInvalid())
      old = Loc;
  }
  SemaRef.tryCaptureVariable(V, Loc);

  V->markUsed(SemaRef.Context);
}
} // namespace

void diagnoseUncapturableValueReferenceOrBinding(Sema &S, SourceLocation loc,
                                                 ValueDecl *var) {
  DeclContext *VarDC = var->getDeclContext();

  //  If the parameter still belongs to the translation unit, then
  //  we're actually just using one parameter in the declaration of
  //  the next.
  if (isa<ParmVarDecl>(var) && isa<TranslationUnitDecl>(VarDC))
    return;

  // Outside a function, other diagnostics will cover invalid captures.
  if (!S.CurContext->isFunctionOrMethod())
    return;

  unsigned ContextKind = isa<FunctionDecl>(VarDC) ? 0 : 1;

  S.Diag(loc, diag::err_reference_to_local_in_enclosing_context)
      << var << ContextKind << VarDC;
  S.Diag(var->getLocation(), diag::note_entity_declared_at) << var;
}

bool Sema::tryCaptureVariable(ValueDecl *Var, SourceLocation Loc) {
  DeclContext *VarDC = Var->getDeclContext();
  if (VarDC == CurContext)
    return true;

  const auto *VD = dyn_cast<VarDecl>(Var);
  if (!VD || !VD->hasLocalStorage())
    return true;

  diagnoseUncapturableValueReferenceOrBinding(*this, Loc, Var);
  return true;
}

namespace {
ExprResult rebuildPotentialResultsAsNonOdrUsed(Sema &S, Expr *E,
                                               NonOdrUseReason NOUR) {
  // Strip odr-use when a variable only appears in the "constant / immediately
  // lvalue-to-rvalue" cases.
  //
  // If we encounter a node that claims to be an odr-use but shouldn't be, we
  // transform it into the relevant kind of non-odr-use node and rebuild the
  // tree of nodes leading to it.
  //
  // This is a mini-TreeTransform that only transforms a restricted subset of
  // nodes (and only certain operands of them).

  // Rebuild a subexpression.
  auto Rebuild = [&](Expr *Sub) {
    return rebuildPotentialResultsAsNonOdrUsed(S, Sub, NOUR);
  };

  // Check whether a potential result satisfies the requirements of NOUR.
  auto IsPotentialResultOdrUsed = [&](NamedDecl *D) {
    // Any entity other than a VarDecl is always odr-used whenever it's named
    // in a potentially-evaluated expression.
    auto *VD = dyn_cast<VarDecl>(D);
    if (!VD)
      return true;

    // Variable odr-use exceptions: constexpr-usable refs; non-class potential
    // results with lvalue-to-rvalue; discarded-value cases (see also
    // CheckLValueToRValueConversionOperand).
    switch (NOUR) {
    case NOUR_None:
    case NOUR_Unevaluated:
      llvm_unreachable("unexpected non-odr-use-reason");

    case NOUR_Constant:
      return true;
    }
    return false;
  };

  auto MarkNotOdrUsed = [&] {};

  // Walk the "potential results" of an expression for non-odr-use rebuilds.
  switch (E->getStmtClass()) {
  //   -- If e is an id-expression, ...
  case Expr::DeclRefExprClass: {
    auto *DRE = cast<DeclRefExpr>(E);
    if (DRE->isNonOdrUse() || IsPotentialResultOdrUsed(DRE->getDecl()))
      break;

    // Rebuild as a non-odr-use DeclRefExpr.
    MarkNotOdrUsed();
    return DeclRefExpr::Create(S.Context, DRE->getDecl(), DRE->getNameInfo(),
                               DRE->getType(), DRE->getValueKind(),
                               DRE->getFoundDecl(), NOUR);
  }

  //   -- If e is a subscripting operation with an array operand...
  case Expr::ArraySubscriptExprClass: {
    auto *ASE = cast<ArraySubscriptExpr>(E);
    Expr *OldBase = ASE->getBase()->IgnoreImplicit();
    if (!OldBase->getType()->isArrayType())
      break;
    ExprResult Base = Rebuild(OldBase);
    if (!Base.isUsable())
      return Base;
    Expr *LHS = ASE->getBase() == ASE->getLHS() ? Base.get() : ASE->getLHS();
    Expr *RHS = ASE->getBase() == ASE->getRHS() ? Base.get() : ASE->getRHS();
    SourceLocation LBracketLoc = ASE->getBeginLoc();
    return S.OnArraySubscriptExpr(nullptr, LHS, LBracketLoc, RHS,
                                  ASE->getRBracketLoc());
  }

  case Expr::MemberExprClass: {
    auto *ME = cast<MemberExpr>(E);
    // -- If e is a member access expression naming a field...
    if (isa<FieldDecl>(ME->getMemberDecl())) {
      ExprResult Base = Rebuild(ME->getBase());
      if (!Base.isUsable())
        return Base;
      return MemberExpr::Create(S.Context, Base.get(), ME->isArrow(),
                                ME->getOperatorLoc(), ME->getMemberDecl(),
                                ME->getFoundDecl(), ME->getMemberNameInfo(),
                                ME->getType(), ME->getValueKind(),
                                ME->getObjectKind(), ME->isNonOdrUse());
    }

    // -- Otherwise, fall through.
    if (ME->isNonOdrUse() || IsPotentialResultOdrUsed(ME->getMemberDecl()))
      break;

    // Rebuild as a non-odr-use MemberExpr.
    MarkNotOdrUsed();
    return MemberExpr::Create(
        S.Context, ME->getBase(), ME->isArrow(), ME->getOperatorLoc(),
        ME->getMemberDecl(), ME->getFoundDecl(), ME->getMemberNameInfo(),
        ME->getType(), ME->getValueKind(), ME->getObjectKind(), NOUR);
  }

  case Expr::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    Expr *LHS = BO->getLHS();
    Expr *RHS = BO->getRHS();
    //   -- If e is a comma expression, ...
    if (BO->getOpcode() == BO_Comma) {
      ExprResult Sub = Rebuild(RHS);
      if (!Sub.isUsable())
        return Sub;
      RHS = Sub.get();
    } else {
      break;
    }
    return S.FormBinOp(nullptr, BO->getOperatorLoc(), BO->getOpcode(), LHS,
                       RHS);
  }

  //   -- If e has the form (e1)...
  case Expr::ParenExprClass: {
    auto *PE = cast<ParenExpr>(E);
    ExprResult Sub = Rebuild(PE->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return S.OnParenExpr(PE->getLParen(), PE->getRParen(), Sub.get());
  }

  //   -- If e is an lvalue conditional expression, ...
  case Expr::ConditionalOperatorClass: {
    auto *CO = cast<ConditionalOperator>(E);
    ExprResult LHS = Rebuild(CO->getLHS());
    if (LHS.isInvalid())
      return ExprError();
    ExprResult RHS = Rebuild(CO->getRHS());
    if (RHS.isInvalid())
      return ExprError();
    if (!LHS.isUsable() && !RHS.isUsable())
      return ExprEmpty();
    if (!LHS.isUsable())
      LHS = CO->getLHS();
    if (!RHS.isUsable())
      RHS = CO->getRHS();
    return S.OnConditionalOp(CO->getQuestionLoc(), CO->getColonLoc(),
                             CO->getCond(), LHS.get(), RHS.get());
  }

  // [NeverC extension]
  //   -- If e has the form __extension__ e1...
  case Expr::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    if (UO->getOpcode() != UO_Extension)
      break;
    ExprResult Sub = Rebuild(UO->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return S.FormUnaryOp(nullptr, UO->getOperatorLoc(), UO_Extension,
                         Sub.get());
  }

  // [NeverC extension]
  //   -- If e has the form _Generic(...), the set of potential results is the
  //      union of the sets of potential results of the associated expressions.
  case Expr::GenericSelectionExprClass: {
    auto *GSE = cast<GenericSelectionExpr>(E);

    llvm::SmallVector<Expr *, 4> AssocExprs;
    bool AnyChanged = false;
    for (Expr *OrigAssocExpr : GSE->getAssocExprs()) {
      ExprResult AssocExpr = Rebuild(OrigAssocExpr);
      if (AssocExpr.isInvalid())
        return ExprError();
      if (AssocExpr.isUsable()) {
        AssocExprs.push_back(AssocExpr.get());
        AnyChanged = true;
      } else {
        AssocExprs.push_back(OrigAssocExpr);
      }
    }

    void *ExOrTy = nullptr;
    bool IsExpr = GSE->isExprPredicate();
    if (IsExpr)
      ExOrTy = GSE->getControllingExpr();
    else
      ExOrTy = GSE->getControllingType();
    return AnyChanged ? S.CreateGenericSelectionExpr(
                            GSE->getGenericLoc(), GSE->getDefaultLoc(),
                            GSE->getRParenLoc(), IsExpr, ExOrTy,
                            GSE->getAssocTypeSourceInfos(), AssocExprs)
                      : ExprEmpty();
  }

  // [NeverC extension]
  //   -- If e has the form __builtin_choose_expr(...), the set of potential
  //      results is the union of the sets of potential results of the
  //      second and third subexpressions.
  case Expr::ChooseExprClass: {
    auto *CE = cast<ChooseExpr>(E);

    ExprResult LHS = Rebuild(CE->getLHS());
    if (LHS.isInvalid())
      return ExprError();

    ExprResult RHS = Rebuild(CE->getRHS());
    if (RHS.isInvalid())
      return ExprError();

    if (!LHS.get() && !RHS.get())
      return ExprEmpty();
    if (!LHS.isUsable())
      LHS = CE->getLHS();
    if (!RHS.isUsable())
      RHS = CE->getRHS();

    return S.OnChooseExpr(CE->getBuiltinLoc(), CE->getCond(), LHS.get(),
                          RHS.get(), CE->getRParenLoc());
  }

  // Step through non-syntactic nodes.
  case Expr::ConstantExprClass: {
    auto *CE = cast<ConstantExpr>(E);
    ExprResult Sub = Rebuild(CE->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return ConstantExpr::Create(S.Context, Sub.get());
  }

  // We could mostly rely on the recursive rebuilding to rebuild implicit
  // casts, but not at the top level, so rebuild them here.
  case Expr::ImplicitCastExprClass: {
    auto *ICE = cast<ImplicitCastExpr>(E);
    // Only step through no-op implicit casts; other kinds are not used for
    // potential-result / odr-use analysis in C.
    switch (ICE->getCastKind()) {
    case CK_NoOp: {
      ExprResult Sub = Rebuild(ICE->getSubExpr());
      if (!Sub.isUsable())
        return Sub;
      return S.ImpCastExprToType(Sub.get(), ICE->getType(), ICE->getCastKind(),
                                 ICE->getValueKind());
    }

    default:
      break;
    }
    break;
  }

  default:
    break;
  }

  // Can't traverse through this node. Nothing to do.
  return ExprEmpty();
}
} // namespace

ExprResult Sema::CheckLValueToRValueConversionOperand(Expr *E) {
  // Only scalar (non-class, non-volatile) lvalue-to-rvalue can drop odr-use.
  if (E->getType().isVolatileQualified() || E->getType()->getAs<RecordType>())
    return E;

  // NOUR_Constant never transforms a plain DeclRefExpr —
  // IsPotentialResultOdrUsed unconditionally returns true for NOUR_Constant, so
  // the rebuild is a no-op.
  if (LLVM_LIKELY(isa<DeclRefExpr>(E)))
    return E;

  ExprResult Result =
      rebuildPotentialResultsAsNonOdrUsed(*this, E, NOUR_Constant);
  if (Result.isInvalid())
    return ExprError();
  return Result.get() ? Result : E;
}

ExprResult Sema::OnConstantExpression(ExprResult Res) {
  if (!Res.isUsable())
    return Res;

  return CheckLValueToRValueConversionOperand(Res.get());
}

namespace {
void doMarkVarDeclReferenced(
    Sema &SemaRef, SourceLocation Loc, VarDecl *Var, Expr *E,
    llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  assert((!E || isa<DeclRefExpr>(E) || isa<MemberExpr>(E)) &&
         "Invalid Expr argument to doMarkVarDeclReferenced");
  Var->setReferenced();

  if (LLVM_UNLIKELY(Var->isInvalidDecl()))
    return;

  if (LLVM_UNLIKELY(SemaRef.TrackUnusedButSet) && Var->isLocalVarDeclOrParm() &&
      !Var->hasExternalStorage()) {
    ++RefsMinusAssignments[Var];
  }

  if (DeclRefExpr *DRE = dyn_cast_or_null<DeclRefExpr>(E))
    if (DRE->isNonOdrUse())
      return;
  if (MemberExpr *ME = dyn_cast_or_null<MemberExpr>(E))
    if (ME->isNonOdrUse())
      return;

  OdrUseContext OdrUse = isOdrUseContext(SemaRef);

  switch (OdrUse) {
  case OdrUseContext::None:
    assert((!E || SemaRef.isUnevaluatedContext()) &&
           "missing non-odr-use marking for unevaluated decl ref");
    break;

  case OdrUseContext::FormallyOdrUsed:
    break;

  case OdrUseContext::Used:
    markVarDeclODRUsed(Var, Loc, SemaRef);
    break;
  }
}
} // namespace

void Sema::MarkVariableReferenced(SourceLocation Loc, VarDecl *Var) {
  doMarkVarDeclReferenced(*this, Loc, Var, nullptr, RefsMinusAssignments);
}

namespace {
void markExprReferenced(
    Sema &SemaRef, SourceLocation Loc, Decl *D, Expr *E, bool MightBeOdrUse,
    llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  if (VarDecl *Var = dyn_cast<VarDecl>(D)) {
    doMarkVarDeclReferenced(SemaRef, Loc, Var, E, RefsMinusAssignments);
    return;
  }

  SemaRef.MarkAnyDeclReferenced(Loc, D, MightBeOdrUse);
}
} // namespace

void Sema::MarkDeclRefReferenced(DeclRefExpr *E) {
  bool OdrUse = true;

  markExprReferenced(*this, E->getLocation(), E->getDecl(), E, OdrUse,
                     RefsMinusAssignments);
}

void Sema::MarkMemberReferenced(MemberExpr *E) {
  bool MightBeOdrUse = true;
  SourceLocation Loc =
      E->getMemberLoc().isValid() ? E->getMemberLoc() : E->getBeginLoc();
  markExprReferenced(*this, Loc, E->getMemberDecl(), E, MightBeOdrUse,
                     RefsMinusAssignments);
}

void Sema::MarkAnyDeclReferenced(SourceLocation Loc, Decl *D,
                                 bool MightBeOdrUse) {
  if (MightBeOdrUse) {
    if (auto *VD = dyn_cast<VarDecl>(D)) {
      MarkVariableReferenced(Loc, VD);
      return;
    }
  }
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    MarkFunctionReferenced(Loc, FD, MightBeOdrUse);
    return;
  }
  D->setReferenced();
}

namespace {
class EvaluatedExprMarker : public ReferencedDeclWalker<EvaluatedExprMarker> {
public:
  typedef ReferencedDeclWalker<EvaluatedExprMarker> Inherited;
  bool SkipLocalVariables;
  llvm::ArrayRef<const Expr *> StopAt;

  EvaluatedExprMarker(Sema &S, bool SkipLocalVariables,
                      llvm::ArrayRef<const Expr *> StopAt)
      : Inherited(S), SkipLocalVariables(SkipLocalVariables), StopAt(StopAt) {}

  void visitUsedDecl(SourceLocation Loc, Decl *D) {
    S.MarkFunctionReferenced(Loc, cast<FunctionDecl>(D));
  }

  void Visit(Expr *E) {
    if (llvm::is_contained(StopAt, E))
      return;
    Inherited::Visit(E);
  }

  void VisitConstantExpr(ConstantExpr *E) {
    // Don't mark declarations within a ConstantExpression, as this expression
    // will be evaluated and folded to a value.
  }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    // If we were asked not to visit local variables, don't.
    if (SkipLocalVariables) {
      if (VarDecl *VD = dyn_cast<VarDecl>(E->getDecl()))
        if (VD->hasLocalStorage())
          return;
    }

    S.MarkDeclRefReferenced(E);
  }

  void VisitMemberExpr(MemberExpr *E) {
    S.MarkMemberReferenced(E);
    Visit(E->getBase());
  }
};
} // namespace

void Sema::MarkDeclarationsReferencedInExpr(
    Expr *E, bool SkipLocalVariables, llvm::ArrayRef<const Expr *> StopAt) {
  EvaluatedExprMarker(*this, SkipLocalVariables, StopAt).Visit(E);
}

bool Sema::DiagIfReachable(SourceLocation Loc,
                           llvm::ArrayRef<const Stmt *> Stmts,
                           const PartialDiagnostic &PD) {
  if (!Stmts.empty() && getCurFunctionDecl()) {
    if (!FunctionScopes.empty())
      FunctionScopes.back()->PossiblyUnreachableDiags.push_back(
          sema::PossiblyUnreachableDiag(PD, Loc, Stmts));
    return true;
  }

  Diag(Loc, PD);
  return true;
}

bool Sema::DiagRuntimeBehavior(SourceLocation Loc,
                               llvm::ArrayRef<const Stmt *> Stmts,
                               const PartialDiagnostic &PD) {

  switch (ExprEvalContexts.back().Context) {
  case ExpressionEvaluationContext::Unevaluated:
  case ExpressionEvaluationContext::UnevaluatedAbstract:
    break;

  case ExpressionEvaluationContext::ConstantEvaluated:
    break;

  case ExpressionEvaluationContext::PotentiallyEvaluated:
    return DiagIfReachable(Loc, Stmts, PD);
  }

  return false;
}

bool Sema::DiagRuntimeBehavior(SourceLocation Loc, const Stmt *Statement,
                               const PartialDiagnostic &PD) {
  return DiagRuntimeBehavior(
      Loc, Statement ? llvm::ArrayRef(Statement) : std::nullopt, PD);
}

bool Sema::CheckCallReturnType(QualType ReturnType, SourceLocation Loc,
                               CallExpr *CE, FunctionDecl *FD) {
  if (ReturnType->isVoidType() || !ReturnType->isIncompleteType())
    return false;

  class CallReturnIncompleteDiagnoser : public TypeDiagnoser {
    FunctionDecl *FD;
    CallExpr *CE;

  public:
    CallReturnIncompleteDiagnoser(FunctionDecl *FD, CallExpr *CE)
        : FD(FD), CE(CE) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      if (!FD) {
        S.Diag(Loc, diag::err_call_incomplete_return)
            << T << CE->getSourceRange();
        return;
      }

      S.Diag(Loc, diag::err_call_function_incomplete_return)
          << CE->getSourceRange() << FD << T;
      S.Diag(FD->getLocation(), diag::note_entity_declared_at)
          << FD->getDeclName();
    }
  } Diagnoser(FD, CE);

  if (RequireCompleteType(Loc, ReturnType, Diagnoser))
    return true;

  return false;
}

// Diagnose the s/=/==/ and s/\|=/!=/ typos. Note that adding parentheses
// will prevent this condition from triggering, which is what we want.
void Sema::DiagnoseAssignmentAsCondition(Expr *E) {
  SourceLocation Loc;

  unsigned diagnostic = diag::warn_condition_is_assignment;
  bool IsOrAssign = false;

  if (BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (Op->getOpcode() != BO_Assign && Op->getOpcode() != BO_OrAssign)
      return;

    IsOrAssign = Op->getOpcode() == BO_OrAssign;

    // Greylist some idioms by putting them into a warning subcategory.

    Loc = Op->getOperatorLoc();
  } else {
    return;
  }

  Diag(Loc, diagnostic) << E->getSourceRange();

  SourceLocation Open = E->getBeginLoc();
  SourceLocation Close = getLocForEndOfToken(E->getSourceRange().getEnd());
  Diag(Loc, diag::note_condition_assign_silence)
      << FixItHint::CreateInsertion(Open, "(")
      << FixItHint::CreateInsertion(Close, ")");

  if (IsOrAssign)
    Diag(Loc, diag::note_condition_or_assign_to_comparison)
        << FixItHint::CreateReplacement(Loc, "!=");
  else
    Diag(Loc, diag::note_condition_assign_to_comparison)
        << FixItHint::CreateReplacement(Loc, "==");
}

void Sema::DiagnoseEqualityWithExtraParens(ParenExpr *ParenE) {
  // Don't warn if the parens came from a macro.
  SourceLocation parenLoc = ParenE->getBeginLoc();
  if (parenLoc.isInvalid() || parenLoc.isMacroID())
    return;
  Expr *E = ParenE->IgnoreParens();

  if (BinaryOperator *opE = dyn_cast<BinaryOperator>(E))
    if (opE->getOpcode() == BO_EQ &&
        opE->getLHS()->IgnoreParenImpCasts()->isModifiableLvalue(Context) ==
            Expr::MLV_Valid) {
      SourceLocation Loc = opE->getOperatorLoc();

      Diag(Loc, diag::warn_equality_with_extra_parens) << E->getSourceRange();
      SourceRange ParenERange = ParenE->getSourceRange();
      Diag(Loc, diag::note_equality_comparison_silence)
          << FixItHint::CreateRemoval(ParenERange.getBegin())
          << FixItHint::CreateRemoval(ParenERange.getEnd());
      Diag(Loc, diag::note_equality_comparison_to_assign)
          << FixItHint::CreateReplacement(Loc, "=");
    }
}

ExprResult Sema::CheckBooleanCondition(SourceLocation Loc, Expr *E,
                                       bool IsConstexpr) {
  DiagnoseAssignmentAsCondition(E);
  if (ParenExpr *parenE = dyn_cast<ParenExpr>(E))
    DiagnoseEqualityWithExtraParens(parenE);

  ExprResult result = CheckPlaceholderExpr(E);
  if (result.isInvalid())
    return ExprError();
  E = result.get();

  {
    ExprResult ERes = DefaultFunctionArrayLvalueConversion(E);
    if (ERes.isInvalid())
      return ExprError();
    E = ERes.get();

    QualType T = E->getType();
    if (!T->isScalarType()) {
      Diag(Loc, diag::err_typecheck_statement_requires_scalar)
          << T << E->getSourceRange();
      return ExprError();
    }
    CheckBoolLikeConversion(E, Loc);
  }

  return E;
}

Sema::ConditionResult Sema::OnCondition(Scope *S, SourceLocation Loc,
                                        Expr *SubExpr, ConditionKind CK,
                                        bool MissingOK) {
  // MissingOK indicates whether having no condition expression is valid
  // (for loop) or invalid (e.g. while loop).
  if (!SubExpr)
    return MissingOK ? ConditionResult() : ConditionError();

  ExprResult Cond;
  switch (CK) {
  case ConditionKind::Boolean:
    Cond = CheckBooleanCondition(Loc, SubExpr);
    break;

  case ConditionKind::Switch:
    Cond = CheckSwitchCondition(Loc, SubExpr);
    break;
  }
  if (Cond.isInvalid()) {
    Cond = CreateRecoveryExpr(SubExpr->getBeginLoc(), SubExpr->getEndLoc(),
                              {SubExpr}, PreferredConditionType(CK));
    if (!Cond.get())
      return ConditionError();
  }
  FullExprArg FullExpr = MakeFullExpr(Cond.get(), Loc);
  if (!FullExpr.get())
    return ConditionError();

  return ConditionResult(*this, nullptr, FullExpr, false);
}

ExprResult Sema::CheckPlaceholderExpr(Expr *E) {
  const BuiltinType *placeholderType = E->getType()->getAsPlaceholderType();
  if (!placeholderType)
    return E;

  switch (placeholderType->getKind()) {

  case BuiltinType::Overload: {
    ExprResult Result = E;
    tryToRecoverWithCall(Result, PDiag(diag::err_ovl_unresolvable),
                         /*complain*/ true);
    return Result;
  }

  case BuiltinType::PseudoObject:
    return ExprError();

  case BuiltinType::BuiltinFn: {
    // Accept __noop without parens by implicitly converting it to a call expr.
    auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    if (DRE) {
      auto *FD = cast<FunctionDecl>(DRE->getDecl());
      unsigned BuiltinID = FD->getBuiltinID();
      if (BuiltinID == Builtin::BI__noop) {
        E = ImpCastExprToType(E, Context.getPointerType(FD->getType()),
                              CK_BuiltinFnToFnPtr)
                .get();
        return CallExpr::Create(Context, E, /*Args=*/{}, Context.IntTy,
                                VK_PRValue, SourceLocation(),
                                FPOptionsOverride());
      }
    }

    Diag(E->getBeginLoc(), diag::err_builtin_fn_use);
    return ExprError();
  }

  case BuiltinType::IncompleteMatrixIdx:
    Diag(cast<MatrixSubscriptExpr>(E->IgnoreParens())
             ->getRowIdx()
             ->getBeginLoc(),
         diag::err_matrix_incomplete_index);
    return ExprError();

    // Everything else should be impossible.
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
#define BUILTIN_TYPE(Id, SingletonId) case BuiltinType::Id:
#define PLACEHOLDER_TYPE(Id, SingletonId)
#include "neverc/Tree/Type/BuiltinTypes.def"
    break;
  }

  llvm_unreachable("invalid placeholder type!");
}

bool Sema::CheckCaseExpression(Expr *E) {
  if (E->isIntegerConstantExpr(Context))
    return E->getType()->isIntegralOrEnumerationType();
  return false;
}

ExprResult Sema::CreateRecoveryExpr(SourceLocation Begin, SourceLocation End,
                                    llvm::ArrayRef<Expr *> SubExprs,
                                    QualType T) {
  if (!Context.getLangOpts().RecoveryAST)
    return ExprError();

  if (T.isNull() || T->isUndeducedType() ||
      !Context.getLangOpts().RecoveryASTType)
    // We don't know the concrete type, fallback to dependent type.
    T = Context.DependentTy;

  return RecoveryExpr::Create(Context, T, Begin, End, SubExprs);
}
