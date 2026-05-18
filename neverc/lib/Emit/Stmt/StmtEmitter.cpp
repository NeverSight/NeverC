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

__attribute__((hot)) void
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

__attribute__((hot)) bool
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

__attribute__((hot)) Address FunctionEmitter::genCompoundStmt(
    const CompoundStmt &S, bool GetLast, AggValueSlot AggSlot) {
#if ENABLE_CRASH_OVERRIDES
  PrettyStackTraceLoc CrashInfo(
      getContext().getSourceManager(), S.getLBracLoc(),
      "LLVM IR generation of compound statement ('{}')");
#endif

  LexicalScope Scope(*this, S.getSourceRange());

  return genCompoundStmtWithoutScope(S, GetLast, AggSlot);
}

__attribute__((hot)) Address FunctionEmitter::genCompoundStmtWithoutScope(
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

__attribute__((hot)) void FunctionEmitter::genIfStmt(const IfStmt &S) {
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

__attribute__((hot)) void
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

__attribute__((hot)) void
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

__attribute__((hot)) void FunctionEmitter::genReturnStmt(const ReturnStmt &S) {

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

__attribute__((hot)) void FunctionEmitter::genDeclStmt(const DeclStmt &S) {
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

__attribute__((hot)) void FunctionEmitter::genSwitchStmt(const SwitchStmt &S) {
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
  // Don't bother if not optimizing because that metadata would not be used.
  auto *Call = dyn_cast<CallExpr>(S.getCond());
  if (Call && ME.getCodeGenOpts().OptimizationLevel != 0) {
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

namespace {

std::string simplifyConstraint(
    const char *Constraint, const TargetInfo &Target,
    llvm::SmallVectorImpl<TargetInfo::ConstraintInfo> *OutCons = nullptr) {
  std::string Result;

  while (*Constraint) {
    switch (*Constraint) {
    default:
      Result += Target.convertConstraint(Constraint);
      break;
    // Ignore these
    case '*':
    case '?':
    case '!':
    case '=': // Will see this and the following in mult-alt constraints.
    case '+':
      break;
    case '#': // Ignore the rest of the constraint alternative.
      while (Constraint[1] && Constraint[1] != ',')
        Constraint++;
      break;
    case '&':
    case '%':
      Result += *Constraint;
      while (Constraint[1] && Constraint[1] == *Constraint)
        Constraint++;
      break;
    case ',':
      Result += "|";
      break;
    case 'g':
      Result += "imr";
      break;
    case '[': {
      assert(OutCons &&
             "Must pass output names to constraints with a symbolic name");
      unsigned Index;
      bool result = Target.resolveSymbolicName(Constraint, *OutCons, Index);
      assert(result && "Could not resolve symbolic name");
      (void)result;
      Result += llvm::utostr(Index);
      break;
    }
    }

    Constraint++;
  }

  return Result;
}

std::string addVariableConstraints(const std::string &Constraint,
                                   const Expr &AsmExpr,
                                   const TargetInfo &Target, ModuleEmitter &ME,
                                   const AsmStmt &Stmt, const bool EarlyClobber,
                                   std::string *GCCReg = nullptr) {
  const DeclRefExpr *AsmDeclRef = dyn_cast<DeclRefExpr>(&AsmExpr);
  if (!AsmDeclRef)
    return Constraint;
  const ValueDecl &Value = *AsmDeclRef->getDecl();
  const VarDecl *Variable = dyn_cast<VarDecl>(&Value);
  if (!Variable)
    return Constraint;
  if (Variable->getStorageClass() != SC_Register)
    return Constraint;
  AsmLabelAttr *Attr = Variable->getAttr<AsmLabelAttr>();
  if (!Attr)
    return Constraint;
  llvm::StringRef Register = Attr->getLabel();
  assert(Target.isValidGCCRegisterName(Register));
  // We're using validateOutputConstraint here because we only care if
  // this is a register constraint.
  TargetInfo::ConstraintInfo Info(Constraint, "");
  if (Target.validateOutputConstraint(Info) && !Info.allowsRegister()) {
    ME.errorUnsupported(&Stmt, "__asm__");
    return Constraint;
  }
  // Canonicalize the register here before returning it.
  Register = Target.getNormalizedGCCRegisterName(Register);
  if (GCCReg != nullptr)
    *GCCReg = Register.str();
  return (EarlyClobber ? "&{" : "{") + Register.str() + "}";
}

} // namespace

std::pair<llvm::Value *, llvm::Type *> FunctionEmitter::genAsmInputLValue(
    const TargetInfo::ConstraintInfo &Info, LValue InputValue,
    QualType InputType, std::string &ConstraintStr, SourceLocation Loc) {
  if (Info.allowsRegister() || !Info.allowsMemory()) {
    if (FunctionEmitter::hasScalarEvaluationKind(InputType))
      return {genLoadOfLValue(InputValue, Loc).getScalarVal(), nullptr};

    llvm::Type *Ty = convertType(InputType);
    uint64_t Size = ME.getDataLayout().getTypeSizeInBits(Ty);
    if ((Size <= 64 && llvm::isPowerOf2_64(Size)) ||
        getTargetHooks().isScalarizableAsmOperand(*this, Ty)) {
      Ty = llvm::IntegerType::get(getLLVMContext(), Size);

      return {
          Builder.CreateLoad(InputValue.getAddress(*this).withElementType(Ty)),
          nullptr};
    }
  }

  Address Addr = InputValue.getAddress(*this);
  ConstraintStr += '*';
  return {Addr.getPointer(), Addr.getElementType()};
}

std::pair<llvm::Value *, llvm::Type *>
FunctionEmitter::genAsmInput(const TargetInfo::ConstraintInfo &Info,
                             const Expr *InputExpr,
                             std::string &ConstraintStr) {
  // If this can't be a register or memory, i.e., has to be a constant
  // (immediate or symbolic), try to emit it as such.
  if (!Info.allowsRegister() && !Info.allowsMemory()) {
    if (Info.requiresImmediateConstant()) {
      Expr::EvalResult EVResult;
      InputExpr->EvaluateAsRValue(EVResult, getContext(), true);

      llvm::APSInt IntResult;
      if (EVResult.Val.toIntegralConstant(IntResult, InputExpr->getType(),
                                          getContext()))
        return {llvm::ConstantInt::get(getLLVMContext(), IntResult), nullptr};
    }

    Expr::EvalResult Result;
    if (InputExpr->EvaluateAsInt(Result, getContext()))
      return {llvm::ConstantInt::get(getLLVMContext(), Result.Val.getInt()),
              nullptr};
  }

  if (Info.allowsRegister() || !Info.allowsMemory())
    if (FunctionEmitter::hasScalarEvaluationKind(InputExpr->getType()))
      return {genScalarExpr(InputExpr), nullptr};
  InputExpr = InputExpr->IgnoreParenNoopCasts(getContext());
  LValue Dest = genLValue(InputExpr);
  return genAsmInputLValue(Info, Dest, InputExpr->getType(), ConstraintStr,
                           InputExpr->getExprLoc());
}

namespace {

llvm::MDNode *getAsmSrcLocInfo(const StringLiteral *Str, FunctionEmitter &FE) {
  llvm::SmallVector<llvm::Metadata *, 8> Locs;
  Locs.push_back(llvm::ConstantAsMetadata::get(
      llvm::ConstantInt::get(FE.Int64Ty, Str->getBeginLoc().getRawEncoding())));
  llvm::StringRef StrVal = Str->getString();
  if (!StrVal.empty()) {
    const SourceManager &SM = FE.ME.getContext().getSourceManager();
    const LangOptions &LangOpts = FE.ME.getLangOpts();
    unsigned StartToken = 0;
    unsigned ByteOffset = 0;

    for (unsigned i = 0, e = StrVal.size() - 1; i != e; ++i) {
      if (StrVal[i] != '\n')
        continue;
      SourceLocation LineLoc = Str->getLocationOfByte(
          i + 1, SM, LangOpts, FE.getTarget(), &StartToken, &ByteOffset);
      Locs.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(FE.Int64Ty, LineLoc.getRawEncoding())));
    }
  }

  return llvm::MDNode::get(FE.getLLVMContext(), Locs);
}

void updateAsmCallInst(llvm::CallBase &Result, bool HasSideEffect,
                       bool HasUnwindClobber, bool ReadOnly, bool ReadNone,
                       bool NoMerge, const AsmStmt &S,
                       const std::vector<llvm::Type *> &ResultRegTypes,
                       const std::vector<llvm::Type *> &ArgElemTypes,
                       FunctionEmitter &FE,
                       std::vector<llvm::Value *> &RegResults) {
  if (!HasUnwindClobber)
    Result.addFnAttr(llvm::Attribute::NoUnwind);

  if (NoMerge)
    Result.addFnAttr(llvm::Attribute::NoMerge);
  // Attach readnone and readonly attributes.
  if (!HasSideEffect) {
    if (ReadNone)
      Result.setDoesNotAccessMemory();
    else if (ReadOnly)
      Result.setOnlyReadsMemory();
  }

  for (auto Pair : llvm::enumerate(ArgElemTypes)) {
    if (Pair.value()) {
      auto Attr = llvm::Attribute::get(
          FE.getLLVMContext(), llvm::Attribute::ElementType, Pair.value());
      Result.addParamAttr(Pair.index(), Attr);
    }
  }

  // Slap the source location of the inline asm into a !srcloc metadata on the
  // call.
  if (const auto *gccAsmStmt = dyn_cast<GCCAsmStmt>(&S))
    Result.setMetadata("srcloc",
                       getAsmSrcLocInfo(gccAsmStmt->getAsmString(), FE));
  else {
    // At least put the line number on MS inline asm blobs.
    llvm::Constant *Loc =
        llvm::ConstantInt::get(FE.Int64Ty, S.getAsmLoc().getRawEncoding());
    Result.setMetadata("srcloc",
                       llvm::MDNode::get(FE.getLLVMContext(),
                                         llvm::ConstantAsMetadata::get(Loc)));
  }

  // Extract all of the register value results from the asm.
  if (ResultRegTypes.size() == 1) {
    RegResults.push_back(&Result);
  } else {
    for (unsigned i = 0, e = ResultRegTypes.size(); i != e; ++i) {
      llvm::Value *Tmp = FE.Builder.CreateExtractValue(&Result, i, "asmresult");
      RegResults.push_back(Tmp);
    }
  }
}

void genAsmStores(FunctionEmitter &FE, const AsmStmt &S,
                  const llvm::ArrayRef<llvm::Value *> RegResults,
                  const llvm::ArrayRef<llvm::Type *> ResultRegTypes,
                  const llvm::ArrayRef<llvm::Type *> ResultTruncRegTypes,
                  const llvm::ArrayRef<LValue> ResultRegDests,
                  const llvm::ArrayRef<QualType> ResultRegQualTys,
                  const llvm::BitVector &ResultTypeRequiresCast,
                  const llvm::BitVector &ResultRegIsFlagReg) {
  CGBuilderTy &Builder = FE.Builder;
  ModuleEmitter &ME = FE.ME;
  llvm::LLVMContext &CTX = FE.getLLVMContext();

  assert(RegResults.size() == ResultRegTypes.size());
  assert(RegResults.size() == ResultTruncRegTypes.size());
  assert(RegResults.size() == ResultRegDests.size());
  // ResultRegDests can be also populated by addReturnRegisterOutputs() above,
  // in which case its size may grow.
  assert(ResultTypeRequiresCast.size() <= ResultRegDests.size());
  assert(ResultRegIsFlagReg.size() <= ResultRegDests.size());

  for (unsigned i = 0, e = RegResults.size(); i != e; ++i) {
    llvm::Value *Tmp = RegResults[i];
    llvm::Type *TruncTy = ResultTruncRegTypes[i];

    if ((i < ResultRegIsFlagReg.size()) && ResultRegIsFlagReg[i]) {
      // Target must guarantee the Value `Tmp` here is lowered to a boolean
      // value.
      llvm::Constant *Two = llvm::ConstantInt::get(Tmp->getType(), 2);
      llvm::Value *IsBooleanValue =
          Builder.CreateCmp(llvm::CmpInst::ICMP_ULT, Tmp, Two);
      llvm::Function *FnAssume = ME.getIntrinsic(llvm::Intrinsic::assume);
      Builder.CreateCall(FnAssume, IsBooleanValue);
    }

    // If the result type of the LLVM IR asm doesn't match the result type of
    // the expression, do the conversion.
    if (ResultRegTypes[i] != TruncTy) {

      // Truncate the integer result to the right size, note that TruncTy can be
      // a pointer.
      if (TruncTy->isFloatingPointTy())
        Tmp = Builder.CreateFPTrunc(Tmp, TruncTy);
      else if (TruncTy->isPointerTy() && Tmp->getType()->isIntegerTy()) {
        uint64_t ResSize = ME.getDataLayout().getTypeSizeInBits(TruncTy);
        Tmp = Builder.CreateTrunc(
            Tmp, llvm::IntegerType::get(CTX, (unsigned)ResSize));
        Tmp = Builder.CreateIntToPtr(Tmp, TruncTy);
      } else if (Tmp->getType()->isPointerTy() && TruncTy->isIntegerTy()) {
        uint64_t TmpSize = ME.getDataLayout().getTypeSizeInBits(Tmp->getType());
        Tmp = Builder.CreatePtrToInt(
            Tmp, llvm::IntegerType::get(CTX, (unsigned)TmpSize));
        Tmp = Builder.CreateTrunc(Tmp, TruncTy);
      } else if (TruncTy->isIntegerTy()) {
        Tmp = Builder.CreateZExtOrTrunc(Tmp, TruncTy);
      } else if (TruncTy->isVectorTy()) {
        Tmp = Builder.CreateBitCast(Tmp, TruncTy);
      }
    }

    LValue Dest = ResultRegDests[i];
    // ResultTypeRequiresCast elements correspond to the first
    // ResultTypeRequiresCast.size() elements of RegResults.
    if ((i < ResultTypeRequiresCast.size()) && ResultTypeRequiresCast[i]) {
      unsigned Size = FE.getContext().getTypeSize(ResultRegQualTys[i]);
      Address A = Dest.getAddress(FE).withElementType(ResultRegTypes[i]);
      if (FE.getTargetHooks().isScalarizableAsmOperand(FE, TruncTy)) {
        Builder.CreateStore(Tmp, A);
        continue;
      }

      QualType Ty =
          FE.getContext().getIntTypeForBitwidth(Size, /*Signed=*/false);
      if (Ty.isNull()) {
        const Expr *OutExpr = S.getOutputExpr(i);
        ME.getDiags().Report(OutExpr->getExprLoc(),
                             diag::err_store_value_to_reg);
        return;
      }
      Dest = FE.makeAddrLValue(A, Ty);
    }
    FE.genStoreThroughLValue(RValue::get(Tmp), Dest);
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Inline assembly
// ===----------------------------------------------------------------------===

void FunctionEmitter::genAsmStmt(const AsmStmt &S) {
  // Pop all cleanup blocks at the end of the asm statement.
  FunctionEmitter::RunCleanupsScope Cleanups(*this);

  // Assemble the final asm string.
  std::string AsmString = S.generateAsmString(getContext());

  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;
  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> InputConstraintInfos;

  for (unsigned i = 0, e = S.getNumOutputs(); i != e; i++) {
    llvm::StringRef Name;
    if (const GCCAsmStmt *GAS = dyn_cast<GCCAsmStmt>(&S))
      Name = GAS->getOutputName(i);
    TargetInfo::ConstraintInfo Info(S.getOutputConstraint(i), Name);
    bool IsValid = getTarget().validateOutputConstraint(Info);
    (void)IsValid;
    assert(IsValid && "Failed to parse output constraint");
    OutputConstraintInfos.push_back(Info);
  }

  for (unsigned i = 0, e = S.getNumInputs(); i != e; i++) {
    llvm::StringRef Name;
    if (const GCCAsmStmt *GAS = dyn_cast<GCCAsmStmt>(&S))
      Name = GAS->getInputName(i);
    TargetInfo::ConstraintInfo Info(S.getInputConstraint(i), Name);
    assert(getTarget().validateInputConstraint(OutputConstraintInfos, Info) &&
           "Failed to parse input constraint");
    InputConstraintInfos.push_back(Info);
  }

  std::string Constraints;

  std::vector<LValue> ResultRegDests;
  std::vector<QualType> ResultRegQualTys;
  std::vector<llvm::Type *> ResultRegTypes;
  std::vector<llvm::Type *> ResultTruncRegTypes;
  std::vector<llvm::Type *> ArgTypes;
  std::vector<llvm::Type *> ArgElemTypes;
  std::vector<llvm::Value *> Args;
  llvm::BitVector ResultTypeRequiresCast;
  llvm::BitVector ResultRegIsFlagReg;

  // Keep track of inout constraints.
  std::string InOutConstraints;
  std::vector<llvm::Value *> InOutArgs;
  std::vector<llvm::Type *> InOutArgTypes;
  std::vector<llvm::Type *> InOutArgElemTypes;

  // Keep track of out constraints for tied input operand.
  std::vector<std::string> OutputConstraints;

  // Keep track of defined physregs.
  llvm::SmallSet<std::string, 8> PhysRegOutputs;

  // An inline asm can be marked readonly if it meets the following conditions:
  //  - it doesn't have any sideeffects
  //  - it doesn't clobber memory
  //  - it doesn't return a value by-reference
  // It can be marked readnone if it doesn't have any input memory constraints
  // in addition to meeting the conditions listed above.
  bool ReadOnly = true, ReadNone = true;

  for (unsigned i = 0, e = S.getNumOutputs(); i != e; i++) {
    TargetInfo::ConstraintInfo &Info = OutputConstraintInfos[i];

    // Simplify the output constraint.
    std::string OutputConstraint(S.getOutputConstraint(i));
    OutputConstraint = simplifyConstraint(OutputConstraint.c_str() + 1,
                                          getTarget(), &OutputConstraintInfos);

    const Expr *OutExpr = S.getOutputExpr(i);
    OutExpr = OutExpr->IgnoreParenNoopCasts(getContext());

    std::string GCCReg;
    OutputConstraint =
        addVariableConstraints(OutputConstraint, *OutExpr, getTarget(), ME, S,
                               Info.earlyClobber(), &GCCReg);
    // Give an error on multiple outputs to same physreg.
    if (!GCCReg.empty() && !PhysRegOutputs.insert(GCCReg).second)
      ME.error(S.getAsmLoc(), "multiple outputs to hard register: " + GCCReg);

    OutputConstraints.push_back(OutputConstraint);
    LValue Dest = genLValue(OutExpr);
    if (!Constraints.empty())
      Constraints += ',';

    // If this is a register output, then make the inline asm return it
    // by-value.  If this is a memory result, return the value by-reference.
    QualType QTy = OutExpr->getType();
    const bool IsScalarOrAggregate =
        hasScalarEvaluationKind(QTy) || hasAggregateEvaluationKind(QTy);
    if (!Info.allowsMemory() && IsScalarOrAggregate) {

      Constraints += "=" + OutputConstraint;
      ResultRegQualTys.push_back(QTy);
      ResultRegDests.push_back(Dest);

      bool IsFlagReg = llvm::StringRef(OutputConstraint).starts_with("{@cc");
      ResultRegIsFlagReg.push_back(IsFlagReg);

      llvm::Type *Ty = convertTypeForMem(QTy);
      const bool RequiresCast =
          Info.allowsRegister() &&
          (getTargetHooks().isScalarizableAsmOperand(*this, Ty) ||
           Ty->isAggregateType());

      ResultTruncRegTypes.push_back(Ty);
      ResultTypeRequiresCast.push_back(RequiresCast);

      if (RequiresCast) {
        unsigned Size = getContext().getTypeSize(QTy);
        Ty = llvm::IntegerType::get(getLLVMContext(), Size);
      }
      ResultRegTypes.push_back(Ty);
      // If this output is tied to an input, and if the input is larger, then
      // we need to set the actual result type of the inline asm node to be the
      // same as the input type.
      if (Info.hasMatchingInput()) {
        unsigned InputNo;
        for (InputNo = 0; InputNo != S.getNumInputs(); ++InputNo) {
          TargetInfo::ConstraintInfo &Input = InputConstraintInfos[InputNo];
          if (Input.hasTiedOperand() && Input.getTiedOperand() == i)
            break;
        }
        assert(InputNo != S.getNumInputs() && "Didn't find matching input!");

        QualType InputTy = S.getInputExpr(InputNo)->getType();
        QualType OutputType = OutExpr->getType();

        uint64_t InputSize = getContext().getTypeSize(InputTy);
        if (getContext().getTypeSize(OutputType) < InputSize) {
          // Form the asm to return the value as a larger integer or fp type.
          ResultRegTypes.back() = convertType(InputTy);
        }
      }
      if (llvm::Type *AdjTy = getTargetHooks().adjustInlineAsmType(
              *this, OutputConstraint, ResultRegTypes.back()))
        ResultRegTypes.back() = AdjTy;
      else {
        ME.getDiags().Report(S.getAsmLoc(), diag::err_asm_invalid_type_in_input)
            << OutExpr->getType() << OutputConstraint;
      }

      if (auto *VT = dyn_cast<llvm::VectorType>(ResultRegTypes.back()))
        LargestVectorWidth =
            std::max((uint64_t)LargestVectorWidth,
                     VT->getPrimitiveSizeInBits().getKnownMinValue());
    } else {
      Address DestAddr = Dest.getAddress(*this);
      // Matrix types in memory are represented by arrays, but accessed through
      // vector pointers, with the alignment specified on the access operation.
      // For inline assembly, update pointer arguments to use vector pointers.
      // Otherwise there will be a mis-match if the matrix is also an
      // input-argument which is represented as vector.
      if (isa<MatrixType>(OutExpr->getType().getCanonicalType()))
        DestAddr = DestAddr.withElementType(convertType(OutExpr->getType()));

      ArgTypes.push_back(DestAddr.getType());
      ArgElemTypes.push_back(DestAddr.getElementType());
      Args.push_back(DestAddr.getPointer());
      Constraints += "=*";
      Constraints += OutputConstraint;
      ReadOnly = ReadNone = false;
    }

    if (Info.isReadWrite()) {
      InOutConstraints += ',';

      const Expr *InputExpr = S.getOutputExpr(i);
      llvm::Value *Arg;
      llvm::Type *ArgElemType;
      std::tie(Arg, ArgElemType) =
          genAsmInputLValue(Info, Dest, InputExpr->getType(), InOutConstraints,
                            InputExpr->getExprLoc());

      if (llvm::Type *AdjTy = getTargetHooks().adjustInlineAsmType(
              *this, OutputConstraint, Arg->getType()))
        Arg = Builder.CreateBitCast(Arg, AdjTy);

      if (auto *VT = dyn_cast<llvm::VectorType>(Arg->getType()))
        LargestVectorWidth =
            std::max((uint64_t)LargestVectorWidth,
                     VT->getPrimitiveSizeInBits().getKnownMinValue());
      // Only tie earlyclobber physregs.
      if (Info.allowsRegister() && (GCCReg.empty() || Info.earlyClobber()))
        InOutConstraints += llvm::utostr(i);
      else
        InOutConstraints += OutputConstraint;

      InOutArgTypes.push_back(Arg->getType());
      InOutArgElemTypes.push_back(ArgElemType);
      InOutArgs.push_back(Arg);
    }
  }

  // If this is a Microsoft-style asm blob, store the return registers (RAX:RDX)
  // to the return value slot. Only do this when returning in registers.
  if (isa<MSAsmStmt>(&S)) {
    const ABIArgInfo &RetAI = CurFnInfo->getReturnInfo();
    if (RetAI.isDirect() || RetAI.isExtend()) {
      // Make a fake lvalue for the return value slot.
      LValue ReturnSlot = makeAddrLValueWithoutTBAA(ReturnValue, FnRetTy);
      ME.getTargetCodeGenInfo().addReturnRegisterOutputs(
          *this, ReturnSlot, Constraints, ResultRegTypes, ResultTruncRegTypes,
          ResultRegDests, AsmString, S.getNumOutputs());
      SawAsmBlock = true;
    }
  }

  for (unsigned i = 0, e = S.getNumInputs(); i != e; i++) {
    const Expr *InputExpr = S.getInputExpr(i);

    TargetInfo::ConstraintInfo &Info = InputConstraintInfos[i];

    if (Info.allowsMemory())
      ReadNone = false;

    if (!Constraints.empty())
      Constraints += ',';

    // Simplify the input constraint.
    std::string InputConstraint(S.getInputConstraint(i));
    InputConstraint = simplifyConstraint(InputConstraint.c_str(), getTarget(),
                                         &OutputConstraintInfos);

    InputConstraint = addVariableConstraints(
        InputConstraint, *InputExpr->IgnoreParenNoopCasts(getContext()),
        getTarget(), ME, S, false /* No EarlyClobber */);

    std::string ReplaceConstraint(InputConstraint);
    llvm::Value *Arg;
    llvm::Type *ArgElemType;
    std::tie(Arg, ArgElemType) = genAsmInput(Info, InputExpr, Constraints);

    // If this input argument is tied to a larger output result, extend the
    // input to be the same size as the output.  The LLVM backend wants to see
    // the input and output of a matching constraint be the same size.  Note
    // that GCC does not define what the top bits are here.  We use zext because
    // that is usually cheaper, but LLVM IR should really get an anyext someday.
    if (Info.hasTiedOperand()) {
      unsigned Output = Info.getTiedOperand();
      QualType OutputType = S.getOutputExpr(Output)->getType();
      QualType InputTy = InputExpr->getType();

      if (getContext().getTypeSize(OutputType) >
          getContext().getTypeSize(InputTy)) {
        // Use ptrtoint as appropriate so that we can do our extension.
        if (isa<llvm::PointerType>(Arg->getType()))
          Arg = Builder.CreatePtrToInt(Arg, IntPtrTy);
        llvm::Type *OutputTy = convertType(OutputType);
        if (isa<llvm::IntegerType>(OutputTy))
          Arg = Builder.CreateZExt(Arg, OutputTy);
        else if (isa<llvm::PointerType>(OutputTy))
          Arg = Builder.CreateZExt(Arg, IntPtrTy);
        else if (OutputTy->isFloatingPointTy())
          Arg = Builder.CreateFPExt(Arg, OutputTy);
      }
      // Deal with the tied operands' constraint code in adjustInlineAsmType.
      ReplaceConstraint = OutputConstraints[Output];
    }
    if (llvm::Type *AdjTy = getTargetHooks().adjustInlineAsmType(
            *this, ReplaceConstraint, Arg->getType()))
      Arg = Builder.CreateBitCast(Arg, AdjTy);
    else
      ME.getDiags().Report(S.getAsmLoc(), diag::err_asm_invalid_type_in_input)
          << InputExpr->getType() << InputConstraint;

    if (auto *VT = dyn_cast<llvm::VectorType>(Arg->getType()))
      LargestVectorWidth =
          std::max((uint64_t)LargestVectorWidth,
                   VT->getPrimitiveSizeInBits().getKnownMinValue());

    ArgTypes.push_back(Arg->getType());
    ArgElemTypes.push_back(ArgElemType);
    Args.push_back(Arg);
    Constraints += InputConstraint;
  }

  // Append the "input" part of inout constraints.
  for (unsigned i = 0, e = InOutArgs.size(); i != e; i++) {
    ArgTypes.push_back(InOutArgTypes[i]);
    ArgElemTypes.push_back(InOutArgElemTypes[i]);
    Args.push_back(InOutArgs[i]);
  }
  Constraints += InOutConstraints;

  // Labels
  llvm::SmallVector<llvm::BasicBlock *, 16> Transfer;
  llvm::BasicBlock *Fallthrough = nullptr;
  bool IsGCCAsmGoto = false;
  if (const auto *GS = dyn_cast<GCCAsmStmt>(&S)) {
    IsGCCAsmGoto = GS->isAsmGoto();
    if (IsGCCAsmGoto) {
      for (const auto *E : GS->labels()) {
        JumpDest Dest = getJumpDestForLabel(E->getLabel());
        Transfer.push_back(Dest.getBlock());
        if (!Constraints.empty())
          Constraints += ',';
        Constraints += "!i";
      }
      Fallthrough = createBasicBlock("asm.fallthrough");
    }
  }

  bool HasUnwindClobber = false;

  // Clobbers
  for (unsigned i = 0, e = S.getNumClobbers(); i != e; i++) {
    llvm::StringRef Clobber = S.getClobber(i);

    if (Clobber == "memory")
      ReadOnly = ReadNone = false;
    else if (Clobber == "unwind") {
      HasUnwindClobber = true;
      continue;
    } else if (Clobber != "cc") {
      Clobber = getTarget().getNormalizedGCCRegisterName(Clobber);
      if (ME.getCodeGenOpts().StackClashProtector &&
          getTarget().isSPRegName(Clobber)) {
        ME.getDiags().Report(S.getAsmLoc(),
                             diag::warn_stack_clash_protection_inline_asm);
      }
    }

    if (isa<MSAsmStmt>(&S)) {
      if (Clobber == "eax" || Clobber == "edx") {
        if (Constraints.find("=&A") != std::string::npos)
          continue;
        std::string::size_type position1 =
            Constraints.find("={" + Clobber.str() + "}");
        if (position1 != std::string::npos) {
          Constraints.insert(position1 + 1, "&");
          continue;
        }
        std::string::size_type position2 = Constraints.find("=A");
        if (position2 != std::string::npos) {
          Constraints.insert(position2 + 1, "&");
          continue;
        }
      }
    }
    if (!Constraints.empty())
      Constraints += ',';

    Constraints += "~{";
    Constraints += Clobber;
    Constraints += '}';
  }

  assert(!(HasUnwindClobber && IsGCCAsmGoto) &&
         "unwind clobber can't be used with asm goto");

  std::string_view MachineClobbers = getTarget().getClobbers();
  if (!MachineClobbers.empty()) {
    if (!Constraints.empty())
      Constraints += ',';
    Constraints += MachineClobbers;
  }

  llvm::Type *ResultType;
  if (ResultRegTypes.empty())
    ResultType = VoidTy;
  else if (ResultRegTypes.size() == 1)
    ResultType = ResultRegTypes[0];
  else
    ResultType = llvm::StructType::get(getLLVMContext(), ResultRegTypes);

  llvm::FunctionType *FTy =
      llvm::FunctionType::get(ResultType, ArgTypes, false);

  bool HasSideEffect = S.isVolatile() || S.getNumOutputs() == 0;

  llvm::InlineAsm::AsmDialect GnuAsmDialect =
      ME.getCodeGenOpts().getInlineAsmDialect() == CodeGenOptions::IAD_ATT
          ? llvm::InlineAsm::AD_ATT
          : llvm::InlineAsm::AD_Intel;
  llvm::InlineAsm::AsmDialect AsmDialect =
      isa<MSAsmStmt>(&S) ? llvm::InlineAsm::AD_Intel : GnuAsmDialect;

  llvm::InlineAsm *IA = llvm::InlineAsm::get(
      FTy, AsmString, Constraints, HasSideEffect,
      /* IsAlignStack */ false, AsmDialect, HasUnwindClobber);
  std::vector<llvm::Value *> RegResults;
  llvm::CallBrInst *CBR;
  llvm::DenseMap<llvm::BasicBlock *, llvm::SmallVector<llvm::Value *, 4>>
      CBRRegResults;
  if (IsGCCAsmGoto) {
    CBR = Builder.CreateCallBr(IA, Fallthrough, Transfer, Args);
    genBlock(Fallthrough);
    updateAsmCallInst(*CBR, HasSideEffect, false, ReadOnly, ReadNone,
                      InNoMergeAttributedStmt, S, ResultRegTypes, ArgElemTypes,
                      *this, RegResults);
    // Because we are emitting code top to bottom, we don't have enough
    // information at this point to know precisely whether we have a critical
    // edge. If we have outputs, split all indirect destinations.
    if (!RegResults.empty()) {
      unsigned i = 0;
      for (llvm::BasicBlock *Dest : CBR->getIndirectDests()) {
        llvm::Twine SynthName = Dest->getName() + ".split";
        llvm::BasicBlock *SynthBB = createBasicBlock(SynthName);
        llvm::IRBuilderBase::InsertPointGuard IPG(Builder);
        Builder.SetInsertPoint(SynthBB);

        if (ResultRegTypes.size() == 1) {
          CBRRegResults[SynthBB].push_back(CBR);
        } else {
          for (unsigned j = 0, e = ResultRegTypes.size(); j != e; ++j) {
            llvm::Value *Tmp = Builder.CreateExtractValue(CBR, j, "asmresult");
            CBRRegResults[SynthBB].push_back(Tmp);
          }
        }

        genBranch(Dest);
        genBlock(SynthBB);
        CBR->setIndirectDest(i++, SynthBB);
      }
    }
  } else if (HasUnwindClobber) {
    llvm::CallBase *Result = genCallOrInvoke(IA, Args, "");
    updateAsmCallInst(*Result, HasSideEffect, true, ReadOnly, ReadNone,
                      InNoMergeAttributedStmt, S, ResultRegTypes, ArgElemTypes,
                      *this, RegResults);
  } else {
    llvm::CallInst *Result =
        Builder.CreateCall(IA, Args, getBundlesForFunclet(IA));
    updateAsmCallInst(*Result, HasSideEffect, false, ReadOnly, ReadNone,
                      InNoMergeAttributedStmt, S, ResultRegTypes, ArgElemTypes,
                      *this, RegResults);
  }

  genAsmStores(*this, S, RegResults, ResultRegTypes, ResultTruncRegTypes,
               ResultRegDests, ResultRegQualTys, ResultTypeRequiresCast,
               ResultRegIsFlagReg);

  // If this is an asm goto with outputs, repeat genAsmStores, but with a
  // different insertion point; one for each indirect destination and with
  // CBRRegResults rather than RegResults.
  if (IsGCCAsmGoto && !CBRRegResults.empty()) {
    for (llvm::BasicBlock *Succ : CBR->getIndirectDests()) {
      llvm::IRBuilderBase::InsertPointGuard IPG(Builder);
      Builder.SetInsertPoint(Succ, --(Succ->end()));
      genAsmStores(*this, S, CBRRegResults[Succ], ResultRegTypes,
                   ResultTruncRegTypes, ResultRegDests, ResultRegQualTys,
                   ResultTypeRequiresCast, ResultRegIsFlagReg);
    }
  }
}
