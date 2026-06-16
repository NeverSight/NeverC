#ifndef NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
#define NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H

namespace llvm {
class Module;
} // namespace llvm

namespace neverc::Emit::AndroidKernel {

/// Emit sections and symbols required for loading an out-of-tree module on
/// GKI (Generic Kernel Image) kernels.  Currently arm64-only.
///
/// This covers:
///  - .plt / .init.plt / .text.ftrace_trampoline placeholder sections
///  - __cfi_check / __cfi_check_fail weak no-op stubs (CONFIG_CFI_CLANG)
void emitFixups(llvm::Module &M, unsigned Arch);

} // namespace neverc::Emit::AndroidKernel

#endif // NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
