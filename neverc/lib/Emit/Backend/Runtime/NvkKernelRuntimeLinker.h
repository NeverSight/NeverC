#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIME_NVKKERNELRUNTIMELINKER_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIME_NVKKERNELRUNTIMELINKER_H

#include "llvm/IR/PassManager.h"

namespace neverc {

/// Module pass that links the precompiled NVK kernel runtime bitcode
/// into the user module.
///
/// NVK runtime headers expose non-inline functions and shared global variables
/// as extern declarations. This pass resolves used declarations from the
/// bootstrapped unity bitcode built from runtime/android/kernel/src, then
/// prunes unreachable runtime definitions.
///
/// Registered at PipelineStartEP when -fandroid-kernel-driver-mode is
/// active, so downstream optimisation can inline across the boundary.
///
/// Falls back gracefully: if the bitcode is empty (pre-bootstrap build)
/// or the target is not aarch64, the pass is a no-op.
struct NvkKernelRuntimeLinkerPass
    : public llvm::PassInfoMixin<NvkKernelRuntimeLinkerPass> {

  bool IsPreLink = false;

  NvkKernelRuntimeLinkerPass() = default;
  explicit NvkKernelRuntimeLinkerPass(bool PreLink) : IsPreLink(PreLink) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  static bool isRequired() { return true; }
};

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_RUNTIME_NVKKERNELRUNTIMELINKER_H
