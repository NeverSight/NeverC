#include "neverc/Transforms/StrHash/StrHashFoldPass.h"
#include "neverc/Transforms/StrHash/StrHashCompute.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace strhash {

namespace {

StringRef getConstantStringFromArg(Value *V) {
  V = V->stripPointerCasts();
  GlobalVariable *GV = dyn_cast<GlobalVariable>(V);
  if (!GV)
    return StringRef();
  if (!GV->isConstant() || !GV->hasInitializer())
    return StringRef();
  auto *CDA = dyn_cast<ConstantDataArray>(GV->getInitializer());
  if (!CDA || !CDA->isString())
    return StringRef();
  return CDA->getAsString().drop_back(); // strip null terminator
}

bool isConstantLength(Value *LenArg, uint64_t &OutLen) {
  if (auto *CI = dyn_cast<ConstantInt>(LenArg)) {
    OutLen = CI->getZExtValue();
    return true;
  }
  return false;
}

#define STRHASH_FN_FNV32A "neverc_fnv_sum32a"
#define STRHASH_FN_FNV64A "neverc_fnv_sum64a"
#define STRHASH_FN_XXHASH64 "neverc_xxhash64"

#define STRHASH_ALGO_FNV32A 1u
#define STRHASH_ALGO_FNV64A 2u
#define STRHASH_ALGO_XXHASH64 3u

unsigned getAlgoForFunction(Function *F) {
  if (!F)
    return 0;
  StringRef Name = F->getName();
  if (Name == STRHASH_FN_FNV32A)
    return STRHASH_ALGO_FNV32A;
  if (Name == STRHASH_FN_FNV64A)
    return STRHASH_ALGO_FNV64A;
  if (Name == STRHASH_FN_XXHASH64)
    return STRHASH_ALGO_XXHASH64;
  return 0;
}

bool isAnyHashFunction(Function *F) {
  return getAlgoForFunction(F) != 0;
}

} // anonymous namespace

PreservedAnalyses StrHashFoldPass::run(Module &M,
                                       ModuleAnalysisManager &MAM) {
  SmallVector<CallInst *, 16> ToFold;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F) {
      for (auto &I : BB) {
        auto *CI = dyn_cast<CallInst>(&I);
        if (!CI)
          continue;
        Function *Callee = CI->getCalledFunction();
        if (!isAnyHashFunction(Callee))
          continue;

        unsigned CalleeAlgo = getAlgoForFunction(Callee);
        unsigned NumArgs = CI->arg_size();

        if ((CalleeAlgo <= STRHASH_ALGO_FNV64A && NumArgs != 2) ||
            (CalleeAlgo == STRHASH_ALGO_XXHASH64 && NumArgs != 3))
          continue;

        if (CalleeAlgo == STRHASH_ALGO_XXHASH64) {
          auto *SeedCI = dyn_cast<ConstantInt>(CI->getArgOperand(2));
          if (!SeedCI || !SeedCI->isZero())
            continue;
        }

        Value *DataArg = CI->getArgOperand(0);
        Value *LenArg = CI->getArgOperand(1);

        StringRef Str = getConstantStringFromArg(DataArg);
        if (Str.data() == nullptr)
          continue;

        uint64_t Len;
        if (!isConstantLength(LenArg, Len))
          continue;

        if (Len > Str.size())
          continue;

        ToFold.push_back(CI);
      }
    }
  }

  if (ToFold.empty())
    return PreservedAnalyses::all();

  for (CallInst *CI : ToFold) {
    Function *Callee = CI->getCalledFunction();
    unsigned CalleeAlgo = getAlgoForFunction(Callee);

    Value *DataArg = CI->getArgOperand(0);
    Value *LenArg = CI->getArgOperand(1);

    StringRef Str = getConstantStringFromArg(DataArg);
    uint64_t Len = cast<ConstantInt>(LenArg)->getZExtValue();
    StringRef Bytes = Str.substr(0, Len);

    uint64_t Hash = computeStrHash(Bytes, CalleeAlgo);

    Type *RetTy = CI->getType();
    Constant *Result = ConstantInt::get(RetTy, Hash);
    CI->replaceAllUsesWith(Result);
    CI->eraseFromParent();
  }

  return PreservedAnalyses::none();
}

} // namespace strhash
} // namespace neverc
