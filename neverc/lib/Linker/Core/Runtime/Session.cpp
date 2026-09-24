//===----------------------------------------------------------------------===//
//
//  Session — task-local ownership for linker allocator and diagnostics.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

using namespace llvm;
using namespace linker;

namespace {
// Hybrid CPUs run serial work far faster on performance cores than on
// efficiency cores, and the scheduler freely moves a busy thread between the
// two. Linux lists the performance cores in /sys/devices/cpu_core/cpus.
#if defined(__linux__)
struct PerformanceCores {
  cpu_set_t Cpus;
  bool Known = false;
};

const PerformanceCores &performanceCores() {
  static const PerformanceCores Cores = [] {
    PerformanceCores P;
    CPU_ZERO(&P.Cpus);
    auto List = MemoryBuffer::getFile("/sys/devices/cpu_core/cpus",
                                      /*IsText=*/true);
    if (!List)
      return P;
    SmallVector<StringRef, 8> Ranges;
    StringRef((*List)->getBuffer()).trim().split(Ranges, ',');
    for (StringRef Range : Ranges) {
      auto [Lo, Hi] = Range.split('-');
      unsigned First = 0, Last = 0;
      if (Lo.getAsInteger(10, First))
        return P;
      Last = First;
      if (!Hi.empty() && Hi.getAsInteger(10, Last))
        return P;
      for (unsigned Cpu = First; Cpu <= Last && Cpu < CPU_SETSIZE; ++Cpu)
        CPU_SET(Cpu, &P.Cpus);
    }
    P.Known = CPU_COUNT(&P.Cpus) > 0;
    return P;
  }();
  return Cores;
}

void setAffinity(pthread_t Thread, const std::vector<unsigned char> &Mask) {
  if (Mask.size() != sizeof(cpu_set_t))
    return;
  cpu_set_t Set;
  memcpy(&Set, Mask.data(), sizeof(Set));
  (void)pthread_setaffinity_np(Thread, sizeof(Set), &Set);
}
#endif
} // namespace

// Pins the calling thread to the performance cores among those it may run
// on, saving its current mask. Returns false, leaving the thread alone, when
// that subset is empty or is already the whole mask, or when
// NEVERC_LINK_NO_PIN is set.
bool linker::pinToPerformanceCores(unsigned long &Handle,
                                   std::vector<unsigned char> &SavedAffinity) {
#if defined(__linux__)
  const PerformanceCores &Cores = performanceCores();
  if (!Cores.Known || std::getenv("NEVERC_LINK_NO_PIN"))
    return false;
  cpu_set_t Current, Pinned;
  if (sched_getaffinity(0, sizeof(Current), &Current) != 0)
    return false;
  CPU_AND(&Pinned, &Current, &Cores.Cpus);
  const int Count = CPU_COUNT(&Pinned);
  if (Count == 0 || Count == CPU_COUNT(&Current))
    return false;
  if (sched_setaffinity(0, sizeof(Pinned), &Pinned) != 0)
    return false;
  SavedAffinity.assign(reinterpret_cast<unsigned char *>(&Current),
                       reinterpret_cast<unsigned char *>(&Current) +
                           sizeof(Current));
  Handle = static_cast<unsigned long>(pthread_self());
  return true;
#else
  (void)Handle;
  (void)SavedAffinity;
  return false;
#endif
}

// Called on a thread's first task for a context. Pool threads are started by
// the pinned thread and inherit its mask; hand them the saved one.
void linker::restoreWorkerAffinity(
    unsigned long PinnedHandle,
    const std::vector<unsigned char> &SavedAffinity) {
#if defined(__linux__)
  if (static_cast<unsigned long>(pthread_self()) != PinnedHandle)
    setAffinity(pthread_self(), SavedAffinity);
#else
  (void)PinnedHandle;
  (void)SavedAffinity;
#endif
}

void linker::unpinThread(unsigned long Handle,
                         const std::vector<unsigned char> &SavedAffinity) {
#if defined(__linux__)
  setAffinity(static_cast<pthread_t>(Handle), SavedAffinity);
#else
  (void)Handle;
  (void)SavedAffinity;
#endif
}

namespace {
thread_local CommonLinkerContext *ActiveLinkerContext = nullptr;
thread_local unsigned CurrentWorkerSlot = 0;

// Every context construction and finalization advances this epoch, so a
// per-thread cache filled for one context can never be observed by a later
// context that happens to reuse the same address.
std::atomic<uint64_t> WorkerCacheEpoch{1};

// Per-thread memo of the worker slot and per-type arenas for one context.
// makeThreadLocal<T>() runs on every section and symbol a worker creates, so
// resolving the arena under the context mutex serialized the parallel phases.
// The cache is filled under that mutex once per (thread, context, type) and
// then read without locking.
struct WorkerAllocCache {
  static constexpr unsigned NumEntries = 64;
  const CommonLinkerContext *Context = nullptr;
  uint64_t Epoch = 0;
  unsigned Slot = 0;
  const void *Tags[NumEntries];
  SpecificAllocBase *Instances[NumEntries];

  bool matches(const CommonLinkerContext *C) const {
    return Context == C &&
           Epoch == WorkerCacheEpoch.load(std::memory_order_acquire);
  }
  void reset(const CommonLinkerContext *C, unsigned S) {
    Context = C;
    Epoch = WorkerCacheEpoch.load(std::memory_order_acquire);
    Slot = S;
    std::memset(Tags, 0, sizeof(Tags));
  }
  static unsigned bucket(const void *Tag) {
    return static_cast<unsigned>(reinterpret_cast<uintptr_t>(Tag) >> 3) %
           NumEntries;
  }
  SpecificAllocBase *find(const void *Tag) const {
    for (unsigned I = bucket(Tag), N = 0; N != NumEntries;
         I = (I + 1) % NumEntries, ++N) {
      if (Tags[I] == Tag)
        return Instances[I];
      if (!Tags[I])
        return nullptr;
    }
    return nullptr;
  }
  void insert(const void *Tag, SpecificAllocBase *Instance) {
    for (unsigned I = bucket(Tag), N = 0; N != NumEntries;
         I = (I + 1) % NumEntries, ++N) {
      if (!Tags[I]) {
        Tags[I] = Tag;
        Instances[I] = Instance;
        return;
      }
    }
  }
};
thread_local WorkerAllocCache WorkerCache;
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
  WorkerCacheEpoch.fetch_add(1, std::memory_order_acq_rel);
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
  if (MainThreadPinned) {
    unpinThread(PinnedThreadHandle, SavedAffinity);
    MainThreadPinned = false;
  }
  if (ParallelPool)
    ParallelPool->wait();
  WorkerCacheEpoch.fetch_add(1, std::memory_order_acq_rel);
  e.runCleanup();
  // Worker arenas hold objects that tasks created; their destructors only
  // release memory those objects own. Destroy them concurrently while the
  // pool still exists, since a large link spends a noticeable time here.
  if (ParallelPool && WorkerInstanceOrder.size() > 1) {
    for (auto &Entry : WorkerInstanceOrder)
      ParallelPool->async([Instance = Entry.second] { Instance->destroy(); });
    ParallelPool->wait();
    WorkerInstanceOrder.clear();
  }
  ParallelPool.reset();
  // Destroy the remaining arenas newest first.
  std::vector<std::pair<uint64_t, SpecificAllocBase *>> Order =
      std::move(WorkerInstanceOrder);
  Order.reserve(Order.size() + instanceOrder.size());
  for (size_t I = 0; I != instanceOrder.size(); ++I)
    Order.emplace_back(instanceSequence[I], instanceOrder[I]);
  llvm::sort(Order,
             [](const auto &A, const auto &B) { return A.first > B.first; });
  for (auto &Entry : Order)
    Entry.second->destroy();
  instances.clear();
  WorkerInstances.clear();
  instanceOrder.clear();
  instanceSequence.clear();
  WorkerInstanceOrder.clear();
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
  if (ThreadCount > 1) {
    ParallelPool = std::make_unique<ThreadPool>(Strategy);
    MainThreadPinned = pinToPerformanceCores(PinnedThreadHandle, SavedAffinity);
  }
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
  if (WorkerCache.matches(this))
    return WorkerCache.Slot;
  // Pool threads start lazily from the pinned thread and inherit its mask;
  // give them back the original one before their first task.
  if (MainThreadPinned)
    restoreWorkerAffinity(PinnedThreadHandle, SavedAffinity);
  unsigned Slot;
  {
    std::lock_guard<std::mutex> Lock(WorkerMutex);
    auto [It, Inserted] =
        WorkerSlots.try_emplace(std::this_thread::get_id(), NextWorkerSlot);
    if (Inserted)
      ++NextWorkerSlot;
    Slot = It->second;
  }
  WorkerCache.reset(this, Slot);
  return Slot;
}

SpecificAllocBase *CommonLinkerContext::getOrCreateWorkerAllocator(
    const void *Tag, size_t Size, size_t Alignment,
    SpecificAllocBase *(&Creator)(void *)) {
  const unsigned Slot = workerSlotForCurrentThread();
  if (SpecificAllocBase *Cached = WorkerCache.find(Tag))
    return Cached;
  SpecificAllocBase *Result;
  {
    std::lock_guard<std::mutex> Lock(WorkerMutex);
    SpecificAllocBase *&Instance = WorkerInstances[{Slot, Tag}];
    if (!Instance) {
      void *Storage = WorkerAllocatorStorage.Allocate(Size, Alignment);
      Instance = Creator(Storage);
      WorkerInstanceOrder.emplace_back(
          allocatorSequence.fetch_add(1, std::memory_order_relaxed), Instance);
    }
    Result = Instance;
  }
  WorkerCache.insert(Tag, Result);
  return Result;
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
