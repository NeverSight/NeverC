//===-- llvm/Support/Threading.h - Control multithreading mode --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares helper functions for running LLVM in a multi-threaded
// environment.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREADING_H
#define LLVM_SUPPORT_THREADING_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h" // for LLVM_ON_UNIX
#include "llvm/Support/Compiler.h"
#include <ciso646> // So we can check the C++ standard lib macros.
#include <optional>

#if defined(_MSC_VER)
// MSVC's call_once implementation worked since VS 2015, which is the minimum
// supported version as of this writing.
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#elif defined(LLVM_ON_UNIX) &&                                                 \
    (defined(_LIBCPP_VERSION) ||                                               \
     !(defined(__NetBSD__) || defined(__OpenBSD__) || defined(__powerpc__)))
// std::call_once from libc++ is used on all Unix platforms. Other
// implementations like libstdc++ are known to have problems on some
// platforms.
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#elif defined(LLVM_ON_UNIX) &&                                                 \
    (defined(__powerpc__) && defined(__LITTLE_ENDIAN__))
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#else
#define LLVM_THREADING_USE_STD_CALL_ONCE 0
#endif

#if LLVM_THREADING_USE_STD_CALL_ONCE
#include <mutex>
#else
#include "llvm/Support/Atomic.h"
#endif

namespace llvm {
class Twine;

/// Returns true if LLVM is compiled with support for multi-threading, and
/// false otherwise.
constexpr bool llvm_is_multithreaded() { return LLVM_ENABLE_THREADS; }

#if LLVM_THREADING_USE_STD_CALL_ONCE

typedef std::once_flag once_flag;

#else

enum InitStatus { Uninitialized = 0, Wait = 1, Done = 2 };

/// The llvm::once_flag structure
///
/// This type is modeled after std::once_flag to use with llvm::call_once.
/// This structure must be used as an opaque object. It is a struct to force
/// autoinitialization and behave like std::once_flag.
struct once_flag {
  volatile sys::cas_flag status = Uninitialized;
};

#endif

/// Execute the function specified as a parameter once.
///
/// Typical usage:
/// \code
///   void foo() {...};
///   ...
///   static once_flag flag;
///   call_once(flag, foo);
/// \endcode
///
/// \param flag Flag used for tracking whether or not this has run.
/// \param F Function to call once.
template <typename Function, typename... Args>
void call_once(once_flag &flag, Function &&F, Args &&...ArgList) {
#if LLVM_THREADING_USE_STD_CALL_ONCE
  std::call_once(flag, std::forward<Function>(F),
                 std::forward<Args>(ArgList)...);
#else
  // For other platforms we use a generic (if brittle) version based on our
  // atomics.
  sys::cas_flag old_val =
      sys::CompareAndSwap(&flag.status, Wait, Uninitialized);
  if (old_val == Uninitialized) {
    std::forward<Function>(F)(std::forward<Args>(ArgList)...);
    sys::MemoryFence();
    TsanIgnoreWritesBegin();
    TsanHappensBefore(&flag.status);
    flag.status = Done;
    TsanIgnoreWritesEnd();
  } else {
    // Wait until any thread doing the call has finished.
    sys::cas_flag tmp = flag.status;
    sys::MemoryFence();
    while (tmp != Done) {
      tmp = flag.status;
      sys::MemoryFence();
    }
  }
  TsanHappensAfter(&flag.status);
#endif
}

/// This tells how a thread pool will be used
class ThreadPoolStrategy {
public:
  // The default value (0) means all available threads should be used,
  // taking the affinity mask into account. If set, this value only represents
  // a suggested high bound, the runtime might choose a lower value (not
  // higher).
  unsigned ThreadsRequested = 0;

  // If SMT is active, use hyper threads. If false, there will be only one
  // std::thread per core.
  bool UseHyperThreads = true;

  // If set, will constrain 'ThreadsRequested' to the number of hardware
  // threads, or hardware cores.
  bool Limit = false;

  /// Retrieves the max available threads for the current strategy. This
  /// accounts for affinity masks and takes advantage of all CPU sockets.
  unsigned compute_thread_count() const;

  /// Assign the current thread to an ideal hardware CPU or NUMA node. In a
  /// multi-socket system, this ensures threads are assigned to all CPU
  /// sockets. \p ThreadPoolNum represents a number bounded by [0,
  /// compute_thread_count()).
  void apply_thread_strategy(unsigned ThreadPoolNum) const;

  /// Finds the CPU socket where a thread should go. Returns 'std::nullopt' if
  /// the thread shall remain on the actual CPU socket.
  std::optional<unsigned> compute_cpu_socket(unsigned ThreadPoolNum) const;
};

/// Build a strategy from a number of threads as a string provided in \p Num.
/// When Num is above the max number of threads specified by the \p Default
/// strategy, we attempt to equally allocate the threads on all CPU sockets.
/// "0" or an empty string will return the \p Default strategy.
/// "all" for using all hardware threads.
ThreadPoolStrategy get_threadpool_strategy(StringRef Num,
                                           ThreadPoolStrategy Default = {});

/// Returns a thread strategy for tasks requiring significant memory or other
/// resources. To be used for workloads where hardware_concurrency() proves to
/// be less efficient. Avoid this strategy if doing lots of I/O. Currently
/// based on physical cores, if available for the host system, otherwise falls
/// back to hardware_concurrency(). Returns 1 when LLVM is configured with
/// LLVM_ENABLE_THREADS = OFF.
inline ThreadPoolStrategy
heavyweight_hardware_concurrency(unsigned ThreadCount = 0) {
  ThreadPoolStrategy S;
  S.UseHyperThreads = false;
  S.ThreadsRequested = ThreadCount;
  return S;
}

/// Like heavyweight_hardware_concurrency() above, but builds a strategy
/// based on the rules described for get_threadpool_strategy().
/// If \p Num is invalid, returns a default strategy where one thread per
/// hardware core is used.
inline ThreadPoolStrategy heavyweight_hardware_concurrency(StringRef Num) {
  ThreadPoolStrategy S =
      get_threadpool_strategy(Num, heavyweight_hardware_concurrency());
  if (S.ThreadsRequested != UINT_MAX)
    return S;
  return heavyweight_hardware_concurrency();
}

/// Returns a default thread strategy where all available hardware resources
/// are to be used, except for those initially excluded by an affinity mask.
/// This function takes affinity into consideration. Returns 1 when LLVM is
/// configured with LLVM_ENABLE_THREADS=OFF.
inline ThreadPoolStrategy hardware_concurrency(unsigned ThreadCount = 0) {
  ThreadPoolStrategy S;
  S.ThreadsRequested = ThreadCount;
  return S;
}

/// Returns an optimal thread strategy to execute specified amount of tasks.
/// This strategy should prevent us from creating too many threads if we
/// occasionaly have an unexpectedly small amount of tasks.
inline ThreadPoolStrategy optimal_concurrency(unsigned TaskCount = 0) {
  ThreadPoolStrategy S;
  S.Limit = true;
  S.ThreadsRequested = TaskCount;
  return S;
}

enum class ThreadPriority {
  Background = 0,
  Low = 1,
  Default = 2,
};
enum class SetThreadPriorityResult { FAILURE, SUCCESS };
inline ThreadPoolStrategy get_threadpool_strategy(StringRef Num,
                                                  ThreadPoolStrategy Default) {
  if (Num == "all")
    return hardware_concurrency();
  if (Num.empty())
    return Default;
  unsigned V;
  if (Num.getAsInteger(10, V)) {
    ThreadPoolStrategy Invalid;
    Invalid.ThreadsRequested = UINT_MAX;
    return Invalid;
  }
  if (V == 0)
    return Default;
  ThreadPoolStrategy S = hardware_concurrency();
  S.ThreadsRequested = V;
  return S;
}

#if LLVM_ENABLE_THREADS == 0 || (!defined(_WIN32) && !defined(HAVE_PTHREAD_H))
inline uint64_t get_threadid() { return 0; }
inline uint32_t get_max_thread_name_length() { return 0; }
inline void set_thread_name(const Twine &Name) {}
inline void get_thread_name(SmallVectorImpl<char> &Name) { Name.clear(); }
inline BitVector get_thread_affinity_mask() { return {}; }
inline unsigned ThreadPoolStrategy::compute_thread_count() const { return 1; }
inline int get_physical_cores() { return -1; }
inline unsigned get_cpus() { return 1; }
inline SetThreadPriorityResult set_thread_priority(ThreadPriority) {
  return SetThreadPriorityResult::FAILURE;
}
inline void ThreadPoolStrategy::apply_thread_strategy(unsigned) const {}
inline std::optional<unsigned>
ThreadPoolStrategy::compute_cpu_socket(unsigned) const {
  return std::nullopt;
}
#else
extern "C" int csupport_get_physical_cores(void);
extern "C" uint64_t csupport_get_thread_id(void);
extern "C" uint32_t csupport_get_max_thread_name_length(void);
extern "C" int csupport_set_thread_name_cstr(const char *name);
extern "C" int csupport_get_thread_name_buf(char *buf, size_t buflen);
extern "C" int csupport_set_thread_priority_val(int priority);
extern "C" int csupport_compute_host_num_hardware_threads(void);
extern "C" unsigned csupport_get_cpus(void);
extern "C" void csupport_apply_thread_strategy_noop(unsigned thread_pool_num);

inline uint64_t get_threadid() { return csupport_get_thread_id(); }

inline uint32_t get_max_thread_name_length() {
  return csupport_get_max_thread_name_length();
}

inline void set_thread_name(const Twine &Name) {
  SmallString<64> Storage;
  StringRef NameStr = Name.toNullTerminatedStringRef(Storage);
  if (get_max_thread_name_length() > 0)
    NameStr = NameStr.take_back(get_max_thread_name_length() - 1);
  csupport_set_thread_name_cstr(NameStr.data());
}

inline void get_thread_name(SmallVectorImpl<char> &Name) {
  Name.clear();
  char buf[256];
  int n = csupport_get_thread_name_buf(buf, sizeof(buf));
  if (n > 0)
    Name.append(buf, buf + n);
}

inline BitVector get_thread_affinity_mask() {
  llvm_unreachable("Not implemented!");
}

inline unsigned get_cpus() { return csupport_get_cpus(); }

inline int get_physical_cores() { return csupport_get_physical_cores(); }

inline SetThreadPriorityResult set_thread_priority(ThreadPriority Priority) {
  return csupport_set_thread_priority_val(static_cast<int>(Priority)) == 0
             ? SetThreadPriorityResult::SUCCESS
             : SetThreadPriorityResult::FAILURE;
}

inline void
ThreadPoolStrategy::apply_thread_strategy(unsigned ThreadPoolNum) const {
  csupport_apply_thread_strategy_noop(ThreadPoolNum);
}

inline unsigned ThreadPoolStrategy::compute_thread_count() const {
  int MaxThreadCount = UseHyperThreads
                           ? csupport_compute_host_num_hardware_threads()
                           : get_physical_cores();
  if (MaxThreadCount <= 0)
    MaxThreadCount = 1;
  if (ThreadsRequested == 0)
    return MaxThreadCount;
  if (!Limit)
    return ThreadsRequested;
  return std::min((unsigned)MaxThreadCount, ThreadsRequested);
}

inline std::optional<unsigned>
ThreadPoolStrategy::compute_cpu_socket(unsigned) const {
  return std::nullopt;
}
#endif

} // namespace llvm

#if LLVM_ENABLE_THREADS == 1 && (defined(_WIN32) || defined(HAVE_PTHREAD_H))
#include "llvm/Support/thread.h"
#endif

#endif
