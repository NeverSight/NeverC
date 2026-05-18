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

namespace {
class CommaVisitor : public EvaluatedExprVisitor<CommaVisitor> {
  typedef EvaluatedExprVisitor<CommaVisitor> Inherited;
  Sema &SemaRef;

public:
  CommaVisitor(Sema &SemaRef) : Inherited(SemaRef.Context), SemaRef(SemaRef) {}
  void VisitBinaryOperator(BinaryOperator *E) {
    if (E->getOpcode() == BO_Comma)
      SemaRef.DiagnoseCommaOperator(E->getLHS(), E->getExprLoc());
    EvaluatedExprVisitor<CommaVisitor>::VisitBinaryOperator(E);
  }
};
} // namespace

// ===----------------------------------------------------------------------===
// If / switch
// ===----------------------------------------------------------------------===

StmtResult Sema::OnIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                          Stmt *InitStmt, ConditionResult Cond,
                          SourceLocation RParenLoc, Stmt *thenStmt,
                          SourceLocation ElseLoc, Stmt *elseStmt) {
  if (Cond.isInvalid())
    return StmtError();

  Expr *CondExpr = Cond.get().second;
  assert(CondExpr && "If statement: missing condition");
  // Only call the CommaVisitor when not C89 due to differences in scope flags.
  if (getLangOpts().C99 &&
      !Diags.isIgnored(diag::warn_comma_operator, CondExpr->getExprLoc()))
    CommaVisitor(*this).Visit(CondExpr);

  if (!elseStmt)
    DiagnoseEmptyStmtBody(RParenLoc, thenStmt, diag::warn_empty_if_body);

  std::tuple<bool, const Attr *, const Attr *> LHC =
      Stmt::determineLikelihoodConflict(thenStmt, elseStmt);
  if (std::get<0>(LHC)) {
    const Attr *ThenAttr = std::get<1>(LHC);
    const Attr *ElseAttr = std::get<2>(LHC);
    Diags.Report(ThenAttr->getLocation(),
                 diag::warn_attributes_likelihood_ifstmt_conflict)
        << ThenAttr << ThenAttr->getRange();
    Diags.Report(ElseAttr->getLocation(), diag::note_conflicting_attribute)
        << ElseAttr << ElseAttr->getRange();
  }

  return FormIfStmt(IfLoc, LParenLoc, InitStmt, Cond, RParenLoc, thenStmt,
                    ElseLoc, elseStmt);
}

StmtResult Sema::FormIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                            Stmt *InitStmt, ConditionResult Cond,
                            SourceLocation RParenLoc, Stmt *thenStmt,
                            SourceLocation ElseLoc, Stmt *elseStmt) {
  if (Cond.isInvalid())
    return StmtError();

  return IfStmt::Create(Context, IfLoc, InitStmt, Cond.get().first,
                        Cond.get().second, LParenLoc, RParenLoc, thenStmt,
                        ElseLoc, elseStmt);
}

namespace {
struct CaseCompareFunctor {
  bool operator()(const std::pair<llvm::APSInt, CaseStmt *> &LHS,
                  const llvm::APSInt &RHS) {
    return LHS.first < RHS;
  }
  bool operator()(const std::pair<llvm::APSInt, CaseStmt *> &LHS,
                  const std::pair<llvm::APSInt, CaseStmt *> &RHS) {
    return LHS.first < RHS.first;
  }
  bool operator()(const llvm::APSInt &LHS,
                  const std::pair<llvm::APSInt, CaseStmt *> &RHS) {
    return LHS < RHS.first;
  }
};

bool cmpCaseVals(const std::pair<llvm::APSInt, CaseStmt *> &lhs,
                 const std::pair<llvm::APSInt, CaseStmt *> &rhs) {
  if (lhs.first < rhs.first)
    return true;

  if (lhs.first == rhs.first &&
      lhs.second->getCaseLoc() < rhs.second->getCaseLoc())
    return true;
  return false;
}

bool cmpEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl *> &lhs,
                 const std::pair<llvm::APSInt, EnumConstantDecl *> &rhs) {
  return lhs.first < rhs.first;
}

bool eqEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl *> &lhs,
                const std::pair<llvm::APSInt, EnumConstantDecl *> &rhs) {
  return lhs.first == rhs.first;
}

QualType getTypeBeforeIntegralPromotion(const Expr *&E) {
  if (const auto *FE = dyn_cast<FullExpr>(E))
    E = FE->getSubExpr();
  while (const auto *ImpCast = dyn_cast<ImplicitCastExpr>(E)) {
    if (ImpCast->getCastKind() != CK_IntegralCast)
      break;
    E = ImpCast->getSubExpr();
  }
  return E->getType();
}
} // namespace

ExprResult Sema::CheckSwitchCondition(SourceLocation SwitchLoc, Expr *Cond) {
  if (Cond->hasPlaceholderType()) {
    ExprResult PH = CheckPlaceholderExpr(Cond);
    if (PH.isInvalid())
      return ExprError();
    Cond = PH.get();
  }

  ExprResult Converted = DefaultLvalueConversion(Cond);
  if (!Converted.isUsable())
    return ExprError();

  QualType T = Converted.get()->getType();
  if (!T->isIntegralOrEnumerationType()) {
    Diag(SwitchLoc, diag::err_typecheck_statement_requires_integer)
        << T << Cond->getSourceRange();
    return ExprError();
  }

  Cond = Converted.get();
  // Integer promotions on the switch controlling expression.
  return UsualUnaryConversions(Cond);
}

StmtResult Sema::OnStartOfSwitchStmt(SourceLocation SwitchLoc,
                                     SourceLocation LParenLoc, Stmt *InitStmt,
                                     ConditionResult Cond,
                                     SourceLocation RParenLoc) {
  Expr *CondExpr = Cond.get().second;
  assert((Cond.isInvalid() || CondExpr) && "switch with no condition");

  if (CondExpr) {
    // We have already converted the expression to an integral or enumeration
    // type, when we parsed the switch condition. There are cases where we don't
    // have an appropriate type, e.g. a typo-expr Cond was corrected to an
    // inappropriate-type expr, we just return an error.
    if (!CondExpr->getType()->isIntegralOrEnumerationType())
      return StmtError();
    if (CondExpr->isKnownToHaveBooleanValue()) {
      // switch(bool_expr) {...} is often a programmer error, e.g.
      //   switch(n && mask) { ... }  // Doh - should be "n & mask".
      // One can always use an if statement instead of switch(bool_expr).
      Diag(SwitchLoc, diag::warn_bool_switch_condition)
          << CondExpr->getSourceRange();
    }
  }

  setFunctionHasBranchIntoScope();

  auto *SS = SwitchStmt::Create(Context, InitStmt, Cond.get().first, CondExpr,
                                LParenLoc, RParenLoc);
  getCurFunction()->SwitchStack.push_back(
      FunctionScopeInfo::SwitchInfo(SS, false));
  return SS;
}

namespace {
void adjustAPSInt(llvm::APSInt &Val, unsigned BitWidth, bool IsSigned) {
  Val = Val.extOrTrunc(BitWidth);
  Val.setIsSigned(IsSigned);
}

void checkCaseValue(Sema &S, SourceLocation Loc, const llvm::APSInt &Val,
                    unsigned UnpromotedWidth, bool UnpromotedSign) {
  // If the case value was signed and negative and the switch expression is
  // unsigned, don't bother to warn: this is implementation-defined behavior.
  if (UnpromotedWidth < Val.getBitWidth()) {
    llvm::APSInt ConvVal(Val);
    adjustAPSInt(ConvVal, UnpromotedWidth, UnpromotedSign);
    adjustAPSInt(ConvVal, Val.getBitWidth(), Val.isSigned());
    // Use different diagnostics for overflow in conversion to promoted
    // type versus "switch expression cannot have this value". Use proper
    // IntRange checking rather than just looking at the unpromoted type here.
    if (ConvVal != Val)
      S.Diag(Loc, diag::warn_case_value_overflow)
          << toString(Val, 10) << toString(ConvVal, 10);
  }
}

typedef llvm::SmallVector<std::pair<llvm::APSInt, EnumConstantDecl *>, 64>
    EnumValsTy;

bool shouldDiagnoseSwitchCaseNotInEnum(const Sema &S, const EnumDecl *ED,
                                       const Expr *CaseExpr,
                                       EnumValsTy::iterator &EI,
                                       EnumValsTy::iterator &EIEnd,
                                       const llvm::APSInt &Val) {
  if (!ED->isClosed())
    return false;

  if (const DeclRefExpr *DRE =
          dyn_cast<DeclRefExpr>(CaseExpr->IgnoreParenImpCasts())) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      QualType VarType = VD->getType();
      QualType EnumType = S.Context.getTypeDeclType(ED);
      if (VD->hasGlobalStorage() && VarType.isConstQualified() &&
          S.Context.hasSameUnqualifiedType(EnumType, VarType))
        return false;
    }
  }

  if (ED->hasAttr<FlagEnumAttr>())
    return !S.IsValueInFlagEnum(ED, Val, false);

  while (EI != EIEnd && EI->first < Val)
    EI++;

  if (EI != EIEnd && EI->first == Val)
    return false;

  return true;
}

void checkEnumTypesInSwitchStmt(Sema &S, const Expr *Cond, const Expr *Case) {
  QualType CondType = Cond->getType();
  QualType CaseType = Case->getType();

  const EnumType *CondEnumType = CondType->getAs<EnumType>();
  const EnumType *CaseEnumType = CaseType->getAs<EnumType>();
  if (!CondEnumType || !CaseEnumType)
    return;

  // Ignore anonymous enums.
  if (!CondEnumType->getDecl()->getIdentifier() &&
      !CondEnumType->getDecl()->getTypedefNameForAnonDecl())
    return;
  if (!CaseEnumType->getDecl()->getIdentifier() &&
      !CaseEnumType->getDecl()->getTypedefNameForAnonDecl())
    return;

  if (S.Context.hasSameUnqualifiedType(CondType, CaseType))
    return;

  S.Diag(Case->getExprLoc(), diag::warn_comparison_of_mixed_enum_types_switch)
      << CondType << CaseType << Cond->getSourceRange()
      << Case->getSourceRange();
}
} // namespace

StmtResult Sema::OnFinishSwitchStmt(SourceLocation SwitchLoc, Stmt *Switch,
                                    Stmt *BodyStmt) {
  SwitchStmt *SS = cast<SwitchStmt>(Switch);
  bool CaseListIsIncomplete = getCurFunction()->SwitchStack.back().getInt();
  assert(SS == getCurFunction()->SwitchStack.back().getPointer() &&
         "switch stack missing push/pop!");

  getCurFunction()->SwitchStack.pop_back();

  if (!BodyStmt)
    return StmtError();
  SS->setBody(BodyStmt, SwitchLoc);

  Expr *CondExpr = SS->getCond();
  if (!CondExpr)
    return StmtError();

  QualType CondType = CondExpr->getType();

  // Case values are checked against the pre-promotion type of the
  // switch condition, not the promoted type.
  const Expr *CondExprBeforePromotion = CondExpr;
  QualType CondTypeBeforePromotion =
      getTypeBeforeIntegralPromotion(CondExprBeforePromotion);

  // Get the bitwidth of the switched-on value after promotions. We must
  // convert the integer case values to this width before comparison.
  unsigned CondWidth = Context.getIntWidth(CondType);
  bool CondIsSigned = CondType->isSignedIntegerOrEnumerationType();

  unsigned CondWidthBeforePromotion =
      Context.getIntWidth(CondTypeBeforePromotion);
  bool CondIsSignedBeforePromotion =
      CondTypeBeforePromotion->isSignedIntegerOrEnumerationType();

  // Accumulate all of the case values in a vector so that we can sort them
  // and detect duplicates.  This vector contains the APInt for the case after
  // it has been converted to the condition type.
  typedef llvm::SmallVector<std::pair<llvm::APSInt, CaseStmt *>, 64> CaseValsTy;
  CaseValsTy CaseVals;

  // Keep track of any GNU case ranges we see.  The APSInt is the low value.
  typedef std::vector<std::pair<llvm::APSInt, CaseStmt *>> CaseRangesTy;
  CaseRangesTy CaseRanges;

  DefaultStmt *TheDefaultStmt = nullptr;

  bool CaseListIsErroneous = false;

  for (SwitchCase *SC = SS->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase()) {

    if (DefaultStmt *DS = dyn_cast<DefaultStmt>(SC)) {
      if (TheDefaultStmt) {
        Diag(DS->getDefaultLoc(), diag::err_multiple_default_labels_defined);
        Diag(TheDefaultStmt->getDefaultLoc(), diag::note_duplicate_case_prev);

        // Removing the default statement from the switch block to return a
        // valid AST would require recursing down the AST and
        // finding it, not something we are set up to do right now.  For now,
        // just lop the entire switch stmt out of the AST.
        CaseListIsErroneous = true;
      }
      TheDefaultStmt = DS;

    } else {
      CaseStmt *CS = cast<CaseStmt>(SC);

      Expr *Lo = CS->getLHS();

      // We already verified that the expression has a constant value;
      // get that value (prior to conversions).
      const Expr *LoBeforePromotion = Lo;
      getTypeBeforeIntegralPromotion(LoBeforePromotion);
      llvm::APSInt LoVal = LoBeforePromotion->EvaluateKnownConstInt(Context);

      checkCaseValue(*this, Lo->getBeginLoc(), LoVal, CondWidthBeforePromotion,
                     CondIsSignedBeforePromotion);
      checkEnumTypesInSwitchStmt(*this, CondExprBeforePromotion,
                                 LoBeforePromotion);

      adjustAPSInt(LoVal, CondWidth, CondIsSigned);

      // If this is a case range, remember it in CaseRanges, otherwise CaseVals.
      if (CS->getRHS()) {
        CaseRanges.push_back(std::make_pair(LoVal, CS));
      } else
        CaseVals.push_back(std::make_pair(LoVal, CS));
    }
  }

  {
    // If we don't have a default statement, check whether the
    // condition is constant.
    llvm::APSInt ConstantCondValue;
    bool HasConstantCond = false;
    if (!TheDefaultStmt) {
      Expr::EvalResult Result;
      HasConstantCond =
          CondExpr->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects);
      if (Result.Val.isInt())
        ConstantCondValue = Result.Val.getInt();
      assert(!HasConstantCond ||
             (ConstantCondValue.getBitWidth() == CondWidth &&
              ConstantCondValue.isSigned() == CondIsSigned));
      Diag(SwitchLoc, diag::warn_switch_default);
    }
    bool ShouldCheckConstantCond = HasConstantCond;

    // Sort all the scalar case values so we can easily detect duplicates.
    llvm::stable_sort(CaseVals, cmpCaseVals);

    if (!CaseVals.empty()) {
      for (unsigned i = 0, e = CaseVals.size(); i != e; ++i) {
        if (ShouldCheckConstantCond && CaseVals[i].first == ConstantCondValue)
          ShouldCheckConstantCond = false;

        if (i != 0 && CaseVals[i].first == CaseVals[i - 1].first) {
          // If we have a duplicate, report it.
          // First, determine if either case value has a name
          llvm::StringRef PrevString, CurrString;
          Expr *PrevCase = CaseVals[i - 1].second->getLHS()->IgnoreParenCasts();
          Expr *CurrCase = CaseVals[i].second->getLHS()->IgnoreParenCasts();
          if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(PrevCase)) {
            PrevString = DeclRef->getDecl()->getName();
          }
          if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(CurrCase)) {
            CurrString = DeclRef->getDecl()->getName();
          }
          llvm::SmallString<16> CaseValStr;
          CaseVals[i - 1].first.toString(CaseValStr);

          if (PrevString == CurrString)
            Diag(CaseVals[i].second->getLHS()->getBeginLoc(),
                 diag::err_duplicate_case)
                << (PrevString.empty() ? llvm::StringRef(CaseValStr)
                                       : PrevString);
          else
            Diag(CaseVals[i].second->getLHS()->getBeginLoc(),
                 diag::err_duplicate_case_differing_expr)
                << (PrevString.empty() ? llvm::StringRef(CaseValStr)
                                       : PrevString)
                << (CurrString.empty() ? llvm::StringRef(CaseValStr)
                                       : CurrString)
                << CaseValStr;

          Diag(CaseVals[i - 1].second->getLHS()->getBeginLoc(),
               diag::note_duplicate_case_prev);
          CaseListIsErroneous = true;
        }
      }
    }

    // Detect duplicate case ranges, which usually don't exist at all in
    // the first place.
    if (!CaseRanges.empty()) {
      // Sort all the case ranges by their low value so we can easily detect
      // overlaps between ranges.
      llvm::stable_sort(CaseRanges);

      // Scan the ranges, computing the high values and removing empty ranges.
      std::vector<llvm::APSInt> HiVals;
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &LoVal = CaseRanges[i].first;
        CaseStmt *CR = CaseRanges[i].second;
        Expr *Hi = CR->getRHS();

        const Expr *HiBeforePromotion = Hi;
        getTypeBeforeIntegralPromotion(HiBeforePromotion);
        llvm::APSInt HiVal = HiBeforePromotion->EvaluateKnownConstInt(Context);

        checkCaseValue(*this, Hi->getBeginLoc(), HiVal,
                       CondWidthBeforePromotion, CondIsSignedBeforePromotion);

        adjustAPSInt(HiVal, CondWidth, CondIsSigned);

        // If the low value is bigger than the high value, the case is empty.
        if (LoVal > HiVal) {
          Diag(CR->getLHS()->getBeginLoc(), diag::warn_case_empty_range)
              << SourceRange(CR->getLHS()->getBeginLoc(), Hi->getEndLoc());
          CaseRanges.erase(CaseRanges.begin() + i);
          --i;
          --e;
          continue;
        }

        if (ShouldCheckConstantCond && LoVal <= ConstantCondValue &&
            ConstantCondValue <= HiVal)
          ShouldCheckConstantCond = false;

        HiVals.push_back(HiVal);
      }

      // Rescan the ranges, looking for overlap with singleton values and other
      // ranges.  Since the range list is sorted, we only need to compare case
      // ranges with their neighbors.
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &CRLo = CaseRanges[i].first;
        llvm::APSInt &CRHi = HiVals[i];
        CaseStmt *CR = CaseRanges[i].second;

        CaseStmt *OverlapStmt = nullptr;
        llvm::APSInt OverlapVal(32);

        // Find the smallest value >= the lower bound.  If I is in the
        // case range, then we have overlap.
        CaseValsTy::iterator I =
            llvm::lower_bound(CaseVals, CRLo, CaseCompareFunctor());
        if (I != CaseVals.end() && I->first < CRHi) {
          OverlapVal = I->first; // Found overlap with scalar.
          OverlapStmt = I->second;
        }

        // Find the smallest value bigger than the upper bound.
        I = std::upper_bound(I, CaseVals.end(), CRHi, CaseCompareFunctor());
        if (I != CaseVals.begin() && (I - 1)->first >= CRLo) {
          OverlapVal = (I - 1)->first; // Found overlap with scalar.
          OverlapStmt = (I - 1)->second;
        }

        if (i && CRLo <= HiVals[i - 1]) {
          OverlapVal = HiVals[i - 1]; // Found overlap with range.
          OverlapStmt = CaseRanges[i - 1].second;
        }

        if (OverlapStmt) {
          // If we have a duplicate, report it.
          Diag(CR->getLHS()->getBeginLoc(), diag::err_duplicate_case)
              << toString(OverlapVal, 10);
          Diag(OverlapStmt->getLHS()->getBeginLoc(),
               diag::note_duplicate_case_prev);
          CaseListIsErroneous = true;
        }
      }
    }

    // Complain if we have a constant condition and we didn't find a match.
    if (!CaseListIsErroneous && !CaseListIsIncomplete &&
        ShouldCheckConstantCond) {
      Diag(CondExpr->getExprLoc(), diag::warn_missing_case_for_condition)
          << toString(ConstantCondValue, 10) << CondExpr->getSourceRange();
    }

    // Preserve enum coverage info in the AST even when 'default:' is present.
    const EnumType *ET = CondTypeBeforePromotion->getAs<EnumType>();
    if (!CaseListIsErroneous && !CaseListIsIncomplete && !HasConstantCond &&
        ET && ET->getDecl()->isCompleteDefinition() &&
        !ET->getDecl()->enumerators().empty()) {
      const EnumDecl *ED = ET->getDecl();
      EnumValsTy EnumVals;

      // Gather all enum values, set their type and sort them,
      // allowing easier comparison with CaseVals.
      for (auto *EDI : ED->enumerators()) {
        llvm::APSInt Val = EDI->getInitVal();
        adjustAPSInt(Val, CondWidth, CondIsSigned);
        EnumVals.push_back(std::make_pair(Val, EDI));
      }
      llvm::stable_sort(EnumVals, cmpEnumVals);
      auto EI = EnumVals.begin(),
           EIEnd = std::unique(EnumVals.begin(), EnumVals.end(), eqEnumVals);

      // See which case values aren't in enum.
      for (CaseValsTy::const_iterator CI = CaseVals.begin();
           CI != CaseVals.end(); CI++) {
        Expr *CaseExpr = CI->second->getLHS();
        if (shouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              CI->first))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
              << CondTypeBeforePromotion;
      }

      // See which of case ranges aren't in enum
      EI = EnumVals.begin();
      for (CaseRangesTy::const_iterator RI = CaseRanges.begin();
           RI != CaseRanges.end(); RI++) {
        Expr *CaseExpr = RI->second->getLHS();
        if (shouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              RI->first))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
              << CondTypeBeforePromotion;

        llvm::APSInt Hi = RI->second->getRHS()->EvaluateKnownConstInt(Context);
        adjustAPSInt(Hi, CondWidth, CondIsSigned);

        CaseExpr = RI->second->getRHS();
        if (shouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              Hi))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
              << CondTypeBeforePromotion;
      }

      auto CI = CaseVals.begin();
      auto RI = CaseRanges.begin();
      bool hasCasesNotInSwitch = false;

      llvm::SmallVector<DeclarationName, 8> UnhandledNames;

      for (EI = EnumVals.begin(); EI != EIEnd; EI++) {
        // Don't warn about omitted unavailable EnumConstantDecls.
        switch (EI->second->getAvailability()) {
        case AR_Deprecated:
          // Omitting a deprecated constant is ok; it should never materialize.
        case AR_Unavailable:
          continue;

        case AR_NotYetIntroduced:
          // Partially available enum constants should be present. Note that we
          // suppress -Wunguarded-availability diagnostics for such uses.
        case AR_Available:
          break;
        }

        if (EI->second->hasAttr<UnusedAttr>())
          continue;

        // Drop unneeded case values
        while (CI != CaseVals.end() && CI->first < EI->first)
          CI++;

        if (CI != CaseVals.end() && CI->first == EI->first)
          continue;

        // Drop unneeded case ranges
        for (; RI != CaseRanges.end(); RI++) {
          llvm::APSInt Hi =
              RI->second->getRHS()->EvaluateKnownConstInt(Context);
          adjustAPSInt(Hi, CondWidth, CondIsSigned);
          if (EI->first <= Hi)
            break;
        }

        if (RI == CaseRanges.end() || EI->first < RI->first) {
          hasCasesNotInSwitch = true;
          UnhandledNames.push_back(EI->second->getDeclName());
        }
      }

      if (TheDefaultStmt && UnhandledNames.empty() && ED->isClosedNonFlag())
        Diag(TheDefaultStmt->getDefaultLoc(), diag::warn_unreachable_default);

      // Produce a nice diagnostic if multiple values aren't handled.
      if (!UnhandledNames.empty()) {
        auto DB = Diag(CondExpr->getExprLoc(), TheDefaultStmt
                                                   ? diag::warn_def_missing_case
                                                   : diag::warn_missing_case)
                  << CondExpr->getSourceRange() << (int)UnhandledNames.size();

        for (size_t I = 0, E = std::min(UnhandledNames.size(), (size_t)3);
             I != E; ++I)
          DB << UnhandledNames[I];
      }

      if (!hasCasesNotInSwitch)
        SS->setAllEnumCasesCovered();
    }
  }

  if (BodyStmt)
    DiagnoseEmptyStmtBody(CondExpr->getEndLoc(), BodyStmt,
                          diag::warn_empty_switch_body);

  // If the case list was broken, just return the whole substmt as broken.
  if (CaseListIsErroneous)
    return StmtError();

  return SS;
}

void Sema::DiagnoseAssignmentEnum(QualType DstType, QualType SrcType,
                                  Expr *SrcExpr) {
  if (Diags.isIgnored(diag::warn_not_in_enum_assignment, SrcExpr->getExprLoc()))
    return;

  if (const EnumType *ET = DstType->getAs<EnumType>())
    if (!Context.hasSameUnqualifiedType(SrcType, DstType) &&
        SrcType->isIntegerType()) {
      if (SrcExpr->isIntegerConstantExpr(Context)) {
        // Get the bitwidth of the enum value before promotions.
        unsigned DstWidth = Context.getIntWidth(DstType);
        bool DstIsSigned = DstType->isSignedIntegerOrEnumerationType();

        llvm::APSInt RhsVal = SrcExpr->EvaluateKnownConstInt(Context);
        adjustAPSInt(RhsVal, DstWidth, DstIsSigned);
        const EnumDecl *ED = ET->getDecl();

        if (!ED->isClosed())
          return;

        if (ED->hasAttr<FlagEnumAttr>()) {
          if (!IsValueInFlagEnum(ED, RhsVal, true))
            Diag(SrcExpr->getExprLoc(), diag::warn_not_in_enum_assignment)
                << DstType.getUnqualifiedType();
        } else {
          typedef llvm::SmallVector<std::pair<llvm::APSInt, EnumConstantDecl *>,
                                    64>
              EnumValsTy;
          EnumValsTy EnumVals;

          // Gather all enum values, set their type and sort them,
          // allowing easier comparison with rhs constant.
          for (auto *EDI : ED->enumerators()) {
            llvm::APSInt Val = EDI->getInitVal();
            adjustAPSInt(Val, DstWidth, DstIsSigned);
            EnumVals.push_back(std::make_pair(Val, EDI));
          }
          if (EnumVals.empty())
            return;
          llvm::stable_sort(EnumVals, cmpEnumVals);
          EnumValsTy::iterator EIend =
              std::unique(EnumVals.begin(), EnumVals.end(), eqEnumVals);

          // See which values aren't in the enum.
          EnumValsTy::const_iterator EI = EnumVals.begin();
          while (EI != EIend && EI->first < RhsVal)
            EI++;
          if (EI == EIend || EI->first != RhsVal) {
            Diag(SrcExpr->getExprLoc(), diag::warn_not_in_enum_assignment)
                << DstType.getUnqualifiedType();
          }
        }
      }
    }
}

StmtResult Sema::OnWhileStmt(SourceLocation WhileLoc, SourceLocation LParenLoc,
                             ConditionResult Cond, SourceLocation RParenLoc,
                             Stmt *Body) {
  if (Cond.isInvalid())
    return StmtError();

  auto CondVal = Cond.get();
  CheckBreakContinueBinding(CondVal.second);

  if (CondVal.second &&
      !Diags.isIgnored(diag::warn_comma_operator, CondVal.second->getExprLoc()))
    CommaVisitor(*this).Visit(CondVal.second);

  if (isa<NullStmt>(Body))
    getCurCompoundScope().setHasEmptyLoopBodies();

  return WhileStmt::Create(Context, CondVal.first, CondVal.second, Body,
                           WhileLoc, LParenLoc, RParenLoc);
}

// ===----------------------------------------------------------------------===
// Loops & iteration
// ===----------------------------------------------------------------------===

StmtResult Sema::OnDoStmt(SourceLocation DoLoc, Stmt *Body,
                          SourceLocation WhileLoc, SourceLocation CondLParen,
                          Expr *Cond, SourceLocation CondRParen) {
  assert(Cond && "OnDoStmt(): missing expression");

  CheckBreakContinueBinding(Cond);
  ExprResult CondResult = CheckBooleanCondition(DoLoc, Cond);
  if (CondResult.isInvalid())
    return StmtError();
  Cond = CondResult.get();

  CondResult = OnFinishFullExpr(Cond, DoLoc, /*DiscardedValue*/ false);
  if (CondResult.isInvalid())
    return StmtError();
  Cond = CondResult.get();

  // Only call the CommaVisitor for C89 due to differences in scope flags.
  if (Cond && !getLangOpts().C99 &&
      !Diags.isIgnored(diag::warn_comma_operator, Cond->getExprLoc()))
    CommaVisitor(*this).Visit(Cond);

  return new (Context) DoStmt(Body, Cond, DoLoc, WhileLoc, CondRParen);
}

namespace {
// Use SetVector since the diagnostic cares about the ordering of the Decl's.
using DeclSetVector = llvm::SmallSetVector<VarDecl *, 8>;

// This visitor will traverse a conditional statement and store all
// the evaluated decls into a vector.  Simple is set to true if none
// of the excluded constructs are used.
class DeclExtractor : public EvaluatedExprVisitor<DeclExtractor> {
  DeclSetVector &Decls;
  llvm::SmallVectorImpl<SourceRange> &Ranges;
  bool Simple;

public:
  typedef EvaluatedExprVisitor<DeclExtractor> Inherited;

  DeclExtractor(Sema &S, DeclSetVector &Decls,
                llvm::SmallVectorImpl<SourceRange> &Ranges)
      : Inherited(S.Context), Decls(Decls), Ranges(Ranges), Simple(true) {}

  bool isSimple() { return Simple; }

  // Replaces the method in EvaluatedExprVisitor.
  void VisitMemberExpr(MemberExpr *E) { Simple = false; }

  // Any Stmt not explicitly listed will cause the condition to be marked
  // complex.
  void VisitStmt(Stmt *S) { Simple = false; }

  void VisitBinaryOperator(BinaryOperator *E) {
    Visit(E->getLHS());
    Visit(E->getRHS());
  }

  void VisitCastExpr(CastExpr *E) { Visit(E->getSubExpr()); }

  void VisitUnaryOperator(UnaryOperator *E) {
    // Skip checking conditionals with derefernces.
    if (E->getOpcode() == UO_Deref)
      Simple = false;
    else
      Visit(E->getSubExpr());
  }

  void VisitConditionalOperator(ConditionalOperator *E) {
    Visit(E->getCond());
    Visit(E->getTrueExpr());
    Visit(E->getFalseExpr());
  }

  void VisitParenExpr(ParenExpr *E) { Visit(E->getSubExpr()); }

  void VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
    Visit(E->getOpaqueValue()->getSourceExpr());
    Visit(E->getFalseExpr());
  }

  void VisitIntegerLiteral(IntegerLiteral *E) {}
  void VisitFloatingLiteral(FloatingLiteral *E) {}
  void VisitCharacterLiteral(CharacterLiteral *E) {}
  void VisitImaginaryLiteral(ImaginaryLiteral *E) {}

  void VisitDeclRefExpr(DeclRefExpr *E) {
    VarDecl *VD = dyn_cast<VarDecl>(E->getDecl());
    if (!VD) {
      // Don't allow unhandled Decl types.
      Simple = false;
      return;
    }

    Ranges.push_back(E->getSourceRange());

    Decls.insert(VD);
  }

}; // end class DeclExtractor

// DeclMatcher checks to see if the decls are used in a non-evaluated
// context.
class DeclMatcher : public EvaluatedExprVisitor<DeclMatcher> {
  DeclSetVector &Decls;
  bool FoundDecl;

public:
  typedef EvaluatedExprVisitor<DeclMatcher> Inherited;

  DeclMatcher(Sema &S, DeclSetVector &Decls, Stmt *Statement)
      : Inherited(S.Context), Decls(Decls), FoundDecl(false) {
    if (!Statement)
      return;

    Visit(Statement);
  }

  void VisitReturnStmt(ReturnStmt *S) { FoundDecl = true; }

  void VisitBreakStmt(BreakStmt *S) { FoundDecl = true; }

  void VisitGotoStmt(GotoStmt *S) { FoundDecl = true; }

  void VisitCastExpr(CastExpr *E) {
    if (E->getCastKind() == CK_LValueToRValue)
      CheckLValueToRValueCast(E->getSubExpr());
    else
      Visit(E->getSubExpr());
  }

  void CheckLValueToRValueCast(Expr *E) {
    E = E->IgnoreParenImpCasts();

    if (isa<DeclRefExpr>(E)) {
      return;
    }

    if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
      Visit(CO->getCond());
      CheckLValueToRValueCast(CO->getTrueExpr());
      CheckLValueToRValueCast(CO->getFalseExpr());
      return;
    }

    if (BinaryConditionalOperator *BCO =
            dyn_cast<BinaryConditionalOperator>(E)) {
      CheckLValueToRValueCast(BCO->getOpaqueValue()->getSourceExpr());
      CheckLValueToRValueCast(BCO->getFalseExpr());
      return;
    }

    Visit(E);
  }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    if (VarDecl *VD = dyn_cast<VarDecl>(E->getDecl()))
      if (Decls.contains(VD))
        FoundDecl = true;
  }

  void VisitPseudoObjectExpr(PseudoObjectExpr *POE) {
    // Only need to visit the semantics for POE.
    // SyntaticForm doesn't really use the Decal.
    for (auto *S : POE->semantics()) {
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(S))
        // Look past the OVE into the expression it binds.
        Visit(OVE->getSourceExpr());
      else
        Visit(S);
    }
  }

  bool FoundDeclInUse() { return FoundDecl; }

}; // end class DeclMatcher

void checkForLoopConditionalStatement(Sema &S, Expr *Second, Expr *Third,
                                      Stmt *Body) {
  // Condition is empty
  if (!Second)
    return;

  if (S.Diags.isIgnored(diag::warn_variables_not_in_loop_body,
                        Second->getBeginLoc()))
    return;

  PartialDiagnostic PDiag = S.PDiag(diag::warn_variables_not_in_loop_body);
  DeclSetVector Decls;
  llvm::SmallVector<SourceRange, 10> Ranges;
  DeclExtractor DE(S, Decls, Ranges);
  DE.Visit(Second);

  // Don't analyze complex conditionals.
  if (!DE.isSimple())
    return;

  // No decls found.
  if (Decls.size() == 0)
    return;

  // Don't warn on volatile, static, or global variables.
  for (auto *VD : Decls)
    if (VD->getType().isVolatileQualified() || VD->hasGlobalStorage())
      return;

  if (DeclMatcher(S, Decls, Second).FoundDeclInUse() ||
      DeclMatcher(S, Decls, Third).FoundDeclInUse() ||
      DeclMatcher(S, Decls, Body).FoundDeclInUse())
    return;

  // Load decl names into diagnostic.
  if (Decls.size() > 4) {
    PDiag << 0;
  } else {
    PDiag << (unsigned)Decls.size();
    for (auto *VD : Decls)
      PDiag << VD->getDeclName();
  }

  for (auto Range : Ranges)
    PDiag << Range;

  S.Diag(Ranges.begin()->getBegin(), PDiag);
}

// If Statement is an incemement or decrement, return true and sets the
// variables Increment and DRE.
bool processIterationStmt(Sema &S, Stmt *Statement, bool &Increment,
                          DeclRefExpr *&DRE) {
  if (auto Cleanups = dyn_cast<ExprWithCleanups>(Statement))
    if (!Cleanups->cleanupsHaveSideEffects())
      Statement = Cleanups->getSubExpr();

  if (UnaryOperator *UO = dyn_cast<UnaryOperator>(Statement)) {
    switch (UO->getOpcode()) {
    default:
      return false;
    case UO_PostInc:
    case UO_PreInc:
      Increment = true;
      break;
    case UO_PostDec:
    case UO_PreDec:
      Increment = false;
      break;
    }
    DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr());
    return DRE;
  }

  return false;
}

// A visitor to determine if a continue or break statement is a
// subexpression.
class BreakContinueFinder
    : public ConstEvaluatedExprVisitor<BreakContinueFinder> {
  SourceLocation BreakLoc;
  SourceLocation ContinueLoc;
  bool InSwitch = false;

public:
  BreakContinueFinder(Sema &S, const Stmt *Body) : Inherited(S.Context) {
    Visit(Body);
  }

  typedef ConstEvaluatedExprVisitor<BreakContinueFinder> Inherited;

  void VisitContinueStmt(const ContinueStmt *E) {
    ContinueLoc = E->getContinueLoc();
  }

  void VisitBreakStmt(const BreakStmt *E) {
    if (!InSwitch)
      BreakLoc = E->getBreakLoc();
  }

  void VisitSwitchStmt(const SwitchStmt *S) {
    if (const Stmt *Init = S->getInit())
      Visit(Init);
    if (const Stmt *CondVar = S->getConditionVariableDeclStmt())
      Visit(CondVar);
    if (const Stmt *Cond = S->getCond())
      Visit(Cond);

    // Don't return break statements from the body of a switch.
    InSwitch = true;
    if (const Stmt *Body = S->getBody())
      Visit(Body);
    InSwitch = false;
  }

  void VisitForStmt(const ForStmt *S) {
    // Only visit the init statement of a for loop; the body
    // has a different break/continue scope.
    if (const Stmt *Init = S->getInit())
      Visit(Init);
  }

  void VisitWhileStmt(const WhileStmt *) {
    // Do nothing; the children of a while loop have a different
    // break/continue scope.
  }

  void VisitDoStmt(const DoStmt *) {
    // Do nothing; the children of a while loop have a different
    // break/continue scope.
  }

  bool ContinueFound() { return ContinueLoc.isValid(); }
  bool BreakFound() { return BreakLoc.isValid(); }
  SourceLocation GetContinueLoc() { return ContinueLoc; }
  SourceLocation GetBreakLoc() { return BreakLoc; }

}; // end class BreakContinueFinder

// Emit a warning when a loop increment/decrement appears twice per loop
// iteration.  The conditions which trigger this warning are:
// 1) The last statement in the loop body and the third expression in the
//    for loop are both increment or both decrement of the same variable
// 2) No continue statements in the loop body.
void checkForRedundantIteration(Sema &S, Expr *Third, Stmt *Body) {
  if (!Body || !Third)
    return;

  if (S.Diags.isIgnored(diag::warn_redundant_loop_iteration,
                        Third->getBeginLoc()))
    return;

  CompoundStmt *CS = dyn_cast<CompoundStmt>(Body);
  if (!CS || CS->body_empty())
    return;
  Stmt *LastStmt = CS->body_back();
  if (!LastStmt)
    return;

  bool LoopIncrement, LastIncrement;
  DeclRefExpr *LoopDRE, *LastDRE;

  if (!processIterationStmt(S, Third, LoopIncrement, LoopDRE))
    return;
  if (!processIterationStmt(S, LastStmt, LastIncrement, LastDRE))
    return;

  if (LoopIncrement != LastIncrement ||
      LoopDRE->getDecl() != LastDRE->getDecl())
    return;

  if (BreakContinueFinder(S, Body).ContinueFound())
    return;

  S.Diag(LastDRE->getLocation(), diag::warn_redundant_loop_iteration)
      << LastDRE->getDecl() << LastIncrement;
  S.Diag(LoopDRE->getLocation(), diag::note_loop_iteration_here)
      << LoopIncrement;
}

} // end namespace

void Sema::CheckBreakContinueBinding(Expr *E) {
  if (!E)
    return;
  if (Diags.isIgnored(diag::warn_break_binds_to_switch, E->getBeginLoc()) &&
      Diags.isIgnored(diag::warn_loop_ctrl_binds_to_inner, E->getBeginLoc()))
    return;
  BreakContinueFinder BCFinder(*this, E);
  Scope *BreakParent = CurScope->getBreakParent();
  if (BCFinder.BreakFound() && BreakParent) {
    if (BreakParent->getFlags() & Scope::SwitchScope) {
      Diag(BCFinder.GetBreakLoc(), diag::warn_break_binds_to_switch);
    } else {
      Diag(BCFinder.GetBreakLoc(), diag::warn_loop_ctrl_binds_to_inner)
          << tok::getKeywordSpelling(tok::kw_break);
    }
  } else if (BCFinder.ContinueFound() && CurScope->getContinueParent()) {
    Diag(BCFinder.GetContinueLoc(), diag::warn_loop_ctrl_binds_to_inner)
        << tok::getKeywordSpelling(tok::kw_continue);
  }
}

StmtResult Sema::OnForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                           Stmt *First, ConditionResult Second,
                           FullExprArg third, SourceLocation RParenLoc,
                           Stmt *Body) {
  if (Second.isInvalid())
    return StmtError();

  if (DeclStmt *DS = dyn_cast_or_null<DeclStmt>(First)) {
    // for-loop init declarations must have auto/register storage class.
    const Decl *NonVarSeen = nullptr;
    bool VarDeclSeen = false;
    for (auto *DI : DS->decls()) {
      if (VarDecl *VD = dyn_cast<VarDecl>(DI)) {
        VarDeclSeen = true;
        if (VD->isLocalVarDecl() && !VD->hasLocalStorage()) {
          Diag(DI->getLocation(), diag::err_non_local_variable_decl_in_for);
          DI->setInvalidDecl();
        }
      } else if (!NonVarSeen) {
        // Keep track of the first non-variable declaration we saw so that
        // we can diagnose if we don't see any variable declarations. This
        // covers a case like declaring a typedef, function, or structure
        // type rather than a variable.
        NonVarSeen = DI;
      }
    }
    // Diagnose if we saw a non-variable declaration but no variable
    // declarations.
    if (NonVarSeen && !VarDeclSeen)
      Diag(NonVarSeen->getLocation(), diag::err_non_variable_decl_in_for);
  }

  CheckBreakContinueBinding(Second.get().second);
  CheckBreakContinueBinding(third.get());

  if (!Second.get().first)
    checkForLoopConditionalStatement(*this, Second.get().second, third.get(),
                                     Body);
  checkForRedundantIteration(*this, third.get(), Body);

  if (Second.get().second &&
      !Diags.isIgnored(diag::warn_comma_operator,
                       Second.get().second->getExprLoc()))
    CommaVisitor(*this).Visit(Second.get().second);

  Expr *Third = third.release().getAs<Expr>();
  if (isa<NullStmt>(Body))
    getCurCompoundScope().setHasEmptyLoopBodies();

  return new (Context)
      ForStmt(Context, First, Second.get().second, Second.get().first, Third,
              Body, ForLoc, LParenLoc, RParenLoc);
}

// ===----------------------------------------------------------------------===
// Jumps & return
// ===----------------------------------------------------------------------===

StmtResult Sema::OnGotoStmt(SourceLocation GotoLoc, SourceLocation LabelLoc,
                            LabelDecl *TheDecl) {
  setFunctionHasBranchIntoScope();
  TheDecl->markUsed(Context);
  return new (Context) GotoStmt(TheDecl, GotoLoc, LabelLoc);
}

StmtResult Sema::OnIndirectGotoStmt(SourceLocation GotoLoc,
                                    SourceLocation StarLoc, Expr *E) {
  {
    QualType ETy = E->getType();
    QualType DestTy = Context.getPointerType(Context.VoidTy.withConst());
    ExprResult ExprRes = E;
    AssignConvertType ConvTy =
        CheckSingleAssignmentConstraints(DestTy, ExprRes);
    if (ExprRes.isInvalid())
      return StmtError();
    E = ExprRes.get();
    if (DiagnoseAssignmentResult(ConvTy, StarLoc, DestTy, ETy, E, AA_Passing))
      return StmtError();
  }

  ExprResult ExprRes = OnFinishFullExpr(E, /*DiscardedValue*/ false);
  if (ExprRes.isInvalid())
    return StmtError();
  E = ExprRes.get();

  setFunctionHasIndirectGoto();

  return new (Context) IndirectGotoStmt(GotoLoc, StarLoc, E);
}

namespace {
void checkJumpOutOfSEHFinally(Sema &S, SourceLocation Loc,
                              const Scope &DestScope) {
  if (!S.CurrentSEHFinally.empty() &&
      DestScope.Contains(*S.CurrentSEHFinally.back())) {
    S.Diag(Loc, diag::warn_jump_out_of_seh_finally);
  }
}
} // namespace

StmtResult Sema::OnContinueStmt(SourceLocation ContinueLoc, Scope *CurScope) {
  Scope *S = CurScope->getContinueParent();
  if (!S) {
    // continue must appear within a loop body.
    return StmtError(Diag(ContinueLoc, diag::err_continue_not_in_loop));
  }
  checkJumpOutOfSEHFinally(*this, ContinueLoc, *S);

  return new (Context) ContinueStmt(ContinueLoc);
}

StmtResult Sema::OnBreakStmt(SourceLocation BreakLoc, Scope *CurScope) {
  Scope *S = CurScope->getBreakParent();
  if (!S) {
    // break must appear within a loop or switch body.
    return StmtError(Diag(BreakLoc, diag::err_break_not_in_loop_or_switch));
  }
  checkJumpOutOfSEHFinally(*this, BreakLoc, *S);

  return new (Context) BreakStmt(BreakLoc);
}

Sema::NamedReturnInfo Sema::getNamedReturnInfo(Expr *&E) {
  if (!E)
    return NamedReturnInfo();
  // - in a return statement in a function [where] ...
  // ... the expression is the name of a non-volatile automatic object ...
  const auto *DR = dyn_cast<DeclRefExpr>(E->IgnoreParens());
  if (!DR)
    return NamedReturnInfo();
  const auto *VD = dyn_cast<VarDecl>(DR->getDecl());
  if (!VD)
    return NamedReturnInfo();
  return getNamedReturnInfo(VD);
}

Sema::NamedReturnInfo Sema::getNamedReturnInfo(const VarDecl *VD) {
  NamedReturnInfo Info{VD, NamedReturnInfo::MoveEligibleAndCopyElidable};

  // Parameters are not NRVO candidates in the same way as locals.
  if (VD->getKind() == Decl::ParmVar)
    Info.S = NamedReturnInfo::MoveEligible;
  else if (VD->getKind() != Decl::Var)
    return NamedReturnInfo();

  // ...automatic...
  if (!VD->hasLocalStorage())
    return NamedReturnInfo();

  QualType VDType = VD->getType();
  if (VDType->isObjectType()) {
    if (VDType.isVolatileQualified())
      return NamedReturnInfo();
  } else {
    return NamedReturnInfo();
  }

  // Variables with higher required alignment than their type's ABI
  // alignment cannot use NRVO.
  if (!VD->hasDependentAlignment() &&
      Context.getDeclAlign(VD) > Context.getTypeAlignInChars(VDType))
    Info.S = NamedReturnInfo::MoveEligible;

  return Info;
}

const VarDecl *Sema::getCopyElisionCandidate(NamedReturnInfo &Info,
                                             QualType ReturnType) {
  if (!Info.Candidate)
    return nullptr;

  auto invalidNRVO = [&] {
    Info = NamedReturnInfo();
    return nullptr;
  };

  // Return-type NRVO: only for struct/union (record) return types.
  if (!ReturnType->isRecordType())
    return invalidNRVO();

  {
    QualType VDType = Info.Candidate->getType();
    // ... the same cv-unqualified type as the function return type ...
    // When considering moving this expression out, allow dissimilar types.
    if (!Context.hasSameUnqualifiedType(ReturnType, VDType))
      Info.S = NamedReturnInfo::MoveEligible;
  }
  return Info.isCopyElidable() ? Info.Candidate : nullptr;
}

StmtResult Sema::OnReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                              Scope *CurScope) {
  StmtResult R = FormReturnStmt(ReturnLoc, RetValExp, /*AllowRecovery=*/true);
  if (R.isInvalid())
    return R;

  VarDecl *VD =
      const_cast<VarDecl *>(cast<ReturnStmt>(R.get())->getNRVOCandidate());

  CurScope->updateNRVOCandidate(VD);

  if (Scope *FP = CurScope->getFnParent())
    checkJumpOutOfSEHFinally(*this, ReturnLoc, *FP);

  return R;
}

StmtResult Sema::FormReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                                bool AllowRecovery) {
  NamedReturnInfo NRInfo = getNamedReturnInfo(RetValExp);

  QualType FnRetType;
  const AttrVec *Attrs = nullptr;

  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    FnRetType = FD->getReturnType();
    if (FD->hasAttrs())
      Attrs = &FD->getAttrs();
    if (FD->isNoReturn())
      Diag(ReturnLoc, diag::warn_noreturn_function_has_return_expr) << FD;
  } else // If we don't have a function context, bail.
    return StmtError();

  const VarDecl *NRVOCandidate = getCopyElisionCandidate(NRInfo, FnRetType);

  ReturnStmt *Result = nullptr;
  if (FnRetType->isVoidType()) {
    if (RetValExp) {
      if (auto *ILE = dyn_cast<InitListExpr>(RetValExp)) {
        FunctionDecl *CurDecl = getCurFunctionDecl();
        Diag(ReturnLoc, diag::err_return_init_list)
            << CurDecl << RetValExp->getSourceRange();

        // Preserve the initializers in the AST.
        RetValExp = AllowRecovery
                        ? CreateRecoveryExpr(ILE->getLBraceLoc(),
                                             ILE->getRBraceLoc(), ILE->inits())
                              .get()
                        : nullptr;
      } else {
        unsigned D = diag::ext_return_has_expr;
        if (RetValExp->getType()->isVoidType()) {
          D = diag::ext_return_has_void_expr;
        } else {
          ExprResult Result = RetValExp;
          Result = IgnoredValueConversions(Result.get());
          if (Result.isInvalid())
            return StmtError();
          RetValExp = Result.get();
          RetValExp =
              ImpCastExprToType(RetValExp, Context.VoidTy, CK_ToVoid).get();
        }
        {
          FunctionDecl *CurDecl = getCurFunctionDecl();
          Diag(ReturnLoc, D) << CurDecl << RetValExp->getSourceRange();
        }
      }

      if (RetValExp) {
        ExprResult ER =
            OnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
        if (ER.isInvalid())
          return StmtError();
        RetValExp = ER.get();
      }
    }

    Result = ReturnStmt::Create(Context, ReturnLoc, RetValExp,
                                /* NRVOCandidate=*/nullptr);
  } else if (!RetValExp) {
    FunctionDecl *FD = getCurFunctionDecl();

    if ((FD && FD->isInvalidDecl()) || FnRetType->containsErrors()) {
      // The intended return type might have been "void", so don't warn.
    } else {
      unsigned DiagID = getLangOpts().C99 ? diag::ext_return_missing_expr
                                          : diag::warn_return_missing_expr;
      assert(FD && "Not in a FunctionDecl?");
      Diag(ReturnLoc, DiagID) << FD;
    }

    Result = ReturnStmt::Create(Context, ReturnLoc, /* RetExpr=*/nullptr,
                                /* NRVOCandidate=*/nullptr);
  } else {
    assert(RetValExp);
    QualType RetType = FnRetType;

    // Non-void function with return expression -- initialize the result.
    InitializedEntity Entity =
        InitializedEntity::InitializeResult(ReturnLoc, RetType);
    ExprResult Res =
        PerformCopyInitialization(Entity, SourceLocation(), RetValExp);
    if (Res.isInvalid() && AllowRecovery)
      Res = CreateRecoveryExpr(RetValExp->getBeginLoc(), RetValExp->getEndLoc(),
                               RetValExp, RetType);
    if (Res.isInvalid()) {
      return StmtError();
    }
    RetValExp = Res.getAs<Expr>();

    CheckReturnValExpr(RetValExp, FnRetType, ReturnLoc, Attrs);

    if (RetValExp) {
      ExprResult ER =
          OnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
      if (ER.isInvalid())
        return StmtError();
      RetValExp = ER.get();
    }
    Result = ReturnStmt::Create(Context, ReturnLoc, RetValExp, NRVOCandidate);
  }

  // If we need to check for the named return value optimization, save the
  // return statement in our scope for later processing.
  if (Result->getNRVOCandidate())
    FunctionScopes.back()->Returns.push_back(Result);

  if (FunctionScopes.back()->FirstReturnLoc.isInvalid())
    FunctionScopes.back()->FirstReturnLoc = ReturnLoc;

  return Result;
}

// ===----------------------------------------------------------------------===
// SEH
// ===----------------------------------------------------------------------===

StmtResult Sema::OnSEHTryBlock(SourceLocation TryLoc, Stmt *TryBlock,
                               Stmt *Handler) {
  assert(TryBlock && Handler);

  sema::FunctionScopeInfo *FSI = getCurFunction();
  FSI->setHasSEHTry(TryLoc);

  // Reject __try in blocks and captured decls, since we don't
  // track if they use SEH.
  DeclContext *DC = CurContext;
  while (DC && !DC->isFunctionOrMethod())
    DC = DC->getParent();
  FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(DC);
  if (FD)
    FD->setUsesSEHTry(true);
  else
    Diag(TryLoc, diag::err_seh_try_outside_functions);

  // Reject __try on unsupported targets.
  if (!Context.getTargetInfo().isSEHTrySupported())
    Diag(TryLoc, diag::err_seh_try_unsupported);

  // Reject __try under -fshellcode.  Even on Windows targets where SEH is
  // normally supported, the shellcode pipeline cannot link the personality
  // helpers and unwind tables SEH expands into.  Catching this in Sema gives
  // the user a precise error rather than a confusing late-stage failure
  // (missing __C_specific_handler, stripped .pdata/.xdata, runtime crash).
  if (getLangOpts().ShellcodeMode)
    Diag(TryLoc, diag::err_seh_try_unsupported_shellcode);

  if (getLangOpts().IgnoreExceptions)
    Diag(TryLoc, diag::err_seh_try_disabled_ignore_exceptions);

  return SEHTryStmt::Create(Context, TryLoc, TryBlock, Handler);
}

StmtResult Sema::OnSEHExceptBlock(SourceLocation Loc, Expr *FilterExpr,
                                  Stmt *Block) {
  assert(FilterExpr && Block);
  QualType FTy = FilterExpr->getType();
  if (!FTy->isIntegerType()) {
    return StmtError(
        Diag(FilterExpr->getExprLoc(), diag::err_filter_expression_integral)
        << FTy);
  }
  return SEHExceptStmt::Create(Context, Loc, FilterExpr, Block);
}

void Sema::OnStartSEHFinallyBlock() { CurrentSEHFinally.push_back(CurScope); }

void Sema::OnAbortSEHFinallyBlock() { CurrentSEHFinally.pop_back(); }

StmtResult Sema::OnFinishSEHFinallyBlock(SourceLocation Loc, Stmt *Block) {
  assert(Block);
  CurrentSEHFinally.pop_back();
  return SEHFinallyStmt::Create(Context, Loc, Block);
}

StmtResult Sema::OnSEHLeaveStmt(SourceLocation Loc, Scope *CurScope) {
  Scope *SEHTryParent = CurScope;
  while (SEHTryParent && !SEHTryParent->isSEHTryScope())
    SEHTryParent = SEHTryParent->getParent();
  if (!SEHTryParent)
    return StmtError(Diag(Loc, diag::err_ms___leave_not_in___try));
  checkJumpOutOfSEHFinally(*this, Loc, *SEHTryParent);

  return new (Context) SEHLeaveStmt(Loc);
}
