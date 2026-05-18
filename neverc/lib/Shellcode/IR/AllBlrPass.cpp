#include "neverc/Shellcode/IR/AllBlrPass.h"
#include "ExtractorCommon.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

bool shouldIndirect(const CallBase &CB) {
  if (CB.getIntrinsicID() != Intrinsic::not_intrinsic)
    return false;
  if (CB.isInlineAsm())
    return false;
  Function *Callee = CB.getCalledFunction();
  if (!Callee)
    return false;
  if (isShellcodeInternalRuntimeName(Callee->getName()))
    return false;
  return true;
}

AllocaInst *getOrInsertFnSlot(Function &F, Type *PtrTy) {
  BasicBlock &Entry = F.getEntryBlock();
  for (Instruction &I : Entry)
    if (auto *A = dyn_cast<AllocaInst>(&I))
      if (A->getName() == "__sc_blr_slot" && A->getAllocatedType() == PtrTy)
        return A;

  IRBuilder<> B(&Entry, Entry.getFirstInsertionPt());
  return B.CreateAlloca(PtrTy, nullptr, "__sc_blr_slot");
}

bool rewriteOne(CallBase &CB, AllocaInst *Slot) {
  Function *Callee = CB.getCalledFunction();
  if (!Callee)
    return false;

  Type *PtrTy = Slot->getAllocatedType();
  IRBuilder<> B(&CB);
  auto *Store = B.CreateStore(Callee, Slot);
  Store->setVolatile(true);
  auto *Load = B.CreateLoad(PtrTy, Slot, /*isVolatile=*/true, "__sc_blr_fn");

  CB.setCalledOperand(Load);
  if (auto *Call = dyn_cast<CallInst>(&CB))
    Call->setTailCallKind(CallInst::TCK_None);
  return true;
}

} // namespace

PreservedAnalyses AllBlrPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<CallBase *, 32> Calls;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    Calls.clear();
    for (Instruction &I : instructions(F))
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (shouldIndirect(*CB))
          Calls.push_back(CB);
    if (Calls.empty())
      continue;

    Type *PtrTy = PointerType::getUnqual(F.getContext());
    AllocaInst *Slot = getOrInsertFnSlot(F, PtrTy);

    for (CallBase *CB : Calls)
      if (rewriteOne(*CB, Slot))
        Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
