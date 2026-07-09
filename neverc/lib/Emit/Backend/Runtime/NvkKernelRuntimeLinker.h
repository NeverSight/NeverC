#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIME_NVKKERNELRUNTIMELINKER_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIME_NVKKERNELRUNTIMELINKER_H

#include "llvm/IR/PassManager.h"

namespace neverc {

/// Module pass that links the precompiled NVK kernel runtime bitcode
/// into the user module.
///
/// NVK runtime headers (nvk_interpose.h, kallsyms.h, etc.) expose non-inline
/// functions and shared global variables as extern declarations via the
/// NVK_RT_FN / NVK_RT_VAR macros.  This pass resolves those declarations
/// by linking in the bitcode compiled from nvk_runtime_bc.c (which defines
/// everything with _NVK_IMPL).
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
