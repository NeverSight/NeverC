#include "Core/AndroidKernelEmitter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc::Emit;

static llvm::GlobalVariable *
emitWeakPad(llvm::Module &M, llvm::StringRef Name, llvm::StringRef Section) {
  if (auto *GV = M.getGlobalVariable(Name))
    return GV;
  auto &Ctx = M.getContext();
  auto *I8 = llvm::Type::getInt8Ty(Ctx);
  auto *GV = new llvm::GlobalVariable(
      M, I8, true, llvm::GlobalValue::WeakAnyLinkage,
      llvm::ConstantInt::get(I8, 0), Name);
  GV->setSection(Section);
  GV->setAlignment(llvm::Align(1));
  GV->setDSOLocal(true);
  return GV;
}

static void emitPLTSections(llvm::Module &M) {
  emitWeakPad(M, "__nvk_plt", ".plt");
  emitWeakPad(M, "__nvk_init_plt", ".init.plt");
  emitWeakPad(M, "__nvk_ftrace", ".text.ftrace_trampoline");
}

static void emitEmptyVersionsSection(llvm::Module &M) {
  if (auto *GV = M.getGlobalVariable("__nvk_versions"))
    return;
  auto &Ctx = M.getContext();
  auto *Arr = llvm::ArrayType::get(llvm::Type::getInt8Ty(Ctx), 0);
  auto *GV = new llvm::GlobalVariable(
      M, Arr, true, llvm::GlobalValue::WeakAnyLinkage,
      llvm::ConstantAggregateZero::get(Arr), "__nvk_versions");
  GV->setSection("__versions");
  GV->setDSOLocal(true);
}

static void emitCFIStubFn(llvm::Module &M, llvm::StringRef Name,
                          llvm::GlobalValue::VisibilityTypes Vis,
                          unsigned Align) {
  if (M.getFunction(Name))
    return;
  auto &Ctx = M.getContext();
  auto *FTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
  auto *F = llvm::Function::Create(FTy, llvm::GlobalValue::WeakAnyLinkage,
                                   Name, &M);
  F->setVisibility(Vis);
  F->setDSOLocal(true);
  F->setAlignment(llvm::Align(Align));
  F->setSection(".text");
  F->addFnAttr(llvm::Attribute::Naked);
  F->addFnAttr(llvm::Attribute::NoUnwind);
  auto *BB = llvm::BasicBlock::Create(Ctx, "", F);
  auto *IAsmTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
  auto *Body = llvm::InlineAsm::get(IAsmTy, "hint #25\nhint #29\nret",
                                    "", true, false);
  auto *CI = llvm::CallInst::Create(IAsmTy, Body, "", BB);
  CI->setDoesNotThrow();
  new llvm::UnreachableInst(Ctx, BB);
}

static void emitCFICheckStubs(llvm::Module &M) {
  emitCFIStubFn(M, "__cfi_check",
                llvm::GlobalValue::DefaultVisibility, 4096);
  emitCFIStubFn(M, "__cfi_check_fail",
                llvm::GlobalValue::HiddenVisibility, 4);
}


// Apply per-function attributes that cannot be expressed via ToolChain flags:
//   ShadowCallStack  — -fsanitize=shadow-call-stack would pull in the
//                      sanitizer runtime which the kernel does not export
//   remove UWTable   — kernel modules do not use .eh_frame
static void applyKernelFunctionAttrs(llvm::Module &M) {
  for (llvm::Function &F : M) {
    if (F.isDeclaration())
      continue;
    F.addFnAttr(llvm::Attribute::ShadowCallStack);
    F.addFnAttr("branch-target-enforcement", "true");
    F.addFnAttr("sign-return-address", "all");
    F.addFnAttr("sign-return-address-key", "a_key");
    F.removeFnAttr(llvm::Attribute::UWTable);
    F.setUWTableKind(llvm::UWTableKind::None);
  }
}

llvm::PreservedAnalyses
AndroidKernel::KernelFunctionAttrsPass::run(llvm::Module &M,
                                            llvm::ModuleAnalysisManager &) {
  applyKernelFunctionAttrs(M);
  return llvm::PreservedAnalyses::none();
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch) {
  if (Arch != llvm::Triple::aarch64)
    return;

  emitPLTSections(M);
  emitEmptyVersionsSection(M);
  emitCFICheckStubs(M);
}
