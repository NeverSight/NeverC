//===----------------------------------------------------------------------===//
//
//  Session — task-local ownership for linker allocator and diagnostics.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include <algorithm>

using namespace llvm;
using namespace linker;

namespace {
thread_local CommonLinkerContext *ActiveLinkerContext = nullptr;
thread_local unsigned CurrentWorkerSlot = 0;
} // namespace

CommonLinkerContext::CommonLinkerContext()
    : PreviousContext(ActiveLinkerContext) {
  ActiveLinkerContext = this;
  CurrentWorkerSlot = 0;
  WorkerSlots.emplace(std::this_thread::get_id(), 0);
}

CommonLinkerContext::~CommonLinkerContext() {
  assert(ActiveLinkerContext == this &&
         "linker context destroyed outside its active scope");
  finalizeOwnedState();
  ActiveLinkerContext = PreviousContext;
  CurrentWorkerSlot = 0;
}

void CommonLinkerContext::finalizeOwnedState() noexcept {
  if (Finalized)
    return;
  Finalized = true;
  ParallelPool.reset();
  e.runCleanup();
  for (auto It = instanceOrder.rbegin(); It != instanceOrder.rend(); ++It)
    (*It)->destroy();
  instances.clear();
  WorkerInstances.clear();
  instanceOrder.clear();
}

void CommonLinkerContext::configureParallel(unsigned RequestedThreads,
                                            unsigned DefaultThreadLimit) {
  assert(!ParallelPool && "parallel runtime already configured");
  ThreadPoolStrategy Strategy = hardware_concurrency(RequestedThreads);
  unsigned ThreadCount = std::max(1U, Strategy.compute_thread_count());
  if (RequestedThreads == 0 && DefaultThreadLimit != 0 &&
      ThreadCount > DefaultThreadLimit) {
    Strategy = hardware_concurrency(DefaultThreadLimit);
    ThreadCount = DefaultThreadLimit;
  }
  ParallelThreadCount = ThreadCount;
  if (ThreadCount > 1)
    ParallelPool = std::make_unique<ThreadPool>(Strategy);
}

unsigned CommonLinkerContext::workerSlotForCurrentThread() {
  std::lock_guard<std::mutex> Lock(WorkerMutex);
  auto [It, Inserted] =
      WorkerSlots.try_emplace(std::this_thread::get_id(), NextWorkerSlot);
  if (Inserted)
    ++NextWorkerSlot;
  return It->second;
}

SpecificAllocBase *CommonLinkerContext::getOrCreateWorkerAllocator(
    const void *Tag, size_t Size, size_t Alignment,
    SpecificAllocBase *(&Creator)(void *)) {
  const unsigned Slot = workerSlotForCurrentThread();
  std::lock_guard<std::mutex> Lock(WorkerMutex);
  SpecificAllocBase *&Instance = WorkerInstances[{Slot, Tag}];
  if (!Instance) {
    void *Storage = bAlloc.Allocate(Size, Alignment);
    Instance = Creator(Storage);
    instanceOrder.push_back(Instance);
  }
  return Instance;
}

CommonLinkerContext &linker::commonContext() {
  assert(ActiveLinkerContext && "no active linker execution context");
  return *ActiveLinkerContext;
}

CommonLinkerContext *linker::currentLinkerContext() noexcept {
  return ActiveLinkerContext;
}

unsigned linker::currentLinkerWorkerSlot() noexcept {
  return CurrentWorkerSlot;
}

bool linker::hasContext() { return ActiveLinkerContext != nullptr; }

LinkerContextGuard::LinkerContextGuard(CommonLinkerContext &Context,
                                       unsigned WorkerSlot)
    : Previous(ActiveLinkerContext), PreviousWorkerSlot(CurrentWorkerSlot) {
  ActiveLinkerContext = &Context;
  CurrentWorkerSlot = WorkerSlot;
}

LinkerContextGuard::~LinkerContextGuard() {
  ActiveLinkerContext = Previous;
  CurrentWorkerSlot = PreviousWorkerSlot;
}
