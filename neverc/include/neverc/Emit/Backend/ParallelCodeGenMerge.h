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
  /// (pipeTag, partitionBitcode) -> key + cached artifact.  Returns true on
  /// a hit with the artifact copied into the buffer; on a miss keyOut still
  /// receives the entry key retained by the coordinator for a later Store.
  std::function<bool(llvm::StringRef pipeTag, llvm::StringRef bitcode,
                     std::string &keyOut,
                     llvm::SmallVectorImpl<char> &artifact)>
      Lookup;
  /// Commits one newly produced partition artifact under a miss key from
  /// Lookup.  Only the coordinator calls Store, after aggregate validation
  /// succeeds (including object merge/self-verification or whole-module
  /// reassembly and final codegen as applicable); workers never call it.
  std::function<void(llvm::StringRef key, llvm::ArrayRef<char> artifact)> Store;
  // Parallel opt computes its key before post-opt hooks run. When that module
  // can reach xorstr finalization and the link uses fresh entropy, the key
  // cannot safely replay a previously finalized object.
  bool BypassForUnseededXorStr = false;
  // Automatic encryption may create the first decoder only in a post-opt hook,
  // so pre-hook marker scanning alone is not sufficient.
  bool AutomaticXorStrEnabled = false;

  bool enabled() const { return Lookup && Store; }
};

struct ParallelOptimizationHooks {
  std::function<void(llvm::PassBuilder &)> ConfigurePassBuilder;
  std::function<void(llvm::ModulePassManager &)> PreOpt;
  std::function<void(llvm::ModulePassManager &)> PostOpt;
  /// Runs once after all optimized partitions have been reassembled into a
  /// complete module and before that module is split again for code generation.
  /// Use this for module-scope plugins and any pass that owns global symbols or
  /// module-wide state.
  std::function<void(llvm::ModulePassManager &)> WholeModulePostOpt;
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

/// ABI-compatible wrapper for existing callers. \p NumPartitions is retained
/// in the signature as the caller-side parallel-hook enablement sentinel; the
/// implementation has always resolved its deterministic count from module work
/// and PCG policy.
bool runParallelCodeGen(llvm::Module &Mod, llvm::TargetMachine &TM,
                        ParallelCodeGenOutputs Outputs, unsigned NumPartitions,
                        const PartitionCacheHooks *Cache = nullptr);

/// ABI-compatible optimization wrapper. \p NumPartitions is retained only for
/// the legacy call contract.
bool runParallelOptAndCodeGen(llvm::Module &Mod, llvm::TargetMachine &TM,
                              ParallelCodeGenOutputs Outputs,
                              unsigned NumPartitions, unsigned OptLevel,
                              const PartitionCacheHooks *Cache = nullptr,
                              const ParallelOptimizationHooks *Hooks = nullptr);

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_PARALLEL_CODEGEN_MERGE_H
