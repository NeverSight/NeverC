//===- PreISelIntrinsicLowering.cpp - Pre-ISel intrinsic lowering pass ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR lowering for the llvm.memcpy, llvm.memmove,
// llvm.memset and llvm.load.relative intrinsics.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/PreISelIntrinsicLowering.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

using namespace llvm;

/// Threshold to leave statically sized memory intrinsic calls. Calls of known
/// size larger than this will be expanded by the pass. Calls of unknown or
/// lower size will be left for expansion in codegen.
static cl::opt<int64_t> MemIntrinsicExpandSizeThresholdOpt(
    "mem-intrinsic-expand-size",
    cl::desc("Set minimum mem intrinsic size to expand in IR"), cl::init(-1),
    cl::Hidden);

namespace {

struct PreISelIntrinsicLowering {
  const TargetMachine &TM;
  const function_ref<TargetTransformInfo &(Function &)> LookupTTI;

  /// If this is true, assume it's preferably to leave memory intrinsic calls
  /// for replacement with a library call later. Otherwise this depends on
  /// TargetLoweringInfo availability of the corresponding function.
  const bool UseMemIntrinsicLibFunc;

  explicit PreISelIntrinsicLowering(
      const TargetMachine &TM_,
      function_ref<TargetTransformInfo &(Function &)> LookupTTI_,
      bool UseMemIntrinsicLibFunc_ = true)
      : TM(TM_), LookupTTI(LookupTTI_),
        UseMemIntrinsicLibFunc(UseMemIntrinsicLibFunc_) {}

  static bool shouldExpandMemIntrinsicWithSize(Value *Size,
                                               const TargetTransformInfo &TTI);
  bool expandMemIntrinsicUses(Function &F) const;
  bool lowerIntrinsics(Module &M) const;
};

} // namespace

static bool lowerLoadRelative(Function &F) {
  if (F.use_empty())
    return false;

  bool Changed = false;
  Type *Int32Ty = Type::getInt32Ty(F.getContext());
  Type *Int8Ty = Type::getInt8Ty(F.getContext());

  for (Use &U : llvm::make_early_inc_range(F.uses())) {
    auto CI = dyn_cast<CallInst>(U.getUser());
    if (!CI || CI->getCalledOperand() != &F)
      continue;

    IRBuilder<> B(CI);
    Value *OffsetPtr =
        B.CreateGEP(Int8Ty, CI->getArgOperand(0), CI->getArgOperand(1));
    Value *OffsetI32 = B.CreateAlignedLoad(Int32Ty, OffsetPtr, Align(4));

    Value *ResultPtr = B.CreateGEP(Int8Ty, CI->getArgOperand(0), OffsetI32);

    CI->replaceAllUsesWith(ResultPtr);
    CI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

// TODO: Should refine based on estimated number of accesses (e.g. does it
// require splitting based on alignment)
bool PreISelIntrinsicLowering::shouldExpandMemIntrinsicWithSize(
    Value *Size, const TargetTransformInfo &TTI) {
  ConstantInt *CI = dyn_cast<ConstantInt>(Size);
  if (!CI)
    return true;
  uint64_t Threshold = MemIntrinsicExpandSizeThresholdOpt.getNumOccurrences()
                           ? MemIntrinsicExpandSizeThresholdOpt
                           : TTI.getMaxMemIntrinsicInlineSizeThreshold();
  uint64_t SizeVal = CI->getZExtValue();

  // Treat a threshold of 0 as a special case to force expansion of all
  // intrinsics, including size 0.
  return SizeVal > Threshold || Threshold == 0;
}

static bool canEmitLibcall(const TargetMachine &TM, Function *F,
                           RTLIB::Libcall LC) {
  // TODO: Should this consider the address space of the memcpy?
  const TargetLowering *TLI = TM.getSubtargetImpl(*F)->getTargetLowering();
  return TLI->getLibcallName(LC) != nullptr;
}

// TODO: Handle atomic memcpy and memcpy.inline
// TODO: Pass ScalarEvolution
bool PreISelIntrinsicLowering::expandMemIntrinsicUses(Function &F) const {
  Intrinsic::ID ID = F.getIntrinsicID();
  bool Changed = false;

  for (User *U : llvm::make_early_inc_range(F.users())) {
    Instruction *Inst = cast<Instruction>(U);

    switch (ID) {
    case Intrinsic::memcpy: {
      auto *Memcpy = cast<MemCpyInst>(Inst);
      Function *ParentFunc = Memcpy->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      if (shouldExpandMemIntrinsicWithSize(Memcpy->getLength(), TTI)) {
        if (UseMemIntrinsicLibFunc &&
            canEmitLibcall(TM, ParentFunc, RTLIB::MEMCPY))
          break;

        // TODO: For optsize, emit the loop into a separate function
        expandMemCpyAsLoop(Memcpy, TTI);
        Changed = true;
        Memcpy->eraseFromParent();
      }

      break;
    }
    case Intrinsic::memmove: {
      auto *Memmove = cast<MemMoveInst>(Inst);
      Function *ParentFunc = Memmove->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      if (shouldExpandMemIntrinsicWithSize(Memmove->getLength(), TTI)) {
        if (UseMemIntrinsicLibFunc &&
            canEmitLibcall(TM, ParentFunc, RTLIB::MEMMOVE))
          break;

        if (expandMemMoveAsLoop(Memmove, TTI)) {
          Changed = true;
          Memmove->eraseFromParent();
        }
      }

      break;
    }
    case Intrinsic::memset: {
      auto *Memset = cast<MemSetInst>(Inst);
      Function *ParentFunc = Memset->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      if (shouldExpandMemIntrinsicWithSize(Memset->getLength(), TTI)) {
        if (UseMemIntrinsicLibFunc &&
            canEmitLibcall(TM, ParentFunc, RTLIB::MEMSET))
          break;

        expandMemSetAsLoop(Memset);
        Changed = true;
        Memset->eraseFromParent();
      }

      break;
    }
    default:
      llvm_unreachable("unhandled intrinsic");
    }
  }

  return Changed;
}

bool PreISelIntrinsicLowering::lowerIntrinsics(Module &M) const {
  bool Changed = false;
  for (Function &F : M) {
    switch (F.getIntrinsicID()) {
    default:
      break;
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset:
      Changed |= expandMemIntrinsicUses(F);
      break;
    case Intrinsic::load_relative:
      Changed |= lowerLoadRelative(F);
      break;
    }
  }
  return Changed;
}

namespace {

class PreISelIntrinsicLoweringLegacyPass : public ModulePass {
public:
  static char ID;

  PreISelIntrinsicLoweringLegacyPass() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
  }

  bool runOnModule(Module &M) override {
    auto LookupTTI = [this](Function &F) -> TargetTransformInfo & {
      return this->getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    };

    const auto &TM = getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    PreISelIntrinsicLowering Lowering(TM, LookupTTI);
    return Lowering.lowerIntrinsics(M);
  }
};

} // end anonymous namespace

char PreISelIntrinsicLoweringLegacyPass::ID;

INITIALIZE_PASS_BEGIN(PreISelIntrinsicLoweringLegacyPass,
                      "pre-isel-intrinsic-lowering",
                      "Pre-ISel Intrinsic Lowering", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(PreISelIntrinsicLoweringLegacyPass,
                    "pre-isel-intrinsic-lowering",
                    "Pre-ISel Intrinsic Lowering", false, false)

ModulePass *llvm::createPreISelIntrinsicLoweringPass() {
  return new PreISelIntrinsicLoweringLegacyPass();
}

PreservedAnalyses PreISelIntrinsicLoweringPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto LookupTTI = [&FAM](Function &F) -> TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };

  PreISelIntrinsicLowering Lowering(TM, LookupTTI);
  if (!Lowering.lowerIntrinsics(M))
    return PreservedAnalyses::all();
  else
    return PreservedAnalyses::none();
}
