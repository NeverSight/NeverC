#include "Core/AndroidKernelEmitter.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc::Emit;

// Emit placeholder sections that the arm64 module loader expects.
static void emitPLTSections(llvm::Module &M) {
  M.appendModuleInlineAsm(
      ".pushsection .plt,\"ax\",%progbits\n\t.byte 0\n\t.popsection\n"
      ".pushsection .init.plt,\"ax\",%progbits\n\t.byte 0\n\t.popsection\n"
      ".pushsection .text.ftrace_trampoline,\"ax\",%progbits\n\t"
      ".byte 0\n\t.popsection\n");
}

// Emit an empty __versions section so CONFIG_MODVERSIONS=y kernels find
// the section but skip all CRC checks.
static void emitEmptyVersionsSection(llvm::Module &M) {
  M.appendModuleInlineAsm(".pushsection __versions,\"a\"\n\t.popsection\n");
}

// Emit weak no-op stubs for __cfi_check and __cfi_check_fail.
// Uses inline asm to ensure PACIASP (hint #25) at function entry — GKI
// kernels with CONFIG_ARM64_BTI_KERNEL=y require a BTI-compatible landing
// pad on functions reached via indirect call (BLR from __cfi_slowpath).
static void emitCFIStubs(llvm::Module &M) {
  M.appendModuleInlineAsm(
      ".weak __cfi_check\n"
      ".type __cfi_check, %function\n"
      ".p2align 12\n"
      "__cfi_check:\n\thint #25\n\thint #29\n\tret\n"
      ".size __cfi_check, .-__cfi_check\n"
      ".weak __cfi_check_fail\n"
      ".type __cfi_check_fail, %function\n"
      "__cfi_check_fail:\n\thint #25\n\thint #29\n\tret\n"
      ".size __cfi_check_fail, .-__cfi_check_fail\n");
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch) {
  if (Arch != llvm::Triple::aarch64)
    return;

  emitPLTSections(M);
  emitEmptyVersionsSection(M);
  emitCFIStubs(M);
}
