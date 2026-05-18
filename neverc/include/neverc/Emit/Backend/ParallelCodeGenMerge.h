#ifndef NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H
#define NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class Module;
class TargetMachine;
} // namespace llvm

namespace neverc {

/// Run parallel codegen on an already-optimized module.
/// Splits the module into \p NumPartitions, runs codegen in parallel,
/// then merges the resulting object files using `neverc::merge`.
/// Returns true on success.
bool runParallelCodeGen(llvm::Module &Mod, llvm::TargetMachine &TM,
                        llvm::raw_pwrite_stream &OS, unsigned NumPartitions);

/// Same as runParallelCodeGen but also runs function-level optimization
/// passes on each partition before codegen. Use when the input module has
/// only been through IPO simplification (not full optimization).
bool runParallelOptAndCodeGen(llvm::Module &Mod, llvm::TargetMachine &TM,
                              llvm::raw_pwrite_stream &OS,
                              unsigned NumPartitions, unsigned OptLevel);

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H
