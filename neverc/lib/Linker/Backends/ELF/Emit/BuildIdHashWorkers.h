#ifndef LINKER_ELF_EMIT_BUILDIDHASHWORKERS_H
#define LINKER_ELF_EMIT_BUILDIDHASHWORKERS_H

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/thread.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace linker::elf::detail {

/// Select the physical worker count for a transient content build-id hash.
/// Small outputs remain serial; larger independent chunks use at most four
/// workers and never exceed the capacity granted by the process broker.
inline unsigned selectBuildIdHashWorkerCount(uint64_t OutputBytes,
                                             unsigned ChunkCount,
                                             unsigned AvailableWorkers) {
  constexpr uint64_t MinParallelOutputBytes = 2ULL * 1024 * 1024;
  constexpr unsigned MaxHashWorkers = 4;
  if (OutputBytes < MinParallelOutputBytes || ChunkCount < 2 ||
      AvailableWorkers < 2)
    return 1;
  return std::min(MaxHashWorkers, std::min(ChunkCount, AvailableWorkers));
}

/// Run independent build-id chunk work with up to WorkerCount physical
/// participants. TryStart may decline any optional helper; the caller then
/// computes every stripe that was not assigned to a live worker.
template <class ChunkFn, class TryStartFn>
void runBuildIdHashChunks(size_t ChunkCount, unsigned WorkerCount,
                          ChunkFn &&HashChunk, TryStartFn &&TryStart) {
  if (WorkerCount == 0)
    WorkerCount = 1;
  auto HashStripe = [&](unsigned WorkerIndex) {
    for (size_t I = WorkerIndex; I < ChunkCount; I += WorkerCount)
      HashChunk(I);
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

} // namespace linker::elf::detail

#endif // LINKER_ELF_EMIT_BUILDIDHASHWORKERS_H
