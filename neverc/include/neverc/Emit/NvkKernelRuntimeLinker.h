#ifndef NEVERC_EMIT_NVKKERNELRUNTIMELINKER_H
#define NEVERC_EMIT_NVKKERNELRUNTIMELINKER_H

#include "llvm/IR/PassManager.h"

namespace neverc {

/// Link the embedded Android-kernel runtime definitions needed by a module.
///
/// This is a public backend-boundary pass because both the frontend codegen
/// pipeline and the final LTO pipeline must materialize references introduced
/// by replacement providers or late IR passes.
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

#endif // NEVERC_EMIT_NVKKERNELRUNTIMELINKER_H
