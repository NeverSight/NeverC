#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIME_STRINGRUNTIMELINKER_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIME_STRINGRUNTIMELINKER_H

#include "llvm/IR/PassManager.h"

namespace neverc {

/// Module pass that links the precompiled BuiltinString runtime
/// bitcode into the user module and internalises the symbols.
///
/// Registered at PipelineStartEP (before StringRuntimePass and the
/// dyncode pipeline) so that all downstream passes see the full
/// function bodies.
struct StringRuntimeLinkerPass
    : public llvm::PassInfoMixin<StringRuntimeLinkerPass> {

  bool IsPreLink = false;

  StringRuntimeLinkerPass() = default;
  explicit StringRuntimeLinkerPass(bool PreLink) : IsPreLink(PreLink) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  static bool isRequired() { return true; }
};

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_RUNTIME_STRINGRUNTIMELINKER_H
