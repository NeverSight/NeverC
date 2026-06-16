#include "Core/AndroidKernelEmitter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc::Emit;

// Emit placeholder sections that the arm64 module loader expects.
// arch/arm64/kernel/module-plts.c rejects (-ENOEXEC) any module missing
// .plt / .init.plt; the in-tree build supplies these via module.lds.
// A single placeholder byte is sufficient — the loader overwrites their
// type/flags/size at load time.
static void emitPLTSections(llvm::Module &M) {
  M.appendModuleInlineAsm(
      ".pushsection .plt,\"ax\",%progbits\n\t.byte 0\n\t.popsection\n"
      ".pushsection .init.plt,\"ax\",%progbits\n\t.byte 0\n\t.popsection\n"
      ".pushsection .text.ftrace_trampoline,\"ax\",%progbits\n\t"
      ".byte 0\n\t.popsection\n");
}

// Emit weak no-op stubs for __cfi_check and __cfi_check_fail.
//
// GKI kernels built with CONFIG_CFI_CLANG=y call
// find_kallsyms_symbol_value(mod, "__cfi_check") during load_module().
// If found, mod->cfi_check is set to our function and __cfi_slowpath
// delegates to it instead of panicking.  On type mismatch __cfi_slowpath
// calls __cfi_check_fail.
//
// Providing weak return-only stubs for both satisfies the loader and
// effectively disables CFI enforcement for this module.
static void emitCFIStubs(llvm::Module &M) {
  llvm::LLVMContext &Ctx = M.getContext();
  auto *VoidTy = llvm::Type::getVoidTy(Ctx);
  auto *PtrTy = llvm::PointerType::getUnqual(Ctx);
  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);

  auto Stub = [&](llvm::StringRef Name, llvm::FunctionType *FTy,
                  llvm::MaybeAlign Align = std::nullopt) {
    auto *F = llvm::Function::Create(FTy, llvm::GlobalValue::WeakAnyLinkage,
                                     Name, &M);
    if (Align)
      F->setAlignment(*Align);
    llvm::ReturnInst::Create(Ctx, nullptr,
                             llvm::BasicBlock::Create(Ctx, "entry", F));
  };

  // void __cfi_check(uint64_t id, void *ptr, void *diag)
  Stub("__cfi_check",
       llvm::FunctionType::get(VoidTy, {I64Ty, PtrTy, PtrTy}, false),
       llvm::Align(4096));

  // void __cfi_check_fail(void *data, void *ptr)
  Stub("__cfi_check_fail",
       llvm::FunctionType::get(VoidTy, {PtrTy, PtrTy}, false));
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch) {
  if (Arch != llvm::Triple::aarch64)
    return;

  emitPLTSections(M);
  emitCFIStubs(M);
}
