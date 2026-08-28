#ifndef NEVERC_LIB_EMIT_BACKEND_PARALLELCODEGENMERGEINTERNAL_H
#define NEVERC_LIB_EMIT_BACKEND_PARALLELCODEGENMERGEINTERNAL_H

#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Foundation/LangOpts/ParallelCodeGenTuning.h"

#include "llvm/Support/NevercPipelineTuning.h"

#include <cstdint>
#include <optional>

namespace neverc {

enum class ParallelCodeGenWorkerPhase {
  Prepare,
  CodeGen,
  OptCodeGen,
};

enum class ParallelCodeGenRetentionPoint {
  AfterPrepare,
  AfterPartitionWorkReclaim,
  BeforeWholeModulePostOpt,
  BeforeObjectMerge,
  BeforeSplitDwarfMerge,
  Complete,
};

/// Coordinator-only ownership snapshot. Capacities, rather than logical
/// sizes, make the test seam describe memory the PCG request still retains.
/// It deliberately counts only partition/request storage, not the caller-owned
/// mother module or a reassembled whole-module value.
struct ParallelCodeGenRetentionSnapshot {
  unsigned LiveModules = 0;
  unsigned LiveContexts = 0;
  unsigned LiveTargetMachines = 0;
  /// Maximum number of fully prepared partition states retained at once
  /// during this request. Unlike the live counters above, this remains useful
  /// after worker-local state has been released.
  unsigned MaxLivePreparedPartitions = 0;
  std::uint64_t FullBitcodeCapacityBytes = 0;
  std::uint64_t ObjectBufferCapacityBytes = 0;
  std::uint64_t SplitDwarfBufferCapacityBytes = 0;
  std::uint64_t PendingOptimizedIRCapacityBytes = 0;
};

/// Saturating inputs to the request-local heavy-first scheduler. This is an
/// internal test seam used by the production ordering path, not an installed
/// C++ ABI.
struct ParallelCodeGenWorkEstimate {
  std::uint64_t InstructionWeight = 0;
  std::uint64_t LoopCount = 0;
};

void accumulateParallelCodeGenWorkEstimate(
    ParallelCodeGenWorkEstimate &Estimate, std::uint64_t InstructionWeight,
    std::uint64_t LoopCount);

std::uint64_t
scoreParallelCodeGenWork(const ParallelCodeGenWorkEstimate &Estimate,
                         unsigned WeightDiv, unsigned LoopDiv);

/// Diagnostic observations for request-local PCG tests and internal callers.
/// This is a lockstep implementation interface, not an installed C++ ABI.
struct ParallelCodeGenObservers {
  /// Coordinator-only snapshot of the actual ticket-to-partition mapping after
  /// the named worker queue has joined. Workers record into unique ticket slots
  /// and never invoke the callback. In a whole-module-barrier request, the
  /// outer Prepare/OptCodeGen pair is followed by the nested final
  /// Prepare/CodeGen pair. The ArrayRef is valid only for the callback.
  std::function<void(ParallelCodeGenWorkerPhase, llvm::ArrayRef<unsigned>)>
      ObservePartitionExecutionOrder;
  /// Coordinator-only record of the desired and physically granted workers
  /// immediately before each queue starts. Resource policy never enters a
  /// cache key, partition decision, or output identity.
  std::function<void(ParallelCodeGenWorkerPhase, unsigned, unsigned)>
      ObserveResourceWorkerGrant;
  std::function<void(unsigned)> ObserveResolvedFinalCodeGenPartitions;
  std::function<void(unsigned)> ObserveResolvedFinalCodeGenSCEVThreshold;
  /// Coordinator-only snapshot of every materialized final-codegen partition
  /// context, after preparation has joined and before codegen workers start.
  std::function<void(unsigned, const llvm::NevercPipelineTuningOptions &)>
      ObserveFinalCodeGenPartitionPipelineTuning;
  /// Invoked only by the coordinator after the relevant worker queue has
  /// joined. The snapshot is a value and remains valid after the callback.
  std::function<void(ParallelCodeGenRetentionPoint,
                     ParallelCodeGenRetentionSnapshot)>
      ObserveRetention;
};

/// Capture every registered PCG command-line option into one value. This is the
/// sole full-capture boundary used by frontend and legacy direct callers; LTO's
/// occurrence-only overlay below is the other global read boundary and runs
/// while its option-profile lease is held.
ParallelCodeGenTuning captureParallelCodeGenTuning();

/// Overlay only PCG options that actually occurred in the most recent LLVM
/// command-line parse. LLVM's parser remains authoritative for duplicate
/// ordering, split values, diagnostics, and last-occurrence wins.
ParallelCodeGenTuning
overlayOccurredParallelCodeGenTuning(ParallelCodeGenTuning Base);

/// Run parallel codegen with an explicit request-local PCG policy. The module
/// context supplies the matching pipeline policy.
bool runParallelCodeGenWithTuning(
    llvm::Module &Mod, llvm::TargetMachine &TM, ParallelCodeGenOutputs Outputs,
    const ParallelCodeGenTuning &Tuning,
    const PartitionCacheHooks *Cache = nullptr,
    const ParallelCodeGenObservers *Observers = nullptr);

/// Fully explicit internal variant. Both values are copied at request entry;
/// the pipeline value is installed on the mother context before any decline or
/// fallback, and all effective values join the direct cache pipe tag.
bool runParallelCodeGenWithTunings(
    llvm::Module &Mod, llvm::TargetMachine &TM, ParallelCodeGenOutputs Outputs,
    const ParallelCodeGenTuning &Tuning,
    const llvm::NevercPipelineTuningOptions &PipelineTuning,
    const PartitionCacheHooks *Cache = nullptr,
    std::optional<unsigned> ResolvedSCEVHugeExprThreshold = std::nullopt,
    const ParallelCodeGenObservers *Observers = nullptr);

/// Parallel partition optimization plus codegen with an explicit PCG policy.
/// The module context supplies the matching pipeline policy.
bool runParallelOptAndCodeGenWithTuning(
    llvm::Module &Mod, llvm::TargetMachine &TM, ParallelCodeGenOutputs Outputs,
    unsigned OptLevel, const ParallelCodeGenTuning &Tuning,
    const PartitionCacheHooks *Cache = nullptr,
    const ParallelOptimizationHooks *Hooks = nullptr,
    const ParallelCodeGenObservers *Observers = nullptr);

/// Fully explicit internal optimization variant. Prefer this when the caller
/// already owns both immutable request-policy values.
bool runParallelOptAndCodeGenWithTunings(
    llvm::Module &Mod, llvm::TargetMachine &TM, ParallelCodeGenOutputs Outputs,
    unsigned OptLevel, const ParallelCodeGenTuning &Tuning,
    const llvm::NevercPipelineTuningOptions &PipelineTuning,
    const PartitionCacheHooks *Cache = nullptr,
    const ParallelOptimizationHooks *Hooks = nullptr,
    const ParallelCodeGenObservers *Observers = nullptr,
    std::optional<unsigned> ResolvedSCEVHugeExprThreshold = std::nullopt);

} // namespace neverc

#endif // NEVERC_LIB_EMIT_BACKEND_PARALLELCODEGENMERGEINTERNAL_H
