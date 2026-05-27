#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace xorstr {

PreservedAnalyses XorStrCleanupPass::run(Function &F,
                                         FunctionAnalysisManager &FAM) {
  SmallVector<AllocaInst *, 8> XorstrAllocas;
  SmallVector<ReturnInst *, 4> Returns;

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (auto *AI = dyn_cast<AllocaInst>(&I)) {
        if (AI->hasMetadata("neverc.xorstr"))
          XorstrAllocas.push_back(AI);
      } else if (auto *RI = dyn_cast<ReturnInst>(&I)) {
        Returns.push_back(RI);
      }
    }
  }

  if (XorstrAllocas.empty())
    return PreservedAnalyses::all();

  const DataLayout &DL = F.getParent()->getDataLayout();

  for (ReturnInst *RI : Returns) {
    IRBuilder<> B(RI);
    for (AllocaInst *AI : XorstrAllocas) {
      Type *AllocTy = AI->getAllocatedType();
      uint64_t Size = DL.getTypeAllocSize(AllocTy);
      if (Size == 0)
        continue;
      B.CreateMemSet(AI, B.getInt8(0), Size, AI->getAlign(),
                     /*isVolatile=*/true);
    }
  }

  return PreservedAnalyses::none();
}

} // namespace xorstr
} // namespace neverc
