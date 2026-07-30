#ifndef NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H
#define NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>
#include <string>

namespace llvm {
class Module;
class PassBuilder;
class TargetMachine;
class Triple;
} // namespace llvm

namespace neverc {

/// Content-addressed cache callbacks for individual codegen partitions,
/// injected by the linker driver (which owns cache storage and the
/// configuration digest).  With stable partition assignment, editing one
/// source file leaves most partitions' post-IPO bitcode unchanged, so
/// their optimization + codegen can be skipped on incremental relinks.
struct PartitionCacheHooks {
  /// (pipeTag, partitionBitcode) -> key + cached object.  Returns true on
  /// a hit with the object copied into the buffer; on a miss keyOut still
  /// receives the entry key for the matching Store call.
  std::function<bool(llvm::StringRef pipeTag, llvm::StringRef bitcode,
                     std::string &keyOut, llvm::SmallVectorImpl<char> &obj)>
      Lookup;
  /// Stores one produced partition object under a key from Lookup.
  std::function<void(llvm::StringRef key, llvm::ArrayRef<char> obj)> Store;

  bool enabled() const { return Lookup && Store; }
};

struct ParallelOptimizationHooks {
  std::function<void(llvm::PassBuilder &)> ConfigurePassBuilder;
  std::function<void(llvm::ModulePassManager &)> PreOpt;
  std::function<void(llvm::ModulePassManager &)> PostOpt;
};

/// The complete artifact set owned by one codegen request. A DWP is optional,
/// but when present parallel codegen commits it with the main object only after
/// both merges and their cross-artifact verification pass.
struct ParallelCodeGenOutputs {
  llvm::raw_pwrite_stream &Object;
  llvm::raw_pwrite_stream *DwarfPackage = nullptr;
};

/// Turn one serially generated object/DWO pair into the same verified artifact
/// model as parallel codegen: a main object plus an indexed DWP package.
/// Neither destination is written unless packaging and pair verification pass.
bool finalizeSplitDwarfArtifacts(const llvm::Triple &Target,
                                 llvm::ArrayRef<char> Object,
                                 llvm::ArrayRef<char> Dwo,
                                 llvm::DebugCompressionType DebugCompression,
                                 ParallelCodeGenOutputs Outputs,
                                 std::string *Error = nullptr);

/// Run parallel codegen on an already-optimized module.
/// Splits the module into \p NumPartitions, runs codegen in parallel,
/// then merges the resulting objects and optional DWO contributions into one
/// main object and indexed DWP package using `neverc::merge`.
/// Returns true on success.
bool runParallelCodeGen(llvm::Module &Mod, llvm::TargetMachine &TM,
                        ParallelCodeGenOutputs Outputs, unsigned NumPartitions,
                        const PartitionCacheHooks *Cache = nullptr);

/// Same as runParallelCodeGen but also runs function-level optimization
/// passes on each partition before codegen. Use when the input module has
/// only been through IPO simplification (not full optimization).
bool runParallelOptAndCodeGen(llvm::Module &Mod, llvm::TargetMachine &TM,
                              ParallelCodeGenOutputs Outputs,
                              unsigned NumPartitions, unsigned OptLevel,
                              const PartitionCacheHooks *Cache = nullptr,
                              const ParallelOptimizationHooks *Hooks = nullptr);

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H
