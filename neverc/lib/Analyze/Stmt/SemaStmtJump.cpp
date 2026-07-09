//===- SemaStmtJump.cpp - Jump / return / SEH statement semantics ---------===//

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

  // Reject __try under -fdyncode.  Even on Windows targets where SEH is
  // normally supported, the dyncode pipeline cannot link the personality
  // helpers and unwind tables SEH expands into.  Catching this in Sema gives
  // the user a precise error rather than a confusing late-stage failure
  // (missing __C_specific_handler, stripped .pdata/.xdata, runtime crash).
  if (getLangOpts().DynCodeMode)
    Diag(TryLoc, diag::err_seh_try_unsupported_dyncode);

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
