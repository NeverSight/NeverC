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
#include "llvm/Support/StringSaver.h"
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

class CommonLinkerContext {
public:
  CommonLinkerContext();
  virtual ~CommonLinkerContext();
  CommonLinkerContext(const CommonLinkerContext &) = delete;
  CommonLinkerContext &operator=(const CommonLinkerContext &) = delete;
  void finalizeOwnedState() noexcept;
  void configureParallel(unsigned RequestedThreads,
                         unsigned DefaultThreadLimit = 0);
  unsigned parallelThreadCount() const { return ParallelThreadCount; }
  unsigned parallelShardCount() const {
    return ParallelPool ? ParallelThreadCount + 1 : 1;
  }
  bool parallelEnabled() const { return ParallelPool != nullptr; }
  llvm::ThreadPool *parallelPool() const { return ParallelPool.get(); }
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
  CommonLinkerContext *PreviousContext = nullptr;
  bool Finalized = false;
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
