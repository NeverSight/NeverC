#ifndef LINKER_CORE_RUNTIME_CONTENTHASHWORKERS_H
#define LINKER_CORE_RUNTIME_CONTENTHASHWORKERS_H

#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Runtime/Session.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/thread.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace linker::detail {

inline constexpr uint64_t DefaultMinParallelContentHashBytes =
    2ULL * 1024ULL * 1024ULL;
inline constexpr unsigned DefaultMaxTransientContentHashWorkers = 4;

/// Select the physical worker count for independent content-hash chunks.
/// Small outputs remain serial and transient worker creation is bounded even
/// on hosts with a very large CPU affinity mask.
inline unsigned selectContentHashWorkerCount(
    uint64_t OutputBytes, unsigned ChunkCount, unsigned AvailableWorkers,
    uint64_t MinParallelBytes = DefaultMinParallelContentHashBytes,
    unsigned MaxWorkers = DefaultMaxTransientContentHashWorkers) {
  if (OutputBytes < MinParallelBytes || ChunkCount < 2 ||
      AvailableWorkers < 2 || MaxWorkers < 2)
    return 1;
  return std::min(MaxWorkers, std::min(ChunkCount, AvailableWorkers));
}

/// Run independent chunk stripes with up to WorkerCount physical
/// participants. TryStart may decline any optional helper; the calling thread
/// then computes every stripe that was not assigned to a live worker.
template <class ChunkFn, class TryStartFn>
void runContentHashChunkStripes(size_t ChunkCount, unsigned WorkerCount,
                                ChunkFn &&HashChunk, TryStartFn &&TryStart) {
  if (WorkerCount == 0)
    WorkerCount = 1;
  auto HashStripe = [&](unsigned WorkerIndex) {
    for (size_t I = WorkerIndex; I < ChunkCount; I += WorkerCount)
      std::invoke(HashChunk, I);
  };

  llvm::SmallVector<llvm::thread, 3> Workers;
  if (WorkerCount > 1)
    Workers.reserve(WorkerCount - 1);
  auto JoinWorkers = llvm::make_scope_exit([&] {
    for (llvm::thread &Worker : Workers)
      Worker.join();
  });
  unsigned FirstUnstartedWorker = WorkerCount;
  for (unsigned WorkerIndex = 1; WorkerIndex < WorkerCount; ++WorkerIndex) {
    Workers.emplace_back();
    if (!TryStart(Workers.back(),
                  [&, WorkerIndex] { HashStripe(WorkerIndex); })) {
      Workers.pop_back();
      FirstUnstartedWorker = WorkerIndex;
      break;
    }
  }

  HashStripe(/*WorkerIndex=*/0);
  for (unsigned WorkerIndex = FirstUnstartedWorker; WorkerIndex < WorkerCount;
       ++WorkerIndex)
    HashStripe(WorkerIndex);
  for (llvm::thread &Worker : Workers)
    Worker.join();
  JoinWorkers.release();
}

/// Execute a content-hash map phase without changing its byte-level reduction
/// semantics. Existing linker pools are reused. If automatic policy left a
/// link serial, a large output may borrow a bounded LinkWrite grant and start
/// recoverable transient helpers. Callbacks must touch only disjoint output
/// slots and must not access context-owned allocators or diagnostics.
template <class ChunkFn>
void runContentHashChunks(
    uint64_t OutputBytes, size_t ChunkCount, bool ExplicitlySerial,
    ChunkFn &&HashChunk,
    uint64_t MinParallelBytes = DefaultMinParallelContentHashBytes,
    bool AllowTransientWorkers = true) {
  if (ChunkCount == 0)
    return;

  auto RunSerial = [&] {
    for (size_t I = 0; I < ChunkCount; ++I)
      std::invoke(HashChunk, I);
  };
  const unsigned BoundedChunkCount = static_cast<unsigned>(
      std::min<size_t>(ChunkCount, std::numeric_limits<unsigned>::max()));
  if (ExplicitlySerial || selectContentHashWorkerCount(
                              OutputBytes, BoundedChunkCount,
                              /*AvailableWorkers=*/2, MinParallelBytes) == 1) {
    RunSerial();
    return;
  }

  if (parallelEnabled()) {
    auto FunctionOwner = std::make_shared<std::decay_t<ChunkFn>>(
        std::forward<ChunkFn>(HashChunk));
    const size_t TaskCount =
        std::min(ChunkCount, static_cast<size_t>(parallelThreadCount()) * 4);
    const size_t TaskSize = (ChunkCount + TaskCount - 1) / TaskCount;
    LinkerTaskGroup Group(neverc::ResourcePhase::LinkWrite);
    for (size_t TaskBegin = 0; TaskBegin < ChunkCount; TaskBegin += TaskSize) {
      const size_t TaskEnd = std::min(ChunkCount, TaskBegin + TaskSize);
      Group.spawn([FunctionOwner, TaskBegin, TaskEnd] {
        for (size_t I = TaskBegin; I < TaskEnd; ++I)
          std::invoke(*FunctionOwner, I);
      });
    }
    return;
  }

  if (!AllowTransientWorkers || currentLinkerWorkerSlot() != 0 ||
      !hasContext()) {
    RunSerial();
    return;
  }

  const unsigned AvailableWorkers =
      std::min(DefaultMaxTransientContentHashWorkers,
               llvm::thread::hardware_concurrency());
  const unsigned DesiredWorkers = selectContentHashWorkerCount(
      OutputBytes, BoundedChunkCount, AvailableWorkers, MinParallelBytes);
  if (DesiredWorkers < 2) {
    RunSerial();
    return;
  }

  neverc::ResourceWorkerGrant Grant =
      neverc::ProcessResourceBroker::global().grantWorkers(
          commonContext().resourceSession(), neverc::ResourcePhase::LinkWrite,
          DesiredWorkers);
  const unsigned WorkerCount = selectContentHashWorkerCount(
      OutputBytes, BoundedChunkCount, Grant.workerCount(), MinParallelBytes);
  if (WorkerCount < 2) {
    RunSerial();
    return;
  }

  runContentHashChunkStripes(ChunkCount, WorkerCount, HashChunk,
                             [](llvm::thread &Worker, auto HashWorkerStripe) {
                               return Worker.try_create(
                                   /*StackSizeInBytes=*/0,
                                   std::move(HashWorkerStripe));
                             });
}

} // namespace linker::detail

#endif // LINKER_CORE_RUNTIME_CONTENTHASHWORKERS_H
