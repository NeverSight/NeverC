//===----------------------------------------------------------------------===//
//
//  Session — `CommonLinkerContext`, the one object that owns every
//  per-link resource: the bump allocator, the string saver, the map of
//  per-type `SpecificAlloc<T>` arenas and the `ErrorHandler`.
//
//  The active context is a nestable thread-local binding. Backend and worker
//  entry points install it with RAII; no process-global link state survives
//  task teardown.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_RUNTIME_SESSION_H
#define LINKER_CORE_RUNTIME_SESSION_H

#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "llvm/Support/StringSaver.h"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace llvm {
class raw_ostream;
class ThreadPool;
} // namespace llvm

namespace linker {

struct SpecificAllocBase;

struct LinkThreadPolicy {
  uint64_t MinParallelBytes = 16ULL * 1024ULL * 1024ULL;
  // Zero selects the complete available budget once MinParallelBytes is met.
  uint64_t BytesPerAdditionalThread = 32ULL * 1024ULL * 1024ULL;
  uint64_t MinAverageFileBytes = 4ULL * 1024ULL;
  // Zero leaves the automatic budget uncapped.
  unsigned MaxAutoThreads = 16;
};

/// Select a worker budget after a linker's materialized workload is known.
/// An explicit request is preserved; zero requests an automatic budget.
unsigned selectAdaptiveLinkThreadCount(unsigned RequestedThreads,
                                       unsigned AvailableThreads,
                                       uint64_t InputBytes, uint64_t InputFiles,
                                       LinkThreadPolicy Policy);

class CommonLinkerContext {
private:
  neverc::ResourceSessionView ResourceSession;

public:
  CommonLinkerContext();
  virtual ~CommonLinkerContext();
  CommonLinkerContext(const CommonLinkerContext &) = delete;
  CommonLinkerContext &operator=(const CommonLinkerContext &) = delete;
  void finalizeOwnedState() noexcept;
  void configureParallel(unsigned RequestedThreads,
                         unsigned DefaultThreadLimit = 0);
  /// Configure the worker pool from a materialized workload. When
  /// FinalizeSerial is false, an automatic one-thread result is left
  /// unconfigured so inputs discovered later may still raise the budget.
  unsigned configureParallelForInputWorkload(unsigned RequestedThreads,
                                             uint64_t InputBytes,
                                             uint64_t InputFiles,
                                             LinkThreadPolicy Policy = {},
                                             bool FinalizeSerial = true);
  bool parallelConfigured() const {
    return (StateFlags & ParallelConfiguredFlag) != 0;
  }
  unsigned parallelThreadCount() const { return ParallelThreadCount; }
  unsigned parallelShardCount() const {
    return ParallelPool ? ParallelThreadCount + 1 : 1;
  }
  bool parallelEnabled() const { return ParallelPool != nullptr; }
  llvm::ThreadPool *parallelPool() const { return ParallelPool.get(); }
  neverc::ResourceSessionView resourceSession() const {
    return ResourceSession;
  }
  unsigned workerSlotForCurrentThread();
  SpecificAllocBase *
  getOrCreateWorkerAllocator(const void *Tag, size_t Size, size_t Alignment,
                             SpecificAllocBase *(&Creator)(void *));

  llvm::BumpPtrAllocator bAlloc;
  llvm::StringSaver saver{bAlloc};
  llvm::DenseMap<const void *, SpecificAllocBase *> instances;
  std::vector<SpecificAllocBase *> instanceOrder;

  ErrorHandler e;

private:
  enum StateFlag : uint8_t {
    FinalizedFlag = 1U << 0,
    ParallelConfiguredFlag = 1U << 1,
  };

  CommonLinkerContext *PreviousContext = nullptr;
  unsigned PreviousWorkerSlot = 0;
  // This byte was the legacy Finalized field. Sharing it between internal
  // state bits keeps every following member and the total object size stable.
  uint8_t StateFlags = 0;
  std::mutex WorkerMutex;
  std::map<std::thread::id, unsigned> WorkerSlots;
  unsigned NextWorkerSlot = 1;
  std::map<std::pair<unsigned, const void *>, SpecificAllocBase *>
      WorkerInstances;
  std::unique_ptr<llvm::ThreadPool> ParallelPool;
  unsigned ParallelThreadCount = 1;
};

class LinkerContextGuard {
public:
  explicit LinkerContextGuard(CommonLinkerContext &Context,
                              unsigned WorkerSlot = 0);
  LinkerContextGuard(const LinkerContextGuard &) = delete;
  LinkerContextGuard &operator=(const LinkerContextGuard &) = delete;
  ~LinkerContextGuard();

private:
  neverc::ResourceSessionScope ResourceScope;
  CommonLinkerContext *Previous = nullptr;
  unsigned PreviousWorkerSlot = 0;
};

// Active task/worker-local context accessor.
CommonLinkerContext &commonContext();
CommonLinkerContext *currentLinkerContext() noexcept;
unsigned currentLinkerWorkerSlot() noexcept;

template <typename T = CommonLinkerContext> T &context() {
  return static_cast<T &>(commonContext());
}

bool hasContext();

inline llvm::StringSaver &saver() { return context().saver; }
inline llvm::BumpPtrAllocator &bAlloc() { return context().bAlloc; }

} // namespace linker

#endif // LINKER_CORE_RUNTIME_SESSION_H
