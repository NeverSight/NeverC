#ifndef NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
#define NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H

#include "neverc/Emit/AndroidKernelKCFI.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
} // namespace llvm

namespace neverc::Emit::AndroidKernel {

/// Emit sections, symbols, and linkage fixups required for loading an
/// out-of-tree module on GKI (Generic Kernel Image) kernels.  arm64-only.
void emitFixups(llvm::Module &M, unsigned Arch, KCFIMode Mode);

} // namespace neverc::Emit::AndroidKernel

#endif // NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
