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

unsigned linker::selectAdaptiveLinkThreadCount(unsigned RequestedThreads,
                                               unsigned AvailableThreads,
                                               uint64_t InputBytes,
                                               uint64_t InputFiles,
                                               LinkThreadPolicy Policy) {
  if (RequestedThreads != 0)
    return RequestedThreads;
  if (Policy.MaxAutoThreads != 0)
    AvailableThreads = std::min(AvailableThreads, Policy.MaxAutoThreads);
  if (InputBytes < Policy.MinParallelBytes || AvailableThreads <= 1)
    return 1;
  if (InputFiles != 0 && InputBytes / InputFiles < Policy.MinAverageFileBytes)
    return 1;
  if (Policy.BytesPerAdditionalThread == 0)
    return AvailableThreads;
  const uint64_t AdditionalThreads =
      InputBytes / Policy.BytesPerAdditionalThread +
      (InputBytes % Policy.BytesPerAdditionalThread != 0);
  if (AdditionalThreads >= static_cast<uint64_t>(AvailableThreads - 1))
    return AvailableThreads;
  return 1 + static_cast<unsigned>(AdditionalThreads);
}

CommonLinkerContext::CommonLinkerContext()
    : PreviousContext(ActiveLinkerContext),
      PreviousWorkerSlot(CurrentWorkerSlot) {
  ActiveLinkerContext = this;
  CurrentWorkerSlot = 0;
  WorkerSlots.emplace(std::this_thread::get_id(), 0);
}

CommonLinkerContext::CommonLinkerContext(
    neverc::ResourceSessionView ResourceSession)
    : CommonLinkerContext() {
  bindResourceSession(std::move(ResourceSession));
}

void CommonLinkerContext::bindResourceSession(
    neverc::ResourceSessionView Session) {
  assert((StateFlags & ResourceSessionBoundFlag) == 0 &&
         "linker resource session bound twice");
  ResourceSession = std::move(Session);
  StateFlags |= ResourceSessionBoundFlag;
}

CommonLinkerContext::~CommonLinkerContext() {
  assert(ActiveLinkerContext == this &&
         "linker context destroyed outside its active scope");
  finalizeOwnedState();
  ActiveLinkerContext = PreviousContext;
  CurrentWorkerSlot = PreviousWorkerSlot;
}

void CommonLinkerContext::finalizeOwnedState() noexcept {
  if ((StateFlags & FinalizedFlag) != 0)
    return;
  StateFlags |= FinalizedFlag;
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
  // Selection is intentionally monotonic.  Several backends discover inputs
  // in phases, and a defensive repeated call must not tear down a live pool or
  // change the worker budget underneath already-created per-worker state.
  if (parallelConfigured())
    return;
  StateFlags |= ParallelConfiguredFlag;
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

unsigned CommonLinkerContext::configureParallelForInputWorkload(
    unsigned RequestedThreads, uint64_t InputBytes, uint64_t InputFiles,
    LinkThreadPolicy Policy, bool FinalizeSerial) {
  if (parallelConfigured())
    return ParallelThreadCount;
  ThreadPoolStrategy Strategy = hardware_concurrency();
  unsigned AvailableThreads = std::max(1U, Strategy.compute_thread_count());
  if (Policy.MaxAutoThreads != 0)
    AvailableThreads = std::min(AvailableThreads, Policy.MaxAutoThreads);
  const unsigned SelectedThreads = selectAdaptiveLinkThreadCount(
      RequestedThreads, AvailableThreads, InputBytes, InputFiles, Policy);
  if (RequestedThreads != 0 || SelectedThreads > 1 || FinalizeSerial)
    configureParallel(SelectedThreads);
  return SelectedThreads;
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
  (void)Context.resourceSession();
  ActiveLinkerContext = &Context;
  CurrentWorkerSlot = WorkerSlot;
}

LinkerContextGuard::~LinkerContextGuard() {
  ActiveLinkerContext = Previous;
  CurrentWorkerSlot = PreviousWorkerSlot;
}
