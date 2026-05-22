//===- llvm/Support/Parallel.h - Parallel algorithms ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PARALLEL_H
#define LLVM_SUPPORT_PARALLEL_H

#include "llvm/ADT/STLExtras.h"
#include "csupport/cpp_compat_stl.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Threading.h"

#include "llvm/ADT/FunctionExtras.h"
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>

#if LLVM_ENABLE_THREADS
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif
#endif

namespace llvm {

namespace parallel {

// Strategy for the default executor used by the parallel routines provided by
// this file. It defaults to using all hardware threads and should be
// initialized before the first use of parallel routines.
extern ThreadPoolStrategy strategy;

#if LLVM_ENABLE_THREADS
#define GET_THREAD_INDEX_IMPL                                                  \
  if (parallel::strategy.ThreadsRequested == 1)                                \
    return 0;                                                                  \
  assert((threadIndex != UINT_MAX) &&                                          \
         "getThreadIndex() must be called from a thread created by "           \
         "ThreadPoolExecutor");                                                \
  return threadIndex;

#ifdef _WIN32
// Direct access to thread_local variables from a different DLL isn't
// possible with Windows Native TLS.
unsigned getThreadIndex();
#else
// Don't access this directly, use the getThreadIndex wrapper.
extern thread_local unsigned threadIndex;

inline unsigned getThreadIndex() { GET_THREAD_INDEX_IMPL; }
#endif

size_t getThreadCount();
#else
inline unsigned getThreadIndex() { return 0; }
inline size_t getThreadCount() { return 1; }
#endif

namespace detail {
class Latch {
  uint32_t Count;
  mutable std::mutex Mutex;
  mutable std::condition_variable Cond;

public:
  explicit Latch(uint32_t Count = 0) : Count(Count) {}
  ~Latch() {
    // Ensure at least that sync() was called.
    assert(Count == 0);
  }

  void inc() {
    std::lock_guard<std::mutex> lock(Mutex);
    ++Count;
  }

  void dec() {
    std::lock_guard<std::mutex> lock(Mutex);
    if (--Count == 0)
      Cond.notify_all();
  }

  void sync() const {
    std::unique_lock<std::mutex> lock(Mutex);
    Cond.wait(lock, [&] { return Count == 0; });
  }
};
} // namespace detail

class TaskGroup {
  detail::Latch L;
  bool Parallel;

public:
  TaskGroup();
  ~TaskGroup();

  // Spawn a task, but does not wait for it to finish.
  // Tasks marked with \p Sequential will be executed
  // exactly in the order which they were spawned.
  // Note: Sequential tasks may be executed on different
  // threads, but strictly in sequential order.
  void spawn(llvm::unique_function<void()> f, bool Sequential = false);

  void sync() const { L.sync(); }

  bool isParallel() const { return Parallel; }
};

namespace detail {

#if LLVM_ENABLE_THREADS
const ptrdiff_t MinParallelSize = 1024;

/// Inclusive median.
template <class RandomAccessIterator, class Comparator>
RandomAccessIterator medianOf3(RandomAccessIterator Start,
                               RandomAccessIterator End,
                               const Comparator &Comp) {
  RandomAccessIterator Mid = Start + (std::distance(Start, End) / 2);
  return Comp(*Start, *(End - 1))
             ? (Comp(*Mid, *(End - 1)) ? (Comp(*Start, *Mid) ? Mid : Start)
                                       : End - 1)
             : (Comp(*Mid, *Start) ? (Comp(*(End - 1), *Mid) ? Mid : End - 1)
                                   : Start);
}

template <class RandomAccessIterator, class Comparator>
void parallel_quick_sort(RandomAccessIterator Start, RandomAccessIterator End,
                         const Comparator &Comp, TaskGroup &TG, size_t Depth) {
  // Do a sequential sort for small inputs.
  if (std::distance(Start, End) < detail::MinParallelSize || Depth == 0) {
    llvm::sort(Start, End, Comp);
    return;
  }

  // Partition.
  auto Pivot = medianOf3(Start, End, Comp);
  // Move Pivot to End.
  std::swap(*(End - 1), *Pivot);
  Pivot = std::partition(Start, End - 1, [&Comp, End](decltype(*Start) V) {
    return Comp(V, *(End - 1));
  });
  // Move Pivot to middle of partition.
  std::swap(*Pivot, *(End - 1));

  // Recurse.
  TG.spawn([=, &Comp, &TG] {
    parallel_quick_sort(Start, Pivot, Comp, TG, Depth - 1);
  });
  parallel_quick_sort(Pivot + 1, End, Comp, TG, Depth - 1);
}

template <class RandomAccessIterator, class Comparator>
void parallel_sort(RandomAccessIterator Start, RandomAccessIterator End,
                   const Comparator &Comp) {
  TaskGroup TG;
  parallel_quick_sort(Start, End, Comp, TG,
                      llvm::Log2_64(std::distance(Start, End)) + 1);
}

// TaskGroup has a relatively high overhead, so we want to reduce
// the number of spawn() calls. We'll create up to 1024 tasks here.
// (Note that 1024 is an arbitrary number. This code probably needs
// improving to take the number of available cores into account.)
enum { MaxTasksPerGroup = 1024 };

template <class IterTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallel_transform_reduce(IterTy Begin, IterTy End, ResultTy Init,
                                   ReduceFuncTy Reduce,
                                   TransformFuncTy Transform) {
  // Limit the number of tasks to MaxTasksPerGroup to limit job scheduling
  // overhead on large inputs.
  size_t NumInputs = std::distance(Begin, End);
  if (NumInputs == 0)
    return std::move(Init);
  size_t NumTasks = std::min(static_cast<size_t>(MaxTasksPerGroup), NumInputs);
  std::vector<ResultTy> Results(NumTasks, Init);
  {
    // Each task processes either TaskSize or TaskSize+1 inputs. Any inputs
    // remaining after dividing them equally amongst tasks are distributed as
    // one extra input over the first tasks.
    TaskGroup TG;
    size_t TaskSize = NumInputs / NumTasks;
    size_t RemainingInputs = NumInputs % NumTasks;
    IterTy TBegin = Begin;
    for (size_t TaskId = 0; TaskId < NumTasks; ++TaskId) {
      IterTy TEnd = TBegin + TaskSize + (TaskId < RemainingInputs ? 1 : 0);
      TG.spawn([=, &Transform, &Reduce, &Results] {
        // Reduce the result of transformation eagerly within each task.
        ResultTy R = Init;
        for (IterTy It = TBegin; It != TEnd; ++It)
          R = Reduce(R, Transform(*It));
        Results[TaskId] = R;
      });
      TBegin = TEnd;
    }
    assert(TBegin == End);
  }

  // Do a final reduction. There are at most 1024 tasks, so this only adds
  // constant single-threaded overhead for large inputs. Hopefully most
  // reductions are cheaper than the transformation.
  ResultTy FinalResult = std::move(Results.front());
  for (ResultTy &PartialResult :
       MutableArrayRef(Results.data() + 1, Results.size() - 1))
    FinalResult = Reduce(FinalResult, std::move(PartialResult));
  return std::move(FinalResult);
}

#endif

} // namespace detail
} // namespace parallel

template <class RandomAccessIterator,
          class Comparator = std::less<
              typename std::iterator_traits<RandomAccessIterator>::value_type>>
void parallelSort(RandomAccessIterator Start, RandomAccessIterator End,
                  const Comparator &Comp = Comparator()) {
#if LLVM_ENABLE_THREADS
  if (parallel::strategy.ThreadsRequested != 1) {
    parallel::detail::parallel_sort(Start, End, Comp);
    return;
  }
#endif
  llvm::sort(Start, End, Comp);
}

void parallelFor(size_t Begin, size_t End, function_ref<void(size_t)> Fn);

template <class IterTy, class FuncTy>
void parallelForEach(IterTy Begin, IterTy End, FuncTy Fn) {
  parallelFor(0, End - Begin, [&](size_t I) { Fn(Begin[I]); });
}

template <class IterTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallelTransformReduce(IterTy Begin, IterTy End, ResultTy Init,
                                 ReduceFuncTy Reduce,
                                 TransformFuncTy Transform) {
#if LLVM_ENABLE_THREADS
  if (parallel::strategy.ThreadsRequested != 1) {
    return parallel::detail::parallel_transform_reduce(Begin, End, Init, Reduce,
                                                       Transform);
  }
#endif
  for (IterTy I = Begin; I != End; ++I)
    Init = Reduce(std::move(Init), Transform(*I));
  return std::move(Init);
}

// Range wrappers.
template <class RangeTy,
          class Comparator = std::less<decltype(*std::begin(RangeTy()))>>
void parallelSort(RangeTy &&R, const Comparator &Comp = Comparator()) {
  parallelSort(std::begin(R), std::end(R), Comp);
}

template <class RangeTy, class FuncTy>
void parallelForEach(RangeTy &&R, FuncTy Fn) {
  parallelForEach(std::begin(R), std::end(R), Fn);
}

template <class RangeTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallelTransformReduce(RangeTy &&R, ResultTy Init,
                                 ReduceFuncTy Reduce,
                                 TransformFuncTy Transform) {
  return parallelTransformReduce(std::begin(R), std::end(R), Init, Reduce,
                                 Transform);
}

// Parallel for-each, but with error handling.
template <class RangeTy, class FuncTy>
Error parallelForEachError(RangeTy &&R, FuncTy Fn) {
  // parallelTransformReduce requires a copyable Init. Error is uncopyable,
  // so fold over success values (nothing to copy) by running Fn sequentially
  // and joining the resulting Errors. For production workloads Fn itself is
  // already cheap (a linker-side per-item check), so the outer fold does not
  // need to be parallel.
  Error Result = Error::success();
  for (auto &&V : R)
    Result = joinErrors(std::move(Result), Fn(V));
  return Result;
}

} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

inline llvm::ThreadPoolStrategy llvm::parallel::strategy;

namespace llvm {
namespace parallel {
#if LLVM_ENABLE_THREADS

#ifdef _WIN32
inline static thread_local unsigned threadIndex = UINT_MAX;

inline unsigned getThreadIndex() { GET_THREAD_INDEX_IMPL; }
#else
inline thread_local unsigned threadIndex = UINT_MAX;
#endif

namespace detail {

namespace {

// ── Platform-agnostic threading primitives ──────────────────────────
#ifdef _WIN32

using nc_mutex_t = SRWLOCK;
using nc_cond_t  = CONDITION_VARIABLE;
using nc_thread_t = HANDLE;

inline void nc_mutex_init(nc_mutex_t &m)    { InitializeSRWLock(&m); }
inline void nc_mutex_destroy(nc_mutex_t &)  {}
inline void nc_mutex_lock(nc_mutex_t &m)    { AcquireSRWLockExclusive(&m); }
inline void nc_mutex_unlock(nc_mutex_t &m)  { ReleaseSRWLockExclusive(&m); }

inline void nc_cond_init(nc_cond_t &c)      { InitializeConditionVariable(&c); }
inline void nc_cond_destroy(nc_cond_t &)    {}
inline void nc_cond_signal(nc_cond_t &c)    { WakeConditionVariable(&c); }
inline void nc_cond_broadcast(nc_cond_t &c) { WakeAllConditionVariable(&c); }
inline void nc_cond_wait(nc_cond_t &c, nc_mutex_t &m) {
  SleepConditionVariableSRW(&c, &m, INFINITE, 0);
}

inline bool nc_thread_is_self(nc_thread_t t) {
  return GetThreadId(t) == GetCurrentThreadId();
}
inline void nc_thread_detach(nc_thread_t t) { CloseHandle(t); }
inline void nc_thread_join(nc_thread_t t) {
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}

using nc_thread_func_t = unsigned (__stdcall *)(void *);
inline nc_thread_t nc_thread_create(nc_thread_func_t fn, void *arg) {
  return (HANDLE)_beginthreadex(0, 0, fn, arg, 0, 0);
}

#define NC_THREAD_ENTRY_RET  unsigned
#define NC_THREAD_ENTRY_CALL __stdcall

#else // POSIX

using nc_mutex_t  = pthread_mutex_t;
using nc_cond_t   = pthread_cond_t;
using nc_thread_t = pthread_t;

inline void nc_mutex_init(nc_mutex_t &m)    { pthread_mutex_init(&m, 0); }
inline void nc_mutex_destroy(nc_mutex_t &m) { pthread_mutex_destroy(&m); }
inline void nc_mutex_lock(nc_mutex_t &m)    { pthread_mutex_lock(&m); }
inline void nc_mutex_unlock(nc_mutex_t &m)  { pthread_mutex_unlock(&m); }

inline void nc_cond_init(nc_cond_t &c)      { pthread_cond_init(&c, 0); }
inline void nc_cond_destroy(nc_cond_t &c)   { pthread_cond_destroy(&c); }
inline void nc_cond_signal(nc_cond_t &c)    { pthread_cond_signal(&c); }
inline void nc_cond_broadcast(nc_cond_t &c) { pthread_cond_broadcast(&c); }
inline void nc_cond_wait(nc_cond_t &c, nc_mutex_t &m) {
  pthread_cond_wait(&c, &m);
}

inline bool nc_thread_is_self(nc_thread_t t) {
  return pthread_equal(t, pthread_self()) != 0;
}
inline void nc_thread_detach(nc_thread_t t) { pthread_detach(t); }
inline void nc_thread_join(nc_thread_t t)   { pthread_join(t, 0); }

using nc_thread_func_t = void *(*)(void *);
inline nc_thread_t nc_thread_create(nc_thread_func_t fn, void *arg) {
  nc_thread_t t;
  pthread_create(&t, 0, fn, arg);
  return t;
}

#define NC_THREAD_ENTRY_RET  void *
#define NC_THREAD_ENTRY_CALL

#endif // _WIN32
// ────────────────────────────────────────────────────────────────────

/// O(1) push_front + pop_back for move-only closures; replaces deque here.
struct UniqueFunctionDeque {
  SmallVector<llvm::unique_function<void()>, 32> Data;
  size_t Head = 0;
  size_t Size = 0;

  bool empty() const { return Size == 0; }

  void push_front(llvm::unique_function<void()> F) {
    if (Data.empty())
      Data.resize(8);
    else if (Size >= Data.size())
      grow();
    Head = (Head == 0) ? Data.size() - 1 : Head - 1;
    Data[Head] = CMOVE(F);
    ++Size;
  }

  llvm::unique_function<void()> pop_back() {
    assert(Size > 0);
    size_t Idx = (Head + Size - 1) % Data.size();
    --Size;
    return CMOVE(Data[Idx]);
  }

private:
  void grow() {
    size_t OldCap = Data.size();
    size_t NewCap = OldCap * 2;
    SmallVector<llvm::unique_function<void()>, 32> NewD;
    NewD.resize(NewCap);
    for (size_t I = 0; I < Size; ++I)
      NewD[I] = CMOVE(Data[(Head + I) % OldCap]);
    Data = CMOVE(NewD);
    Head = 0;
  }
};

/// An abstract class that takes closures and runs them asynchronously.
class Executor {
public:
  virtual ~Executor() = default;
  virtual void add(llvm::unique_function<void()> func,
                   bool Sequential = false) = 0;
  virtual size_t getThreadCount() const = 0;

  static Executor *getDefaultExecutor();
};

/// An implementation of an Executor that runs closures on a thread pool
///   in filo order.
class ThreadPoolExecutor : public Executor {
public:
  struct WorkerArg {
    ThreadPoolExecutor *self;
    ThreadPoolStrategy S;
    unsigned ThreadID;
    bool isInitial;
  };

  static NC_THREAD_ENTRY_RET NC_THREAD_ENTRY_CALL workerEntry(void *arg) {
    WorkerArg *wa = (WorkerArg *)arg;
    ThreadPoolExecutor *self = wa->self;
    ThreadPoolStrategy S = wa->S;
    unsigned id = wa->ThreadID;
    bool isInitial = wa->isInitial;
    free(wa);
    if (isInitial) {
      for (unsigned I = 1; I < self->ThreadCount; ++I) {
        WorkerArg *child = (WorkerArg *)malloc(sizeof(WorkerArg));
        child->self = self;
        child->S = S;
        child->ThreadID = I;
        child->isInitial = false;
        nc_thread_t t =
            nc_thread_create(&ThreadPoolExecutor::workerEntry, child);
        self->Threads.push_back(t);
        if (__atomic_load_n(&self->Stop, __ATOMIC_ACQUIRE))
          break;
      }
      nc_mutex_lock(self->CreatedMtx);
      self->ThreadsReady = true;
      nc_cond_signal(self->CreatedCond);
      nc_mutex_unlock(self->CreatedMtx);
    }
    self->work(S, id);
    return 0;
  }

  explicit ThreadPoolExecutor(ThreadPoolStrategy S = hardware_concurrency()) {
    nc_mutex_init(Mutex);
    nc_cond_init(Cond);
    nc_mutex_init(CreatedMtx);
    nc_cond_init(CreatedCond);
    ThreadCount = S.compute_thread_count();
    Threads.reserve(ThreadCount);
    Threads.resize(1);
    nc_mutex_lock(Mutex);
    WorkerArg *initArg = (WorkerArg *)malloc(sizeof(WorkerArg));
    initArg->self = this;
    initArg->S = S;
    initArg->ThreadID = 0;
    initArg->isInitial = true;
    Threads[0] = nc_thread_create(&ThreadPoolExecutor::workerEntry, initArg);
    nc_mutex_unlock(Mutex);
  }

  void stop() {
    nc_mutex_lock(Mutex);
    if (__atomic_load_n(&Stop, __ATOMIC_RELAXED)) {
      nc_mutex_unlock(Mutex);
      return;
    }
    __atomic_store_n(&Stop, true, __ATOMIC_RELEASE);
    nc_mutex_unlock(Mutex);
    nc_cond_broadcast(Cond);
    nc_mutex_lock(CreatedMtx);
    while (!ThreadsReady)
      nc_cond_wait(CreatedCond, CreatedMtx);
    nc_mutex_unlock(CreatedMtx);
  }

  ~ThreadPoolExecutor() override {
    stop();
    for (nc_thread_t &T : Threads)
      if (nc_thread_is_self(T))
        nc_thread_detach(T);
      else
        nc_thread_join(T);
    nc_mutex_destroy(Mutex);
    nc_cond_destroy(Cond);
    nc_mutex_destroy(CreatedMtx);
    nc_cond_destroy(CreatedCond);
  }

  struct Creator {
    static void *call() { return new ThreadPoolExecutor(strategy); }
  };
  struct Deleter {
    static void call(void *Ptr) { ((ThreadPoolExecutor *)Ptr)->stop(); }
  };

  void add(llvm::unique_function<void()> F, bool Sequential = false) override {
    nc_mutex_lock(Mutex);
    if (Sequential)
      WorkQueueSequential.push_front(CMOVE(F));
    else
      WorkQueue.push_back(CMOVE(F));
    nc_mutex_unlock(Mutex);
    nc_cond_signal(Cond);
  }

  size_t getThreadCount() const override { return ThreadCount; }

private:
  bool hasSequentialTasks() const {
    return !WorkQueueSequential.empty() &&
           !__atomic_load_n(&SeqQueueLocked, __ATOMIC_ACQUIRE);
  }

  bool hasGeneralTasks() const { return !WorkQueue.empty(); }

  void work(ThreadPoolStrategy S, unsigned ThreadID) {
    threadIndex = ThreadID;
    S.apply_thread_strategy(ThreadID);
    while (true) {
      nc_mutex_lock(Mutex);
      while (!__atomic_load_n(&Stop, __ATOMIC_RELAXED) && !hasGeneralTasks() &&
             !hasSequentialTasks())
        nc_cond_wait(Cond, Mutex);
      if (__atomic_load_n(&Stop, __ATOMIC_RELAXED)) {
        nc_mutex_unlock(Mutex);
        break;
      }
      bool Sequential = hasSequentialTasks();
      if (Sequential)
        __atomic_store_n(&SeqQueueLocked, true, __ATOMIC_RELEASE);
      else
        assert(hasGeneralTasks());

      llvm::unique_function<void()> Task;
      if (Sequential)
        Task = CMOVE(WorkQueueSequential.pop_back());
      else {
        Task = CMOVE(WorkQueue.back());
        WorkQueue.pop_back();
      }
      nc_mutex_unlock(Mutex);
      Task();
      if (Sequential)
        __atomic_store_n(&SeqQueueLocked, false, __ATOMIC_RELEASE);
    }
  }

  bool Stop = false;           /* accessed atomically via __atomic builtins */
  bool SeqQueueLocked = false; /* accessed atomically via __atomic builtins */
  SmallVector<llvm::unique_function<void()>, 32> WorkQueue;
  UniqueFunctionDeque WorkQueueSequential;
  nc_mutex_t Mutex;
  nc_cond_t Cond;
  nc_mutex_t CreatedMtx;
  nc_cond_t CreatedCond;
  bool ThreadsReady = false;
  SmallVector<nc_thread_t, 8> Threads;
  unsigned ThreadCount;
};

inline Executor *Executor::getDefaultExecutor() {
  // The ManagedStatic enables the ThreadPoolExecutor to be stopped via
  // llvm_shutdown() which allows a "clean" fast exit, e.g. via _exit(). This
  // stops the thread pool and waits for any worker thread creation to complete
  // but does not wait for the threads to finish. The wait for worker thread
  // creation to complete is important as it prevents intermittent crashes on
  // Windows due to a race condition between thread creation and process exit.
  //
  // The ThreadPoolExecutor will only be destroyed when the static unique_ptr to
  // it is destroyed, i.e. in a normal full exit. The ThreadPoolExecutor
  // destructor ensures it has been stopped and waits for worker threads to
  // finish. The wait is important as it prevents intermittent crashes on
  // Windows when the process is doing a full exit.
  //
  // The Windows crashes appear to only occur with the MSVC static runtimes and
  // are more frequent with the debug static runtime.
  //
  // This also prevents intermittent deadlocks on exit with the MinGW runtime.

  static ManagedStatic<ThreadPoolExecutor, ThreadPoolExecutor::Creator,
                       ThreadPoolExecutor::Deleter>
      ManagedExec;
  static uptr_t<ThreadPoolExecutor> Exec(&(*ManagedExec));
  return Exec.get();
}
} // namespace
} // namespace detail

inline size_t getThreadCount() {
  return detail::Executor::getDefaultExecutor()->getThreadCount();
}
#endif

// Latch::sync() called by the dtor may cause one thread to block. If is a dead
// lock if all threads in the default executor are blocked. To prevent the dead
// lock, only allow the root TaskGroup to run tasks parallelly. In the scenario
// of nested parallel_for_each(), only the outermost one runs parallelly.
inline TaskGroup::TaskGroup()
#if LLVM_ENABLE_THREADS
    : Parallel((parallel::strategy.ThreadsRequested != 1) &&
               (threadIndex == UINT_MAX)){}
#else
    : Parallel(false) {
}
#endif

      inline TaskGroup::~TaskGroup() {
  // We must ensure that all the workloads have finished before decrementing the
  // instances count.
  L.sync();
}

inline void TaskGroup::spawn(llvm::unique_function<void()> F, bool Sequential) {
#if LLVM_ENABLE_THREADS
  if (Parallel) {
    L.inc();
    detail::Executor::getDefaultExecutor()->add(
        [&, F = CMOVE(F)]() mutable {
          F();
          L.dec();
        },
        Sequential);
    return;
  }
#endif
  F();
}

} // namespace parallel
} // namespace llvm

inline void llvm::parallelFor(size_t Begin, size_t End,
                              llvm::function_ref<void(size_t)> Fn) {
#if LLVM_ENABLE_THREADS
  if (parallel::strategy.ThreadsRequested != 1) {
    auto NumItems = End - Begin;
    // Limit the number of tasks to MaxTasksPerGroup to limit job scheduling
    // overhead on large inputs.
    auto TaskSize = NumItems / parallel::detail::MaxTasksPerGroup;
    if (TaskSize == 0)
      TaskSize = 1;

    parallel::TaskGroup TG;
    for (; Begin + TaskSize < End; Begin += TaskSize) {
      TG.spawn([=, &Fn] {
        for (size_t I = Begin, E = Begin + TaskSize; I != E; ++I)
          Fn(I);
      });
    }
    if (Begin != End) {
      TG.spawn([=, &Fn] {
        for (size_t I = Begin; I != End; ++I)
          Fn(I);
      });
    }
    return;
  }
#endif

  for (; Begin != End; ++Begin)
    Fn(Begin);
}

#endif // LLVM_SUPPORT_PARALLEL_H
