#include "Core/FunctionEmitter.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Dominating value save/restore
// ===----------------------------------------------------------------------===

bool DominatingValue<RValue>::saved_type::needsSaving(RValue rv) {
  if (rv.isScalar())
    return DominatingLLVMValue::needsSaving(rv.getScalarVal());
  if (rv.isAggregate())
    return DominatingLLVMValue::needsSaving(rv.getAggregatePointer());
  return true;
}

DominatingValue<RValue>::saved_type
DominatingValue<RValue>::saved_type::save(FunctionEmitter &FE, RValue rv) {
  if (rv.isScalar()) {
    llvm::Value *V = rv.getScalarVal();

    // These automatically dominate and don't need to be saved.
    if (!DominatingLLVMValue::needsSaving(V))
      return saved_type(V, nullptr, ScalarLiteral);

    // Everything else needs an alloca.
    Address addr =
        FE.createDefaultAlignTempAlloca(V->getType(), "saved-rvalue");
    FE.Builder.CreateStore(V, addr);
    return saved_type(addr.getPointer(), nullptr, ScalarAddress);
  }

  if (rv.isComplex()) {
    FunctionEmitter::ComplexPairTy V = rv.getComplexVal();
    llvm::Type *ComplexTy =
        llvm::StructType::get(V.first->getType(), V.second->getType());
    Address addr = FE.createDefaultAlignTempAlloca(ComplexTy, "saved-complex");
    FE.Builder.CreateStore(V.first, FE.Builder.CreateStructGEP(addr, 0));
    FE.Builder.CreateStore(V.second, FE.Builder.CreateStructGEP(addr, 1));
    return saved_type(addr.getPointer(), nullptr, ComplexAddress);
  }

  assert(rv.isAggregate());
  Address V = rv.getAggregateAddress();
  if (!DominatingLLVMValue::needsSaving(V.getPointer()))
    return saved_type(V.getPointer(), V.getElementType(), AggregateLiteral,
                      V.getAlignment().getQuantity());

  Address addr =
      FE.createTempAlloca(V.getType(), FE.getPointerAlign(), "saved-rvalue");
  FE.Builder.CreateStore(V.getPointer(), addr);
  return saved_type(addr.getPointer(), V.getElementType(), AggregateAddress,
                    V.getAlignment().getQuantity());
}

RValue DominatingValue<RValue>::saved_type::restore(FunctionEmitter &FE) {
  auto getSavingAddress = [&](llvm::Value *value) {
    auto *AI = cast<llvm::AllocaInst>(value);
    return Address(value, AI->getAllocatedType(),
                   CharUnits::fromQuantity(AI->getAlign().value()));
  };
  switch (K) {
  case ScalarLiteral:
    return RValue::get(Value);
  case ScalarAddress:
    return RValue::get(FE.Builder.CreateLoad(getSavingAddress(Value)));
  case AggregateLiteral:
    return RValue::getAggregate(
        Address(Value, ElementType, CharUnits::fromQuantity(Align)));
  case AggregateAddress: {
    auto addr = FE.Builder.CreateLoad(getSavingAddress(Value));
    return RValue::getAggregate(
        Address(addr, ElementType, CharUnits::fromQuantity(Align)));
  }
  case ComplexAddress: {
    Address address = getSavingAddress(Value);
    llvm::Value *real =
        FE.Builder.CreateLoad(FE.Builder.CreateStructGEP(address, 0));
    llvm::Value *imag =
        FE.Builder.CreateLoad(FE.Builder.CreateStructGEP(address, 1));
    return RValue::getComplex(real, imag);
  }
  }

  llvm_unreachable("bad saved r-value kind");
}

// ===----------------------------------------------------------------------===
// EH scope stack management
// ===----------------------------------------------------------------------===

char *EHScopeStack::allocate(size_t Size) {
  Size = llvm::alignTo(Size, ScopeStackAlignment);
  if (!StartOfBuffer) {
    unsigned Capacity = 1024;
    while (Capacity < Size)
      Capacity *= 2;
    StartOfBuffer = new char[Capacity];
    StartOfData = EndOfBuffer = StartOfBuffer + Capacity;
  } else if (static_cast<size_t>(StartOfData - StartOfBuffer) < Size) {
    unsigned CurrentCapacity = EndOfBuffer - StartOfBuffer;
    unsigned UsedCapacity = CurrentCapacity - (StartOfData - StartOfBuffer);

    unsigned NewCapacity = CurrentCapacity;
    do {
      NewCapacity *= 2;
    } while (NewCapacity < UsedCapacity + Size);

    char *NewStartOfBuffer = new char[NewCapacity];
    char *NewEndOfBuffer = NewStartOfBuffer + NewCapacity;
    char *NewStartOfData = NewEndOfBuffer - UsedCapacity;
    memcpy(NewStartOfData, StartOfData, UsedCapacity);
    delete[] StartOfBuffer;
    StartOfBuffer = NewStartOfBuffer;
    EndOfBuffer = NewEndOfBuffer;
    StartOfData = NewStartOfData;
  }

  assert(StartOfBuffer + Size <= StartOfData);
  StartOfData -= Size;
  return StartOfData;
}

void EHScopeStack::deallocate(size_t Size) {
  StartOfData += llvm::alignTo(Size, ScopeStackAlignment);
}

bool EHScopeStack::containsOnlyLifetimeMarkers(
    EHScopeStack::stable_iterator Old) const {
  for (EHScopeStack::iterator it = begin(); stabilize(it) != Old; it++) {
    EHCleanupScope *cleanup = dyn_cast<EHCleanupScope>(&*it);
    if (!cleanup || !cleanup->isLifetimeMarker())
      return false;
  }

  return true;
}

bool EHScopeStack::requiresLandingPad() const {
  for (stable_iterator si = getInnermostEHScope(); si != stable_end();) {
    // Skip lifetime markers.
    if (auto *cleanup = dyn_cast<EHCleanupScope>(&*find(si)))
      if (cleanup->isLifetimeMarker()) {
        si = cleanup->getEnclosingEHScope();
        continue;
      }
    return true;
  }

  return false;
}

EHScopeStack::stable_iterator
EHScopeStack::getInnermostActiveNormalCleanup() const {
  for (stable_iterator si = getInnermostNormalCleanup(), se = stable_end();
       si != se;) {
    EHCleanupScope &cleanup = cast<EHCleanupScope>(*find(si));
    if (cleanup.isActive())
      return si;
    si = cleanup.getEnclosingNormalCleanup();
  }
  return stable_end();
}

void *EHScopeStack::pushCleanup(CleanupKind Kind, size_t Size) {
  char *Buffer = allocate(EHCleanupScope::getSizeForCleanupSize(Size));
  bool IsNormalCleanup = Kind & NormalCleanup;
  bool IsEHCleanup = Kind & EHCleanup;
  bool IsLifetimeMarker = Kind & LifetimeMarker;

  // If `std::terminate` is the active EH scope, the standard leaves cleanup
  // ordering unspecified; we skip registering further EH cleanups in that case.
  if (InnermostEHScope != stable_end() &&
      find(InnermostEHScope)->getKind() == EHScope::Terminate)
    IsEHCleanup = false;

  EHCleanupScope *Scope = new (Buffer)
      EHCleanupScope(IsNormalCleanup, IsEHCleanup, Size, BranchFixups.size(),
                     InnermostNormalCleanup, InnermostEHScope);
  if (IsNormalCleanup)
    InnermostNormalCleanup = stable_begin();
  if (IsEHCleanup)
    InnermostEHScope = stable_begin();
  if (IsLifetimeMarker)
    Scope->setLifetimeMarker();

  return Scope->getCleanupBuffer();
}

void EHScopeStack::popCleanup() {
  assert(!empty() && "popping exception stack when not empty");

  assert(isa<EHCleanupScope>(*begin()));
  EHCleanupScope &Cleanup = cast<EHCleanupScope>(*begin());
  InnermostNormalCleanup = Cleanup.getEnclosingNormalCleanup();
  InnermostEHScope = Cleanup.getEnclosingEHScope();
  deallocate(Cleanup.getAllocatedSize());

  Cleanup.Destroy();

  if (!BranchFixups.empty()) {
    if (!hasNormalCleanups())
      BranchFixups.clear();

    // Otherwise we can still trim out unnecessary nulls.
    else
      popNullFixups();
  }
}

EHCatchScope *EHScopeStack::pushCatch(unsigned numHandlers) {
  char *buffer = allocate(EHCatchScope::getSizeForNumHandlers(numHandlers));
  EHCatchScope *scope =
      new (buffer) EHCatchScope(numHandlers, InnermostEHScope);
  InnermostEHScope = stable_begin();
  return scope;
}

void EHScopeStack::pushTerminate() {
  char *Buffer = allocate(EHTerminateScope::getSize());
  new (Buffer) EHTerminateScope(InnermostEHScope);
  InnermostEHScope = stable_begin();
}

void EHScopeStack::popNullFixups() {
  // We expect this to only be called when there's still an innermost
  // normal cleanup;  otherwise there really shouldn't be any fixups.
  assert(hasNormalCleanups());

  EHScopeStack::iterator it = find(InnermostNormalCleanup);
  unsigned MinSize = cast<EHCleanupScope>(*it).getFixupDepth();
  assert(BranchFixups.size() >= MinSize && "fixup stack out of order");

  while (BranchFixups.size() > MinSize &&
         BranchFixups.back().Destination == nullptr)
    BranchFixups.pop_back();
}

Address FunctionEmitter::createCleanupActiveFlag() {
  Address active = createTempAllocaWithoutCast(
      Builder.getInt1Ty(), CharUnits::One(), "cleanup.cond");
  setBeforeOutermostConditional(Builder.getFalse(), active);
  Builder.CreateStore(Builder.getTrue(), active);

  return active;
}

void FunctionEmitter::initFullExprCleanupWithFlag(Address ActiveFlag) {
  EHCleanupScope &cleanup = cast<EHCleanupScope>(*EHStack.begin());
  assert(!cleanup.hasActiveFlag() && "cleanup already has active flag?");
  cleanup.setActiveFlag(ActiveFlag);

  if (cleanup.isNormalCleanup())
    cleanup.setTestFlagInNormalCleanup();
  if (cleanup.isEHCleanup())
    cleanup.setTestFlagInEHCleanup();
}

// ===----------------------------------------------------------------------===
// Cleanup block emission
// ===----------------------------------------------------------------------===

void EHScopeStack::Cleanup::anchor() {}

namespace {
void createStoreInstBefore(llvm::Value *value, Address addr,
                           llvm::Instruction *beforeInst) {
  auto store = new llvm::StoreInst(value, addr.getPointer(), beforeInst);
  store->setAlignment(addr.getAlignment().getAsAlign());
}

llvm::LoadInst *createLoadInstBefore(Address addr, const llvm::Twine &name,
                                     llvm::Instruction *beforeInst) {
  return new llvm::LoadInst(addr.getElementType(), addr.getPointer(), name,
                            false, addr.getAlignment().getAsAlign(),
                            beforeInst);
}

void resolveAllBranchFixups(FunctionEmitter &FE, llvm::SwitchInst *Switch,
                            llvm::BasicBlock *CleanupEntry) {
  llvm::SmallPtrSet<llvm::BasicBlock *, 4> CasesAdded;

  for (unsigned I = 0, E = FE.EHStack.getNumBranchFixups(); I != E; ++I) {
    // Skip this fixup if its destination isn't set.
    BranchFixup &Fixup = FE.EHStack.getBranchFixup(I);
    if (Fixup.Destination == nullptr)
      continue;

    // If there isn't an OptimisticBranchBlock, then InitialBranch is
    // still pointing directly to its destination; forward it to the
    // appropriate cleanup entry.  This is required in the specific
    // case of
    //   { std::string s; goto lbl; }
    //   lbl:
    // i.e. where there's an unresolved fixup inside a single cleanup
    // entry which we're currently popping.
    if (Fixup.OptimisticBranchBlock == nullptr) {
      createStoreInstBefore(FE.Builder.getInt32(Fixup.DestinationIndex),
                            FE.getNormalCleanupDestSlot(), Fixup.InitialBranch);
      Fixup.InitialBranch->setSuccessor(0, CleanupEntry);
    }

    // Don't add this case to the switch statement twice.
    if (!CasesAdded.insert(Fixup.Destination).second)
      continue;

    Switch->addCase(FE.Builder.getInt32(Fixup.DestinationIndex),
                    Fixup.Destination);
  }

  FE.EHStack.clearFixups();
}

llvm::SwitchInst *TransitionToCleanupSwitch(FunctionEmitter &FE,
                                            llvm::BasicBlock *Block) {
  // If it's a branch, turn it into a switch whose default
  // destination is its original target.
  llvm::Instruction *Term = Block->getTerminator();
  assert(Term && "can't transition block without terminator");

  if (llvm::BranchInst *Br = dyn_cast<llvm::BranchInst>(Term)) {
    assert(Br->isUnconditional());
    auto Load = createLoadInstBefore(FE.getNormalCleanupDestSlot(),
                                     "cleanup.dest", Term);
    llvm::SwitchInst *Switch =
        llvm::SwitchInst::Create(Load, Br->getSuccessor(0), 4, Block);
    Br->eraseFromParent();
    return Switch;
  } else {
    return cast<llvm::SwitchInst>(Term);
  }
}
} // namespace

void FunctionEmitter::resolveBranchFixups(llvm::BasicBlock *Block) {
  assert(Block && "resolving a null target block");
  if (!EHStack.getNumBranchFixups())
    return;

  assert(EHStack.hasNormalCleanups() &&
         "branch fixups exist with no normal cleanups on stack");

  llvm::SmallPtrSet<llvm::BasicBlock *, 4> ModifiedOptimisticBlocks;
  bool ResolvedAny = false;

  for (unsigned I = 0, E = EHStack.getNumBranchFixups(); I != E; ++I) {
    // Skip this fixup if its destination doesn't match.
    BranchFixup &Fixup = EHStack.getBranchFixup(I);
    if (Fixup.Destination != Block)
      continue;

    Fixup.Destination = nullptr;
    ResolvedAny = true;

    // If it doesn't have an optimistic branch block, LatestBranch is
    // already pointing to the right place.
    llvm::BasicBlock *BranchBB = Fixup.OptimisticBranchBlock;
    if (!BranchBB)
      continue;

    // Don't process the same optimistic branch block twice.
    if (!ModifiedOptimisticBlocks.insert(BranchBB).second)
      continue;

    llvm::SwitchInst *Switch = TransitionToCleanupSwitch(*this, BranchBB);

    Switch->addCase(Builder.getInt32(Fixup.DestinationIndex), Block);
  }

  if (ResolvedAny)
    EHStack.popNullFixups();
}

void FunctionEmitter::popCleanupBlocks(
    EHScopeStack::stable_iterator Old,
    std::initializer_list<llvm::Value **> ValuesToReload) {
  assert(Old.isValid());

  bool HadBranches = false;
  while (EHStack.stable_begin() != Old) {
    EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.begin());
    HadBranches |= Scope.hasBranches();

    // As long as Old strictly encloses the scope's enclosing normal
    // cleanup, we're going to emit another normal cleanup which
    // fallthrough can propagate through.
    bool FallThroughIsBranchThrough =
        Old.strictlyEncloses(Scope.getEnclosingNormalCleanup());

    popCleanupBlock(FallThroughIsBranchThrough);
  }

  // If we didn't have any branches, the insertion point before cleanups must
  // dominate the current insertion point and we don't need to reload any
  // values.
  if (!HadBranches)
    return;

  // Spill and reload all values that the caller wants to be live at the current
  // insertion point.
  for (llvm::Value **ReloadedValue : ValuesToReload) {
    auto *Inst = dyn_cast_or_null<llvm::Instruction>(*ReloadedValue);
    if (!Inst)
      continue;

    // Don't spill static allocas, they dominate all cleanups. These are created
    // by binding a reference to a local variable or temporary.
    auto *AI = dyn_cast<llvm::AllocaInst>(Inst);
    if (AI && AI->isStaticAlloca())
      continue;

    Address Tmp =
        createDefaultAlignTempAlloca(Inst->getType(), "tmp.exprcleanup");

    llvm::BasicBlock::iterator InsertBefore;
    if (auto *Invoke = dyn_cast<llvm::InvokeInst>(Inst))
      InsertBefore = Invoke->getNormalDest()->getFirstInsertionPt();
    else
      InsertBefore = std::next(Inst->getIterator());
    CGBuilderTy(ME, &*InsertBefore).CreateStore(Inst, Tmp);

    // Reload the value at the current insertion point.
    *ReloadedValue = Builder.CreateLoad(Tmp);
  }
}

void FunctionEmitter::popCleanupBlocks(
    EHScopeStack::stable_iterator Old, size_t OldLifetimeExtendedSize,
    std::initializer_list<llvm::Value **> ValuesToReload) {
  popCleanupBlocks(Old, ValuesToReload);

  // Move our deferred cleanups onto the EH stack.
  for (size_t I = OldLifetimeExtendedSize,
              E = LifetimeExtendedCleanupStack.size();
       I != E;
       /**/) {
    // Alignment should be guaranteed by the headers in the individual cleanups.
    assert((I % alignof(LifetimeExtendedCleanupHeader) == 0) &&
           "misaligned cleanup stack entry");

    LifetimeExtendedCleanupHeader &Header =
        reinterpret_cast<LifetimeExtendedCleanupHeader &>(
            LifetimeExtendedCleanupStack[I]);
    I += sizeof(Header);

    EHStack.pushCopyOfCleanup(
        Header.getKind(), &LifetimeExtendedCleanupStack[I], Header.getSize());
    I += Header.getSize();

    if (Header.isConditional()) {
      Address ActiveFlag =
          reinterpret_cast<Address &>(LifetimeExtendedCleanupStack[I]);
      initFullExprCleanupWithFlag(ActiveFlag);
      I += sizeof(ActiveFlag);
    }
  }
  LifetimeExtendedCleanupStack.resize(OldLifetimeExtendedSize);
}

namespace {
llvm::BasicBlock *CreateNormalEntry(FunctionEmitter &FE,
                                    EHCleanupScope &Scope) {
  assert(Scope.isNormalCleanup());
  llvm::BasicBlock *Entry = Scope.getNormalBlock();
  if (!Entry) {
    Entry = FE.createBasicBlock("cleanup");
    Scope.setNormalBlock(Entry);
  }
  return Entry;
}

llvm::BasicBlock *SimplifyCleanupEntry(FunctionEmitter &FE,
                                       llvm::BasicBlock *Entry) {
  llvm::BasicBlock *Pred = Entry->getSinglePredecessor();
  if (!Pred)
    return Entry;

  llvm::BranchInst *Br = dyn_cast<llvm::BranchInst>(Pred->getTerminator());
  if (!Br || Br->isConditional())
    return Entry;
  assert(Br->getSuccessor(0) == Entry);

  // If we were previously inserting at the end of the cleanup entry
  // block, we'll need to continue inserting at the end of the
  // predecessor.
  bool WasInsertBlock = FE.Builder.GetInsertBlock() == Entry;
  assert(!WasInsertBlock || FE.Builder.GetInsertPoint() == Entry->end());

  // Kill the branch.
  Br->eraseFromParent();

  // Replace all uses of the entry with the predecessor, in case there
  // are phis in the cleanup.
  Entry->replaceAllUsesWith(Pred);

  // Merge the blocks.
  Pred->splice(Pred->end(), Entry);

  // Kill the entry block.
  Entry->eraseFromParent();

  if (WasInsertBlock)
    FE.Builder.SetInsertPoint(Pred);

  return Pred;
}

void genCleanup(FunctionEmitter &FE, EHScopeStack::Cleanup *Fn,
                EHScopeStack::Cleanup::Flags flags, Address ActiveFlag) {
  // If there's an active flag, load it and skip the cleanup if it's
  // false.
  llvm::BasicBlock *ContBB = nullptr;
  if (ActiveFlag.isValid()) {
    ContBB = FE.createBasicBlock("cleanup.done");
    llvm::BasicBlock *CleanupBB = FE.createBasicBlock("cleanup.action");
    llvm::Value *IsActive =
        FE.Builder.CreateLoad(ActiveFlag, "cleanup.is_active");
    FE.Builder.CreateCondBr(IsActive, CleanupBB, ContBB);
    FE.genBlock(CleanupBB);
  }

  // Ask the cleanup to emit itself.
  Fn->Emit(FE, flags);
  assert(FE.haveInsertPoint() && "cleanup ended with no insertion point?");

  if (ActiveFlag.isValid())
    FE.genBlock(ContBB);
}

void forwardPrebranchedFallthrough(llvm::BasicBlock *Exit,
                                   llvm::BasicBlock *From,
                                   llvm::BasicBlock *To) {
  // Exit is the exit block of a cleanup, so it always terminates in
  // an unconditional branch or a switch.
  llvm::Instruction *Term = Exit->getTerminator();

  if (llvm::BranchInst *Br = dyn_cast<llvm::BranchInst>(Term)) {
    assert(Br->isUnconditional() && Br->getSuccessor(0) == From);
    Br->setSuccessor(0, To);
  } else {
    llvm::SwitchInst *Switch = cast<llvm::SwitchInst>(Term);
    for (unsigned I = 0, E = Switch->getNumSuccessors(); I != E; ++I)
      if (Switch->getSuccessor(I) == From)
        Switch->setSuccessor(I, To);
  }
}

void destroyOptimisticNormalEntry(FunctionEmitter &FE, EHCleanupScope &scope) {
  llvm::BasicBlock *entry = scope.getNormalBlock();
  if (!entry)
    return;

  // Replace all the uses with unreachable.
  llvm::BasicBlock *unreachableBB = FE.getUnreachableBlock();
  for (llvm::BasicBlock::use_iterator i = entry->use_begin(),
                                      e = entry->use_end();
       i != e;) {
    llvm::Use &use = *i;
    ++i;

    use.set(unreachableBB);

    // The only uses should be fixup switches.
    llvm::SwitchInst *si = cast<llvm::SwitchInst>(use.getUser());
    if (si->getNumCases() == 1 && si->getDefaultDest() == unreachableBB) {
      // Replace the switch with a branch.
      llvm::BranchInst::Create(si->case_begin()->getCaseSuccessor(), si);

      // The switch operand is a load from the cleanup-dest alloca.
      llvm::LoadInst *condition = cast<llvm::LoadInst>(si->getCondition());

      si->eraseFromParent();
      assert(condition->getOperand(0) == FE.NormalCleanupDest.getPointer());
      assert(condition->use_empty());
      condition->eraseFromParent();
    }
  }

  assert(entry->use_empty());
  delete entry;
}
} // namespace

void FunctionEmitter::popCleanupBlock(bool FallthroughIsBranchThrough) {
  assert(!EHStack.empty() && "cleanup stack is empty!");
  assert(isa<EHCleanupScope>(*EHStack.begin()) && "top not a cleanup!");
  EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.begin());
  assert(Scope.getFixupDepth() <= EHStack.getNumBranchFixups());

  // Remember activation information.
  bool IsActive = Scope.isActive();
  Address NormalActiveFlag = Scope.shouldTestFlagInNormalCleanup()
                                 ? Scope.getActiveFlag()
                                 : Address::invalid();
  Address EHActiveFlag = Scope.shouldTestFlagInEHCleanup()
                             ? Scope.getActiveFlag()
                             : Address::invalid();

  llvm::BasicBlock *EHEntry = Scope.getCachedEHDispatchBlock();
  assert(Scope.hasEHBranches() == (EHEntry != nullptr));
  bool RequiresEHCleanup = (EHEntry != nullptr);
  EHScopeStack::stable_iterator EHParent = Scope.getEnclosingEHScope();

  // - whether there are branch fix-ups through this cleanup
  unsigned FixupDepth = Scope.getFixupDepth();
  bool HasFixups = EHStack.getNumBranchFixups() != FixupDepth;

  // - whether there are branch-throughs or branch-afters
  bool HasExistingBranches = Scope.hasBranches();

  // - whether there's a fallthrough
  llvm::BasicBlock *FallthroughSource = Builder.GetInsertBlock();
  bool HasFallthrough = (FallthroughSource != nullptr && IsActive);

  // Branch-through fall-throughs leave the insertion point set to the
  // end of the last cleanup, which points to the current scope.  The
  // rest of IR gen doesn't need to worry about this; it only happens
  // during the execution of popCleanupBlocks().
  bool HasPrebranchedFallthrough =
      (FallthroughSource && FallthroughSource->getTerminator());

  // If this is a normal cleanup, then having a prebranched
  // fallthrough implies that the fallthrough source unconditionally
  // jumps here.
  assert(!Scope.isNormalCleanup() || !HasPrebranchedFallthrough ||
         (Scope.getNormalBlock() &&
          FallthroughSource->getTerminator()->getSuccessor(0) ==
              Scope.getNormalBlock()));

  bool RequiresNormalCleanup = false;
  if (Scope.isNormalCleanup() &&
      (HasFixups || HasExistingBranches || HasFallthrough)) {
    RequiresNormalCleanup = true;
  }

  // If we have a prebranched fallthrough into an inactive normal
  // cleanup, rewrite it so that it leads to the appropriate place.
  if (Scope.isNormalCleanup() && HasPrebranchedFallthrough && !IsActive) {
    llvm::BasicBlock *prebranchDest;

    // If the prebranch is semantically branching through the next
    // cleanup, just forward it to the next block, leaving the
    // insertion point in the prebranched block.
    if (FallthroughIsBranchThrough) {
      EHScope &enclosing = *EHStack.find(Scope.getEnclosingNormalCleanup());
      prebranchDest = CreateNormalEntry(*this, cast<EHCleanupScope>(enclosing));

      // Otherwise, we need to make a new block.  If the normal cleanup
      // isn't being used at all, we could actually reuse the normal
      // entry block, but this is simpler, and it avoids conflicts with
      // dead optimistic fixup branches.
    } else {
      prebranchDest = createBasicBlock("forwarded-prebranch");
      genBlock(prebranchDest);
    }

    llvm::BasicBlock *normalEntry = Scope.getNormalBlock();
    assert(normalEntry && !normalEntry->use_empty());

    forwardPrebranchedFallthrough(FallthroughSource, normalEntry,
                                  prebranchDest);
  }

  // If we don't need the cleanup at all, we're done.
  if (!RequiresNormalCleanup && !RequiresEHCleanup) {
    destroyOptimisticNormalEntry(*this, Scope);
    EHStack.popCleanup(); // safe because there are no fixups
    assert(EHStack.getNumBranchFixups() == 0 || EHStack.hasNormalCleanups());
    return;
  }

  // Copy the cleanup emission data out.  This uses either a stack
  // array or malloc'd memory, depending on the size, which is
  // behavior that llvm::SmallVector would provide, if we could use it
  // here. Unfortunately, if you ask for a llvm::SmallVector<char>, the
  // alignment isn't sufficient.
  auto *CleanupSource = reinterpret_cast<char *>(Scope.getCleanupBuffer());
  alignas(EHScopeStack::ScopeStackAlignment) char
      CleanupBufferStack[8 * sizeof(void *)];
  std::unique_ptr<char[]> CleanupBufferHeap;
  size_t CleanupSize = Scope.getCleanupSize();
  EHScopeStack::Cleanup *Fn;

  if (CleanupSize <= sizeof(CleanupBufferStack)) {
    memcpy(CleanupBufferStack, CleanupSource, CleanupSize);
    Fn = reinterpret_cast<EHScopeStack::Cleanup *>(CleanupBufferStack);
  } else {
    CleanupBufferHeap.reset(new char[CleanupSize]);
    memcpy(CleanupBufferHeap.get(), CleanupSource, CleanupSize);
    Fn = reinterpret_cast<EHScopeStack::Cleanup *>(CleanupBufferHeap.get());
  }

  EHScopeStack::Cleanup::Flags cleanupFlags;
  if (Scope.isNormalCleanup())
    cleanupFlags.setIsNormalCleanupKind();
  if (Scope.isEHCleanup())
    cleanupFlags.setIsEHCleanupKind();

  if (!RequiresNormalCleanup) {
    destroyOptimisticNormalEntry(*this, Scope);
    EHStack.popCleanup();
  } else {
    // If we have a fallthrough and no other need for the cleanup,
    // emit it directly.
    if (HasFallthrough && !HasPrebranchedFallthrough && !HasFixups &&
        !HasExistingBranches) {

      destroyOptimisticNormalEntry(*this, Scope);
      EHStack.popCleanup();

      genCleanup(*this, Fn, cleanupFlags, NormalActiveFlag);

      // Otherwise, the best approach is to thread everything through
      // the cleanup block and then try to clean up after ourselves.
    } else {
      // Force the entry block to exist.
      llvm::BasicBlock *NormalEntry = CreateNormalEntry(*this, Scope);

      // I.  Set up the fallthrough edge in.

      CGBuilderTy::InsertPoint savedInactiveFallthroughIP;

      // If there's a fallthrough, we need to store the cleanup
      // destination index.  For fall-throughs this is always zero.
      if (HasFallthrough) {
        if (!HasPrebranchedFallthrough)
          Builder.CreateStore(Builder.getInt32(0), getNormalCleanupDestSlot());

        // Otherwise, save and clear the IP if we don't have fallthrough
        // because the cleanup is inactive.
      } else if (FallthroughSource) {
        assert(!IsActive && "source without fallthrough for active cleanup");
        savedInactiveFallthroughIP = Builder.saveAndClearIP();
      }

      // II.  Emit the entry block.  This implicitly branches to it if
      // we have fallthrough.  All the fixups and existing branches
      // should already be branched to it.
      genBlock(NormalEntry);

      // III.  Figure out where we're going and build the cleanup
      // epilogue.

      bool HasEnclosingCleanups =
          (Scope.getEnclosingNormalCleanup() != EHStack.stable_end());

      // Compute the branch-through dest if we need it:
      //   - if there are branch-throughs threaded through the scope
      //   - if fall-through is a branch-through
      //   - if there are fixups that will be optimistically forwarded
      //     to the enclosing cleanup
      llvm::BasicBlock *BranchThroughDest = nullptr;
      if (Scope.hasBranchThroughs() ||
          (FallthroughSource && FallthroughIsBranchThrough) ||
          (HasFixups && HasEnclosingCleanups)) {
        assert(HasEnclosingCleanups);
        EHScope &S = *EHStack.find(Scope.getEnclosingNormalCleanup());
        BranchThroughDest = CreateNormalEntry(*this, cast<EHCleanupScope>(S));
      }

      llvm::BasicBlock *FallthroughDest = nullptr;
      llvm::SmallVector<llvm::Instruction *, 2> InstsToAppend;

      // If there's exactly one branch-after and no other threads,
      // we can route it without a switch.
      // Skip for SEH, since ExitSwitch is used to generate code to indicate
      // abnormal termination. (SEH: Except _leave and fall-through at
      // the end, all other exits in a _try (return/goto/continue/break)
      // are considered as abnormal terminations, using NormalCleanupDestSlot
      // to indicate abnormal termination)
      if (!Scope.hasBranchThroughs() && !HasFixups && !HasFallthrough &&
          !currentFunctionUsesSEHTry() && Scope.getNumBranchAfters() == 1) {
        assert(!BranchThroughDest || !IsActive);

        // Clean up the possibly dead store to the cleanup dest slot.
        llvm::Instruction *NormalCleanupDestSlot =
            cast<llvm::Instruction>(getNormalCleanupDestSlot().getPointer());
        if (NormalCleanupDestSlot->hasOneUse()) {
          NormalCleanupDestSlot->user_back()->eraseFromParent();
          NormalCleanupDestSlot->eraseFromParent();
          NormalCleanupDest = Address::invalid();
        }

        llvm::BasicBlock *BranchAfter = Scope.getBranchAfterBlock(0);
        InstsToAppend.push_back(llvm::BranchInst::Create(BranchAfter));

        // Build a switch-out if we need it:
        //   - if there are branch-afters threaded through the scope
        //   - if fall-through is a branch-after
        //   - if there are fixups that have nowhere left to go and
        //     so must be immediately resolved
      } else if (Scope.getNumBranchAfters() ||
                 (HasFallthrough && !FallthroughIsBranchThrough) ||
                 (HasFixups && !HasEnclosingCleanups)) {

        llvm::BasicBlock *Default =
            (BranchThroughDest ? BranchThroughDest : getUnreachableBlock());

        const unsigned SwitchCapacity = 10;

        // pass the abnormal exit flag to Fn (SEH cleanup)
        cleanupFlags.setHasExitSwitch();

        llvm::LoadInst *Load = createLoadInstBefore(getNormalCleanupDestSlot(),
                                                    "cleanup.dest", nullptr);
        llvm::SwitchInst *Switch =
            llvm::SwitchInst::Create(Load, Default, SwitchCapacity);

        InstsToAppend.push_back(Load);
        InstsToAppend.push_back(Switch);

        // Branch-after fallthrough.
        if (FallthroughSource && !FallthroughIsBranchThrough) {
          FallthroughDest = createBasicBlock("cleanup.cont");
          if (HasFallthrough)
            Switch->addCase(Builder.getInt32(0), FallthroughDest);
        }

        for (unsigned I = 0, E = Scope.getNumBranchAfters(); I != E; ++I) {
          Switch->addCase(Scope.getBranchAfterIndex(I),
                          Scope.getBranchAfterBlock(I));
        }

        // If there aren't any enclosing cleanups, we can resolve all
        // the fixups now.
        if (HasFixups && !HasEnclosingCleanups)
          resolveAllBranchFixups(*this, Switch, NormalEntry);
      } else {
        // We should always have a branch-through destination in this case.
        assert(BranchThroughDest);
        InstsToAppend.push_back(llvm::BranchInst::Create(BranchThroughDest));
      }

      // IV.  Pop the cleanup and emit it.
      EHStack.popCleanup();
      assert(EHStack.hasNormalCleanups() == HasEnclosingCleanups);

      genCleanup(*this, Fn, cleanupFlags, NormalActiveFlag);

      // Append the prepared cleanup prologue from above.
      llvm::BasicBlock *NormalExit = Builder.GetInsertBlock();
      for (unsigned I = 0, E = InstsToAppend.size(); I != E; ++I)
        InstsToAppend[I]->insertInto(NormalExit, NormalExit->end());

      // Optimistically hope that any fixups will continue falling through.
      for (unsigned I = FixupDepth, E = EHStack.getNumBranchFixups(); I < E;
           ++I) {
        BranchFixup &Fixup = EHStack.getBranchFixup(I);
        if (!Fixup.Destination)
          continue;
        if (!Fixup.OptimisticBranchBlock) {
          createStoreInstBefore(Builder.getInt32(Fixup.DestinationIndex),
                                getNormalCleanupDestSlot(),
                                Fixup.InitialBranch);
          Fixup.InitialBranch->setSuccessor(0, NormalEntry);
        }
        Fixup.OptimisticBranchBlock = NormalExit;
      }

      // V.  Set up the fallthrough edge out.

      // Case 1: a fallthrough source exists but doesn't branch to the
      // cleanup because the cleanup is inactive.
      if (!HasFallthrough && FallthroughSource) {
        // Prebranched fallthrough was forwarded earlier.
        // Non-prebranched fallthrough doesn't need to be forwarded.
        // Either way, all we need to do is restore the IP we cleared before.
        assert(!IsActive);
        Builder.restoreIP(savedInactiveFallthroughIP);

        // Case 2: a fallthrough source exists and should branch to the
        // cleanup, but we're not supposed to branch through to the next
        // cleanup.
      } else if (HasFallthrough && FallthroughDest) {
        assert(!FallthroughIsBranchThrough);
        genBlock(FallthroughDest);

        // Case 3: a fallthrough source exists and should branch to the
        // cleanup and then through to the next.
      } else if (HasFallthrough) {
        // Everything is already set up for this.

        // Case 4: no fallthrough source exists.
      } else {
        Builder.ClearInsertionPoint();
      }

      // VI.  Assorted cleaning.

      // Merging NormalEntry might invalidate (non-IR) pointers to it.
      llvm::BasicBlock *NewNormalEntry =
          SimplifyCleanupEntry(*this, NormalEntry);

      // If it did invalidate those pointers, and NormalEntry was the same
      // as NormalExit, go back and patch up the fixups.
      if (NewNormalEntry != NormalEntry && NormalEntry == NormalExit)
        for (unsigned I = FixupDepth, E = EHStack.getNumBranchFixups(); I < E;
             ++I)
          EHStack.getBranchFixup(I).OptimisticBranchBlock = NewNormalEntry;
    }
  }

  assert(EHStack.hasNormalCleanups() || EHStack.getNumBranchFixups() == 0);

  if (RequiresEHCleanup) {
    CGBuilderTy::InsertPoint SavedIP = Builder.saveAndClearIP();

    genBlock(EHEntry);

    llvm::BasicBlock *NextAction = getEHDispatchBlock(EHParent);

    // Push a terminate scope or cleanupendpad scope around the potentially
    // throwing cleanups. For funclet EH personalities, the cleanupendpad models
    // program termination when cleanups throw.
    bool PushedTerminate = false;
    SaveAndRestore RestoreCurrentFuncletPad(CurrentFuncletPad);
    llvm::CleanupPadInst *CPI = nullptr;

    const EHPersonality &Personality = EHPersonality::get(*this);
    if (Personality.usesFuncletPads()) {
      llvm::Value *ParentPad = CurrentFuncletPad;
      if (!ParentPad)
        ParentPad = llvm::ConstantTokenNone::get(ME.getLLVMContext());
      CurrentFuncletPad = CPI = Builder.CreateCleanupPad(ParentPad);
    }

    // Non-MSVC personalities need to terminate when an EH cleanup throws.
    if (!Personality.isMSVCPersonality()) {
      EHStack.pushTerminate();
      PushedTerminate = true;
    }

    // We only actually emit the cleanup code if the cleanup is either
    // active or was used before it was deactivated.
    if (EHActiveFlag.isValid() || IsActive) {
      cleanupFlags.setIsForEHCleanup();
      genCleanup(*this, Fn, cleanupFlags, EHActiveFlag);
    }

    if (CPI)
      Builder.CreateCleanupRet(CPI, NextAction);
    else
      Builder.CreateBr(NextAction);

    // Leave the terminate scope.
    if (PushedTerminate)
      EHStack.popTerminate();

    Builder.restoreIP(SavedIP);

    SimplifyCleanupEntry(*this, EHEntry);
  }
}

bool FunctionEmitter::isObviouslyBranchWithoutCleanups(JumpDest Dest) const {
  assert(Dest.getScopeDepth().encloses(EHStack.stable_begin()) &&
         "stale jump destination");

  EHScopeStack::stable_iterator TopCleanup =
      EHStack.getInnermostActiveNormalCleanup();

  // If we're not in an active normal cleanup scope, or if the
  // destination scope is within the innermost active normal cleanup
  // scope, we don't need to worry about fixups.
  if (TopCleanup == EHStack.stable_end() ||
      TopCleanup.encloses(Dest.getScopeDepth())) // works for invalid
    return true;

  // Otherwise, we might need some cleanups.
  return false;
}

void FunctionEmitter::genBranchThroughCleanup(JumpDest Dest) {
  assert(Dest.getScopeDepth().encloses(EHStack.stable_begin()) &&
         "stale jump destination");

  if (!haveInsertPoint())
    return;

  llvm::BranchInst *BI = Builder.CreateBr(Dest.getBlock());

  EHScopeStack::stable_iterator TopCleanup =
      EHStack.getInnermostActiveNormalCleanup();

  // If we're not in an active normal cleanup scope, or if the
  // destination scope is within the innermost active normal cleanup
  // scope, we don't need to worry about fixups.
  if (TopCleanup == EHStack.stable_end() ||
      TopCleanup.encloses(Dest.getScopeDepth())) { // works for invalid
    Builder.ClearInsertionPoint();
    return;
  }

  // If we can't resolve the destination cleanup scope, just add this
  // to the current cleanup scope as a branch fixup.
  if (!Dest.getScopeDepth().isValid()) {
    // SEH: record the dest index early for forward labels.
    llvm::ConstantInt *Index = Builder.getInt32(Dest.getDestIndex());
    createStoreInstBefore(Index, getNormalCleanupDestSlot(), BI);

    BranchFixup &Fixup = EHStack.addBranchFixup();
    Fixup.Destination = Dest.getBlock();
    Fixup.DestinationIndex = Dest.getDestIndex();
    Fixup.InitialBranch = BI;
    Fixup.OptimisticBranchBlock = nullptr;

    Builder.ClearInsertionPoint();
    return;
  }

  // Otherwise, thread through all the normal cleanups in scope.

  llvm::ConstantInt *Index = Builder.getInt32(Dest.getDestIndex());
  createStoreInstBefore(Index, getNormalCleanupDestSlot(), BI);

  // Adjust BI to point to the first cleanup block.
  {
    EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(TopCleanup));
    BI->setSuccessor(0, CreateNormalEntry(*this, Scope));
  }

  EHScopeStack::stable_iterator I = TopCleanup;
  EHScopeStack::stable_iterator E = Dest.getScopeDepth();
  if (E.strictlyEncloses(I)) {
    while (true) {
      EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(I));
      assert(Scope.isNormalCleanup());
      I = Scope.getEnclosingNormalCleanup();

      // If this is the last cleanup we're propagating through, tell it
      // that there's a resolved jump moving through it.
      if (!E.strictlyEncloses(I)) {
        Scope.addBranchAfter(Index, Dest.getBlock());
        break;
      }

      // Otherwise, tell the scope that there's a jump propagating
      // through it.  If this isn't new information, all the rest of
      // the work has been done before.
      if (!Scope.addBranchThrough(Dest.getBlock()))
        break;
    }
  }

  Builder.ClearInsertionPoint();
}

namespace {
bool isUsedAsNormalCleanup(EHScopeStack &EHStack,
                           EHScopeStack::stable_iterator C) {
  // If we needed a normal block for any reason, that counts.
  if (cast<EHCleanupScope>(*EHStack.find(C)).getNormalBlock())
    return true;

  for (EHScopeStack::stable_iterator I = EHStack.getInnermostNormalCleanup();
       I != C;) {
    assert(C.strictlyEncloses(I));
    EHCleanupScope &S = cast<EHCleanupScope>(*EHStack.find(I));
    if (S.getNormalBlock())
      return true;
    I = S.getEnclosingNormalCleanup();
  }

  return false;
}

bool isUsedAsEHCleanup(EHScopeStack &EHStack,
                       EHScopeStack::stable_iterator cleanup) {
  // If we needed an EH block for any reason, that counts.
  if (EHStack.find(cleanup)->hasEHBranches())
    return true;

  for (EHScopeStack::stable_iterator i = EHStack.getInnermostEHScope();
       i != cleanup;) {
    assert(cleanup.strictlyEncloses(i));

    EHScope &scope = *EHStack.find(i);
    if (scope.hasEHBranches())
      return true;

    i = scope.getEnclosingEHScope();
  }

  return false;
}

enum ForActivation_t { ForActivation, ForDeactivation };

void setupCleanupBlockActivation(FunctionEmitter &FE,
                                 EHScopeStack::stable_iterator C,
                                 ForActivation_t kind,
                                 llvm::Instruction *dominatingIP) {
  EHCleanupScope &Scope = cast<EHCleanupScope>(*FE.EHStack.find(C));

  // We always need the flag if we're activating the cleanup in a
  // conditional context, because we have to assume that the current
  // location doesn't necessarily dominate the cleanup's code.
  bool isActivatedInConditional =
      (kind == ForActivation && FE.isInConditionalBranch());

  bool needFlag = false;

  //   - as a normal cleanup
  if (Scope.isNormalCleanup() &&
      (isActivatedInConditional || isUsedAsNormalCleanup(FE.EHStack, C))) {
    Scope.setTestFlagInNormalCleanup();
    needFlag = true;
  }

  //  - as an EH cleanup
  if (Scope.isEHCleanup() &&
      (isActivatedInConditional || isUsedAsEHCleanup(FE.EHStack, C))) {
    Scope.setTestFlagInEHCleanup();
    needFlag = true;
  }

  // If it hasn't yet been used as either, we're done.
  if (!needFlag)
    return;

  Address var = Scope.getActiveFlag();
  if (!var.isValid()) {
    var = FE.createTempAlloca(FE.Builder.getInt1Ty(), CharUnits::One(),
                              "cleanup.isactive");
    Scope.setActiveFlag(var);

    assert(dominatingIP && "no existing variable and no dominating IP!");

    llvm::Constant *value = FE.Builder.getInt1(kind == ForDeactivation);

    // If we're in a conditional block, ignore the dominating IP and
    // use the outermost conditional branch.
    if (FE.isInConditionalBranch()) {
      FE.setBeforeOutermostConditional(value, var);
    } else {
      createStoreInstBefore(value, var, dominatingIP);
    }
  }

  FE.Builder.CreateStore(FE.Builder.getInt1(kind == ForActivation), var);
}
} // namespace

void FunctionEmitter::activateCleanupBlock(EHScopeStack::stable_iterator C,
                                           llvm::Instruction *dominatingIP) {
  assert(C != EHStack.stable_end() && "activating bottom of stack?");
  EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(C));
  assert(!Scope.isActive() && "double activation");

  setupCleanupBlockActivation(*this, C, ForActivation, dominatingIP);

  Scope.setActive(true);
}

void FunctionEmitter::deactivateCleanupBlock(EHScopeStack::stable_iterator C,
                                             llvm::Instruction *dominatingIP) {
  assert(C != EHStack.stable_end() && "deactivating bottom of stack?");
  EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(C));
  assert(Scope.isActive() && "double deactivation");

  // If it's the top of the stack, just pop it, but do so only if it belongs
  // to the current RunCleanupsScope.
  if (C == EHStack.stable_begin() &&
      CurrentCleanupScopeDepth.strictlyEncloses(C)) {
    // Per comment below, checking EHAsynch is not really necessary
    // it's there to assure zero-impact w/o EHAsynch option
    if (!Scope.isNormalCleanup() && getLangOpts().EHAsynch) {
      popCleanupBlock();
    } else {
      // If it's a normal cleanup, we need to pretend that the
      // fallthrough is unreachable.
      CGBuilderTy::InsertPoint SavedIP = Builder.saveAndClearIP();
      popCleanupBlock();
      Builder.restoreIP(SavedIP);
    }
    return;
  }

  // Otherwise, follow the general case.
  setupCleanupBlockActivation(*this, C, ForDeactivation, dominatingIP);

  Scope.setActive(false);
}

Address FunctionEmitter::getNormalCleanupDestSlot() {
  if (!NormalCleanupDest.isValid())
    NormalCleanupDest =
        createDefaultAlignTempAlloca(Builder.getInt32Ty(), "cleanup.dest.slot");
  return NormalCleanupDest;
}

namespace {
/// "funclet" OperandBundle is required for noThrow intrinsics (see
/// CallEmitter).
llvm::InvokeInst *genSehScope(FunctionEmitter &FE,
                              llvm::FunctionCallee &SehScope) {
  llvm::BasicBlock *InvokeDest = FE.getInvokeDest();
  if (!InvokeDest)
    return nullptr;
  if (!(FE.Builder.GetInsertBlock()))
    return nullptr; // Not found the insert point.
  llvm::BasicBlock *Cont = FE.createBasicBlock("invoke.cont");
  llvm::SmallVector<llvm::OperandBundleDef, 1> BundleList =
      FE.getBundlesForFunclet(SehScope.getCallee());
  if (FE.CurrentFuncletPad)
    BundleList.emplace_back("funclet", FE.CurrentFuncletPad);
  auto InvokeIst = FE.Builder.CreateInvoke(SehScope, Cont, InvokeDest,
                                           std::nullopt, BundleList);
  FE.genBlock(Cont);
  return InvokeIst;
}
} // namespace

llvm::InvokeInst *FunctionEmitter::genSehTryScopeBegin() {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(ME.VoidTy, /*isVarArg=*/false);
  llvm::FunctionCallee SehScope =
      ME.createRuntimeFunction(FTy, "llvm.seh.try.begin");
  return genSehScope(*this, SehScope);
}

// Invoke a llvm.seh.try.end at the end of a SEH scope for -EHa
llvm::InvokeInst *FunctionEmitter::genSehTryScopeEnd() {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(ME.VoidTy, /*isVarArg=*/false);
  llvm::FunctionCallee SehScope =
      ME.createRuntimeFunction(FTy, "llvm.seh.try.end");
  return genSehScope(*this, SehScope);
}
