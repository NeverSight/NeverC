#ifndef NEVERC_LIB_EMIT_BACKEND_STDRUNTIMELINKER_H
#define NEVERC_LIB_EMIT_BACKEND_STDRUNTIMELINKER_H

#include "llvm/IR/PassManager.h"

namespace neverc {

/// Module pass that links the precompiled std library bitcode into the
/// user module, pruning unreferenced functions via call-graph reachability.
///
/// Registered at PipelineStartEP (alongside StringRuntimeLinkerPass)
/// so that downstream optimisation passes see the full function bodies
/// and can inline across the user/std boundary.
struct StdRuntimeLinkerPass
    : public llvm::PassInfoMixin<StdRuntimeLinkerPass> {

  bool IsPreLink = false;

  StdRuntimeLinkerPass() = default;
  explicit StdRuntimeLinkerPass(bool PreLink) : IsPreLink(PreLink) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  static bool isRequired() { return true; }
};

} // namespace neverc

#endif
