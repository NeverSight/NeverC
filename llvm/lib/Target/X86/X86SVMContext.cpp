//===- X86SVMContext.cpp - Isolate VMRUN compiler frames ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86SVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
bool outlineSVMContext(Function &F) {
  if (F.hasFnAttribute(X86::SVMContextAttribute)) {
    if (!X86::isSVMContextHelper(F))
      report_fatal_error("invalid VMRUN context helper");
    return false;
  }
  SmallVector<IntrinsicInst *, 4> Calls;
  for (auto &BB : F)
    for (auto &I : BB)
      if (auto *Call = dyn_cast<IntrinsicInst>(&I);
          Call && Call->getIntrinsicID() == Intrinsic::x86_vmrun_context)
        Calls.push_back(Call);
  for (auto *Call : Calls) {
    if (Call->hasOperandBundles())
      report_fatal_error("VMRUN context does not support operand bundles");
    const auto *Width = dyn_cast<ConstantInt>(Call->getArgOperand(1));
    if (!Width || (Width->getZExtValue() != 32 && Width->getZExtValue() != 64))
      report_fatal_error("VMRUN context address width must be 32 or 64");
    IRBuilder<> B(F.getContext());
    auto *Type = FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false);
    auto *Helper = Function::Create(Type, GlobalValue::InternalLinkage,
                                    "__llvm_x86_vmrun_context", F.getParent());
    Helper->addFnAttr(X86::SVMContextAttribute);
    Helper->addFnAttr("frame-pointer", "none");
    Helper->addFnAttr(Attribute::NoInline);
    Helper->addFnAttr(Attribute::NoRedZone);
    Helper->addFnAttr(Attribute::NoUnwind);
    Helper->addFnAttr(Attribute::Convergent);
    Helper->addFnAttr(Attribute::NoDuplicate);
    Helper->setUWTableKind(UWTableKind::Async);
    // The wrapper uses scalar moves and XSAVE only. Do not inherit a caller's
    // stack realignment, sanitizer instrumentation or frame-pointer policy.
    for (StringRef Name : {"target-cpu", "tune-cpu", "target-features"})
      if (F.hasFnAttribute(Name))
        Helper->addFnAttr(F.getFnAttribute(Name));
    B.SetInsertPoint(BasicBlock::Create(F.getContext(), "entry", Helper));
    B.CreateCall(Call->getCalledFunction(),
                 {Helper->getArg(0), Call->getArgOperand(1)});
    B.CreateRetVoid();
    B.SetInsertPoint(Call);
    auto *Replacement = B.CreateCall(Helper, {Call->getArgOperand(0)});
    Replacement->setDebugLoc(Call->getDebugLoc());
    Call->eraseFromParent();
  }
  return !Calls.empty();
}

class X86SVMContextPass : public FunctionPass {
public:
  static char ID;
  X86SVMContextPass() : FunctionPass(ID) {}
  StringRef getPassName() const override { return "X86 VMRUN context frames"; }
  bool runOnFunction(Function &F) override { return outlineSVMContext(F); }
};
char X86SVMContextPass::ID = 0;
} // namespace

FunctionPass *llvm::createX86SVMContextPass() {
  return new X86SVMContextPass();
}

PreservedAnalyses X86SVMContextLoweringPass::run(Function &F,
                                                 FunctionAnalysisManager &) {
  return outlineSVMContext(F) ? PreservedAnalyses::none()
                              : PreservedAnalyses::all();
}
