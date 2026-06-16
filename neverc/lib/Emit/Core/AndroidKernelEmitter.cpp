#include "Core/AndroidKernelEmitter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
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

// Emit weak no-op stubs that CONFIG_CFI_CLANG kernels require.
// Without these the module loader rejects the .ko or panics.
static void emitCFICheckStubs(llvm::Module &M) {
  M.appendModuleInlineAsm(
      ".weak __cfi_check\n"
      ".type __cfi_check, %function\n"
      ".p2align 12\n"
      "__cfi_check:\n\thint #25\n\thint #29\n\tret\n"
      ".size __cfi_check, .-__cfi_check\n"
      ".weak __cfi_check_fail\n"
      ".hidden __cfi_check_fail\n"
      ".type __cfi_check_fail, %function\n"
      "__cfi_check_fail:\n\thint #25\n\thint #29\n\tret\n"
      ".size __cfi_check_fail, .-__cfi_check_fail\n");
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
    F.removeFnAttr(llvm::Attribute::UWTable);
    F.setUWTableKind(llvm::UWTableKind::None);
  }
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch) {
  if (Arch != llvm::Triple::aarch64)
    return;

  applyKernelFunctionAttrs(M);
  emitPLTSections(M);
  emitEmptyVersionsSection(M);
  emitCFICheckStubs(M);
}
