#ifndef NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
#define NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H

namespace llvm {
class Module;
} // namespace llvm

namespace neverc::Emit::AndroidKernel {

/// Emit sections, symbols, and linkage fixups required for loading an
/// out-of-tree module on GKI (Generic Kernel Image) kernels.  arm64-only.
///
/// NVK runtime global variables are now declared as `extern` in the headers
/// (via NVK_RT_VAR) and defined in precompiled bitcode, so all TUs share
/// a single copy automatically.  NvkKernelRuntimeLinkerPass links the
/// bitcode at compile time.
void emitFixups(llvm::Module &M, unsigned Arch);

} // namespace neverc::Emit::AndroidKernel

#endif // NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
