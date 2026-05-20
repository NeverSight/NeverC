//===-- llvm/Support/ThreadPool.h - A ThreadPool implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a crude C++11 based thread pool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREADPOOL_H
#define LLVM_SUPPORT_THREADPOOL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/thread.h"

#include <future>

#include <deque>
#include <functional>
#include <memory>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define LLVM_TPOOL_MUTEX_T SRWLOCK
#define LLVM_TPOOL_MUTEX_INITIALIZER SRWLOCK_INIT
#define LLVM_TPOOL_MUTEX_INIT(m) InitializeSRWLock(m)
#define LLVM_TPOOL_MUTEX_DESTROY(m) ((void)0)
#define LLVM_TPOOL_MUTEX_LOCK(m) AcquireSRWLockExclusive(m)
#define LLVM_TPOOL_MUTEX_UNLOCK(m) ReleaseSRWLockExclusive(m)
#define LLVM_TPOOL_COND_T CONDITION_VARIABLE
#define LLVM_TPOOL_COND_INITIALIZER CONDITION_VARIABLE_INIT
#define LLVM_TPOOL_COND_INIT(c) InitializeConditionVariable(c)
#define LLVM_TPOOL_COND_DESTROY(c) ((void)0)
#define LLVM_TPOOL_COND_WAIT(c, m) SleepConditionVariableSRW((c), (m), INFINITE, 0)
#define LLVM_TPOOL_COND_SIGNAL(c) WakeConditionVariable(c)
#define LLVM_TPOOL_COND_BROADCAST(c) WakeAllConditionVariable(c)
#else
#include <pthread.h>
#define LLVM_TPOOL_MUTEX_T pthread_mutex_t
#define LLVM_TPOOL_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define LLVM_TPOOL_MUTEX_INIT(m) pthread_mutex_init((m), 0)
#define LLVM_TPOOL_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#define LLVM_TPOOL_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define LLVM_TPOOL_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define LLVM_TPOOL_COND_T pthread_cond_t
#define LLVM_TPOOL_COND_INITIALIZER PTHREAD_COND_INITIALIZER
#define LLVM_TPOOL_COND_INIT(c) pthread_cond_init((c), 0)
#define LLVM_TPOOL_COND_DESTROY(c) pthread_cond_destroy(c)
#define LLVM_TPOOL_COND_WAIT(c, m) pthread_cond_wait((c), (m))
#define LLVM_TPOOL_COND_SIGNAL(c) pthread_cond_signal(c)
#define LLVM_TPOOL_COND_BROADCAST(c) pthread_cond_broadcast(c)
#endif
#include <utility>

#ifndef BRIDGE_MIN
#define BRIDGE_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef CMOVE
#define CMOVE(x) std::move(x)
#endif

namespace llvm {

class ThreadPoolTaskGroup;

/// A ThreadPool for asynchronous parallel execution on a defined number of
/// threads.
///
/// The pool keeps a vector of threads alive, waiting on a condition variable
/// for some work to become available.
///
/// It is possible to reuse one thread pool for different groups of tasks
/// by grouping tasks using ThreadPoolTaskGroup. All tasks are processed using
/// the same queue, but it is possible to wait only for a specific group of
/// tasks to finish.
///
/// It is also possible for worker threads to submit new tasks and wait for
/// them. Note that this may result in a deadlock in cases such as when a task
/// (directly or indirectly) tries to wait for its own completion, or when all
/// available threads are used up by tasks waiting for a task that has no thread
/// left to run on (this includes waiting on the returned future). It should be
/// generally safe to wait() for a group as long as groups do not form a cycle.
class ThreadPool {
public:
  /// Construct a pool using the hardware strategy \p S for mapping hardware
  /// execution resources (threads, cores, CPUs)
  /// Defaults to using the maximum execution resources in the system, but
  /// accounting for the affinity mask.
  ThreadPool(ThreadPoolStrategy S = hardware_concurrency());

  /// Blocking destructor: the pool will wait for all the threads to complete.
  ~ThreadPool();

  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  template <typename Function, typename... Args>
  auto async(Function &&F, Args &&...ArgList) {
    auto Task =
        std::bind(std::forward<Function>(F), std::forward<Args>(ArgList)...);
    return async(std::move(Task));
  }

  /// Overload, task will be in the given task group.
  template <typename Function, typename... Args>
  auto async(ThreadPoolTaskGroup &Group, Function &&F, Args &&...ArgList) {
    auto Task =
        std::bind(std::forward<Function>(F), std::forward<Args>(ArgList)...);
    return async(Group, std::move(Task));
  }

  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  template <typename Func>
  auto async(Func &&F) -> std::shared_future<decltype(F())> {
    return asyncImpl(std::function<decltype(F())()>(std::forward<Func>(F)),
                     nullptr);
  }

  template <typename Func>
  auto async(ThreadPoolTaskGroup &Group, Func &&F)
      -> std::shared_future<decltype(F())> {
    return asyncImpl(std::function<decltype(F())()>(std::forward<Func>(F)),
                     &Group);
  }

  /// Blocking wait for all the threads to complete and the queue to be empty.
  /// It is an error to try to add new tasks while blocking on this call.
  /// Calling wait() from a task would deadlock waiting for itself.
  void wait();

  /// Blocking wait for only all the threads in the given group to complete.
  /// It is possible to wait even inside a task, but waiting (directly or
  /// indirectly) on itself will deadlock. If called from a task running on a
  /// worker thread, the call may process pending tasks while waiting in order
  /// not to waste the thread.
  void wait(ThreadPoolTaskGroup &Group);

  // TODO: misleading legacy name warning!
  // Returns the maximum number of worker threads in the pool, not the current
  // number of threads!
  unsigned getThreadCount() const { return MaxThreadCount; }

  /// Returns true if the current thread is a worker thread of this thread pool.
  bool isWorkerThread() const;

private:
  /// Helpers to create a promise and a callable wrapper of \p Task that sets
  /// the result of the promise. Returns the callable and a future to access the
  /// result.
  template <typename ResTy>
  static std::pair<std::function<void()>, std::future<ResTy>>
  createTaskAndFuture(std::function<ResTy()> Task) {
    std::shared_ptr<std::promise<ResTy>> Promise =
        std::make_shared<std::promise<ResTy>>();
    auto F = Promise->get_future();
    return {
        [Promise = std::move(Promise), Task]() { Promise->set_value(Task()); },
        std::move(F)};
  }
  static std::pair<std::function<void()>, std::future<void>>
  createTaskAndFuture(std::function<void()> Task) {
    std::shared_ptr<std::promise<void>> Promise =
        std::make_shared<std::promise<void>>();
    auto F = Promise->get_future();
    return {[Promise = std::move(Promise), Task]() {
              Task();
              Promise->set_value();
            },
            std::move(F)};
  }

  /// Returns true if all tasks in the given group have finished (nullptr means
  /// all tasks regardless of their group). QueueLock must be locked.
  bool workCompletedUnlocked(ThreadPoolTaskGroup *Group) const;

  /// Asynchronous submission of a task to the pool. The returned future can be
  /// used to wait for the task to finish and is *non-blocking* on destruction.
  template <typename ResTy>
  std::shared_future<ResTy> asyncImpl(std::function<ResTy()> Task,
                                      ThreadPoolTaskGroup *Group) {

#if LLVM_ENABLE_THREADS
    /// Wrap the Task in a std::function<void()> that sets the result of the
    /// corresponding future.
    auto R = createTaskAndFuture(Task);

    int requestedThreads;
    LLVM_TPOOL_MUTEX_LOCK(&QueueLock);

    // Don't allow enqueueing after disabling the pool
    assert(EnableFlag && "Queuing a thread during ThreadPool destruction");
    Tasks.emplace_back(std::make_pair(std::move(R.first), Group));
    requestedThreads = ActiveThreads + Tasks.size();
    LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
    LLVM_TPOOL_COND_SIGNAL(&QueueCondition);
    grow(requestedThreads);
    return R.second.share();

#else // LLVM_ENABLE_THREADS Disabled

    // Get a Future with launch::deferred execution using std::async
    auto Future = std::async(std::launch::deferred, std::move(Task)).share();
    // Wrap the future so that both ThreadPool::wait() can operate and the
    // returned future can be sync'ed on.
    Tasks.emplace_back(std::make_pair([Future]() { Future.get(); }, Group));
    return Future;
#endif
  }

#if LLVM_ENABLE_THREADS
  // Grow to ensure that we have at least `requested` Threads, but do not go
  // over MaxThreadCount.
  void grow(int requested);

  void processTasks(ThreadPoolTaskGroup *WaitingForGroup);
#endif

  /// Threads in flight
  std::vector<llvm::thread> Threads;
  /// Lock protecting access to the Threads vector.
  mutable llvm::sys::RWMutex ThreadsLock;

  /// Tasks waiting for execution in the pool.
  std::deque<std::pair<std::function<void()>, ThreadPoolTaskGroup *>> Tasks;

  /// Locking and signaling for accessing the Tasks queue.
  LLVM_TPOOL_MUTEX_T QueueLock = LLVM_TPOOL_MUTEX_INITIALIZER;
  LLVM_TPOOL_COND_T QueueCondition = LLVM_TPOOL_COND_INITIALIZER;

  /// Signaling for job completion (all tasks or all tasks in a group).
  LLVM_TPOOL_COND_T CompletionCondition = LLVM_TPOOL_COND_INITIALIZER;

  /// Keep track of the number of thread actually busy
  unsigned ActiveThreads = 0;
  /// Number of threads active for tasks in the given group (only non-zero).
  DenseMap<ThreadPoolTaskGroup *, unsigned> ActiveGroups;

#if LLVM_ENABLE_THREADS // avoids warning for unused variable
  /// Signal for the destruction of the pool, asking thread to exit.
  bool EnableFlag = true;
#endif

  const ThreadPoolStrategy Strategy;

  /// Maximum number of threads to potentially grow this pool to.
  const unsigned MaxThreadCount;
};

/// A group of tasks to be run on a thread pool. Thread pool tasks in different
/// groups can run on the same threadpool but can be waited for separately.
/// It is even possible for tasks of one group to submit and wait for tasks
/// of another group, as long as this does not form a loop.
class ThreadPoolTaskGroup {
public:
  /// The ThreadPool argument is the thread pool to forward calls to.
  ThreadPoolTaskGroup(ThreadPool &Pool) : Pool(Pool) {}

  /// Blocking destructor: will wait for all the tasks in the group to complete
  /// by calling ThreadPool::wait().
  ~ThreadPoolTaskGroup() { wait(); }

  /// Calls ThreadPool::async() for this group.
  template <typename Function, typename... Args>
  inline auto async(Function &&F, Args &&...ArgList) {
    return Pool.async(*this, std::forward<Function>(F),
                      std::forward<Args>(ArgList)...);
  }

  /// Calls ThreadPool::wait() for this group.
  void wait() { Pool.wait(*this); }

private:
  ThreadPool &Pool;
};

} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===
//
// Includes must stay at file scope: FormatVariadic → raw_ostream defines
// Twine out-of-line members; including raw_ostream inside namespace llvm would
// nest everything under llvm::llvm.

#if LLVM_ENABLE_THREADS
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Threading.h"
#else
#include "llvm/Support/raw_ostream.h"
#endif

namespace llvm {

#if LLVM_ENABLE_THREADS

// A note on thread groups: Tasks are by default in no group (represented
// by 0 ThreadPoolTaskGroup pointer in the Tasks queue) and functionality
// here normally works on all tasks regardless of their group (functions
// in that case receive 0 ThreadPoolTaskGroup pointer as argument).
// A task in a group has a pointer to that ThreadPoolTaskGroup in the Tasks
// queue, and functions called to work only on tasks from one group take that
// pointer.

inline ThreadPool::ThreadPool(ThreadPoolStrategy S)
    : Strategy(S), MaxThreadCount(S.compute_thread_count()) {
  LLVM_TPOOL_MUTEX_INIT(&QueueLock);
  LLVM_TPOOL_COND_INIT(&QueueCondition);
  LLVM_TPOOL_COND_INIT(&CompletionCondition);
}

inline void ThreadPool::grow(int requested) {
  llvm::sys::ScopedWriter LockGuard(ThreadsLock);
  if (Threads.size() >= MaxThreadCount)
    return; // Already hit the max thread pool size.
  unsigned newThreadCount = BRIDGE_MIN((unsigned)requested, MaxThreadCount);
  while (Threads.size() < newThreadCount) {
    int ThreadID = Threads.size();
    Threads.emplace_back([this, ThreadID] {
      set_thread_name(formatv("llvm-worker-{0}", ThreadID));
      Strategy.apply_thread_strategy(ThreadID);
      processTasks(0);
    });
  }
}

#ifndef NDEBUG
typedef struct {
  ThreadPoolTaskGroup **items;
  unsigned count;
  unsigned cap;
} TaskGroupStack;
inline static LLVM_THREAD_LOCAL TaskGroupStack *CurrentThreadTaskGroups = 0;
inline static inline int tgs_contains(TaskGroupStack *s,
                                      ThreadPoolTaskGroup *g) {
  if (!s)
    return 0;
  for (unsigned i = 0; i < s->count; ++i)
    if (s->items[i] == g)
      return 1;
  return 0;
}
#endif

// WaitingForGroup == 0 means all tasks regardless of their group.
inline void ThreadPool::processTasks(ThreadPoolTaskGroup *WaitingForGroup) {
  while (true) {
    llvm::unique_function<void()> Task;
    ThreadPoolTaskGroup *GroupOfTask;
    {
      LLVM_TPOOL_MUTEX_LOCK(&QueueLock);
      bool workCompletedForGroup = false; // Result of workCompletedUnlocked()
      for (;;) {
        if (!EnableFlag || !Tasks.empty())
          break;
        if (WaitingForGroup != 0) {
          workCompletedForGroup = workCompletedUnlocked(WaitingForGroup);
          if (workCompletedForGroup)
            break;
        }
        LLVM_TPOOL_COND_WAIT(&QueueCondition, &QueueLock);
      }
      // Exit condition
      if (!EnableFlag && Tasks.empty()) {
        LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
        return;
      }
      if (WaitingForGroup != 0 && workCompletedForGroup) {
        LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
        return;
      }
      // Yeah, we have a task, grab it and release the lock on the queue

      // We first need to signal that we are active before popping the queue
      // in order for wait() to properly detect that even if the queue is
      // empty, there is still a task in flight.
      ++ActiveThreads;
      Task = CMOVE(Tasks.front().first);
      GroupOfTask = Tasks.front().second;
      // Need to count active threads in each group separately, ActiveThreads
      // would never be 0 if waiting for another group inside a wait.
      if (GroupOfTask != 0)
        ++ActiveGroups[GroupOfTask]; // Increment or set to 1 if new item
      Tasks.pop_front();
      LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
    }
#ifndef NDEBUG
    if (CurrentThreadTaskGroups == 0) {
      CurrentThreadTaskGroups =
          (TaskGroupStack *)calloc(1, sizeof(TaskGroupStack));
      CurrentThreadTaskGroups->cap = 4;
      CurrentThreadTaskGroups->items =
          (ThreadPoolTaskGroup **)malloc(4 * sizeof(ThreadPoolTaskGroup *));
    }
    if (CurrentThreadTaskGroups->count >= CurrentThreadTaskGroups->cap) {
      CurrentThreadTaskGroups->cap *= 2;
      CurrentThreadTaskGroups->items = (ThreadPoolTaskGroup **)realloc(
          CurrentThreadTaskGroups->items,
          CurrentThreadTaskGroups->cap * sizeof(ThreadPoolTaskGroup *));
    }
    CurrentThreadTaskGroups->items[CurrentThreadTaskGroups->count++] =
        GroupOfTask;
#endif

    Task();

#ifndef NDEBUG
    CurrentThreadTaskGroups->count--;
    if (CurrentThreadTaskGroups->count == 0) {
      free(CurrentThreadTaskGroups->items);
      free(CurrentThreadTaskGroups);
      CurrentThreadTaskGroups = 0;
    }
#endif

    bool Notify;
    bool NotifyGroup;
    LLVM_TPOOL_MUTEX_LOCK(&QueueLock);
    --ActiveThreads;
    if (GroupOfTask != 0) {
      auto A = ActiveGroups.find(GroupOfTask);
      if (--(A->second) == 0)
        ActiveGroups.erase(A);
    }
    Notify = workCompletedUnlocked(GroupOfTask);
    NotifyGroup = GroupOfTask != 0 && Notify;
    LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);

    if (Notify)
      LLVM_TPOOL_COND_BROADCAST(&CompletionCondition);
    if (NotifyGroup)
      LLVM_TPOOL_COND_BROADCAST(&QueueCondition);
  }
}

inline bool
ThreadPool::workCompletedUnlocked(ThreadPoolTaskGroup *Group) const {
  if (Group == 0)
    return !ActiveThreads && Tasks.empty();
  return ActiveGroups.count(Group) == 0 &&
         !llvm::any_of(Tasks,
                       [Group](const auto &T) { return T.second == Group; });
}

inline void ThreadPool::wait() {
  assert(!isWorkerThread()); // Would deadlock waiting for itself.
  LLVM_TPOOL_MUTEX_LOCK(&QueueLock);
  while (!workCompletedUnlocked(0))
    LLVM_TPOOL_COND_WAIT(&CompletionCondition, &QueueLock);
  LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
}

inline void ThreadPool::wait(ThreadPoolTaskGroup &Group) {
  if (!isWorkerThread()) {
    LLVM_TPOOL_MUTEX_LOCK(&QueueLock);
    while (!workCompletedUnlocked(&Group))
      LLVM_TPOOL_COND_WAIT(&CompletionCondition, &QueueLock);
    LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
    return;
  }
  // Make sure to not deadlock waiting for oneself.
  assert(CurrentThreadTaskGroups == 0 ||
         !tgs_contains(CurrentThreadTaskGroups, &Group));
  // Handle the case of recursive call from another task in a different group,
  // in which case process tasks while waiting to keep the thread busy and avoid
  // possible deadlock.
  processTasks(&Group);
}

inline bool ThreadPool::isWorkerThread() const {
  llvm::sys::ScopedReader LockGuard(ThreadsLock);
  llvm::thread::id CurrentThreadId = llvm::this_thread::get_id();
  for (const llvm::thread &Thread : Threads)
    if (CurrentThreadId == Thread.get_id())
      return true;
  return false;
}

// The destructor joins all threads, waiting for completion.
inline ThreadPool::~ThreadPool() {
  LLVM_TPOOL_MUTEX_LOCK(&QueueLock);
  EnableFlag = false;
  LLVM_TPOOL_MUTEX_UNLOCK(&QueueLock);
  LLVM_TPOOL_COND_BROADCAST(&QueueCondition);
  llvm::sys::ScopedReader LockGuard(ThreadsLock);
  for (auto &Worker : Threads)
    Worker.join();
  LLVM_TPOOL_MUTEX_DESTROY(&QueueLock);
  LLVM_TPOOL_COND_DESTROY(&QueueCondition);
  LLVM_TPOOL_COND_DESTROY(&CompletionCondition);
}

#else // LLVM_ENABLE_THREADS Disabled

// No threads are launched, issue a warning if ThreadCount is not 0
inline ThreadPool::ThreadPool(ThreadPoolStrategy S)
    : Strategy(S), MaxThreadCount(1) {
  int ThreadCount = S.compute_thread_count();
  if (ThreadCount != 1) {
    errs() << "Warning: request a ThreadPool with " << ThreadCount
           << " threads, but LLVM_ENABLE_THREADS has been turned off\n";
  }
}

inline void ThreadPool::wait() {
  // Sequential implementation running the tasks
  while (!Tasks.empty()) {
    auto Task = CMOVE(Tasks.front().first);
    Tasks.pop_front();
    Task();
  }
}

inline void ThreadPool::wait(ThreadPoolTaskGroup &) {
  // Simply wait for all, this works even if recursive (the running task
  // is already removed from the queue).
  wait();
}

inline bool ThreadPool::isWorkerThread() const {
  report_fatal_error("LLVM compiled without multithreading");
}

inline ThreadPool::~ThreadPool() {
  wait();
  LLVM_TPOOL_MUTEX_DESTROY(&QueueLock);
  LLVM_TPOOL_COND_DESTROY(&QueueCondition);
  LLVM_TPOOL_COND_DESTROY(&CompletionCondition);
}

#endif

} // namespace llvm

#endif // LLVM_SUPPORT_THREADPOOL_H
