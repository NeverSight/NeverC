#include "ABI/TargetInfo.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Assumptions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Statement dispatch & debug stop points
// ===----------------------------------------------------------------------===

void FunctionEmitter::genStopPoint(const Stmt *S) {
  if (DebugEmitter *DI = getDebugInfo()) {
    SourceLocation Loc;
    Loc = S->getBeginLoc();
    DI->genLocation(Builder, Loc);

    LastStopPoint = Loc;
  }
}

NEVERC_HOT void
FunctionEmitter::genStmt(const Stmt *S, llvm::ArrayRef<const Attr *> Attrs) {
  assert(S && "Null statement?");

  if (genSimpleStmt(S, Attrs))
    return;

  if (LLVM_UNLIKELY(!haveInsertPoint())) {
    if (!containsLabel(S)) {
      assert(!isa<DeclStmt>(*S) && "Unexpected DeclStmt!");
      return;
    }
    ensureInsertPoint();
  }

  genStopPoint(S);

  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass:
  case Stmt::SEHExceptStmtClass:
  case Stmt::SEHFinallyStmtClass:
    llvm_unreachable("invalid statement class to emit generically");
  case Stmt::NullStmtClass:
  case Stmt::CompoundStmtClass:
  case Stmt::DeclStmtClass:
  case Stmt::LabelStmtClass:
  case Stmt::AttributedStmtClass:
  case Stmt::GotoStmtClass:
  case Stmt::BreakStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::SEHLeaveStmtClass:
    llvm_unreachable("should have emitted these statements as simple");

#define STMT(Type, Base)
#define ABSTRACT_STMT(Op)
#define EXPR(Type, Base) case Stmt::Type##Class:
#include "neverc/Tree/StmtNodes.td.h"
    {
      // Remember the block we came in on.
      llvm::BasicBlock *incoming = Builder.GetInsertBlock();
      assert(incoming && "expression emission must have an insertion point");

      genIgnoredExpr(cast<Expr>(S));

      llvm::BasicBlock *outgoing = Builder.GetInsertBlock();
      assert(outgoing && "expression emission cleared block!");

      // The expression emitters assume (reasonably!) that the insertion
      // point is always set.  To maintain that, the call-emission code
      // for noreturn functions has to enter a new block with no
      // predecessors.  We want to kill that block and mark the current
      // insertion point unreachable in the common case of a call like
      // "exit();".  Since expression emission doesn't otherwise create
      // blocks with no predecessors, we can just test for that.
      // However, we must be careful not to do this to our incoming
      // block, because *statement* emission does sometimes create
      // reachable blocks which will have no predecessors until later in
      // the function.  This occurs with, e.g., labels that are not
      // reachable by fallthrough.
      if (incoming != outgoing && outgoing->use_empty()) {
        outgoing->eraseFromParent();
        Builder.ClearInsertionPoint();
      }
      break;
    }

  case Stmt::IndirectGotoStmtClass:
    genIndirectGotoStmt(cast<IndirectGotoStmt>(*S));
    break;

  case Stmt::IfStmtClass:
    genIfStmt(cast<IfStmt>(*S));
    break;
  case Stmt::WhileStmtClass:
    genWhileStmt(cast<WhileStmt>(*S), Attrs);
    break;
  case Stmt::DoStmtClass:
    genDoStmt(cast<DoStmt>(*S), Attrs);
    break;
  case Stmt::ForStmtClass:
    genForStmt(cast<ForStmt>(*S), Attrs);
    break;

  case Stmt::ReturnStmtClass:
    genReturnStmt(cast<ReturnStmt>(*S));
    break;

  case Stmt::SwitchStmtClass:
    genSwitchStmt(cast<SwitchStmt>(*S));
    break;
  case Stmt::GCCAsmStmtClass: // Intentional fall-through.
  case Stmt::MSAsmStmtClass:
    genAsmStmt(cast<AsmStmt>(*S));
    break;
  case Stmt::SEHTryStmtClass:
    genSEHTryStmt(cast<SEHTryStmt>(*S));
    break;
  }
}

NEVERC_HOT bool
FunctionEmitter::genSimpleStmt(const Stmt *S,
                               llvm::ArrayRef<const Attr *> Attrs) {
  switch (S->getStmtClass()) {
  default:
    return false;
  case Stmt::NullStmtClass:
    break;
  case Stmt::CompoundStmtClass:
    genCompoundStmt(cast<CompoundStmt>(*S));
    break;
  case Stmt::DeclStmtClass:
    genDeclStmt(cast<DeclStmt>(*S));
    break;
  case Stmt::LabelStmtClass:
    genLabelStmt(cast<LabelStmt>(*S));
    break;
  case Stmt::AttributedStmtClass:
    genAttributedStmt(cast<AttributedStmt>(*S));
    break;
  case Stmt::GotoStmtClass:
    genGotoStmt(cast<GotoStmt>(*S));
    break;
  case Stmt::BreakStmtClass:
    genBreakStmt(cast<BreakStmt>(*S));
    break;
  case Stmt::ContinueStmtClass:
    genContinueStmt(cast<ContinueStmt>(*S));
    break;
  case Stmt::DefaultStmtClass:
    genDefaultStmt(cast<DefaultStmt>(*S), Attrs);
    break;
  case Stmt::CaseStmtClass:
    genCaseStmt(cast<CaseStmt>(*S), Attrs);
    break;
  case Stmt::SEHLeaveStmtClass:
    genSEHLeaveStmt(cast<SEHLeaveStmt>(*S));
    break;
  }
  return true;
}

NEVERC_HOT Address FunctionEmitter::genCompoundStmt(
    const CompoundStmt &S, bool GetLast, AggValueSlot AggSlot) {
#if ENABLE_CRASH_OVERRIDES
  PrettyStackTraceLoc CrashInfo(
      getContext().getSourceManager(), S.getLBracLoc(),
      "LLVM IR generation of compound statement ('{}')");
#endif

  LexicalScope Scope(*this, S.getSourceRange());

  return genCompoundStmtWithoutScope(S, GetLast, AggSlot);
}

NEVERC_HOT Address FunctionEmitter::genCompoundStmtWithoutScope(
    const CompoundStmt &S, bool GetLast, AggValueSlot AggSlot) {

  const Stmt *ExprResult = S.getStmtExprResult();
  assert((!GetLast || (GetLast && ExprResult)) &&
         "If GetLast is true then the CompoundStmt must have a StmtExprResult");

  Address RetAlloca = Address::invalid();

  for (auto *CurStmt : S.body()) {
    if (LLVM_UNLIKELY(GetLast) && ExprResult == CurStmt) {
      // We have to special case labels here.  They are statements, but when put
      // at the end of a statement expression, they yield the value of their
      // subexpression.  Handle this by walking through all labels we encounter,
      // emitting them before we evaluate the subexpr.
      // Similar issues arise for attributed statements.
      while (!isa<Expr>(ExprResult)) {
        if (const auto *LS = dyn_cast<LabelStmt>(ExprResult)) {
          genLabel(LS->getDecl());
          ExprResult = LS->getSubStmt();
        } else if (const auto *AS = dyn_cast<AttributedStmt>(ExprResult)) {
          ExprResult = AS->getSubStmt();
        } else {
          llvm_unreachable("unknown value statement");
        }
      }

      ensureInsertPoint();

      const Expr *E = cast<Expr>(ExprResult);
      QualType ExprTy = E->getType();
      if (hasAggregateEvaluationKind(ExprTy)) {
        genAggExpr(E, AggSlot);
      } else {
        // We can't return an RValue here because there might be cleanups at
        // the end of the StmtExpr.  Because of that, we have to emit the result
        // here into a temporary alloca.
        RetAlloca = createMemTemp(ExprTy);
        genAnyExprToMem(E, RetAlloca, Qualifiers(),
                        /*IsInit*/ false);
      }
    } else {
      genStmt(CurStmt);
    }
  }

  return RetAlloca;
}

// ===----------------------------------------------------------------------===
// Basic block management & branching
// ===----------------------------------------------------------------------===

void FunctionEmitter::simplifyForwardingBlocks(llvm::BasicBlock *BB) {
  llvm::BranchInst *BI = dyn_cast<llvm::BranchInst>(BB->getTerminator());

  // Not worth simplifying if cleanups are active (would need to update
  // scope map and cleanup entry).
  if (!EHStack.empty())
    return;

  // Can only simplify direct branches.
  if (!BI || !BI->isUnconditional())
    return;

  // Can only simplify empty blocks.
  if (BI->getIterator() != BB->begin())
    return;

  BB->replaceAllUsesWith(BI->getSuccessor(0));
  BI->eraseFromParent();
  BB->eraseFromParent();
}

void FunctionEmitter::genBlock(llvm::BasicBlock *BB, bool IsFinished) {
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  // Fall out of the current block (if necessary).
  genBranch(BB);

  if (IsFinished && BB->use_empty()) {
    delete BB;
    return;
  }

  // Place the block after the current block, if possible, or else at
  // the end of the function.
  if (CurBB && CurBB->getParent())
    CurFn->insert(std::next(CurBB->getIterator()), BB);
  else
    CurFn->insert(CurFn->end(), BB);
  Builder.SetInsertPoint(BB);
}

void FunctionEmitter::genBranch(llvm::BasicBlock *Target) {
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  if (!CurBB || CurBB->getTerminator()) {
  } else {
    Builder.CreateBr(Target);
  }

  Builder.ClearInsertionPoint();
}

void FunctionEmitter::genBlockAfterUses(llvm::BasicBlock *block) {
  bool inserted = false;
  for (llvm::User *u : block->users()) {
    if (llvm::Instruction *insn = dyn_cast<llvm::Instruction>(u)) {
      CurFn->insert(std::next(insn->getParent()->getIterator()), block);
      inserted = true;
      break;
    }
  }

  if (!inserted)
    CurFn->insert(CurFn->end(), block);

  Builder.SetInsertPoint(block);
}

// ===----------------------------------------------------------------------===
// Labels & goto
// ===----------------------------------------------------------------------===

FunctionEmitter::JumpDest
FunctionEmitter::getJumpDestForLabel(const LabelDecl *D) {
  JumpDest &Dest = LabelMap[D];
  if (Dest.isValid())
    return Dest;

  Dest = JumpDest(createBasicBlock(D->getName()),
                  EHScopeStack::stable_iterator::invalid(),
                  NextCleanupDestIndex++);
  return Dest;
}

void FunctionEmitter::genLabel(const LabelDecl *D) {
  if (EHStack.hasNormalCleanups() && CurLexicalScope)
    CurLexicalScope->addLabel(D);

  JumpDest &Dest = LabelMap[D];

  if (!Dest.isValid()) {
    Dest = getJumpDestInCurrentScope(D->getName());
  } else {
    assert(!Dest.getScopeDepth().isValid() && "already emitted label!");
    Dest.setScopeDepth(EHStack.stable_begin());
    resolveBranchFixups(Dest.getBlock());
  }

  genBlock(Dest.getBlock());

  if (DebugEmitter *DI = getDebugInfo()) {
    if (ME.getCodeGenOpts().hasReducedDebugInfo()) {
      DI->setLocation(D->getLocation());
      DI->genLabel(D, Builder);
    }
  }
}

void FunctionEmitter::LexicalScope::rescopeLabels() {
  assert(!Labels.empty());
  EHScopeStack::stable_iterator innermostScope =
      FE.EHStack.getInnermostNormalCleanup();

  // Change the scope depth of all the labels.
  for (llvm::SmallVectorImpl<const LabelDecl *>::const_iterator
           i = Labels.begin(),
           e = Labels.end();
       i != e; ++i) {
    assert(FE.LabelMap.contains(*i));
    JumpDest &dest = FE.LabelMap.find(*i)->second;
    assert(dest.getScopeDepth().isValid());
    assert(innermostScope.encloses(dest.getScopeDepth()));
    dest.setScopeDepth(innermostScope);
  }

  // Reparent the labels if the new scope also has cleanups.
  if (innermostScope != EHScopeStack::stable_end() && ParentScope) {
    ParentScope->Labels.append(Labels.begin(), Labels.end());
  }
}

void FunctionEmitter::genLabelStmt(const LabelStmt &S) {
  genLabel(S.getDecl());
  genStmt(S.getSubStmt());
}

void FunctionEmitter::genAttributedStmt(const AttributedStmt &S) {
  bool nomerge = false;
  bool noinline = false;
  bool alwaysinline = false;
  const CallExpr *musttail = nullptr;

  for (const auto *A : S.getAttrs()) {
    switch (A->getKind()) {
    default:
      break;
    case attr::NoMerge:
      nomerge = true;
      break;
    case attr::NoInline:
      noinline = true;
      break;
    case attr::AlwaysInline:
      alwaysinline = true;
      break;
    case attr::MustTail:
      const Stmt *Sub = S.getSubStmt();
      const ReturnStmt *R = cast<ReturnStmt>(Sub);
      musttail = cast<CallExpr>(R->getRetValue()->IgnoreParens());
      break;
    }
  }
  SaveAndRestore save_nomerge(InNoMergeAttributedStmt, nomerge);
  SaveAndRestore save_noinline(InNoInlineAttributedStmt, noinline);
  SaveAndRestore save_alwaysinline(InAlwaysInlineAttributedStmt, alwaysinline);
  SaveAndRestore save_musttail(MustTailCall, musttail);
  genStmt(S.getSubStmt(), S.getAttrs());
}

void FunctionEmitter::genGotoStmt(const GotoStmt &S) {
  // If this code is reachable then emit a stop point (if generating
  // debug info). We have to do this ourselves because we are on the
  // "simple" statement path.
  if (haveInsertPoint())
    genStopPoint(&S);

  // In an outlined SEH __finally helper, goto targets are in the parent
  // function. Record a bailout request and return; the parent cleanup will
  // perform the actual jump (threading through any remaining cleanups).
  if (IsOutlinedSEHHelper && SEHFinallyBailoutKindParent.isValid() &&
      SEHFinallyBailoutTargetParent.isValid()) {
    unsigned Code = 0;
    auto It = SEHFinallyGotoLabelToCode.find(S.getLabel());
    if (It != SEHFinallyGotoLabelToCode.end())
      Code = It->second;

    if (Code) {
      Builder.CreateStore(
          Builder.getInt8(static_cast<uint8_t>(SEHFinallyBailoutKind::Goto)),
          SEHFinallyBailoutKindParent);
      Builder.CreateStore(Builder.getInt32(Code),
                          SEHFinallyBailoutTargetParent);
      genBranchThroughCleanup(ReturnBlock);
      return;
    }

    // Unknown label (shouldn't happen): avoid crashing the compiler.
    Builder.CreateUnreachable();
    Builder.ClearInsertionPoint();
    return;
  }

  genBranchThroughCleanup(getJumpDestForLabel(S.getLabel()));
}

void FunctionEmitter::genIndirectGotoStmt(const IndirectGotoStmt &S) {
  if (const LabelDecl *Target = S.getConstantTarget()) {
    genBranchThroughCleanup(getJumpDestForLabel(Target));
    return;
  }

  // Ensure that we have an i8* for our PHI node.
  llvm::Value *V =
      Builder.CreateBitCast(genScalarExpr(S.getTarget()), Int8PtrTy, "addr");
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  llvm::BasicBlock *IndGotoBB = getIndirectGotoBlock();

  // The first instruction in the block has to be the PHI for the switch dest,
  // add an entry for this branch.
  cast<llvm::PHINode>(IndGotoBB->begin())->addIncoming(V, CurBB);

  genBranch(IndGotoBB);
}

// ===----------------------------------------------------------------------===
// Control flow: if / while / do / for / switch
// ===----------------------------------------------------------------------===

NEVERC_HOT void FunctionEmitter::genIfStmt(const IfStmt &S) {
  LexicalScope ConditionScope(*this, S.getCond()->getSourceRange());

  if (S.getInit())
    genStmt(S.getInit());

  if (S.getConditionVariable())
    genDecl(*S.getConditionVariable());

  bool CondConstant;
  if (constantFoldsToSimpleInteger(S.getCond(), CondConstant,
                                   /*AllowLabels=*/false)) {
    const Stmt *Executed = S.getThen();
    const Stmt *Skipped = S.getElse();
    if (!CondConstant)
      std::swap(Executed, Skipped);

    if (!containsLabel(Skipped)) {
      if (CondConstant)
        if (Executed) {
          RunCleanupsScope ExecutedScope(*this);
          genStmt(Executed);
        }
      return;
    }
  }

  llvm::BasicBlock *ThenBlock = createBasicBlock("if.then");
  llvm::BasicBlock *ContBlock = createBasicBlock("if.end");
  llvm::BasicBlock *ElseBlock = ContBlock;
  if (S.getElse())
    ElseBlock = createBasicBlock("if.else");

  Stmt::Likelihood LH = Stmt::LH_None;
  if (ME.getCodeGenOpts().OptimizationLevel)
    LH = Stmt::getLikelihood(S.getThen(), S.getElse());
  genBranchOnBoolExpr(S.getCond(), ThenBlock, ElseBlock, LH);

  genBlock(ThenBlock);
  {
    RunCleanupsScope ThenScope(*this);
    genStmt(S.getThen());
  }
  genBranch(ContBlock);

  if (const Stmt *Else = S.getElse()) {
    {
      auto NL = ApplyDebugLocation::CreateEmpty(*this);
      genBlock(ElseBlock);
    }
    {
      RunCleanupsScope ElseScope(*this);
      genStmt(Else);
    }
    {
      auto NL = ApplyDebugLocation::CreateEmpty(*this);
      genBranch(ContBlock);
    }
  }

  genBlock(ContBlock, true);
}

NEVERC_HOT void
FunctionEmitter::genWhileStmt(const WhileStmt &S,
                              llvm::ArrayRef<const Attr *> WhileAttrs) {
  JumpDest LoopHeader = getJumpDestInCurrentScope("while.cond");
  genBlock(LoopHeader.getBlock());

  JumpDest LoopExit = getJumpDestInCurrentScope("while.end");
  BreakContinueStack.push_back(BreakContinue(LoopExit, LoopHeader));

  RunCleanupsScope ConditionScope(*this);

  if (S.getConditionVariable())
    genDecl(*S.getConditionVariable());

  llvm::Value *BoolCondVal = evaluateExprAsBool(S.getCond());

  // while(1) is common, avoid extra exit blocks.  Be sure
  // to correctly handle break/continue though.
  llvm::ConstantInt *C = dyn_cast<llvm::ConstantInt>(BoolCondVal);
  bool CondIsConstInt = C != nullptr;
  bool genBoolCondBranch = !CondIsConstInt || !C->isOne();
  const SourceRange &R = S.getSourceRange();
  LoopStack.push(LoopHeader.getBlock(), ME.getContext(), ME.getCodeGenOpts(),
                 WhileAttrs, sourceLocToDebugLoc(R.getBegin()),
                 sourceLocToDebugLoc(R.getEnd()),
                 checkIfLoopMustProgress(CondIsConstInt));

  llvm::BasicBlock *LoopBody = createBasicBlock("while.body");
  if (genBoolCondBranch) {
    llvm::BasicBlock *ExitBlock = LoopExit.getBlock();
    if (ConditionScope.requiresCleanups())
      ExitBlock = createBasicBlock("while.exit");
    if (ME.getCodeGenOpts().OptimizationLevel)
      BoolCondVal = emitCondLikelihoodViaExpectIntrinsic(
          BoolCondVal, Stmt::getLikelihood(S.getBody()));
    Builder.CreateCondBr(BoolCondVal, LoopBody, ExitBlock);

    if (ExitBlock != LoopExit.getBlock()) {
      genBlock(ExitBlock);
      genBranchThroughCleanup(LoopExit);
    }
  } else if (const Attr *A = Stmt::getLikelihoodAttr(S.getBody())) {
    ME.getDiags().Report(A->getLocation(),
                         diag::warn_attribute_has_no_effect_on_infinite_loop)
        << A << A->getRange();
    ME.getDiags().Report(
        S.getWhileLoc(),
        diag::note_attribute_has_no_effect_on_infinite_loop_here)
        << SourceRange(S.getWhileLoc(), S.getRParenLoc());
  }

  // Cleanup scope needed for possible singleton DeclStmt.
  {
    RunCleanupsScope BodyScope(*this);
    genBlock(LoopBody);
    genStmt(S.getBody());
  }

  BreakContinueStack.pop_back();

  ConditionScope.ForceCleanup();

  genStopPoint(&S);
  genBranch(LoopHeader.getBlock());

  LoopStack.pop();

  genBlock(LoopExit.getBlock(), true);

  // The LoopHeader typically is just a branch if we skipped emitting
  // a branch, try to erase it.
  if (!genBoolCondBranch)
    simplifyForwardingBlocks(LoopHeader.getBlock());
}

void FunctionEmitter::genDoStmt(const DoStmt &S,
                                llvm::ArrayRef<const Attr *> DoAttrs) {
  JumpDest LoopExit = getJumpDestInCurrentScope("do.end");
  JumpDest LoopCond = getJumpDestInCurrentScope("do.cond");

  BreakContinueStack.push_back(BreakContinue(LoopExit, LoopCond));

  llvm::BasicBlock *LoopBody = createBasicBlock("do.body");

  genBlock(LoopBody);
  {
    RunCleanupsScope BodyScope(*this);
    genStmt(S.getBody());
  }

  genBlock(LoopCond.getBlock());

  llvm::Value *BoolCondVal = evaluateExprAsBool(S.getCond());

  BreakContinueStack.pop_back();

  // "do {} while (0)" is common in macros, avoid extra blocks.  Be sure
  // to correctly handle break/continue though.
  llvm::ConstantInt *C = dyn_cast<llvm::ConstantInt>(BoolCondVal);
  bool CondIsConstInt = C;
  bool genBoolCondBranch = !C || !C->isZero();

  const SourceRange &R = S.getSourceRange();
  LoopStack.push(LoopBody, ME.getContext(), ME.getCodeGenOpts(), DoAttrs,
                 sourceLocToDebugLoc(R.getBegin()),
                 sourceLocToDebugLoc(R.getEnd()),
                 checkIfLoopMustProgress(CondIsConstInt));

  if (genBoolCondBranch)
    Builder.CreateCondBr(BoolCondVal, LoopBody, LoopExit.getBlock());

  LoopStack.pop();

  genBlock(LoopExit.getBlock());

  // The DoCond block typically is just a branch if we skipped
  // emitting a branch, try to erase it.
  if (!genBoolCondBranch)
    simplifyForwardingBlocks(LoopCond.getBlock());
}

NEVERC_HOT void
FunctionEmitter::genForStmt(const ForStmt &S,
                            llvm::ArrayRef<const Attr *> ForAttrs) {
  JumpDest LoopExit = getJumpDestInCurrentScope("for.end");

  LexicalScope ForScope(*this, S.getSourceRange());

  if (S.getInit())
    genStmt(S.getInit());

  JumpDest CondDest = getJumpDestInCurrentScope("for.cond");
  llvm::BasicBlock *CondBlock = CondDest.getBlock();
  genBlock(CondBlock);

  Expr::EvalResult Result;
  bool CondIsConstInt =
      !S.getCond() || S.getCond()->EvaluateAsInt(Result, getContext());

  const SourceRange &R = S.getSourceRange();
  LoopStack.push(CondBlock, ME.getContext(), ME.getCodeGenOpts(), ForAttrs,
                 sourceLocToDebugLoc(R.getBegin()),
                 sourceLocToDebugLoc(R.getEnd()),
                 checkIfLoopMustProgress(CondIsConstInt));

  LexicalScope ConditionScope(*this, S.getSourceRange());

  // If the for loop doesn't have an increment we can just use the condition as
  // the continue block. Otherwise, if there is no condition variable, we can
  // form the continue block now. If there is a condition variable, we can't
  // form the continue block until after we've emitted the condition, because
  // the condition is in scope in the increment, but Sema's jump diagnostics
  // ensure that there are no continues from the condition variable that jump
  // to the loop increment.
  JumpDest Continue;
  if (!S.getInc())
    Continue = CondDest;
  else if (!S.getConditionVariable())
    Continue = getJumpDestInCurrentScope("for.inc");
  BreakContinueStack.push_back(BreakContinue(LoopExit, Continue));

  if (S.getCond()) {
    // If the for statement has a condition scope, emit the local variable
    // declaration.
    if (S.getConditionVariable()) {
      genDecl(*S.getConditionVariable());

      // We have entered the condition variable's scope, so we're now able to
      // jump to the continue block.
      Continue = S.getInc() ? getJumpDestInCurrentScope("for.inc") : CondDest;
      BreakContinueStack.back().ContinueBlock = Continue;
    }

    llvm::BasicBlock *ExitBlock = LoopExit.getBlock();
    // If there are any cleanups between here and the loop-exit scope,
    // create a block to stage a loop exit along.
    if (ForScope.requiresCleanups())
      ExitBlock = createBasicBlock("for.cond.cleanup");

    // As long as the condition is true, iterate the loop.
    llvm::BasicBlock *ForBody = createBasicBlock("for.body");

    // Loop body runs when condition is non-zero.
    llvm::Value *BoolCondVal = evaluateExprAsBool(S.getCond());
    if (ME.getCodeGenOpts().OptimizationLevel)
      BoolCondVal = emitCondLikelihoodViaExpectIntrinsic(
          BoolCondVal, Stmt::getLikelihood(S.getBody()));

    Builder.CreateCondBr(BoolCondVal, ForBody, ExitBlock);

    if (ExitBlock != LoopExit.getBlock()) {
      genBlock(ExitBlock);
      genBranchThroughCleanup(LoopExit);
    }

    genBlock(ForBody);
  } else {
    // Treat it as a non-zero constant.  Don't even create a new block for the
    // body, just fall into it.
  }

  {
    // Create a separate cleanup scope for the body, in case it is not
    // a compound statement.
    RunCleanupsScope BodyScope(*this);
    genStmt(S.getBody());
  }

  // If there is an increment, emit it next.
  if (S.getInc()) {
    genBlock(Continue.getBlock());
    genStmt(S.getInc());
  }

  BreakContinueStack.pop_back();

  ConditionScope.ForceCleanup();

  genStopPoint(&S);
  genBranch(CondBlock);

  ForScope.ForceCleanup();

  LoopStack.pop();

  genBlock(LoopExit.getBlock(), true);
}

// ===----------------------------------------------------------------------===
// Return statements
// ===----------------------------------------------------------------------===

void FunctionEmitter::genReturnOfRValue(RValue RV, QualType Ty) {
  if (RV.isScalar()) {
    Builder.CreateStore(RV.getScalarVal(), ReturnValue);
  } else if (RV.isAggregate()) {
    LValue Dest = makeAddrLValue(ReturnValue, Ty);
    LValue Src = makeAddrLValue(RV.getAggregateAddress(), Ty);
    genAggregateCopy(Dest, Src, Ty, getOverlapForReturnValue());
  } else {
    genStoreOfComplex(RV.getComplexVal(), makeAddrLValue(ReturnValue, Ty),
                      /*init*/ true);
  }
  genBranchThroughCleanup(ReturnBlock);
}

namespace {
// RAII struct used to save and restore a return statment's result expression.
struct SaveRetExprRAII {
  SaveRetExprRAII(const Expr *RetExpr, FunctionEmitter &FE)
      : OldRetExpr(FE.RetExpr), FE(FE) {
    FE.RetExpr = RetExpr;
  }
  ~SaveRetExprRAII() { FE.RetExpr = OldRetExpr; }
  const Expr *OldRetExpr;
  FunctionEmitter &FE;
};
} // namespace

NEVERC_HOT void FunctionEmitter::genReturnStmt(const ReturnStmt &S) {

  Address ReturnValue = this->ReturnValue;
  if (IsOutlinedSEHHelper) {
    Builder.CreateStore(Builder.getInt8(1), SEHRetNowParent);
    ReturnValue = SEHReturnValue;
  }

  // Evaluate even if unused, for side effects.
  const Expr *RV = S.getRetValue();

  SaveRetExprRAII SaveRetExpr(RV, *this);

  RunCleanupsScope cleanupScope(*this);
  if (const auto *EWC = dyn_cast_or_null<ExprWithCleanups>(RV))
    RV = EWC->getSubExpr();
  if (!ReturnValue.isValid() || (RV && RV->getType()->isVoidType())) {
    // Make sure not to return anything, but evaluate the expression
    // for side effects.
    if (RV) {
      genAnyExpr(RV);
    }
  } else if (!RV) {
  } else {
    switch (getEvaluationKind(RV->getType())) {
    case TEK_Scalar:
      Builder.CreateStore(genScalarExpr(RV), ReturnValue);
      break;
    case TEK_Complex:
      genComplexExprIntoLValue(RV, makeAddrLValue(ReturnValue, RV->getType()),
                               /*isInit*/ true);
      break;
    case TEK_Aggregate:
      genAggExpr(RV, AggValueSlot::forAddr(ReturnValue, Qualifiers(),
                                           AggValueSlot::IsDestructed,
                                           AggValueSlot::IsNotAliased,
                                           getOverlapForReturnValue()));
      break;
    }
  }

  ++NumReturnExprs;
  if (!RV || RV->isEvaluatable(getContext()))
    ++NumSimpleReturnExprs;

  cleanupScope.ForceCleanup();
  genBranchThroughCleanup(ReturnBlock);
}

// ===----------------------------------------------------------------------===
// Decl, break, continue
// ===----------------------------------------------------------------------===

NEVERC_HOT void FunctionEmitter::genDeclStmt(const DeclStmt &S) {
  if (haveInsertPoint())
    genStopPoint(&S);

  for (const auto *I : S.decls())
    genDecl(*I);
}

void FunctionEmitter::genBreakStmt(const BreakStmt &S) {
  if (IsOutlinedSEHHelper && SEHFinallyBailoutKindParent.isValid() &&
      SEHFinallyBailoutTargetParent.isValid()) {
    Builder.CreateStore(
        Builder.getInt8(static_cast<uint8_t>(SEHFinallyBailoutKind::Break)),
        SEHFinallyBailoutKindParent);
    Builder.CreateStore(Builder.getInt32(0), SEHFinallyBailoutTargetParent);
    genBranchThroughCleanup(ReturnBlock);
    return;
  }

  assert(!BreakContinueStack.empty() && "break stmt not in a loop or switch!");

  if (haveInsertPoint())
    genStopPoint(&S);

  genBranchThroughCleanup(BreakContinueStack.back().BreakBlock);
}

void FunctionEmitter::genContinueStmt(const ContinueStmt &S) {
  // In an outlined SEH __finally helper, continue targets are in the parent
  // function. Record a bailout request and return; the parent cleanup will
  // perform the actual jump.
  if (IsOutlinedSEHHelper && SEHFinallyBailoutKindParent.isValid() &&
      SEHFinallyBailoutTargetParent.isValid()) {
    Builder.CreateStore(
        Builder.getInt8(static_cast<uint8_t>(SEHFinallyBailoutKind::Continue)),
        SEHFinallyBailoutKindParent);
    Builder.CreateStore(Builder.getInt32(0), SEHFinallyBailoutTargetParent);
    genBranchThroughCleanup(ReturnBlock);
    return;
  }

  assert(!BreakContinueStack.empty() && "continue stmt not in a loop!");

  // If this code is reachable then emit a stop point (if generating
  // debug info). We have to do this ourselves because we are on the
  // "simple" statement path.
  if (haveInsertPoint())
    genStopPoint(&S);

  genBranchThroughCleanup(BreakContinueStack.back().ContinueBlock);
}

void FunctionEmitter::genCaseStmtRange(const CaseStmt &S,
                                       llvm::ArrayRef<const Attr *> Attrs) {
  assert(S.getRHS() && "Expected RHS value in CaseStmt");

  llvm::APSInt LHS = S.getLHS()->EvaluateKnownConstInt(getContext());
  llvm::APSInt RHS = S.getRHS()->EvaluateKnownConstInt(getContext());

  // Must emit before switch machinery so it's properly chained from
  // predecessor.
  llvm::BasicBlock *CaseDest = createBasicBlock("sw.bb");
  genBlock(CaseDest);
  genStmt(S.getSubStmt());

  // If range is empty, do nothing.
  if (LHS.isSigned() ? RHS.slt(LHS) : RHS.ult(LHS))
    return;

  Stmt::Likelihood LH = Stmt::getLikelihood(Attrs);
  llvm::APInt Range = RHS - LHS;
  if (Range.ult(llvm::APInt(Range.getBitWidth(), 64))) {
    // Range is small enough to add multiple switch instruction cases.
    unsigned NCases = Range.getZExtValue() + 1;
    for (unsigned I = 0; I != NCases; ++I) {
      SwitchInsn->addCase(Builder.getInt(LHS), CaseDest);
      ++LHS;
    }
    return;
  }

  // The range is too big. Emit "if" condition into a new block,
  // making sure to save and restore the current insertion point.
  llvm::BasicBlock *RestoreBB = Builder.GetInsertBlock();

  // Push this test onto the chain of range checks (which terminates
  // in the default basic block). The switch's default will be changed
  // to the top of this chain after switch emission is complete.
  llvm::BasicBlock *FalseDest = CaseRangeBlock;
  CaseRangeBlock = createBasicBlock("sw.caserange");

  CurFn->insert(CurFn->end(), CaseRangeBlock);
  Builder.SetInsertPoint(CaseRangeBlock);

  llvm::Value *Diff =
      Builder.CreateSub(SwitchInsn->getCondition(), Builder.getInt(LHS));
  llvm::Value *Cond =
      Builder.CreateICmpULE(Diff, Builder.getInt(Range), "inbounds");

  if (ME.getCodeGenOpts().OptimizationLevel)
    Cond = emitCondLikelihoodViaExpectIntrinsic(Cond, LH);

  Builder.CreateCondBr(Cond, CaseDest, FalseDest);

  // Restore the appropriate insertion point.
  if (RestoreBB)
    Builder.SetInsertPoint(RestoreBB);
  else
    Builder.ClearInsertionPoint();
}

void FunctionEmitter::genCaseStmt(const CaseStmt &S,
                                  llvm::ArrayRef<const Attr *> Attrs) {
  // If there is no enclosing switch instance that we're aware of, then this
  // case statement and its block can be elided.  This situation only happens
  // when we've constant-folded the switch, are emitting the constant case,
  // and part of the constant case includes another case statement.  For
  // instance: switch (4) { case 4: do { case 5: } while (1); }
  if (!SwitchInsn) {
    genStmt(S.getSubStmt());
    return;
  }

  if (S.getRHS()) {
    genCaseStmtRange(S, Attrs);
    return;
  }

  llvm::ConstantInt *CaseVal =
      Builder.getInt(S.getLHS()->EvaluateKnownConstInt(getContext()));

  const ConstantExpr *CE;
  if (auto ICE = dyn_cast<ImplicitCastExpr>(S.getLHS()))
    CE = dyn_cast<ConstantExpr>(ICE->getSubExpr());
  else
    CE = dyn_cast<ConstantExpr>(S.getLHS());
  if (CE) {
    if (auto DE = dyn_cast<DeclRefExpr>(CE->getSubExpr()))
      if (DebugEmitter *Dbg = getDebugInfo())
        if (ME.getCodeGenOpts().hasReducedDebugInfo())
          Dbg->genGlobalVariable(DE->getDecl(),
                                 APValue(llvm::APSInt(CaseVal->getValue())));
  }

  // If the body of the case is just a 'break', try to not emit an empty block.
  if (ME.getCodeGenOpts().OptimizationLevel > 0 &&
      isa<BreakStmt>(S.getSubStmt())) {
    JumpDest Block = BreakContinueStack.back().BreakBlock;

    // Only do this optimization if there are no cleanups that need emitting.
    if (isObviouslyBranchWithoutCleanups(Block)) {
      SwitchInsn->addCase(CaseVal, Block.getBlock());

      // If there was a fallthrough into this case, make sure to redirect it to
      // the end of the switch as well.
      if (Builder.GetInsertBlock()) {
        Builder.CreateBr(Block.getBlock());
        Builder.ClearInsertionPoint();
      }
      return;
    }
  }

  llvm::BasicBlock *CaseDest = createBasicBlock("sw.bb");
  genBlock(CaseDest);
  SwitchInsn->addCase(CaseVal, CaseDest);

  // Recursively emitting the statement is acceptable, but is not wonderful for
  // code where we have many case statements nested together, i.e.:
  //  case 1:
  //    case 2:
  //      case 3: etc.
  // Handling this recursively will create a new block for each case statement
  // that falls through to the next case which is IR intensive.  It also causes
  // deep recursion which can run into stack depth limitations.  Handle
  // sequential non-range case statements specially.
  //
  const CaseStmt *CurCase = &S;
  const CaseStmt *NextCase = dyn_cast<CaseStmt>(S.getSubStmt());

  // Otherwise, iteratively add consecutive cases to this switch stmt.
  while (NextCase && NextCase->getRHS() == nullptr) {
    CurCase = NextCase;
    llvm::ConstantInt *CaseVal =
        Builder.getInt(CurCase->getLHS()->EvaluateKnownConstInt(getContext()));

    SwitchInsn->addCase(CaseVal, CaseDest);
    NextCase = dyn_cast<CaseStmt>(CurCase->getSubStmt());
  }

  // Generate a stop point for debug info if the case statement is
  // followed by a default statement. A fallthrough case before a
  // default case gets its own branch target.
  if (CurCase->getSubStmt()->getStmtClass() == Stmt::DefaultStmtClass)
    genStopPoint(CurCase);

  // Normal default recursion for non-cases.
  genStmt(CurCase->getSubStmt());
}

void FunctionEmitter::genDefaultStmt(const DefaultStmt &S,
                                     llvm::ArrayRef<const Attr *> Attrs) {
  // If there is no enclosing switch instance that we're aware of, then this
  // default statement can be elided. This situation only happens when we've
  // constant-folded the switch.
  if (!SwitchInsn) {
    genStmt(S.getSubStmt());
    return;
  }

  llvm::BasicBlock *DefaultBlock = SwitchInsn->getDefaultDest();
  assert(DefaultBlock->empty() &&
         "genDefaultStmt: Default block already defined?");

  genBlock(DefaultBlock);

  genStmt(S.getSubStmt());
}

namespace {

enum CSFC_Result { CSFC_Failure, CSFC_FallThrough, CSFC_Success };

CSFC_Result
collectStatementsForCase(const Stmt *S, const SwitchCase *Case, bool &FoundCase,
                         llvm::SmallVectorImpl<const Stmt *> &ResultStmts) {
  // If this is a null statement, just succeed.
  if (!S)
    return Case ? CSFC_Success : CSFC_FallThrough;

  // If this is the switchcase (case 4: or default) that we're looking for, then
  // we're in business.  Just add the substatement.
  if (const SwitchCase *SC = dyn_cast<SwitchCase>(S)) {
    if (S == Case) {
      FoundCase = true;
      return collectStatementsForCase(SC->getSubStmt(), nullptr, FoundCase,
                                      ResultStmts);
    }

    // Otherwise, this is some other case or default statement, just ignore it.
    return collectStatementsForCase(SC->getSubStmt(), Case, FoundCase,
                                    ResultStmts);
  }

  // If we are in the live part of the code and we found our break statement,
  // return a success!
  if (!Case && isa<BreakStmt>(S))
    return CSFC_Success;

  // If this is a switch statement, then it might contain the SwitchCase, the
  // break, or neither.
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    // Handle this as two cases: we might be looking for the SwitchCase (if so
    // the skipped statements must be skippable) or we might already have it.
    CompoundStmt::const_body_iterator I = CS->body_begin(), E = CS->body_end();
    bool StartedInLiveCode = FoundCase;
    unsigned StartSize = ResultStmts.size();

    // If we've not found the case yet, scan through looking for it.
    if (Case) {
      // Keep track of whether we see a skipped declaration.  The code could be
      // using the declaration even if it is skipped, so we can't optimize out
      // the decl if the kept statements might refer to it.
      bool HadSkippedDecl = false;

      // If we're looking for the case, just see if we can skip each of the
      // substatements.
      for (; Case && I != E; ++I) {
        HadSkippedDecl |= FunctionEmitter::mightAddDeclToScope(*I);

        switch (collectStatementsForCase(*I, Case, FoundCase, ResultStmts)) {
        case CSFC_Failure:
          return CSFC_Failure;
        case CSFC_Success:
          // A successful result means that either 1) that the statement doesn't
          // have the case and is skippable, or 2) does contain the case value
          // and also contains the break to exit the switch.  In the later case,
          // we just verify the rest of the statements are elidable.
          if (FoundCase) {
            // If we found the case and skipped declarations, we can't do the
            // optimization.
            if (HadSkippedDecl)
              return CSFC_Failure;

            for (++I; I != E; ++I)
              if (FunctionEmitter::containsLabel(*I, true))
                return CSFC_Failure;
            return CSFC_Success;
          }
          break;
        case CSFC_FallThrough:
          // If we have a fallthrough condition, then we must have found the
          // case started to include statements.  Consider the rest of the
          // statements in the compound statement as candidates for inclusion.
          assert(FoundCase && "Didn't find case but returned fallthrough?");
          // We recursively found Case, so we're not looking for it anymore.
          Case = nullptr;

          // If we found the case and skipped declarations, we can't do the
          // optimization.
          if (HadSkippedDecl)
            return CSFC_Failure;
          break;
        }
      }

      if (!FoundCase)
        return CSFC_Success;

      assert(!HadSkippedDecl && "fallthrough after skipping decl");
    }

    // If we have statements in our range, then we know that the statements are
    // live and need to be added to the set of statements we're tracking.
    bool AnyDecls = false;
    for (; I != E; ++I) {
      AnyDecls |= FunctionEmitter::mightAddDeclToScope(*I);

      switch (collectStatementsForCase(*I, nullptr, FoundCase, ResultStmts)) {
      case CSFC_Failure:
        return CSFC_Failure;
      case CSFC_FallThrough:
        // A fallthrough result means that the statement was simple and just
        // included in ResultStmt, keep adding them afterwards.
        break;
      case CSFC_Success:
        // A successful result means that we found the break statement and
        // stopped statement inclusion.  We just ensure that any leftover stmts
        // are skippable and return success ourselves.
        for (++I; I != E; ++I)
          if (FunctionEmitter::containsLabel(*I, true))
            return CSFC_Failure;
        return CSFC_Success;
      }
    }

    // If we're about to fall out of a scope without hitting a 'break;', we
    // can't perform the optimization if there were any decls in that scope
    // (we'd lose their end-of-lifetime).
    if (AnyDecls) {
      // If the entire compound statement was live, there's one more thing we
      // can try before giving up: emit the whole thing as a single statement.
      // We can do that unless the statement contains a 'break;'.
      if (StartedInLiveCode && !FunctionEmitter::containsBreak(S)) {
        ResultStmts.resize(StartSize);
        ResultStmts.push_back(S);
      } else {
        return CSFC_Failure;
      }
    }

    return CSFC_FallThrough;
  }

  // Okay, this is some other statement that we don't handle explicitly, like a
  // for statement or increment etc.  If we are skipping over this statement,
  // just verify it doesn't have labels, which would make it invalid to elide.
  if (Case) {
    if (FunctionEmitter::containsLabel(S, true))
      return CSFC_Failure;
    return CSFC_Success;
  }

  // Otherwise, we want to include this statement.  Everything is cool with that
  // so long as it doesn't contain a break out of the switch we're in.
  if (FunctionEmitter::containsBreak(S))
    return CSFC_Failure;

  // Otherwise, everything is great.  Include the statement and tell the caller
  // that we fall through and include the next statement as well.
  ResultStmts.push_back(S);
  return CSFC_FallThrough;
}

bool findCaseStatementsForValue(
    const SwitchStmt &S, const llvm::APSInt &ConstantCondValue,
    llvm::SmallVectorImpl<const Stmt *> &ResultStmts, TreeContext &C,
    const SwitchCase *&ResultCase) {
  // First step, find the switch case that is being branched to.  We can do this
  // efficiently by scanning the SwitchCase list.
  const SwitchCase *Case = S.getSwitchCaseList();
  const DefaultStmt *DefaultCase = nullptr;

  for (; Case; Case = Case->getNextSwitchCase()) {
    // It's either a default or case.  Just remember the default statement in
    // case we're not jumping to any numbered cases.
    if (const DefaultStmt *DS = dyn_cast<DefaultStmt>(Case)) {
      DefaultCase = DS;
      continue;
    }

    const CaseStmt *CS = cast<CaseStmt>(Case);
    // Don't handle case ranges yet.
    if (CS->getRHS())
      return false;

    // If we found our case, remember it as 'case'.
    if (CS->getLHS()->EvaluateKnownConstInt(C) == ConstantCondValue)
      break;
  }

  // If we didn't find a matching case, we use a default if it exists, or we
  // elide the whole switch body!
  if (!Case) {
    // It is safe to elide the body of the switch if it doesn't contain labels
    // etc.  If it is safe, return successfully with an empty ResultStmts list.
    if (!DefaultCase)
      return !FunctionEmitter::containsLabel(&S);
    Case = DefaultCase;
  }

  // Ok, we know which case is being jumped to, try to collect all the
  // statements that follow it.  This can fail for a variety of reasons.  Also,
  // check to see that the recursive walk actually found our case statement.
  // Insane cases like this can fail to find it in the recursive walk since we
  // don't handle every stmt kind:
  // switch (4) {
  //   while (1) {
  //     case 4: ...
  bool FoundCase = false;
  ResultCase = Case;
  return collectStatementsForCase(S.getBody(), Case, FoundCase, ResultStmts) !=
             CSFC_Failure &&
         FoundCase;
}

} // namespace

NEVERC_HOT void FunctionEmitter::genSwitchStmt(const SwitchStmt &S) {
  llvm::SwitchInst *SavedSwitchInsn = SwitchInsn;
  llvm::BasicBlock *SavedCRBlock = CaseRangeBlock;

  // See if we can constant fold the condition of the switch and therefore only
  // emit the live case statement (if any) of the switch.
  llvm::APSInt ConstantCondValue;
  if (constantFoldsToSimpleInteger(S.getCond(), ConstantCondValue)) {
    llvm::SmallVector<const Stmt *, 4> CaseStmts;
    const SwitchCase *Case = nullptr;
    if (findCaseStatementsForValue(S, ConstantCondValue, CaseStmts,
                                   getContext(), Case)) {
      if (Case)
        RunCleanupsScope ExecutedScope(*this);

      if (S.getInit())
        genStmt(S.getInit());

      // Condition variable needs the full cleanup scope for constant-folded
      // switches.
      if (S.getConditionVariable())
        genDecl(*S.getConditionVariable());

      // At this point, we are no longer "within" a switch instance, so
      // we can temporarily enforce this to ensure that any embedded case
      // statements are not emitted.
      SwitchInsn = nullptr;

      // Okay, we can dead code eliminate everything except this case.  Emit the
      // specified series of statements and we're good.
      for (unsigned i = 0, e = CaseStmts.size(); i != e; ++i)
        genStmt(CaseStmts[i]);

      // Now we want to restore the saved switch instance so that nested
      // switches continue to function properly
      SwitchInsn = SavedSwitchInsn;

      return;
    }
  }

  JumpDest SwitchExit = getJumpDestInCurrentScope("sw.epilog");

  RunCleanupsScope ConditionScope(*this);

  if (S.getInit())
    genStmt(S.getInit());

  if (S.getConditionVariable())
    genDecl(*S.getConditionVariable());
  llvm::Value *CondV = genScalarExpr(S.getCond());

  // Default block doubles as the fall-through target for case range tests.
  llvm::BasicBlock *DefaultBlock = createBasicBlock("sw.default");
  SwitchInsn = Builder.CreateSwitch(CondV, DefaultBlock);

  CaseRangeBlock = DefaultBlock;

  // Clear the insertion point to indicate we are in unreachable code.
  Builder.ClearInsertionPoint();

  // All break statements jump to NextBlock. If BreakContinueStack is non-empty
  // then reuse last ContinueBlock.
  JumpDest OuterContinue;
  if (!BreakContinueStack.empty())
    OuterContinue = BreakContinueStack.back().ContinueBlock;

  BreakContinueStack.push_back(BreakContinue(SwitchExit, OuterContinue));

  genStmt(S.getBody());

  BreakContinueStack.pop_back();

  // Update the default block in case explicit case range tests have
  // been chained on top.
  SwitchInsn->setDefaultDest(CaseRangeBlock);

  // If a default was never emitted:
  if (!DefaultBlock->getParent()) {
    // If we have cleanups, emit the default block so that there's a
    // place to jump through the cleanups from.
    if (ConditionScope.requiresCleanups()) {
      genBlock(DefaultBlock);

      // Otherwise, just forward the default block to the switch end.
    } else {
      DefaultBlock->replaceAllUsesWith(SwitchExit.getBlock());
      delete DefaultBlock;
    }
  }

  ConditionScope.ForceCleanup();

  genBlock(SwitchExit.getBlock(), true);

  // If the switch has a condition wrapped by __builtin_unpredictable,
  // create metadata that specifies that the switch is unpredictable.
  // Don't bother when nothing downstream (neither the backend nor an LTO
  // link) would use that metadata.
  auto *Call = dyn_cast<CallExpr>(S.getCond());
  if (Call && ME.getCodeGenOpts().hasDownstreamOptimization()) {
    auto *FD = dyn_cast_or_null<FunctionDecl>(Call->getCalleeDecl());
    if (FD && FD->getBuiltinID() == Builtin::BI__builtin_unpredictable) {
      llvm::MDBuilder MDHelper(getLLVMContext());
      SwitchInsn->setMetadata(llvm::LLVMContext::MD_unpredictable,
                              MDHelper.createUnpredictable());
    }
  }

  SwitchInsn = SavedSwitchInsn;
  CaseRangeBlock = SavedCRBlock;
}

