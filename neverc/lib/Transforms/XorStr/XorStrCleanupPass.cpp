#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"
#include "neverc/Foundation/Builtin/XorStrNames.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <limits>
#include <utility>

using namespace llvm;

namespace neverc {
namespace xorstr {

namespace {

constexpr StringLiteral CleanupMetadataName = "neverc.xorstr.cleanup";
constexpr StringLiteral MustTailDiagnosticMetadataName =
    "neverc.xorstr.musttail-diagnosed";
constexpr StringLiteral OutputDiagnosticMetadataName =
    "neverc.xorstr.output-diagnosed";

bool isGeneratedCleanup(const Instruction &I) {
  return isa<MemSetInst>(I) && I.hasMetadata(CleanupMetadataName);
}

MemSetInst *findCleanupInBlock(Instruction &Terminator,
                               const AllocaInst &Alloca, uint64_t Size) {
  for (const Instruction &I : *Terminator.getParent()) {
    if (&I == &Terminator)
      break;
    const auto *Memset = dyn_cast<MemSetInst>(&I);
    if (!Memset || !Memset->hasMetadata(CleanupMetadataName) ||
        !Memset->isVolatile() || Memset->getDest() != &Alloca)
      continue;
    const auto *Value = dyn_cast<ConstantInt>(Memset->getValue());
    const auto *Length = dyn_cast<ConstantInt>(Memset->getLength());
    if (Value && Value->isZero() && Length && Length->getZExtValue() == Size)
      return const_cast<MemSetInst *>(Memset);
  }
  return nullptr;
}

bool isInCleanupTail(const Instruction &Cleanup,
                     const Instruction &InsertionPoint) {
  for (const Instruction *I = Cleanup.getNextNode(); I; I = I->getNextNode()) {
    if (I == &InsertionPoint)
      return true;
    if (!isGeneratedCleanup(*I))
      return false;
  }
  return false;
}

bool isXorStrDecryptCall(const CallBase &CB) {
  const auto *Callee =
      dyn_cast<Function>(CB.getCalledOperand()->stripPointerCasts());
  return Callee && CB.arg_size() == 4 &&
         XorStrNames::isDecryptFunctionName(Callee->getName());
}

bool collectReferencedAllocas(Value *Root,
                              SmallPtrSetImpl<AllocaInst *> &Allocas) {
  if (!Root || !Root->getType()->isPointerTy())
    return false;

  SmallVector<Value *, 8> Worklist{Root};
  SmallPtrSet<Value *, 16> Seen;
  bool Complete = true;
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!V->getType()->isPointerTy()) {
      Complete = false;
      continue;
    }
    V = V->stripPointerCasts();
    if (!Seen.insert(V).second)
      continue;

    if (auto *AI = dyn_cast<AllocaInst>(getUnderlyingObject(V))) {
      Allocas.insert(AI);
      continue;
    }

    if (auto *CB = dyn_cast<CallBase>(V)) {
      if (isXorStrDecryptCall(*CB)) {
        Worklist.push_back(CB->getArgOperand(3));
      } else {
        Complete = false;
      }
      continue;
    }
    if (auto *PN = dyn_cast<PHINode>(V)) {
      for (Value *Incoming : PN->incoming_values())
        Worklist.push_back(Incoming);
      continue;
    }
    if (auto *SI = dyn_cast<SelectInst>(V)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }
    if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
      Worklist.push_back(GEP->getPointerOperand());
      continue;
    }
    if (auto *Cast = dyn_cast<CastInst>(V)) {
      Worklist.push_back(Cast->getOperand(0));
      continue;
    }
    if (auto *Freeze = dyn_cast<FreezeInst>(V)) {
      Worklist.push_back(Freeze->getOperand(0));
      continue;
    }
    if (auto *Load = dyn_cast<LoadInst>(V)) {
      // At -O0 a local pointer variable remains a load from a promotable
      // stack slot.  Follow every pointer stored into that exact slot instead
      // of relying on mem2reg having exposed the eventual select/phi already.
      auto *Slot =
          dyn_cast<AllocaInst>(getUnderlyingObject(Load->getPointerOperand()));
      if (!Slot || !Slot->getAllocatedType()->isPointerTy() ||
          !isAllocaPromotable(Slot)) {
        Complete = false;
        continue;
      }
      Function *F = Load->getFunction();
      bool SawStore = false;
      for (BasicBlock &BB : *F) {
        for (Instruction &I : BB) {
          auto *Store = dyn_cast<StoreInst>(&I);
          if (!Store || !Store->getValueOperand()->getType()->isPointerTy() ||
              getUnderlyingObject(Store->getPointerOperand()) != Slot)
            continue;
          SawStore = true;
          Worklist.push_back(Store->getValueOperand());
        }
      }
      Complete &= SawStore;
      continue;
    }

    // A decoder output must resolve completely to stack storage.  Silently
    // accepting a global, argument, null pointer, opaque call result, or an
    // unsupported pointer flow could leave runtime plaintext outside every
    // volatile wipe.  Provider/plugin rewrites that cannot prove this
    // invariant therefore fail closed.
    Complete = false;
  }
  return Complete;
}

bool valueReferencesAlloca(Value *Root, const AllocaInst &Alloca) {
  SmallPtrSet<AllocaInst *, 4> Referenced;
  (void)collectReferencedAllocas(Root, Referenced);
  return Referenced.contains(&Alloca);
}

bool mustTailReferencesAlloca(const CallInst &MustTail,
                              const AllocaInst &Alloca) {
  for (const Use &Arg : MustTail.args())
    if (valueReferencesAlloca(Arg.get(), Alloca))
      return true;
  return false;
}

} // namespace

PreservedAnalyses XorStrCleanupPass::run(Function &F,
                                         FunctionAnalysisManager &FAM) {
  SmallVector<AllocaInst *, 8> XorstrAllocas;
  SmallPtrSet<AllocaInst *, 8> SeenAllocas;
  SmallVector<Instruction *, 4> Exits;
  SmallVector<CatchSwitchInst *, 2> CatchSwitchExits;
  bool Changed = false;
  bool HasDecryptCall = false;

  auto RecordAlloca = [&](AllocaInst *Alloca) {
    if (!Alloca || !SeenAllocas.insert(Alloca).second)
      return;
    if (!Alloca->hasMetadata("neverc.xorstr")) {
      Alloca->setMetadata("neverc.xorstr", MDNode::get(F.getContext(), {}));
      Changed = true;
    }
    XorstrAllocas.push_back(Alloca);
  };

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (auto *AI = dyn_cast<AllocaInst>(&I)) {
        if (AI->hasMetadata("neverc.xorstr"))
          RecordAlloca(AI);
      } else if (auto *CB = dyn_cast<CallBase>(&I)) {
        if (isXorStrDecryptCall(*CB)) {
          HasDecryptCall = true;
          SmallPtrSet<AllocaInst *, 4> Referenced;
          const bool Complete =
              collectReferencedAllocas(CB->getArgOperand(3), Referenced);
          if ((!Complete || Referenced.empty()) &&
              !CB->hasMetadata(OutputDiagnosticMetadataName)) {
            F.getContext().emitError(
                "NeverC xorstr decoder output must resolve completely to "
                "fixed stack storage");
            CB->setMetadata(OutputDiagnosticMetadataName,
                            MDNode::get(F.getContext(), {}));
            Changed = true;
          }
          for (AllocaInst *AI : Referenced)
            RecordAlloca(AI);
        }
      } else if (isa<ReturnInst, ResumeInst>(&I)) {
        Exits.push_back(&I);
      } else if (auto *CleanupReturn = dyn_cast<CleanupReturnInst>(&I)) {
        if (CleanupReturn->unwindsToCaller())
          Exits.push_back(CleanupReturn);
      } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(&I)) {
        if (CatchSwitch->unwindsToCaller())
          CatchSwitchExits.push_back(CatchSwitch);
      }
    }
  }

  // The AArch64 and x86 machine outliners run after IR finalization and may
  // otherwise extract repeated portions of an inlined decrypt loop into a
  // shared helper.  A provider can obscure the output alloca while leaving a
  // valid decoder call, so the call itself is sufficient evidence even when
  // no wipe target can be recovered.
  if ((HasDecryptCall || !XorstrAllocas.empty()) &&
      !F.hasFnAttribute("nooutline")) {
    F.addFnAttr("nooutline");
    Changed = true;
  }

  if (XorstrAllocas.empty())
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();

  // A return-block wipe is invalid on a path where llvm.lifetime.end already
  // ended the protected object in a predecessor.  Optimizers may also stack-
  // color and reuse that storage before the wipe.  Keep xorstr buffers live
  // for the complete function instead: their secrecy boundary is stronger
  // than the allocation-lifetime optimization hint.
  SmallVector<IntrinsicInst *, 8> LifetimeMarkers;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      auto *II = dyn_cast<IntrinsicInst>(&I);
      if (!II || (II->getIntrinsicID() != Intrinsic::lifetime_start &&
                  II->getIntrinsicID() != Intrinsic::lifetime_end))
        continue;
      for (AllocaInst *AI : XorstrAllocas) {
        if (!valueReferencesAlloca(II->getArgOperand(1), *AI))
          continue;
        LifetimeMarkers.push_back(II);
        break;
      }
    }
  }
  for (IntrinsicInst *Marker : LifetimeMarkers) {
    Marker->eraseFromParent();
    Changed = true;
  }

  const DataLayout &DL = F.getParent()->getDataLayout();

  // A Windows catchswitch can unwind directly to its caller when none of its
  // handlers match.  Its block may contain only PHIs plus the catchswitch, so
  // a wipe cannot be inserted in place.  Route that edge through a sibling
  // cleanup funclet and let the ordinary exit handling below populate it.
  for (CatchSwitchInst *CatchSwitch : CatchSwitchExits) {
    BasicBlock *CleanupBlock =
        BasicBlock::Create(F.getContext(), "xorstr.cleanup.unwind", &F);
    IRBuilder<> CleanupBuilder(CleanupBlock);
    CleanupPadInst *CleanupPad = CleanupBuilder.CreateCleanupPad(
        CatchSwitch->getParentPad(), {}, "xorstr.cleanup.pad");
    CleanupReturnInst *CleanupReturn =
        CleanupBuilder.CreateCleanupRet(CleanupPad, /*UnwindBB=*/nullptr);

    auto *Replacement =
        CatchSwitchInst::Create(CatchSwitch->getParentPad(), CleanupBlock,
                                CatchSwitch->getNumHandlers(), "", CatchSwitch);
    for (BasicBlock *Handler : CatchSwitch->handlers())
      Replacement->addHandler(Handler);
    Replacement->takeName(CatchSwitch);
    Replacement->setDebugLoc(CatchSwitch->getDebugLoc());
    Replacement->copyMetadata(*CatchSwitch);
    CatchSwitch->replaceAllUsesWith(Replacement);
    CatchSwitch->eraseFromParent();

    Exits.push_back(CleanupReturn);
    Changed = true;
  }

  // The builtins deliberately create fixed-size entry allocas.  Be strict
  // when a provider or hand-written IR supplies a different shape: guessing
  // the byte count or placing a use on an exit not dominated by the alloca can
  // either leave plaintext behind or make the module invalid.  Constant
  // alloca counts are safe and are included in the wipe size; unsupported
  // dynamic/scalable/non-dominating storage fails compilation explicitly.
  DominatorTree DT(F);
  SmallVector<std::pair<AllocaInst *, uint64_t>, 8> ProtectedBuffers;
  for (AllocaInst *AI : XorstrAllocas) {
    TypeSize ElementSize = DL.getTypeAllocSize(AI->getAllocatedType());
    if (ElementSize.isScalable()) {
      F.getContext().emitError(
          "NeverC xorstr cleanup cannot wipe a scalable stack allocation");
      continue;
    }

    auto *ArraySize = dyn_cast<ConstantInt>(AI->getArraySize());
    if (!ArraySize || ArraySize->getValue().getActiveBits() > 64) {
      F.getContext().emitError(
          "NeverC xorstr cleanup requires a constant stack allocation size");
      continue;
    }
    const uint64_t Count = ArraySize->getZExtValue();
    const uint64_t ElementBytes = ElementSize.getFixedValue();
    if (Count == 0 || ElementBytes == 0 ||
        ElementBytes > std::numeric_limits<uint64_t>::max() / Count) {
      F.getContext().emitError(
          "NeverC xorstr cleanup has an invalid stack allocation size");
      continue;
    }

    bool DominatesEveryExit = true;
    for (Instruction *Exit : Exits) {
      if (!isPotentiallyReachable(AI, Exit, /*ExclusionSet=*/nullptr, &DT))
        continue;
      if (DT.dominates(AI, Exit))
        continue;
      DominatesEveryExit = false;
      break;
    }
    if (!DominatesEveryExit) {
      F.getContext().emitError(
          "NeverC xorstr stack buffer must dominate every function exit; "
          "move decryption to entry scope");
      continue;
    }
    ProtectedBuffers.emplace_back(AI, ElementBytes * Count);
  }

  for (Instruction *Exit : Exits) {
    Instruction *Boundary = Exit;
    auto *RI = dyn_cast<ReturnInst>(Exit);
    CallInst *MustTail =
        RI ? RI->getParent()->getTerminatingMustTailCall() : nullptr;
    if (MustTail)
      Boundary = MustTail;
    for (auto [AI, Size] : ProtectedBuffers) {
      if (!DT.dominates(AI, Exit))
        continue;
      if (MustTail && mustTailReferencesAlloca(*MustTail, *AI) &&
          !MustTail->hasMetadata(MustTailDiagnosticMetadataName)) {
        F.getContext().emitError(
            "NeverC xorstr stack buffer cannot be passed to a musttail call; "
            "remove musttail or decrypt in the callee");
        MustTail->setMetadata(MustTailDiagnosticMetadataName,
                              MDNode::get(F.getContext(), {}));
        Changed = true;
      }
      Instruction *InsertionPoint = Boundary;
      if (MemSetInst *Cleanup = findCleanupInBlock(*Exit, *AI, Size)) {
        if (!isInCleanupTail(*Cleanup, *InsertionPoint)) {
          Cleanup->moveBefore(InsertionPoint);
          Changed = true;
        }
        continue;
      }
      IRBuilder<> B(InsertionPoint);
      SmallVector<OperandBundleDef, 1> FuncletBundle;
      if (auto *CleanupReturn = dyn_cast<CleanupReturnInst>(Exit))
        FuncletBundle.emplace_back("funclet", CleanupReturn->getCleanupPad());
      IRBuilderBase::OperandBundlesGuard BundleGuard(B);
      B.setDefaultOperandBundles(FuncletBundle);
      CallInst *Cleanup = B.CreateMemSet(AI, B.getInt8(0), Size, AI->getAlign(),
                                         /*isVolatile=*/true);
      Cleanup->setMetadata(CleanupMetadataName,
                           MDNode::get(F.getContext(), {}));
      Changed = true;
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace xorstr
} // namespace neverc
