#include "SemaStmtUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Expr/IgnoreExpr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Simple statements & compound scope
// ===----------------------------------------------------------------------===

StmtResult Sema::OnExprStmt(ExprResult FE, bool DiscardedValue) {
  if (FE.isInvalid())
    return StmtError();

  FE = OnFinishFullExpr(FE.get(), FE.get()->getExprLoc(), DiscardedValue);
  if (FE.isInvalid())
    return StmtError();

  // Expression statement is evaluated for side effects only (void context).
  return StmtResult(FE.getAs<Stmt>());
}

StmtResult Sema::OnExprStmtError() {
  DiscardCleanupsInEvaluationContext();
  return StmtError();
}

StmtResult Sema::OnNullStmt(SourceLocation SemiLoc, bool HasLeadingEmptyMacro) {
  return new (Context) NullStmt(SemiLoc, HasLeadingEmptyMacro);
}

StmtResult Sema::OnDeclStmt(DeclGroupPtrTy dg, SourceLocation StartLoc,
                            SourceLocation EndLoc) {
  DeclGroupRef DG = dg.get();

  // If we have an invalid decl, just return an error.
  if (DG.isNull())
    return StmtError();

  return new (Context) DeclStmt(DG, StartLoc, EndLoc);
}

namespace {
bool diagnoseUnusedComparison(Sema &S, const Expr *E) {
  SourceLocation Loc;
  bool CanAssign;
  enum { Equality, Inequality, Relational } Kind;

  if (const BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (!Op->isComparisonOp())
      return false;

    if (Op->getOpcode() == BO_EQ)
      Kind = Equality;
    else if (Op->getOpcode() == BO_NE)
      Kind = Inequality;
    else {
      assert(Op->isRelationalOp());
      Kind = Relational;
    }
    Loc = Op->getOperatorLoc();
    CanAssign = Op->getLHS()->IgnoreParenImpCasts()->isLValue();
  } else {
    // Not a typo-prone comparison.
    return false;
  }

  // Suppress warnings when the operator, suspicious as it may be, comes from
  // a macro expansion.
  if (S.SourceMgr.isMacroBodyExpansion(Loc))
    return false;

  S.Diag(Loc, diag::warn_unused_comparison)
      << (unsigned)Kind << E->getSourceRange();

  // If the LHS is a plausible entity to assign to, provide a fixit hint to
  // correct common typos.
  if (CanAssign) {
    if (Kind == Inequality)
      S.Diag(Loc, diag::note_inequality_comparison_to_or_assign)
          << FixItHint::CreateReplacement(Loc, "|=");
    else if (Kind == Equality)
      S.Diag(Loc, diag::note_equality_comparison_to_assign)
          << FixItHint::CreateReplacement(Loc, "=");
  }

  return true;
}

bool diagnoseNoDiscard(Sema &S, const WarnUnusedResultAttr *A,
                       SourceLocation Loc, SourceRange R1, SourceRange R2) {
  if (!A)
    return false;
  llvm::StringRef Msg = A->getMessage();

  if (Msg.empty())
    return S.Diag(Loc, diag::warn_unused_result) << A << R1 << R2;

  return S.Diag(Loc, diag::warn_unused_result_msg) << A << Msg << R1 << R2;
}
} // namespace

void Sema::DiagnoseUnusedExprResult(const Stmt *S, unsigned DiagID) {
  if (const LabelStmt *Label = dyn_cast_or_null<LabelStmt>(S))
    return DiagnoseUnusedExprResult(Label->getSubStmt(), DiagID);

  const Expr *E = dyn_cast_or_null<Expr>(S);
  if (!E)
    return;

  // If we are in an unevaluated expression context, then there can be no unused
  // results because the results aren't expected to be used in the first place.
  if (isUnevaluatedContext())
    return;

  SourceLocation ExprLoc = E->IgnoreParenImpCasts()->getExprLoc();
  // In most cases, we don't want to warn if the expression is written in a
  // macro body, or if the macro comes from a system header. If the offending
  // expression is a call to a function with the warn_unused_result attribute,
  // we warn no matter the location. Because of the order in which the various
  // checks need to happen, we factor out the macro-related test here.
  bool ShouldSuppress = SourceMgr.isMacroBodyExpansion(ExprLoc) ||
                        SourceMgr.isInSystemMacro(ExprLoc);

  const Expr *WarnExpr;
  SourceLocation Loc;
  SourceRange R1, R2;
  if (!E->isUnusedResultAWarning(WarnExpr, Loc, R1, R2, Context))
    return;

  // If this is a GNU statement expression expanded from a macro, it is probably
  // unused because it is a function-like macro that can be used as either an
  // expression or statement.  Don't warn, because it is almost certainly a
  // false positive.
  if (isa<StmtExpr>(E) && Loc.isMacroID())
    return;

  // Check if this is the UNREFERENCED_PARAMETER from the Microsoft headers.
  // That macro is frequently used to suppress "unused parameter" warnings,
  // but its implementation makes -Wunused-value fire.  Prevent this.
  if (isa<ParenExpr>(E->IgnoreImpCasts()) && Loc.isMacroID()) {
    SourceLocation SpellLoc = Loc;
    if (locateMacroSpelling(SpellLoc, "UNREFERENCED_PARAMETER"))
      return;
  }

  // Okay, we have an unused result.  Depending on what the base expression is,
  // we might want to make a more specific diagnostic.  Check for one of these
  // cases now.
  if (const FullExpr *Temps = dyn_cast<FullExpr>(E))
    E = Temps->getSubExpr();
  if (diagnoseUnusedComparison(*this, E))
    return;

  E = WarnExpr;
  if (const auto *Cast = dyn_cast<CastExpr>(E))
    if (Cast->getCastKind() == CK_NoOp)
      E = Cast->getSubExpr()->IgnoreImpCasts();

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (E->getType()->isVoidType())
      return;

    if (diagnoseNoDiscard(*this,
                          cast_or_null<WarnUnusedResultAttr>(
                              CE->getUnusedResultAttr(Context)),
                          Loc, R1, R2))
      return;

    // If the callee has attribute pure, const, or warn_unused_result, warn with
    // a more specific message to make it clear what is happening. If the call
    // is written in a macro body, only warn if it has the warn_unused_result
    // attribute.
    if (const Decl *FD = CE->getCalleeDecl()) {
      if (ShouldSuppress)
        return;
      if (FD->hasAttr<PureAttr>()) {
        Diag(Loc, diag::warn_unused_call) << R1 << R2 << "pure";
        return;
      }
      if (FD->hasAttr<ConstAttr>()) {
        Diag(Loc, diag::warn_unused_call) << R1 << R2 << "const";
        return;
      }
    }
  } else if (const auto *ILE = dyn_cast<InitListExpr>(E)) {
    if (const TagDecl *TD = ILE->getType()->getAsTagDecl()) {

      if (diagnoseNoDiscard(*this, TD->getAttr<WarnUnusedResultAttr>(), Loc, R1,
                            R2))
        return;
    }
  } else if (ShouldSuppress)
    return;

  E = WarnExpr;
  // Diagnose "(void*) blah" as a typo for "(void) blah".
  if (const CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(E)) {
    TypeSourceInfo *TI = CE->getTypeInfoAsWritten();
    QualType T = TI->getType();

    // We really do want to use the non-canonical type here.
    if (T == Context.VoidPtrTy) {
      PointerTypeLoc TL = TI->getTypeLoc().castAs<PointerTypeLoc>();

      Diag(Loc, diag::warn_unused_voidptr)
          << FixItHint::CreateRemoval(TL.getStarLoc());
      return;
    }
  }

  // Tell the user to assign it into a variable to force a volatile load if this
  // isn't an array.
  if (E->isLValue() && E->getType().isVolatileQualified() &&
      !E->getType()->isArrayType()) {
    Diag(Loc, diag::warn_unused_volatile) << R1 << R2;
    return;
  }

  DiagIfReachable(Loc, S ? llvm::ArrayRef(S) : std::nullopt,
                  PDiag(DiagID) << R1 << R2);
}

void Sema::OnStartOfCompoundStmt(bool IsStmtExpr) {
  PushCompoundScope(IsStmtExpr);
}

void Sema::OnAfterCompoundStatementLeadingPragmas() {
  if (getCurFPFeatures().isFPConstrained()) {
    FunctionScopeInfo *FSI = getCurFunction();
    assert(FSI);
    FSI->setUsesFPIntrin();
  }
}

void Sema::OnFinishOfCompoundStmt() { PopCompoundScope(); }

sema::CompoundScopeInfo &Sema::getCurCompoundScope() const {
  return getCurFunction()->CompoundScopes.back();
}

StmtResult Sema::OnCompoundStmt(SourceLocation L, SourceLocation R,
                                llvm::ArrayRef<Stmt *> Elts, bool isStmtExpr) {
  const unsigned NumElts = Elts.size();

  // If we're in C mode, check that we don't have any decls after stmts.  If
  // so, emit an extension diagnostic in C89 and potentially a warning in later
  // versions.
  const unsigned MixedDeclsCodeID = getLangOpts().C99
                                        ? diag::warn_mixed_decls_code
                                        : diag::ext_mixed_decls_code;
  if (!Diags.isIgnored(MixedDeclsCodeID, L)) {
    // Note that __extension__ can be around a decl.
    unsigned i = 0;
    // Skip over all declarations.
    for (; i != NumElts && isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    // We found the end of the list or a statement.  Scan for another declstmt.
    for (; i != NumElts && !isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    if (i != NumElts) {
      Decl *D = *cast<DeclStmt>(Elts[i])->decl_begin();
      Diag(D->getLocation(), MixedDeclsCodeID);
    }
  }

  if (NumElts != 0 && getCurCompoundScope().HasEmptyLoopBodies) {
    for (unsigned i = 0; i != NumElts - 1; ++i)
      DiagnoseEmptyLoopBody(Elts[i], Elts[i + 1]);
  }

  // Calculate difference between FP options in this compound statement and in
  // the enclosing one. If this is a function body, take the difference against
  // default options. In this case the difference will indicate options that are
  // changed upon entry to the statement.
  FPOptions FPO = (getCurFunction()->CompoundScopes.size() == 1)
                      ? FPOptions(getLangOpts())
                      : getCurCompoundScope().InitialFPFeatures;
  FPOptionsOverride FPDiff = getCurFPFeatures().getChangesFrom(FPO);

  return CompoundStmt::Create(Context, Elts, FPDiff, L, R);
}

ExprResult Sema::OnCaseExpr(SourceLocation CaseLoc, ExprResult Val) {
  if (!Val.get())
    return Val;

  // If we're not inside a switch, let the 'case' statement handling diagnose
  // this. Just clean up after the expression as best we can.
  if (getCurFunction()->SwitchStack.empty())
    return OnFinishFullExpr(Val.get(), Val.get()->getExprLoc(), false, false);

  Expr *CondExpr = getCurFunction()->SwitchStack.back().getPointer()->getCond();
  if (!CondExpr)
    return ExprError();
  QualType CondType = CondExpr->getType();

  auto CheckAndFinish = [&](Expr *E) {
    ExprResult ER = E;
    ER = VerifyIntegerConstantExpression(E, AllowFold);
    if (!ER.isInvalid())
      ER = DefaultLvalueConversion(ER.get());
    if (!ER.isInvalid())
      ER = ImpCastExprToType(ER.get(), CondType, CK_IntegralCast);
    if (!ER.isInvalid())
      ER = OnFinishFullExpr(ER.get(), ER.get()->getExprLoc(), false);
    return ER;
  };

  return CheckAndFinish(Val.get());
}

StmtResult Sema::OnCaseStmt(SourceLocation CaseLoc, ExprResult LHSVal,
                            SourceLocation DotDotDotLoc, ExprResult RHSVal,
                            SourceLocation ColonLoc) {
  assert((LHSVal.isInvalid() || LHSVal.get()) && "missing LHS value");
  assert((DotDotDotLoc.isInvalid() ? RHSVal.isUnset()
                                   : RHSVal.isInvalid() || RHSVal.get()) &&
         "missing RHS value");

  if (getCurFunction()->SwitchStack.empty()) {
    Diag(CaseLoc, diag::err_case_not_in_switch);
    return StmtError();
  }

  if (LHSVal.isInvalid() || RHSVal.isInvalid()) {
    getCurFunction()->SwitchStack.back().setInt(true);
    return StmtError();
  }

  auto *CS = CaseStmt::Create(Context, LHSVal.get(), RHSVal.get(), CaseLoc,
                              DotDotDotLoc, ColonLoc);
  getCurFunction()->SwitchStack.back().getPointer()->addSwitchCase(CS);
  return CS;
}

void Sema::OnCaseStmtBody(Stmt *S, Stmt *SubStmt) {
  cast<CaseStmt>(S)->setSubStmt(SubStmt);
}

StmtResult Sema::OnDefaultStmt(SourceLocation DefaultLoc,
                               SourceLocation ColonLoc, Stmt *SubStmt,
                               Scope *CurScope) {
  if (getCurFunction()->SwitchStack.empty()) {
    Diag(DefaultLoc, diag::err_default_not_in_switch);
    return SubStmt;
  }

  DefaultStmt *DS = new (Context) DefaultStmt(DefaultLoc, ColonLoc, SubStmt);
  getCurFunction()->SwitchStack.back().getPointer()->addSwitchCase(DS);
  return DS;
}

StmtResult Sema::OnLabelStmt(SourceLocation IdentLoc, LabelDecl *TheDecl,
                             SourceLocation ColonLoc, Stmt *SubStmt) {
  // If the label was multiply defined, reject it now.
  if (TheDecl->getStmt()) {
    Diag(IdentLoc, diag::err_redefinition_of_label) << TheDecl->getDeclName();
    Diag(TheDecl->getLocation(), diag::note_previous_definition);
    return SubStmt;
  }

  ReservedIdentifierStatus Status = TheDecl->isReserved(getLangOpts());
  if (isReservedInAllContexts(Status) &&
      !Context.getSourceManager().isInSystemHeader(IdentLoc))
    Diag(IdentLoc, diag::warn_reserved_extern_symbol)
        << TheDecl << static_cast<int>(Status);

  // Otherwise, things are good.  Fill in the declaration and return it.
  LabelStmt *LS = new (Context) LabelStmt(IdentLoc, TheDecl, SubStmt);
  TheDecl->setStmt(LS);
  if (!TheDecl->isGnuLocal()) {
    TheDecl->setLocStart(IdentLoc);
    if (!TheDecl->isMSAsmLabel()) {
      // Don't update the location of MS ASM labels.  These will result in
      // a diagnostic, and changing the location here will mess that up.
      TheDecl->setLocation(IdentLoc);
    }
  }
  return LS;
}

StmtResult Sema::FormAttributedStmt(SourceLocation AttrsLoc,
                                    llvm::ArrayRef<const Attr *> Attrs,
                                    Stmt *SubStmt) {
  for (const auto *A : Attrs) {
    if (A->getKind() == attr::MustTail) {
      if (!checkAndRewriteMustTailAttr(SubStmt, *A)) {
        return SubStmt;
      }
      setFunctionHasMustTail();
    }
  }

  return AttributedStmt::Create(Context, AttrsLoc, Attrs, SubStmt);
}

StmtResult Sema::OnAttributedStmt(const ParsedAttributes &Attrs,
                                  Stmt *SubStmt) {
  llvm::SmallVector<const Attr *, 1> SemanticAttrs;
  ProcessStmtAttributes(SubStmt, Attrs, SemanticAttrs);
  if (!SemanticAttrs.empty())
    return FormAttributedStmt(Attrs.Range.getBegin(), SemanticAttrs, SubStmt);
  // If none of the attributes applied, that's fine, we can recover by
  // returning the substatement directly instead of making an AttributedStmt
  // with no attributes on it.
  return SubStmt;
}

bool Sema::checkAndRewriteMustTailAttr(Stmt *St, const Attr &MTA) {
  ReturnStmt *R = cast<ReturnStmt>(St);
  Expr *E = R->getRetValue();

  if (!checkMustTailAttr(St, MTA))
    return false;

  auto IgnoreImplicitAsWritten = [](Expr *E) -> Expr * {
    return IgnoreExprNodes(E, IgnoreImplicitAsWrittenSingleStep);
  };

  // Now that we have verified that 'musttail' is valid here, rewrite the
  // return value to remove all implicit nodes, but retain parentheses.
  R->setRetValue(IgnoreImplicitAsWritten(E));
  return true;
}

bool Sema::checkMustTailAttr(const Stmt *St, const Attr &MTA) {
  auto IgnoreParenImplicitAsWritten = [](const Expr *E) -> const Expr * {
    return IgnoreExprNodes(const_cast<Expr *>(E), IgnoreParensSingleStep,
                           IgnoreImplicitAsWrittenSingleStep);
  };

  const Expr *E = cast<ReturnStmt>(St)->getRetValue();
  const auto *CE = dyn_cast_or_null<CallExpr>(IgnoreParenImplicitAsWritten(E));

  if (!CE) {
    Diag(St->getBeginLoc(), diag::err_musttail_needs_call) << &MTA;
    return false;
  }

  if (const auto *EWC = dyn_cast<ExprWithCleanups>(E)) {
    if (EWC->cleanupsHaveSideEffects()) {
      Diag(St->getBeginLoc(), diag::err_musttail_needs_trivial_args) << &MTA;
      return false;
    }
  }

  struct FuncType {
    const FunctionProtoType *Func;
  } CallerType, CalleeType;

  const auto *CallerDecl = dyn_cast<FunctionDecl>(CurContext);

  if (!CallerDecl) {
    Diag(St->getBeginLoc(), diag::err_musttail_forbidden_from_this_context)
        << &MTA;
    return false;
  }
  CallerType.Func = CallerDecl->getType()->getAs<FunctionProtoType>();

  const Expr *CalleeExpr = CE->getCallee()->IgnoreParens();
  SourceLocation CalleeLoc = CE->getCalleeDecl()
                                 ? CE->getCalleeDecl()->getBeginLoc()
                                 : St->getBeginLoc();

  CalleeType.Func =
      CalleeExpr->getType()->getPointeeType()->getAs<FunctionProtoType>();

  // Both caller and callee must have a prototype (no K&R declarations).
  if (!CalleeType.Func || !CallerType.Func) {
    Diag(St->getBeginLoc(), diag::err_musttail_needs_prototype) << &MTA;
    if (!CalleeType.Func && CE->getDirectCallee()) {
      Diag(CE->getDirectCallee()->getBeginLoc(),
           diag::note_musttail_fix_non_prototype);
    }
    if (!CallerType.Func)
      Diag(CallerDecl->getBeginLoc(), diag::note_musttail_fix_non_prototype);
    return false;
  }

  // Caller and callee must have matching calling conventions.
  //
  // Some calling conventions are physically capable of supporting tail calls
  // even if the function types don't perfectly match. LLVM is currently too
  // strict to allow this, but if LLVM added support for this in the future, we
  // could exit early here and skip the remaining checks if the functions are
  // using such a calling convention.
  if (CallerType.Func->getCallConv() != CalleeType.Func->getCallConv()) {
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl()))
      Diag(St->getBeginLoc(), diag::err_musttail_callconv_mismatch)
          << true << ND->getDeclName();
    else
      Diag(St->getBeginLoc(), diag::err_musttail_callconv_mismatch) << false;
    Diag(CalleeLoc, diag::note_musttail_callconv_mismatch)
        << FunctionType::getNameForCallConv(CallerType.Func->getCallConv())
        << FunctionType::getNameForCallConv(CalleeType.Func->getCallConv());
    Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
    return false;
  }

  if (CalleeType.Func->isVariadic() || CallerType.Func->isVariadic()) {
    Diag(St->getBeginLoc(), diag::err_musttail_no_variadic) << &MTA;
    return false;
  }

  auto CheckTypesMatch = [this](FuncType CallerType, FuncType CalleeType,
                                PartialDiagnostic &PD) -> bool {
    enum {
      ft_parameter_arity,
      ft_parameter_mismatch,
      ft_return_type,
    };

    auto DoTypesMatch = [this, &PD](QualType A, QualType B,
                                    unsigned Select) -> bool {
      if (!Context.hasSimilarType(A, B)) {
        PD << Select << A.getUnqualifiedType() << B.getUnqualifiedType();
        return false;
      }
      return true;
    };

    if (!DoTypesMatch(CallerType.Func->getReturnType(),
                      CalleeType.Func->getReturnType(), ft_return_type))
      return false;

    if (CallerType.Func->getNumParams() != CalleeType.Func->getNumParams()) {
      PD << ft_parameter_arity << CallerType.Func->getNumParams()
         << CalleeType.Func->getNumParams();
      return false;
    }

    llvm::ArrayRef<QualType> CalleeParams = CalleeType.Func->getParamTypes();
    llvm::ArrayRef<QualType> CallerParams = CallerType.Func->getParamTypes();
    size_t N = CallerType.Func->getNumParams();
    for (size_t I = 0; I < N; I++) {
      if (!DoTypesMatch(CalleeParams[I], CallerParams[I],
                        ft_parameter_mismatch)) {
        PD << static_cast<int>(I) + 1;
        return false;
      }
    }

    return true;
  };

  PartialDiagnostic PD = PDiag(diag::note_musttail_mismatch);
  if (!CheckTypesMatch(CallerType, CalleeType, PD)) {
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl()))
      Diag(St->getBeginLoc(), diag::err_musttail_mismatch)
          << true << ND->getDeclName();
    else
      Diag(St->getBeginLoc(), diag::err_musttail_mismatch) << false;
    Diag(CalleeLoc, PD);
    Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
    return false;
  }

  return true;
}
