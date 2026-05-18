#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// EH personality & runtime helpers
// ===----------------------------------------------------------------------===

namespace {
llvm::FunctionCallee getSehTryBeginFn(ModuleEmitter &ME) {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(ME.VoidTy, /*isVarArg=*/false);
  return ME.createRuntimeFunction(FTy, "llvm.seh.try.begin");
}

llvm::FunctionCallee getSehTryEndFn(ModuleEmitter &ME) {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(ME.VoidTy, /*isVarArg=*/false);
  return ME.createRuntimeFunction(FTy, "llvm.seh.try.end");
}
} // namespace

llvm::FunctionCallee ModuleEmitter::getTerminateFn() {
  // void __terminate();

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, /*isVarArg=*/false);

  llvm::StringRef name;

  name = "abort";
  return createRuntimeFunction(FTy, name);
}

const EHPersonality EHPersonality::GNU_C = {"__gcc_personality_v0"};
const EHPersonality EHPersonality::GNU_C_SEH = {"__gcc_personality_seh0"};
const EHPersonality EHPersonality::MSVC_C_specific_handler = {
    "__C_specific_handler"};

namespace {
const EHPersonality &getCPersonality(const TargetInfo &Target,
                                     const LangOptions &L) {
  const llvm::Triple &T = Target.getTriple();
  if (T.isWindowsMSVCEnvironment())
    return EHPersonality::MSVC_C_specific_handler;
  if (L.hasDWARFExceptions())
    return EHPersonality::GNU_C;
  if (L.hasSEHExceptions())
    return EHPersonality::GNU_C_SEH;
  return EHPersonality::GNU_C;
}

const EHPersonality &getSEHPersonalityMSVC(const llvm::Triple &T) {
  return EHPersonality::MSVC_C_specific_handler;
}
} // namespace

const EHPersonality &EHPersonality::get(ModuleEmitter &ME,
                                        const FunctionDecl *FD) {
  const llvm::Triple &T = ME.getTarget().getTriple();
  const LangOptions &L = ME.getLangOpts();
  const TargetInfo &Target = ME.getTarget();

  // Functions using SEH get an SEH personality.
  if (FD && FD->usesSEHTry())
    return getSEHPersonalityMSVC(T);

  return getCPersonality(Target, L);
}

const EHPersonality &EHPersonality::get(FunctionEmitter &FE) {
  const auto *FD = FE.CurCodeDecl;
  // For outlined finallys and filters, use the SEH personality in case they
  // contain more SEH. This mostly only affects finallys. Filters could
  // hypothetically use gnu statement expressions to sneak in nested SEH.
  FD = FD ? FD : FE.CurSEHParent.getDecl();
  return get(FE.ME, dyn_cast_or_null<FunctionDecl>(FD));
}

namespace {
llvm::FunctionCallee getPersonalityFn(ModuleEmitter &ME,
                                      const EHPersonality &Personality) {
  return ME.createRuntimeFunction(llvm::FunctionType::get(ME.Int32Ty, true),
                                  Personality.PersonalityFn,
                                  llvm::AttributeList(), /*Local=*/true);
}

llvm::Constant *getOpaquePersonalityFn(ModuleEmitter &ME,
                                       const EHPersonality &Personality) {
  llvm::FunctionCallee Fn = getPersonalityFn(ME, Personality);
  return cast<llvm::Constant>(Fn.getCallee());
}

llvm::Constant *getCatchAllValue(FunctionEmitter &FE) {
  // Possibly we should use @llvm.eh.catch.all.value here.
  return llvm::ConstantPointerNull::get(FE.Int8PtrTy);
}
} // namespace

// ===----------------------------------------------------------------------===
// Exception slots & dispatch
// ===----------------------------------------------------------------------===

Address FunctionEmitter::getExceptionSlot() {
  if (!ExceptionSlot)
    ExceptionSlot = createTempAlloca(Int8PtrTy, "exn.slot");
  return Address(ExceptionSlot, Int8PtrTy, getPointerAlign());
}

Address FunctionEmitter::getEHSelectorSlot() {
  if (!EHSelectorSlot)
    EHSelectorSlot = createTempAlloca(Int32Ty, "ehselector.slot");
  return Address(EHSelectorSlot, Int32Ty, CharUnits::fromQuantity(4));
}

llvm::Value *FunctionEmitter::getExceptionFromSlot() {
  return Builder.CreateLoad(getExceptionSlot(), "exn");
}

llvm::Value *FunctionEmitter::getSelectorFromSlot() {
  return Builder.CreateLoad(getEHSelectorSlot(), "sel");
}

void FunctionEmitter::fixSEHEnd(llvm::InvokeInst *InvokeIst) {
  assert(InvokeIst);

  auto OldIP = Builder.saveIP();
  Builder.SetInsertPoint(CurFn->back().getLastInstruction());
  if (auto BrInst = dyn_cast<llvm::BranchInst>(&*Builder.GetInsertPoint())) {
    for (unsigned int I = 0; I < BrInst->getNumSuccessors(); ++I) {
      auto TrampolineBB = createBasicBlock("TrampolineBB", CurFn);
      llvm::IRBuilder<> IRB(TrampolineBB);
      IRB.CreateInvoke(getSehTryEndFn(ME), BrInst->getSuccessor(I),
                       InvokeIst->getUnwindDest());
      BrInst->setSuccessor(I, TrampolineBB);
    }
  }
  Builder.restoreIP(OldIP);
}

llvm::BasicBlock *
FunctionEmitter::getEHDispatchBlock(EHScopeStack::stable_iterator si) {
  if (EHPersonality::get(*this).usesFuncletPads())
    return getFuncletEHDispatchBlock(si);

  // The dispatch block for the end of the scope chain is a block that
  // just resumes unwinding.
  if (si == EHStack.stable_end())
    return getEHResumeBlock();

  // Otherwise, we should look at the actual scope.
  EHScope &scope = *EHStack.find(si);

  llvm::BasicBlock *dispatchBlock = scope.getCachedEHDispatchBlock();
  if (!dispatchBlock) {
    switch (scope.getKind()) {
    case EHScope::Catch: {
      // Apply a special case to a single catch-all.
      EHCatchScope &catchScope = cast<EHCatchScope>(scope);
      if (catchScope.getNumHandlers() == 1 &&
          catchScope.getHandler(0).isCatchAll()) {
        dispatchBlock = catchScope.getHandler(0).Block;

        // Otherwise, make a dispatch block.
      } else {
        dispatchBlock = createBasicBlock("catch.dispatch");
      }
      break;
    }

    case EHScope::Cleanup:
      dispatchBlock = createBasicBlock("ehcleanup");
      break;

    case EHScope::Terminate:
      dispatchBlock = getTerminateHandler();
      break;
    }
    scope.setCachedEHDispatchBlock(dispatchBlock);
  }
  return dispatchBlock;
}

llvm::BasicBlock *
FunctionEmitter::getFuncletEHDispatchBlock(EHScopeStack::stable_iterator SI) {
  // Returning nullptr indicates that the previous dispatch block should unwind
  // to caller.
  if (SI == EHStack.stable_end())
    return nullptr;

  // Otherwise, we should look at the actual scope.
  EHScope &EHS = *EHStack.find(SI);

  llvm::BasicBlock *DispatchBlock = EHS.getCachedEHDispatchBlock();
  if (DispatchBlock)
    return DispatchBlock;

  if (EHS.getKind() == EHScope::Terminate)
    DispatchBlock = getTerminateFunclet();
  else
    DispatchBlock = createBasicBlock();
  CGBuilderTy Builder(*this, DispatchBlock);

  switch (EHS.getKind()) {
  case EHScope::Catch:
    DispatchBlock->setName("catch.dispatch");
    break;

  case EHScope::Cleanup:
    DispatchBlock->setName("ehcleanup");
    break;

  case EHScope::Terminate:
    DispatchBlock->setName("terminate");
    break;
  }
  EHS.setCachedEHDispatchBlock(DispatchBlock);
  return DispatchBlock;
}

namespace {
bool isNonEHScope(const EHScope &S) {
  switch (S.getKind()) {
  case EHScope::Cleanup:
    return !cast<EHCleanupScope>(S).isEHCleanup();
  case EHScope::Catch:
  case EHScope::Terminate:
    return false;
  }

  llvm_unreachable("Invalid EHScope Kind!");
}
} // namespace

llvm::BasicBlock *FunctionEmitter::getInvokeDestImpl() {
  assert(EHStack.requiresLandingPad());
  assert(!EHStack.empty());

  // If exceptions are disabled and SEH is not in use, there is no invoke
  // destination. SEH still works when exceptions are off, matching MSVC
  // unless /EHa is used.
  const LangOptions &LO = ME.getLangOpts();
  if (!LO.Exceptions) {
    if (!LO.MicrosoftExt)
      return nullptr;
    if (!currentFunctionUsesSEHTry())
      return nullptr;
  }

  // Non-EH cleanups fall through to enclosing scopes in genLandingPad.
  llvm::BasicBlock *LP = EHStack.begin()->getCachedLandingPad();
  if (LP)
    return LP;

  const EHPersonality &Personality = EHPersonality::get(*this);

  if (!CurFn->hasPersonalityFn())
    CurFn->setPersonalityFn(getOpaquePersonalityFn(ME, Personality));

  if (Personality.usesFuncletPads()) {
    // We don't need separate landing pads in the funclet model.
    LP = getEHDispatchBlock(EHStack.getInnermostEHScope());
  } else {
    LP = genLandingPad();
  }

  assert(LP);

  // Cache the landing pad on the innermost scope.  If this is a
  // non-EH scope, cache the landing pad on the enclosing scope, too.
  for (EHScopeStack::iterator ir = EHStack.begin(); true; ++ir) {
    ir->setCachedLandingPad(LP);
    if (!isNonEHScope(*ir))
      break;
  }

  return LP;
}

llvm::BasicBlock *FunctionEmitter::genLandingPad() {
  assert(EHStack.requiresLandingPad());
  EHScope &innermostEHScope = *EHStack.find(EHStack.getInnermostEHScope());
  switch (innermostEHScope.getKind()) {
  case EHScope::Terminate:
    return getTerminateLandingPad();

  case EHScope::Catch:
  case EHScope::Cleanup:
    if (llvm::BasicBlock *lpad = innermostEHScope.getCachedLandingPad())
      return lpad;
  }

  CGBuilderTy::InsertPoint savedIP = Builder.saveAndClearIP();
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(*this, CurEHLocation);

  llvm::BasicBlock *lpad = createBasicBlock("lpad");
  genBlock(lpad);

  llvm::LandingPadInst *LPadInst =
      Builder.CreateLandingPad(llvm::StructType::get(Int8PtrTy, Int32Ty), 0);

  llvm::Value *LPadExn = Builder.CreateExtractValue(LPadInst, 0);
  Builder.CreateStore(LPadExn, getExceptionSlot());
  llvm::Value *LPadSel = Builder.CreateExtractValue(LPadInst, 1);
  Builder.CreateStore(LPadSel, getEHSelectorSlot());

  // Save the exception pointer.  It's safe to use a single exception
  // pointer per function because EH cleanups can never have nested
  // try/catches.

  bool hasCatchAll = false;
  bool hasCleanup = false;
  llvm::SmallPtrSet<llvm::Value *, 4> catchTypes;
  for (EHScopeStack::iterator I = EHStack.begin(), E = EHStack.end(); I != E;
       ++I) {

    switch (I->getKind()) {
    case EHScope::Cleanup:
      // If we have a cleanup, remember that.
      hasCleanup = (hasCleanup || cast<EHCleanupScope>(*I).isEHCleanup());
      continue;

    case EHScope::Terminate:
      // Terminate scopes are basically catch-alls.
      assert(!hasCatchAll);
      hasCatchAll = true;
      goto done;

    case EHScope::Catch:
      break;
    }

    EHCatchScope &catchScope = cast<EHCatchScope>(*I);
    for (unsigned hi = 0, he = catchScope.getNumHandlers(); hi != he; ++hi) {
      EHCatchScope::Handler handler = catchScope.getHandler(hi);
      assert(handler.Type.Flags == 0 &&
             "landingpads do not support catch handler flags");

      // If this is a catch-all, register that and abort.
      if (!handler.Type.RTTI) {
        assert(!hasCatchAll);
        hasCatchAll = true;
        goto done;
      }

      if (catchTypes.insert(handler.Type.RTTI).second)
        // If not, add it directly to the landingpad.
        LPadInst->addClause(handler.Type.RTTI);
    }
  }

done:
  // If we have a catch-all, add null to the landingpad.
  if (hasCatchAll) {
    LPadInst->addClause(getCatchAllValue(*this));
  } else if (hasCleanup) {
    LPadInst->setCleanup(true);
  }

  assert((LPadInst->getNumClauses() > 0 || LPadInst->isCleanup()) &&
         "landingpad instruction has no clauses!");

  // Tell the backend how to generate the landing pad.
  Builder.CreateBr(getEHDispatchBlock(EHStack.getInnermostEHScope()));

  // Restore the old IR generation state.
  Builder.restoreIP(savedIP);

  return lpad;
}

namespace {
void emitCatchPadBlock(FunctionEmitter &FE, EHCatchScope &CatchScope) {
  llvm::BasicBlock *DispatchBlock = CatchScope.getCachedEHDispatchBlock();
  assert(DispatchBlock);

  CGBuilderTy::InsertPoint SavedIP = FE.Builder.saveIP();
  FE.genBlockAfterUses(DispatchBlock);

  llvm::Value *ParentPad = FE.CurrentFuncletPad;
  if (!ParentPad)
    ParentPad = llvm::ConstantTokenNone::get(FE.getLLVMContext());
  llvm::BasicBlock *UnwindBB =
      FE.getEHDispatchBlock(CatchScope.getEnclosingEHScope());

  unsigned NumHandlers = CatchScope.getNumHandlers();
  llvm::CatchSwitchInst *CatchSwitch =
      FE.Builder.CreateCatchSwitch(ParentPad, UnwindBB, NumHandlers);

  // Test against each of the exception types we claim to catch.
  for (unsigned I = 0; I < NumHandlers; ++I) {
    const EHCatchScope::Handler &Handler = CatchScope.getHandler(I);

    CatchTypeInfo TypeInfo = Handler.Type;
    if (!TypeInfo.RTTI)
      TypeInfo.RTTI = llvm::Constant::getNullValue(FE.VoidPtrTy);

    FE.Builder.SetInsertPoint(Handler.Block);

    FE.Builder.CreateCatchPad(CatchSwitch, {TypeInfo.RTTI});

    CatchSwitch->addHandler(Handler.Block);
  }
  FE.Builder.restoreIP(SavedIP);
}

void emitCatchDispatchBlock(FunctionEmitter &FE, EHCatchScope &catchScope) {
  if (EHPersonality::get(FE).usesFuncletPads())
    return emitCatchPadBlock(FE, catchScope);

  llvm::BasicBlock *dispatchBlock = catchScope.getCachedEHDispatchBlock();
  assert(dispatchBlock);

  // If there's only a single catch-all, getEHDispatchBlock returned
  // that catch-all as the dispatch block.
  if (catchScope.getNumHandlers() == 1 &&
      catchScope.getHandler(0).isCatchAll()) {
    assert(dispatchBlock == catchScope.getHandler(0).Block);
    return;
  }

  CGBuilderTy::InsertPoint savedIP = FE.Builder.saveIP();
  FE.genBlockAfterUses(dispatchBlock);

  // Select the right handler.
  llvm::Function *llvm_eh_typeid_for =
      FE.ME.getIntrinsic(llvm::Intrinsic::eh_typeid_for);
  llvm::Type *argTy = llvm_eh_typeid_for->getArg(0)->getType();
  LangAS globAS = FE.ME.getGlobalVarAddressSpace(nullptr);

  llvm::Value *selector = FE.getSelectorFromSlot();

  for (unsigned i = 0, e = catchScope.getNumHandlers();; ++i) {
    assert(i < e && "ran off end of handlers!");
    const EHCatchScope::Handler &handler = catchScope.getHandler(i);

    llvm::Value *typeValue = handler.Type.RTTI;
    assert(handler.Type.Flags == 0 &&
           "landingpads do not support catch handler flags");
    assert(typeValue && "fell into catch-all case!");
    // With opaque ptrs, only the address space can be a mismatch.
    if (typeValue->getType() != argTy)
      typeValue = FE.getTargetHooks().performAddrSpaceCast(
          FE, typeValue, globAS, LangAS::Default, argTy);

    // Figure out the next block.
    bool nextIsEnd;
    llvm::BasicBlock *nextBlock;

    // If this is the last handler, we're at the end, and the next
    // block is the block for the enclosing EH scope.
    if (i + 1 == e) {
      nextBlock = FE.getEHDispatchBlock(catchScope.getEnclosingEHScope());
      nextIsEnd = true;

      // If the next handler is a catch-all, we're at the end, and the
      // next block is that handler.
    } else if (catchScope.getHandler(i + 1).isCatchAll()) {
      nextBlock = catchScope.getHandler(i + 1).Block;
      nextIsEnd = true;

      // Otherwise, we're not at the end and we need a new block.
    } else {
      nextBlock = FE.createBasicBlock("catch.fallthrough");
      nextIsEnd = false;
    }

    // Figure out the catch type's index in the LSDA's type table.
    llvm::CallInst *typeIndex =
        FE.Builder.CreateCall(llvm_eh_typeid_for, typeValue);
    typeIndex->setDoesNotThrow();

    llvm::Value *matchesTypeIndex =
        FE.Builder.CreateICmpEQ(selector, typeIndex, "matches");
    FE.Builder.CreateCondBr(matchesTypeIndex, handler.Block, nextBlock);

    // If the next handler is a catch-all, we're completely done.
    if (nextIsEnd) {
      FE.Builder.restoreIP(savedIP);
      return;
    }
    // Otherwise we need to emit and continue at that block.
    FE.genBlock(nextBlock);
  }
}
} // namespace

llvm::BasicBlock *FunctionEmitter::getTerminateLandingPad() {
  if (TerminateLandingPad)
    return TerminateLandingPad;

  CGBuilderTy::InsertPoint SavedIP = Builder.saveAndClearIP();

  // This will get inserted at the end of the function.
  TerminateLandingPad = createBasicBlock("terminate.lpad");
  Builder.SetInsertPoint(TerminateLandingPad);

  // Tell the backend that this is a landing pad.
  const EHPersonality &Personality = EHPersonality::get(*this);

  if (!CurFn->hasPersonalityFn())
    CurFn->setPersonalityFn(getOpaquePersonalityFn(ME, Personality));

  llvm::LandingPadInst *LPadInst =
      Builder.CreateLandingPad(llvm::StructType::get(Int8PtrTy, Int32Ty), 0);
  LPadInst->addClause(getCatchAllValue(*this));

  llvm::CallInst *terminateCall = genNounwindRuntimeCall(ME.getTerminateFn());
  terminateCall->setDoesNotReturn();
  Builder.CreateUnreachable();

  Builder.restoreIP(SavedIP);

  return TerminateLandingPad;
}

llvm::BasicBlock *FunctionEmitter::getTerminateHandler() {
  if (TerminateHandler)
    return TerminateHandler;

  // Placed at function end by finishFunction.
  TerminateHandler = createBasicBlock("terminate.handler");
  CGBuilderTy::InsertPoint SavedIP = Builder.saveAndClearIP();
  Builder.SetInsertPoint(TerminateHandler);

  llvm::CallInst *terminateCall = genNounwindRuntimeCall(ME.getTerminateFn());
  terminateCall->setDoesNotReturn();
  Builder.CreateUnreachable();

  // Restore the saved insertion state.
  Builder.restoreIP(SavedIP);

  return TerminateHandler;
}

llvm::BasicBlock *FunctionEmitter::getTerminateFunclet() {
  assert(EHPersonality::get(*this).usesFuncletPads() &&
         "use getTerminateLandingPad for non-funclet EH");

  llvm::BasicBlock *&TerminateFunclet = TerminateFunclets[CurrentFuncletPad];
  if (TerminateFunclet)
    return TerminateFunclet;

  CGBuilderTy::InsertPoint SavedIP = Builder.saveAndClearIP();

  // Placed at function end by finishFunction.
  TerminateFunclet = createBasicBlock("terminate.handler");
  Builder.SetInsertPoint(TerminateFunclet);

  // Token is the parent pad, or 'none' for top-level terminate scopes.
  SaveAndRestore RestoreCurrentFuncletPad(CurrentFuncletPad);
  llvm::Value *ParentPad = CurrentFuncletPad;
  if (!ParentPad)
    ParentPad = llvm::ConstantTokenNone::get(ME.getLLVMContext());
  CurrentFuncletPad = Builder.CreateCleanupPad(ParentPad);

  llvm::CallInst *terminateCall = genNounwindRuntimeCall(ME.getTerminateFn());
  terminateCall->setDoesNotReturn();
  Builder.CreateUnreachable();

  // Restore the saved insertion state.
  Builder.restoreIP(SavedIP);

  return TerminateFunclet;
}

llvm::BasicBlock *FunctionEmitter::getEHResumeBlock() {
  if (EHResumeBlock)
    return EHResumeBlock;

  CGBuilderTy::InsertPoint SavedIP = Builder.saveIP();

  // We emit a jump to a notional label at the outermost unwind state.
  EHResumeBlock = createBasicBlock("eh.resume");
  Builder.SetInsertPoint(EHResumeBlock);

  // Recreate the landingpad's return value for the 'resume' instruction.
  llvm::Value *Exn = getExceptionFromSlot();
  llvm::Value *Sel = getSelectorFromSlot();

  llvm::Type *LPadType = llvm::StructType::get(Exn->getType(), Sel->getType());
  llvm::Value *LPadVal = llvm::PoisonValue::get(LPadType);
  LPadVal = Builder.CreateInsertValue(LPadVal, Exn, 0, "lpad.val");
  LPadVal = Builder.CreateInsertValue(LPadVal, Sel, 1, "lpad.val");

  Builder.CreateResume(LPadVal);
  Builder.restoreIP(SavedIP);
  return EHResumeBlock;
}

// ===----------------------------------------------------------------------===
// SEH try/except/finally
// ===----------------------------------------------------------------------===

void FunctionEmitter::genSEHTryStmt(const SEHTryStmt &S) {

  // If try statements are disabled, just emit the try statement as a compound
  // stmt.
  if (ME.getCodeGenOpts().DisableTryStmt ||
      (CurCodeDecl && CurCodeDecl->hasAttr<DisableTryStmtAttr>())) {
    genStmt(S.getTryBlock());
    return;
  }

  bool ContainsRetStmt = false;
  enterSEHTryStmt(S, ContainsRetStmt);
  {
    // __leave block
    JumpDest TryLeave = getJumpDestInCurrentScope("__try.__leave");

    SEHTryEpilogueStack.push_back(&TryLeave);

    llvm::BasicBlock *TryBB = nullptr;
    auto Inst = genRuntimeCallOrInvoke(getSehTryBeginFn(ME));
    if (SEHTryEpilogueStack.size() == 1) // outermost only
      TryBB = Builder.GetInsertBlock();

    genStmt(S.getTryBlock());

    if (auto InvokeIst = dyn_cast<llvm::InvokeInst>(Inst))
      if (auto TryBB = InvokeIst->getNormalDest()) {
        TryBB->setName("__try.begin");
        TryBB->setItisSEHTryBeginBlock(true);
      }

    if (haveInsertPoint()) {
      genRuntimeCallOrInvoke(getSehTryEndFn(ME));
    } else {
      if (auto InvokeIst = dyn_cast<llvm::InvokeInst>(Inst))
        fixSEHEnd(InvokeIst);
    }

    // Volatilize all blocks in Try, till current insert point
    if (TryBB) {
      llvm::SmallPtrSet<llvm::BasicBlock *, 10> Visited;
      volatilizeTryBlocks(TryBB, Visited);
    }

    SEHTryEpilogueStack.pop_back();

    if (!TryLeave.getBlock()->use_empty())
      genBlock(TryLeave.getBlock(), /*IsFinished=*/true);
    else
      delete TryLeave.getBlock();

    CurFn->removeFnAttr(llvm::Attribute::AlwaysInline);
    CurFn->addFnAttr(llvm::Attribute::NoInline);
    CurFn->setItHasSEH(true);
  }
  exitSEHTryStmt(S, ContainsRetStmt);
}

//  Recursively walk through blocks in a _try
//      and make all memory instructions volatile
void FunctionEmitter::volatilizeTryBlocks(
    llvm::BasicBlock *BB, llvm::SmallPtrSet<llvm::BasicBlock *, 10> &V) {
  if (BB == SEHTryEpilogueStack.back()->getBlock() /* end of Try */ ||
      !V.insert(BB).second /* already visited */ ||
      !BB->getParent() /* not emitted */ || BB->empty())
    return;

  if (!BB->isEHPad()) {
    for (llvm::BasicBlock::iterator J = BB->begin(), JE = BB->end(); J != JE;
         ++J) {
      if (auto LI = dyn_cast<llvm::LoadInst>(J)) {
        LI->setVolatile(true);
      } else if (auto SI = dyn_cast<llvm::StoreInst>(J)) {
        SI->setVolatile(true);
      } else if (auto *MCI = dyn_cast<llvm::MemIntrinsic>(J)) {
        MCI->setVolatile(llvm::ConstantInt::get(Builder.getInt1Ty(), 1));
      }
    }
  }
  const llvm::Instruction *TI = BB->getTerminator();
  if (TI) {
    unsigned N = TI->getNumSuccessors();
    for (unsigned I = 0; I < N; I++)
      volatilizeTryBlocks(TI->getSuccessor(I), V);
  }
}

namespace {
struct PerformSEHFinally final : EHScopeStack::Cleanup {
  llvm::Function *OutlinedFinally;
  bool RetFromFinally;
  PerformSEHFinally(llvm::Function *OutlinedFinally, bool RetFromFinally)
      : OutlinedFinally(OutlinedFinally), RetFromFinally(RetFromFinally) {}

  void Emit(FunctionEmitter &FE, Flags F) override {
    TreeContext &Context = FE.getContext();
    ModuleEmitter &ME = FE.ME;

    CallArgList Args;

    QualType ArgTys[2] = {Context.UnsignedCharTy, Context.VoidPtrTy};
    llvm::Value *FP = nullptr;
    // If CFG.IsOutlinedSEHHelper is true, then we are within a finally block.
    if (FE.IsOutlinedSEHHelper) {
      FP = &FE.CurFn->arg_begin()[1];
    } else {
      llvm::Function *LocalAddrFn =
          ME.getIntrinsic(llvm::Intrinsic::localaddress);
      FP = FE.Builder.CreateCall(LocalAddrFn);
    }

    llvm::Value *IsForEH =
        llvm::ConstantInt::get(FE.convertType(ArgTys[0]), F.isForEHCleanup());

    // SEH: except __leave and fall-through at the end, all other exits in a
    // __try (return/goto/continue/break) are considered abnormal termination.
    // We encode this using the cleanup.dest slot:
    // - 0 for __leave / fall-through
    // - non-zero for other exits (dest indices)
    if (!F.isForEHCleanup() && FE.hasNormalCleanupDestSlot()) {
      Address Addr = FE.getNormalCleanupDestSlotIfExists();
      llvm::Value *Load = FE.Builder.CreateLoad(Addr, "cleanup.dest");
      llvm::Value *Zero = llvm::Constant::getNullValue(ME.Int32Ty);
      IsForEH = FE.Builder.CreateICmpNE(Load, Zero);
    }

    Args.add(RValue::get(IsForEH), ArgTys[0]);
    Args.add(RValue::get(FP), ArgTys[1]);

    // If this outlined finally helper may record a bailout request, clear the
    // parent slots before calling it so we only observe requests from this
    // call.
    auto BailIt = FE.SEHFinallyBailouts.find(OutlinedFinally);
    bool HasBailSlots = (BailIt != FE.SEHFinallyBailouts.end() &&
                         BailIt->second.KindSlot.isValid() &&
                         BailIt->second.TargetSlot.isValid());
    if (HasBailSlots && !F.isForEHCleanup()) {
      FE.Builder.CreateStore(FE.Builder.getInt8(0), BailIt->second.KindSlot);
      FE.Builder.CreateStore(FE.Builder.getInt32(0), BailIt->second.TargetSlot);
    }

    // Arrange a two-arg function info and type.
    const ABIFunctionInfo &FnInfo =
        ME.getTypes().arrangeBuiltinFunctionCall(Context.VoidTy, Args);

    auto Callee = FnCallee::forDirect(OutlinedFinally);
    FE.genCall(FnInfo, Callee, ReturnValueSlot(), Args);

    // Handle bailout requests from the outlined finally (break/continue/goto).
    // Only meaningful for normal cleanups; on EH cleanups, jumping is UB.
    if (HasBailSlots && !F.isForEHCleanup() && FE.haveInsertPoint()) {
      llvm::Value *KindV = FE.Builder.CreateLoad(BailIt->second.KindSlot,
                                                 "seh.finally.bail.kind");
      llvm::Value *HasBail =
          FE.Builder.CreateICmpNE(KindV, FE.Builder.getInt8(0));

      llvm::BasicBlock *ContBB = FE.createBasicBlock("seh.finally.bail.cont");
      llvm::BasicBlock *DispatchBB =
          FE.createBasicBlock("seh.finally.bail.dispatch");
      FE.Builder.CreateCondBr(HasBail, DispatchBB, ContBB);

      FE.genBlock(DispatchBB);

      llvm::SwitchInst *KindSw = llvm::SwitchInst::Create(
          KindV, ContBB, /*NumCases=*/4, FE.Builder.GetInsertBlock());

      auto emitJumpAndClear = [&](FunctionEmitter::JumpDest JD) {
        // Clear the kind so we don't accidentally re-enter.
        FE.Builder.CreateStore(FE.Builder.getInt8(0), BailIt->second.KindSlot);
        FE.genBranchThroughCleanup(JD);
      };

      // continue
      {
        llvm::BasicBlock *CaseBB =
            FE.createBasicBlock("seh.finally.bail.continue");
        KindSw->addCase(FE.Builder.getInt8(static_cast<uint8_t>(
                            FunctionEmitter::SEHFinallyBailoutKind::Continue)),
                        CaseBB);
        FE.genBlock(CaseBB);
        FunctionEmitter::JumpDest JD;
        if (FE.tryGetInnermostContinueDest(JD))
          emitJumpAndClear(JD);
        else
          FE.Builder.CreateUnreachable();
        FE.Builder.ClearInsertionPoint();
      }

      // break
      {
        llvm::BasicBlock *CaseBB =
            FE.createBasicBlock("seh.finally.bail.break");
        KindSw->addCase(FE.Builder.getInt8(static_cast<uint8_t>(
                            FunctionEmitter::SEHFinallyBailoutKind::Break)),
                        CaseBB);
        FE.genBlock(CaseBB);
        FunctionEmitter::JumpDest JD;
        if (FE.tryGetInnermostBreakDest(JD))
          emitJumpAndClear(JD);
        else
          FE.Builder.CreateUnreachable();
        FE.Builder.ClearInsertionPoint();
      }

      // goto
      {
        llvm::BasicBlock *CaseBB = FE.createBasicBlock("seh.finally.bail.goto");
        KindSw->addCase(FE.Builder.getInt8(static_cast<uint8_t>(
                            FunctionEmitter::SEHFinallyBailoutKind::Goto)),
                        CaseBB);
        FE.genBlock(CaseBB);

        llvm::Value *TargetV = FE.Builder.CreateLoad(BailIt->second.TargetSlot,
                                                     "seh.finally.bail.target");

        llvm::BasicBlock *GotoDefault =
            FE.createBasicBlock("seh.finally.bail.goto.default");
        llvm::SwitchInst *GotoSw = llvm::SwitchInst::Create(
            TargetV, GotoDefault, BailIt->second.GotoLabels.size(),
            FE.Builder.GetInsertBlock());

        for (unsigned I = 0, E = BailIt->second.GotoLabels.size(); I != E;
             ++I) {
          const LabelDecl *LD = BailIt->second.GotoLabels[I];
          if (!LD)
            continue;
          llvm::BasicBlock *LblBB =
              FE.createBasicBlock("seh.finally.bail.goto.case");
          GotoSw->addCase(FE.Builder.getInt32(I + 1), LblBB);
          FE.genBlock(LblBB);
          // Don't branch to the label from inside a cleanup; record dest index.
          FunctionEmitter::JumpDest JD = FE.getJumpDestForLabel(LD);
          FE.Builder.CreateStore(FE.Builder.getInt8(0),
                                 BailIt->second.KindSlot);
          FE.Builder.CreateStore(FE.Builder.getInt32(JD.getDestIndex()),
                                 FE.getNormalCleanupDestSlot());
          FE.Builder.CreateBr(ContBB);
          FE.Builder.ClearInsertionPoint();
        }

        FE.genBlock(GotoDefault);
        FE.Builder.CreateUnreachable();
        FE.Builder.ClearInsertionPoint();
      }

      // __leave
      {
        llvm::BasicBlock *CaseBB =
            FE.createBasicBlock("seh.finally.bail.leave");
        KindSw->addCase(FE.Builder.getInt8(static_cast<uint8_t>(
                            FunctionEmitter::SEHFinallyBailoutKind::Leave)),
                        CaseBB);
        FE.genBlock(CaseBB);

        FunctionEmitter::JumpDest JD;
        if (FE.tryGetInnermostSEHLeaveDest(JD)) {
          // __leave is not abnormal; clear cleanup.dest when it exists.
          if (FE.hasNormalCleanupDestSlot())
            FE.Builder.CreateStore(FE.Builder.getInt32(0),
                                   FE.getNormalCleanupDestSlotIfExists());
          FE.Builder.CreateStore(FE.Builder.getInt8(0),
                                 BailIt->second.KindSlot);
          FE.genBranchThroughCleanup(JD);
        } else {
          // Not in an SEH try scope (shouldn't happen here). Don't crash.
          FE.Builder.CreateUnreachable();
          FE.Builder.ClearInsertionPoint();
        }
        FE.Builder.ClearInsertionPoint();
      }

      FE.genBlock(ContBB);
    }

    if (F.isForEHCleanup() && RetFromFinally) {
      llvm::BasicBlock *AbnormalCont = FE.createBasicBlock("if.then");
      llvm::BasicBlock *NormalCont = FE.createBasicBlock("if.end");
      llvm::Value *ShouldRetLoad =
          FE.Builder.CreateLoad(FE.SEHRetNowStack.back());
      llvm::Value *ShouldRet = FE.Builder.CreateIsNotNull(ShouldRetLoad);

      FE.Builder.CreateCondBr(ShouldRet, AbnormalCont, NormalCont);
      FE.genBlock(AbnormalCont);
      FE.genSEHLocalUnwind();
      FE.Builder.CreateUnreachable();

      FE.genBlock(NormalCont);
    }
  }
};
} // end anonymous namespace

namespace {
struct CaptureFinder : ConstStmtVisitor<CaptureFinder> {
  llvm::SmallSetVector<const VarDecl *, 4> Captures;
  bool ContainsRetStmt = false;

  bool foundCaptures() { return !Captures.empty() || ContainsRetStmt; }

  void Visit(const Stmt *S) {
    // See if this is a capture, then recurse.
    ConstStmtVisitor<CaptureFinder>::Visit(S);
    for (const Stmt *Child : S->children())
      if (Child)
        Visit(Child);
  }

  void VisitDeclRefExpr(const DeclRefExpr *E) {
    const auto *D = dyn_cast<VarDecl>(E->getDecl());
    if (D && D->isLocalVarDeclOrParm() && D->hasLocalStorage())
      Captures.insert(D);
  }

  void VisitCallExpr(const CallExpr *) {}

  void VisitReturnStmt(const ReturnStmt *) { ContainsRetStmt = true; }
};
} // end anonymous namespace

namespace {
struct ReturnStmtFinder : ConstStmtVisitor<ReturnStmtFinder> {
  bool ContainsRetStmt = false;

  void Visit(const Stmt *S) {
    // See if this is a capture, then recurse.
    ConstStmtVisitor::Visit(S);
    for (const Stmt *Child : S->children())
      if (Child)
        Visit(Child);
  }

  void VisitReturnStmt(const ReturnStmt *) { ContainsRetStmt = true; }
};
} // end anonymous namespace

Address
FunctionEmitter::recoverAddrOfEscapedLocal(FunctionEmitter &ParentFnEmitter,
                                           Address ParentVar,
                                           llvm::Value *ParentFP) {
  llvm::CallInst *RecoverCall = nullptr;
  CGBuilderTy Builder(*this, AllocaInsertPt);
  if (auto *ParentAlloca = dyn_cast<llvm::AllocaInst>(ParentVar.getPointer())) {
    // Mark the variable escaped if nobody else referenced it and compute the
    // localescape index.
    auto InsertPair = ParentFnEmitter.EscapedLocals.insert(
        std::make_pair(ParentAlloca, ParentFnEmitter.EscapedLocals.size()));
    int FrameEscapeIdx = InsertPair.first->second;
    // call ptr @llvm.localrecover(ptr @parentFn, ptr %fp, i32 N)
    llvm::Function *FrameRecoverFn = llvm::Intrinsic::getDeclaration(
        &ME.getModule(), llvm::Intrinsic::localrecover);
    RecoverCall = Builder.CreateCall(
        FrameRecoverFn, {ParentFnEmitter.CurFn, ParentFP,
                         llvm::ConstantInt::get(Int32Ty, FrameEscapeIdx)});

  } else {
    // If the parent didn't have an alloca, we're doing some nested outlining.
    // Just clone the existing localrecover call, but tweak the FP argument to
    // use our FP value. All other arguments are constants.
    auto *ParentRecover =
        cast<llvm::IntrinsicInst>(ParentVar.getPointer()->stripPointerCasts());
    assert(ParentRecover->getIntrinsicID() == llvm::Intrinsic::localrecover &&
           "expected alloca or localrecover in parent LocalDeclMap");
    RecoverCall = cast<llvm::CallInst>(ParentRecover->clone());
    RecoverCall->setArgOperand(1, ParentFP);
    RecoverCall->insertBefore(AllocaInsertPt);
  }

  // Bitcast the variable, rename it, and insert it in the local decl map.
  llvm::Value *ChildVar =
      Builder.CreateBitCast(RecoverCall, ParentVar.getType());
  ChildVar->setName(ParentVar.getName());
  return ParentVar.withPointer(ChildVar, KnownNonNull);
}

void FunctionEmitter::genCapturedLocals(FunctionEmitter &ParentFnEmitter,
                                        const Stmt *OutlinedStmt,
                                        bool IsFilter) {
  CaptureFinder Finder;
  if (OutlinedStmt)
    Finder.Visit(OutlinedStmt);

  // We can exit early on x86_64 when there are no captures. We just have to
  // save the exception code in filters so that __exception_code() works.
  if (!Finder.foundCaptures()) {
    if (IsFilter)
      genSEHExceptionCodeSave(ParentFnEmitter, nullptr, nullptr);
    return;
  }

  CGBuilderTy Builder(ME, AllocaInsertPt);
  auto AI = CurFn->arg_begin();
  ++AI;
  llvm::Value *EntryFP = &*AI;

  llvm::Value *ParentFP = EntryFP;
  if (IsFilter) {
    // Given whatever FP the runtime provided us in EntryFP, recover the true
    // frame pointer of the parent function. We only need to do this in filters,
    // since finally funclets recover the parent FP for us.
    llvm::Function *RecoverFPIntrin =
        ME.getIntrinsic(llvm::Intrinsic::eh_recoverfp);
    ParentFP =
        Builder.CreateCall(RecoverFPIntrin, {ParentFnEmitter.CurFn, EntryFP});

    // if the parent is a _finally, the passed-in ParentFP is the FP
    // of parent _finally, not Establisher's FP (FP of outermost function).
    // Establkisher FP is 2nd paramenter passed into parent _finally.
    // Fortunately, it's always saved in parent's frame. The following
    // code retrieves it, and escapes it so that spill instruction won't be
    // optimized away.
    if (ParentFnEmitter.ParentFnEmitter != nullptr) {
      // Locate and escape Parent's frame_pointer.addr alloca
      // Depending on target, should be 1st/2nd one in LocalDeclMap.
      // Let's just scan for ImplicitParamDecl with VoidPtrTy.
      llvm::AllocaInst *FramePtrAddrAlloca = nullptr;
      for (auto &I : ParentFnEmitter.LocalDeclMap) {
        const VarDecl *D = cast<VarDecl>(I.first);
        if (isa<ImplicitParamDecl>(D) &&
            D->getType() == getContext().VoidPtrTy) {
          assert(D->getName().starts_with("frame_pointer"));
          FramePtrAddrAlloca = cast<llvm::AllocaInst>(I.second.getPointer());
          break;
        }
      }
      assert(FramePtrAddrAlloca);
      auto InsertPair = ParentFnEmitter.EscapedLocals.insert(std::make_pair(
          FramePtrAddrAlloca, ParentFnEmitter.EscapedLocals.size()));
      int FrameEscapeIdx = InsertPair.first->second;

      // an example of a filter's prolog::
      // %0 = call ptr @llvm.eh.recoverfp(@"?fin$0@0@main@@",..)
      // %1 = call ptr @llvm.localrecover(@"?fin$0@0@main@@",..)
      // %2 = load ptr, ptr %1, align 8
      //   ==> %2 is the frame-pointer of outermost host function
      llvm::Function *FrameRecoverFn = llvm::Intrinsic::getDeclaration(
          &ME.getModule(), llvm::Intrinsic::localrecover);
      ParentFP = Builder.CreateCall(
          FrameRecoverFn, {ParentFnEmitter.CurFn, ParentFP,
                           llvm::ConstantInt::get(Int32Ty, FrameEscapeIdx)});
      ParentFP = Builder.CreateLoad(
          Address(ParentFP, ME.VoidPtrTy, getPointerAlign()));
    }
  }

  for (const VarDecl *VD : Finder.Captures) {
    if (!VD)
      continue;
    if (VD->getType()->isVariablyModifiedType()) {
      ME.errorUnsupported(VD, "VLA captured by SEH");
      continue;
    }
    assert((isa<ImplicitParamDecl>(VD) || VD->isLocalVarDeclOrParm()) &&
           "captured non-local variable");

    // If this decl hasn't been declared yet, it will be declared in the
    // OutlinedStmt.
    auto I = ParentFnEmitter.LocalDeclMap.find(VD);
    if (I == ParentFnEmitter.LocalDeclMap.end())
      continue;

    Address ParentVar = I->second;
    Address Recovered =
        recoverAddrOfEscapedLocal(ParentFnEmitter, ParentVar, ParentFP);
    setAddrOfLocalVar(VD, Recovered);
  }

  if (IsFilter)
    genSEHExceptionCodeSave(ParentFnEmitter, ParentFP, EntryFP);

  if (!IsFilter && Finder.ContainsRetStmt) {
    SEHRetNowParent = recoverAddrOfEscapedLocal(
        ParentFnEmitter, ParentFnEmitter.SEHRetNowStack.back(), ParentFP);
    Address ParentSEHRetVal = ParentFnEmitter.ParentFnEmitter
                                  ? ParentFnEmitter.SEHReturnValue
                                  : ParentFnEmitter.ReturnValue;
    if (ParentSEHRetVal.isValid())
      SEHReturnValue =
          recoverAddrOfEscapedLocal(ParentFnEmitter, ParentSEHRetVal, ParentFP);
  }

  // Recover parent bailout slots for outlined SEH finally helpers.
  if (!IsFilter && SEHFinallyBailoutKindParentAlloca.isValid() &&
      SEHFinallyBailoutTargetParentAlloca.isValid()) {
    SEHFinallyBailoutKindParent = recoverAddrOfEscapedLocal(
        ParentFnEmitter, SEHFinallyBailoutKindParentAlloca, ParentFP);
    SEHFinallyBailoutTargetParent = recoverAddrOfEscapedLocal(
        ParentFnEmitter, SEHFinallyBailoutTargetParentAlloca, ParentFP);
  }
}

void FunctionEmitter::startOutlinedSEHHelper(FunctionEmitter &ParentFnEmitter,
                                             bool IsFilter,
                                             const Stmt *OutlinedStmt) {
  SourceLocation StartLoc = OutlinedStmt->getBeginLoc();

  llvm::SmallString<128> Name;
  {
    llvm::raw_svector_ostream OS(Name);
    GlobalDecl ParentSEHFn = ParentFnEmitter.CurSEHParent;
    assert(ParentSEHFn && "No CurSEHParent!");
    MangleContext &Mangler = ME.getCGABI().getMangleContext();
    if (IsFilter)
      Mangler.mangleSEHFilterExpression(ParentSEHFn, OS);
    else
      Mangler.mangleSEHFinallyBlock(ParentSEHFn, OS);
  }

  FunctionArgList Args;
  {
    if (IsFilter) {
      Args.push_back(ImplicitParamDecl::Create(
          getContext(), /*DC=*/nullptr, StartLoc,
          &getContext().Idents.get("exception_pointers"),
          getContext().VoidPtrTy));
    } else {
      Args.push_back(ImplicitParamDecl::Create(
          getContext(), /*DC=*/nullptr, StartLoc,
          &getContext().Idents.get("abnormal_termination"),
          getContext().UnsignedCharTy));
    }
    Args.push_back(ImplicitParamDecl::Create(
        getContext(), /*DC=*/nullptr, StartLoc,
        &getContext().Idents.get("frame_pointer"), getContext().VoidPtrTy));
  }

  QualType RetTy = IsFilter ? getContext().LongTy : getContext().VoidTy;

  const ABIFunctionInfo &FnInfo =
      ME.getTypes().arrangeBuiltinFunctionDeclaration(RetTy, Args);

  llvm::FunctionType *FnTy = ME.getTypes().GetFunctionType(FnInfo);
  llvm::Function *Fn = llvm::Function::Create(
      FnTy, llvm::GlobalValue::InternalLinkage, Name.str(), &ME.getModule());

  IsOutlinedSEHHelper = true;

  startFunction(GlobalDecl(), RetTy, Fn, FnInfo, Args,
                OutlinedStmt->getBeginLoc(), OutlinedStmt->getBeginLoc());
  CurSEHParent = ParentFnEmitter.CurSEHParent;

  ME.setInternalFunctionAttributes(GlobalDecl(), CurFn, FnInfo);
  genCapturedLocals(ParentFnEmitter, OutlinedStmt, IsFilter);
}

llvm::Function *
FunctionEmitter::generateSEHFilterFunction(FunctionEmitter &ParentFnEmitter,
                                           const SEHExceptStmt &Except) {
  const Expr *FilterExpr = Except.getFilterExpr();
  startOutlinedSEHHelper(ParentFnEmitter, true, FilterExpr);

  llvm::Value *R = genScalarExpr(FilterExpr);
  R = Builder.CreateIntCast(R, convertType(getContext().LongTy),
                            FilterExpr->getType()->isSignedIntegerType());
  Builder.CreateStore(R, ReturnValue);

  finishFunction(FilterExpr->getEndLoc());

  CurFn->setSEHFilterFunction();
  return CurFn;
}

llvm::Function *
FunctionEmitter::generateSEHFinallyFunction(FunctionEmitter &ParentFnEmitter,
                                            const SEHFinallyStmt &Finally) {
  const Stmt *FinallyBlock = Finally.getBlock();
  startOutlinedSEHHelper(ParentFnEmitter, false, FinallyBlock);

  genStmt(FinallyBlock);

  finishFunction(FinallyBlock->getEndLoc());

  CurFn->setSEHFinallyFunction();
  // Keep SEH finally helpers stable.
  CurFn->addFnAttr(llvm::Attribute::OptimizeNone);
  CurFn->removeFnAttr(llvm::Attribute::AlwaysInline);
  CurFn->addFnAttr(llvm::Attribute::NoInline);
  return CurFn;
}

void FunctionEmitter::genSEHExceptionCodeSave(FunctionEmitter &ParentFnEmitter,
                                              llvm::Value *ParentFP,
                                              llvm::Value *EntryFP) {
  // EXCEPTION_POINTERS*, returned by __exception_info.
  SEHInfo = &*CurFn->arg_begin();
  SEHCodeSlotStack.push_back(
      createMemTemp(getContext().IntTy, "__exception_code"));

  // Save the exception code in the exception slot to unify exception access in
  // the filter function and the landing pad.
  // struct EXCEPTION_POINTERS {
  //   EXCEPTION_RECORD *ExceptionRecord;
  //   CONTEXT *ContextRecord;
  // };
  // int exceptioncode = exception_pointers->ExceptionRecord->ExceptionCode;
  llvm::Type *RecordTy = llvm::PointerType::getUnqual(getLLVMContext());
  llvm::Type *PtrsTy = llvm::StructType::get(RecordTy, ME.VoidPtrTy);
  llvm::Value *Rec = Builder.CreateStructGEP(PtrsTy, SEHInfo, 0);
  Rec = Builder.CreateAlignedLoad(RecordTy, Rec, getPointerAlign());
  llvm::Value *Code = Builder.CreateAlignedLoad(Int32Ty, Rec, getIntAlign());
  assert(!SEHCodeSlotStack.empty() && "emitting EH code outside of __except");
  Builder.CreateStore(Code, SEHCodeSlotStack.back());
}

llvm::Value *FunctionEmitter::genSEHExceptionInfo() {
  // Sema should diagnose calling this builtin outside of a filter context, but
  // don't crash if we screw up.
  if (!SEHInfo)
    return llvm::UndefValue::get(Int8PtrTy);
  assert(SEHInfo->getType() == Int8PtrTy);
  return SEHInfo;
}

llvm::Value *FunctionEmitter::genSEHExceptionCode() {
  assert(!SEHCodeSlotStack.empty() && "emitting EH code outside of __except");
  return Builder.CreateLoad(SEHCodeSlotStack.back());
}

llvm::Value *FunctionEmitter::genSEHAbnormalTermination() {
  // Abnormal termination is just the first parameter to the outlined finally
  // helper.
  auto AI = CurFn->arg_begin();
  return Builder.CreateZExt(&*AI, Int32Ty);
}

void FunctionEmitter::pushSEHCleanup(CleanupKind Kind,
                                     llvm::Function *FinallyFunc) {
  EHStack.pushCleanup<PerformSEHFinally>(Kind, FinallyFunc, false);
}

void FunctionEmitter::enterSEHTryStmt(const SEHTryStmt &S,
                                      bool &ContainsRetStmt) {
  ContainsRetStmt = false;
  FunctionEmitter HelperFE(ME, /*suppressNewContext=*/true);
  HelperFE.ParentFnEmitter = this;
  if (const SEHFinallyStmt *Finally = S.getFinallyHandler()) {
    ReturnStmtFinder Finder;
    Finder.Visit(Finally);
    ContainsRetStmt = Finder.ContainsRetStmt;

    // Only allocate bailout slots when we actually need to "bounce" control
    // back to the parent frame
    bool NeedsBailoutSlots = false;
    bool NeedsGotoMapping = false;

    struct BailoutNeedFinder : ConstStmtVisitor<BailoutNeedFinder> {
      FunctionEmitter &FE;
      bool &NeedsBailoutSlots;
      bool &NeedsGotoMapping;
      unsigned LoopDepth = 0;
      unsigned SwitchDepth = 0;
      explicit BailoutNeedFinder(FunctionEmitter &FE, bool &NeedsBailoutSlots,
                                 bool &NeedsGotoMapping)
          : FE(FE), NeedsBailoutSlots(NeedsBailoutSlots),
            NeedsGotoMapping(NeedsGotoMapping) {}

      void Visit(const Stmt *S) {
        if (!S)
          return;
        struct DepthGuard {
          BailoutNeedFinder &F;
          bool IncLoop = false;
          bool IncSwitch = false;
          DepthGuard(BailoutNeedFinder &F, const Stmt *S) : F(F) {
            if (isa<ForStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S)) {
              ++F.LoopDepth;
              IncLoop = true;
            }
            if (isa<SwitchStmt>(S)) {
              ++F.SwitchDepth;
              IncSwitch = true;
            }
          }
          ~DepthGuard() {
            if (IncSwitch)
              --F.SwitchDepth;
            if (IncLoop)
              --F.LoopDepth;
          }
        } Guard(*this, S);

        ConstStmtVisitor<BailoutNeedFinder>::Visit(S);
        for (const Stmt *Child : S->children())
          if (Child)
            Visit(Child);
      }

      void VisitBreakStmt(const BreakStmt *) {
        // `break` only needs bailout if it targets something outside the
        // outlined finally helper (i.e. not within a loop/switch inside it).
        if (LoopDepth + SwitchDepth == 0)
          NeedsBailoutSlots = true;
      }
      void VisitContinueStmt(const ContinueStmt *) {
        // `continue` only needs bailout if it targets an enclosing loop outside
        // the outlined finally helper.
        if (LoopDepth == 0)
          NeedsBailoutSlots = true;
      }
      void VisitGotoStmt(const GotoStmt *) {
        NeedsBailoutSlots = true;
        NeedsGotoMapping = true;
      }
      void VisitSEHLeaveStmt(const SEHLeaveStmt *) {
        // `__leave` exits the innermost surrounding __try, which is outside the
        // outlined finally helper. It must be performed by the parent function.
        NeedsBailoutSlots = true;
      }
      void VisitCallExpr(const CallExpr *) {}
    };

    BailoutNeedFinder BNF(*this, NeedsBailoutSlots, NeedsGotoMapping);
    BNF.Visit(Finally->getBlock());

    Address BailKind = Address::invalid();
    Address BailTarget = Address::invalid();
    llvm::SmallVector<const LabelDecl *, 4> GotoLabels;

    if (NeedsBailoutSlots) {
      BailKind = createTempAlloca(Int8Ty, CharUnits::fromQuantity(1),
                                  "seh.finally.bail.kind");
      BailTarget =
          createTempAlloca(Int32Ty, getIntAlign(), "seh.finally.bail.target");
      Builder.CreateStore(Builder.getInt8(0), BailKind);
      Builder.CreateStore(Builder.getInt32(0), BailTarget);

      if (NeedsGotoMapping) {
        // Scan the finally block for gotos so we can assign stable codes.
        llvm::DenseSet<const LabelDecl *> Seen;
        struct GotoFinder : ConstStmtVisitor<GotoFinder> {
          llvm::SmallVectorImpl<const LabelDecl *> &Labels;
          llvm::DenseSet<const LabelDecl *> &Seen;
          GotoFinder(llvm::SmallVectorImpl<const LabelDecl *> &Labels,
                     llvm::DenseSet<const LabelDecl *> &Seen)
              : Labels(Labels), Seen(Seen) {}
          void Visit(const Stmt *S) {
            ConstStmtVisitor<GotoFinder>::Visit(S);
            for (const Stmt *Child : S->children())
              if (Child)
                Visit(Child);
          }
          void VisitGotoStmt(const GotoStmt *GS) {
            const LabelDecl *LD = GS->getLabel();
            if (LD && Seen.insert(LD).second)
              Labels.push_back(LD);
          }
        } GF(GotoLabels, Seen);
        GF.Visit(Finally->getBlock());
      }
    }

    // Provide the parent allocas and label-code mapping to the helper so it can
    // write bailout requests.
    if (NeedsBailoutSlots) {
      HelperFE.SEHFinallyBailoutKindParentAlloca = BailKind;
      HelperFE.SEHFinallyBailoutTargetParentAlloca = BailTarget;
      for (unsigned I = 0, E = GotoLabels.size(); I != E; ++I)
        HelperFE.SEHFinallyGotoLabelToCode[GotoLabels[I]] = I + 1;
    }

    if (ContainsRetStmt) {
      // Synthesize an "immediate_return" flag and use _local_unwind
      // via a fake CatchPad (__IsLocalUnwind filter) to abort unwinding
      // when a return is encountered inside __finally.
      SEHRetNowStack.push_back(
          createTempAlloca(ME.Int8Ty, CharUnits::fromQuantity(1), "retnow"));
      Builder.CreateStore(Builder.getInt8(0), SEHRetNowStack.back());

      EHCatchScope *CatchScope = EHStack.pushCatch(1);
      llvm::Function *FilterFunc = generateSEHIsLocalUnwindFunction();
      llvm::Constant *OpaqueFunc =
          llvm::ConstantExpr::getBitCast(FilterFunc, Int8PtrTy);
      CatchScope->setHandler(0, OpaqueFunc, createBasicBlock("__except.ret"));
    }
    // Outline the finally block.
    llvm::Function *FinallyFunc =
        HelperFE.generateSEHFinallyFunction(*this, *Finally);

    // Record bailout info for this outlined helper so the cleanup emission can
    // perform the actual jump after the helper call.
    if (NeedsBailoutSlots) {
      SEHFinallyBailoutInfo &BI = SEHFinallyBailouts[FinallyFunc];
      BI.KindSlot = BailKind;
      BI.TargetSlot = BailTarget;
      BI.GotoLabels = std::move(GotoLabels);
    }

    // Push a cleanup for __finally blocks.
    EHStack.pushCleanup<PerformSEHFinally>(NormalAndEHCleanup, FinallyFunc,
                                           ContainsRetStmt);
    return;
  }

  // Otherwise, we must have an __except block.
  const SEHExceptStmt *Except = S.getExceptHandler();
  assert(Except);
  EHCatchScope *CatchScope = EHStack.pushCatch(1);
  SEHCodeSlotStack.push_back(
      createMemTemp(getContext().IntTy, "__exception_code"));

  // If the filter constant-folds to 1, use SEH except-enter + IR catch-all
  // (__except.enter / no outlined filter IR). Otherwise emit an outlined MSVC
  // filter stub (analogous to the typeinfo slot in Itanium EH).
  llvm::Constant *C = ConstantEmitter(*this).tryEmitAbstract(
      Except->getFilterExpr(), getContext().IntTy);
  if (C && C->isOneValue()) {
    auto ExceptEnterBB = createBasicBlock("__except.enter");
    ExceptEnterBB->setItisSEHExceptEnterBlock(true);
    CatchScope->setCatchAllHandler(0, ExceptEnterBB);
    return;
  }

  // Outlined filter function — analogous to the typeinfo slot in Itanium EH.
  llvm::Function *FilterFunc =
      HelperFE.generateSEHFilterFunction(*this, *Except);
  CatchScope->setHandler(0, FilterFunc, createBasicBlock("__except.ret"));
}

llvm::Function *FunctionEmitter::generateSEHIsLocalUnwindFunction() {
  // IsLocalUnwind is a void dummy func just for readability.
  if (llvm::Function *F = ME.getModule().getFunction("__IsLocalUnwind"))
    return F;

  llvm::LLVMContext &Ctx = getLLVMContext();
  llvm::Type *ArgTys[] = {llvm::Type::getInt8PtrTy(Ctx),
                          llvm::Type::getInt8PtrTy(Ctx)};
  return llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), ArgTys, false),
      llvm::GlobalVariable::ExternalWeakLinkage, "__IsLocalUnwind",
      &ME.getModule());
}

void FunctionEmitter::genSEHLocalUnwind() {
  genRuntimeCallOrInvoke(ME.getIntrinsic(llvm::Intrinsic::seh_localunwind));
}

void FunctionEmitter::exitSEHTryStmt(const SEHTryStmt &S,
                                     bool ContainsRetStmt) {
  // Just pop the cleanup if it's a __finally block.
  if (S.getFinallyHandler()) {
    popCleanupBlock();
    if (ContainsRetStmt) {
      // Create __except block and control flow handling for return from
      // __finally. See comment in enterSEHTryStmt.
      //
      // First, create the point where we check for a return
      // from the __finally.
      llvm::BasicBlock *ContBB = createBasicBlock("__finally.cont");
      if (haveInsertPoint())
        Builder.CreateBr(ContBB);

      genBlock(ContBB);

      // On the normal path, check if we have a return-from-finally.
      llvm::BasicBlock *AbnormalCont = createBasicBlock("if.then");
      llvm::BasicBlock *NormalCont = createBasicBlock("if.end");
      llvm::Value *ShouldRetLoad = Builder.CreateLoad(SEHRetNowStack.back());
      llvm::Value *ShouldRet = Builder.CreateIsNotNull(ShouldRetLoad);

      Builder.CreateCondBr(ShouldRet, AbnormalCont, NormalCont);

      EHCatchScope &CatchScope = cast<EHCatchScope>(*EHStack.begin());
      emitCatchDispatchBlock(*this, CatchScope);

      // Grab the block before we pop the handler.
      llvm::BasicBlock *CatchPadBB = CatchScope.getHandler(0).Block;
      EHStack.popCatch();

      // The catch block only catches return-from-finally.
      genBlockAfterUses(CatchPadBB);
      llvm::CatchPadInst *CPI =
          cast<llvm::CatchPadInst>(CatchPadBB->getFirstNonPHI());
      Builder.CreateCatchRet(CPI, AbnormalCont);
      genBlock(AbnormalCont);

      // If the try block is nested inside a finally block, forward the
      // return from __finally to the parent function.
      if (SEHRetNowParent.isValid())
        Builder.CreateStore(Builder.getInt8(1), SEHRetNowParent);
      genBranchThroughCleanup(ReturnBlock);

      genBlock(NormalCont);
    }
    return;
  }

  // Otherwise, we must have an __except block.
  const SEHExceptStmt *Except = S.getExceptHandler();
  assert(Except && "__try must have __finally xor __except");
  EHCatchScope &CatchScope = cast<EHCatchScope>(*EHStack.begin());

  // Don't emit the __except block if the __try block lacked invokes.
  // a try body function.
  if (!CatchScope.hasEHBranches()) {
    CatchScope.clearHandlerBlocks();
    EHStack.popCatch();
    SEHCodeSlotStack.pop_back();
    return;
  }

  llvm::BasicBlock *ContBB = createBasicBlock("__try.end");
  ContBB->setItisSEHTryEndBlock(true);

  if (haveInsertPoint())
    Builder.CreateBr(ContBB);

  emitCatchDispatchBlock(*this, CatchScope);

  llvm::BasicBlock *CatchPadBB = CatchScope.getHandler(0).Block;
  EHStack.popCatch();

  genBlockAfterUses(CatchPadBB);

  // __except blocks don't get outlined into funclets, so immediately do a
  // catchret.
  llvm::CatchPadInst *CPI =
      cast<llvm::CatchPadInst>(CatchPadBB->getFirstNonPHI());
  llvm::BasicBlock *ExceptBB = createBasicBlock("__except.exit");
  ExceptBB->setItisSEHExceptExitBlock(true);
  Builder.CreateCatchRet(CPI, ExceptBB);
  genBlock(ExceptBB);

  {
    llvm::Function *SEHCodeIntrin =
        ME.getIntrinsic(llvm::Intrinsic::eh_exceptioncode);
    llvm::Value *Code = Builder.CreateCall(SEHCodeIntrin, {CPI});
    Builder.CreateStore(Code, SEHCodeSlotStack.back());
  }

  genStmt(Except->getBlock());
  SEHCodeSlotStack.pop_back();

  if (haveInsertPoint())
    Builder.CreateBr(ContBB);

  genBlock(ContBB);
}

void FunctionEmitter::genSEHLeaveStmt(const SEHLeaveStmt &S) {
  // If this code is reachable then emit a stop point (if generating
  // debug info). We have to do this ourselves because we are on the
  // "simple" statement path.
  if (haveInsertPoint())
    genStopPoint(&S);

  // In outlined SEH __finally helpers, __leave targets the parent function.
  // Record a bailout request and return; the parent cleanup will perform the
  // actual leave (threading through any remaining cleanups).
  if (IsOutlinedSEHHelper && SEHFinallyBailoutKindParent.isValid() &&
      SEHFinallyBailoutTargetParent.isValid()) {
    Builder.CreateStore(
        Builder.getInt8(static_cast<uint8_t>(SEHFinallyBailoutKind::Leave)),
        SEHFinallyBailoutKindParent);
    Builder.CreateStore(Builder.getInt32(0), SEHFinallyBailoutTargetParent);
    genBranchThroughCleanup(ReturnBlock);
    return;
  }

  // Otherwise, this must be a __leave from within a __try scope.
  if (!isSEHTryScope()) {
    Builder.CreateUnreachable();
    Builder.ClearInsertionPoint();
    return;
  }

  // __leave is not abnormal; clear cleanup.dest when it exists.
  if (hasNormalCleanupDestSlot())
    Builder.CreateStore(Builder.getInt32(0),
                        getNormalCleanupDestSlotIfExists());
  genBranchThroughCleanup(*SEHTryEpilogueStack.back());
}
