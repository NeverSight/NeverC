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

/// Apply ShadowCallStack, BTI, PAC-RET attributes and strip UWTable from
/// every non-declaration function.  Runs at pipeline start, right after
/// the runtime linker passes, so that functions merged from the NVK /
/// std / string / mimalloc bitcode libraries are covered.  User code
/// already has these attributes from the frontend flags.
class KernelFunctionAttrsPass
    : public llvm::PassInfoMixin<KernelFunctionAttrsPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace neverc::Emit::AndroidKernel

#endif // NEVERC_LIB_EMIT_CORE_ANDROIDKERNELEMITTER_H
