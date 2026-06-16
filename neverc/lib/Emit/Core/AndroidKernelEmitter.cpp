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

// Emit weak no-op stubs for __cfi_check / __cfi_check_fail plus the CFI
// jump-table entries and markers that the GKI module loader expects.
//
//   __cfi_check      — WEAK, 4K-aligned, no-op (PACIASP/AUTIASP/RET)
//   __cfi_check_fail — WEAK HIDDEN, no-op
//   __cfi_jt_start / __cfi_jt_end — empty jump-table region markers in .text
//   __cfi_jt_init_module / __cfi_jt_cleanup_module — func ptrs in .data
//   .note.Linux      — OPEN note the kernel expects for loadable modules
static void emitCFIStubs(llvm::Module &M) {
  M.appendModuleInlineAsm(
      // __cfi_check (weak, 4K-aligned, no-op)
      ".weak __cfi_check\n"
      ".type __cfi_check, %function\n"
      ".p2align 12\n"
      "__cfi_check:\n\thint #25\n\thint #29\n\tret\n"
      ".size __cfi_check, .-__cfi_check\n"
      // __cfi_check_fail (weak hidden, no-op)
      ".weak __cfi_check_fail\n"
      ".hidden __cfi_check_fail\n"
      ".type __cfi_check_fail, %function\n"
      "__cfi_check_fail:\n\thint #25\n\thint #29\n\tret\n"
      ".size __cfi_check_fail, .-__cfi_check_fail\n"
      // CFI jump-table region markers (empty range — no .text JT entries)
      ".global __cfi_jt_start\n"
      ".set __cfi_jt_start, .\n"
      ".global __cfi_jt_end\n"
      ".set __cfi_jt_end, .\n");

  // CFI jump-table data entries: function pointers for exported symbols.
  M.appendModuleInlineAsm(
      ".pushsection .data,\"aw\"\n"
      ".global __cfi_jt_init_module\n"
      ".type __cfi_jt_init_module, %object\n"
      ".size __cfi_jt_init_module, 8\n"
      "__cfi_jt_init_module:\n\t.quad init_module\n"
      ".global __cfi_jt_cleanup_module\n"
      ".type __cfi_jt_cleanup_module, %object\n"
      ".size __cfi_jt_cleanup_module, 8\n"
      "__cfi_jt_cleanup_module:\n\t.quad cleanup_module\n"
      ".popsection\n");

  // .note.Linux section (OPEN note expected by the kernel module loader).
  M.appendModuleInlineAsm(
      ".pushsection .note.Linux,\"a\",@note\n"
      ".balign 4\n"
      ".long 6\n"   // namesz = strlen("Linux") + 1
      ".long 1\n"   // descsz
      ".long 0x100\n" // type = LINUX_ELFNOTE_OPEN
      ".asciz \"Linux\"\n"
      ".balign 4\n"
      ".byte 0\n"   // description: 0 = not built-in
      ".balign 4\n"
      ".popsection\n");
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch) {
  if (Arch != llvm::Triple::aarch64)
    return;

  emitPLTSections(M);
  emitEmptyVersionsSection(M);
  emitCFIStubs(M);
}
