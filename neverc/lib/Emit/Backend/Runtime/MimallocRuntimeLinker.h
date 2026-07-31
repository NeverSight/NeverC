#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIME_MIMALLOCRUNTIMELINKER_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIME_MIMALLOCRUNTIMELINKER_H

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
/// Runtime-private definitions use hidden linkonce_odr linkage so copies from
/// explicit per-module injection coalesce. Allocator override entry points
/// remain default-visible weak_odr definitions.
struct MimallocRuntimeLinkerPass
    : public llvm::PassInfoMixin<MimallocRuntimeLinkerPass> {
  explicit MimallocRuntimeLinkerPass(bool RequiresProgramEntry = false)
      : RequiresProgramEntry(RequiresProgramEntry) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  static bool isRequired() { return true; }

private:
  bool RequiresProgramEntry;
};

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_RUNTIME_MIMALLOCRUNTIMELINKER_H
