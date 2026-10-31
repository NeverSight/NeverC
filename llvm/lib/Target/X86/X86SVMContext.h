//===- X86SVMContext.h - Explicit VMRUN register context -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86SVMCONTEXT_H
#define LLVM_LIB_TARGET_X86_X86SVMCONTEXT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class FunctionPass;
FunctionPass *createX86SVMContextPass();
class X86SVMContextLoweringPass
    : public PassInfoMixin<X86SVMContextLoweringPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

namespace X86 {
inline constexpr StringLiteral SVMContextAttribute = "x86-svm-context";

// The public context consists of four u64 control slots followed by fourteen
// u64 GPRs: RBX, RCX, RDX, RSI, RDI, RBP, R8..R15. RAX/RSP/RIP/RFLAGS are
// transported by VMRUN/VMEXIT through the VMCB, not through this register bank.
inline constexpr unsigned SVMVMCB = 0;
inline constexpr unsigned SVMGuestXState = 8;
inline constexpr unsigned SVMHostXState = 16;
inline constexpr unsigned SVMXStateMask = 24;
inline constexpr unsigned SVMGPRs = 32;

inline bool isSVMContextHelper(const Function &F) {
  if (!F.hasFnAttribute(SVMContextAttribute) || !F.hasLocalLinkage() ||
      F.getCallingConv() != CallingConv::C || F.arg_size() != 1 ||
      !F.getReturnType()->isVoidTy() ||
      !F.getArg(0)->getType()->isPointerTy() || F.size() != 1 ||
      F.front().size() != 2 ||
      F.getFnAttribute("frame-pointer").getValueAsString() != "none")
    return false;
  const auto *I = dyn_cast<IntrinsicInst>(&F.front().front());
  return I && I->getIntrinsicID() == Intrinsic::x86_vmrun_context &&
         I->getArgOperand(0) == F.getArg(0) &&
         isa<ReturnInst>(F.front().back());
}
} // namespace X86
} // namespace llvm

#endif // LLVM_LIB_TARGET_X86_X86SVMCONTEXT_H
