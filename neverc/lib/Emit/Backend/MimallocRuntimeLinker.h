#ifndef NEVERC_LIB_EMIT_BACKEND_MIMALLOCRUNTIMELINKER_H
#define NEVERC_LIB_EMIT_BACKEND_MIMALLOCRUNTIMELINKER_H

#include "llvm/IR/PassManager.h"

namespace neverc {

/// Module pass that links the precompiled mimalloc runtime bitcode into
/// the user module with whole-archive semantics (all symbols preserved).
///
/// Unlike StringRuntimeLinkerPass, this pass does NOT prune unused
/// functions — mimalloc's override mechanism requires the full set of
/// allocation entry points to be present so that the system linker
/// resolves malloc/free/calloc/realloc to mimalloc's implementations.
///
/// Internal helper functions are internalized; only the public
/// allocation API entry points retain external linkage.
struct MimallocRuntimeLinkerPass
    : public llvm::PassInfoMixin<MimallocRuntimeLinkerPass> {

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  static bool isRequired() { return true; }
};

} // namespace neverc

#endif
