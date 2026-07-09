//===- SemaStmtLoop.cpp - Loop statement semantics ------------------------===//

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
// while
// ===----------------------------------------------------------------------===

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
