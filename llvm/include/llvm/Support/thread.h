//===-- llvm/Support/thread.h - Wrapper for <thread> ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header is a wrapper for <thread> that works around problems with the
// MSVC headers when exceptions are disabled. It also provides llvm::thread,
// which is either a typedef of std::thread or a replacement that calls the
// function synchronously depending on the value of LLVM_ENABLE_THREADS.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREAD_H
#define LLVM_SUPPORT_THREAD_H

#include "llvm/Config/llvm-config.h"
#include <optional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define LLVM_ALL_PROCESSOR_GROUPS ((WORD)0xFFFF)
#endif

#if LLVM_ENABLE_THREADS

#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <sched.h>
#elif defined(__FreeBSD__)
#include <pthread_np.h>
#include <sys/cpuset.h>
#include <sys/param.h>
#endif

namespace llvm {

#if LLVM_ON_UNIX || _WIN32

/// LLVM thread following std::thread interface with added constructor to
/// specify stack size.
class thread {
  template <typename CalleeTuple> static void GenericThreadProxy(void *Ptr) {
    std::unique_ptr<CalleeTuple> Callee(static_cast<CalleeTuple *>(Ptr));
    std::apply(
        [](auto &&F, auto &&...Args) {
          std::forward<decltype(F)>(F)(std::forward<decltype(Args)>(Args)...);
        },
        *Callee);
  }

public:
#if LLVM_ON_UNIX
  using native_handle_type = pthread_t;
  using id = pthread_t;
  using start_routine_type = void *(*)(void *);

  template <typename CalleeTuple> static void *ThreadProxy(void *Ptr) {
    GenericThreadProxy<CalleeTuple>(Ptr);
    return nullptr;
  }
#elif _WIN32
  using native_handle_type = HANDLE;
  using id = DWORD;
  using start_routine_type = unsigned(__stdcall *)(void *);

  template <typename CalleeTuple>
  static unsigned __stdcall ThreadProxy(void *Ptr) {
    GenericThreadProxy<CalleeTuple>(Ptr);
    return 0;
  }
#endif

  static const unsigned DefaultStackSize;

  thread() : Thread(native_handle_type()) {}
  thread(thread &&Other) noexcept
      : Thread(std::exchange(Other.Thread, native_handle_type())) {}

  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args)
      : thread(DefaultStackSize, f, args...) {}

  template <class Function, class... Args>
  explicit thread(unsigned StackSizeInBytes, Function &&f, Args &&...args);
  thread(const thread &) = delete;

  ~thread() {
    if (joinable())
      std::terminate();
  }

  thread &operator=(thread &&Other) noexcept {
    if (joinable())
      std::terminate();
    Thread = std::exchange(Other.Thread, native_handle_type());
    return *this;
  }

  bool joinable() const noexcept { return Thread != native_handle_type(); }

  inline id get_id() const noexcept;

  native_handle_type native_handle() const noexcept { return Thread; }

  /// Return the number of hardware threads available, using platform-native
  /// APIs that are more reliable than std::thread::hardware_concurrency()
  /// (which has been observed to return 0 or 1 on some configurations,
  /// notably macOS PGO-instrumented builds).
  ///
  /// Apple:   sysctlbyname("hw.logicalcpu")
  /// Linux:   sched_getaffinity (respects cgroup/taskset affinity)
  /// FreeBSD: cpuset_getaffinity
  /// Windows: GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) — correctly
  ///          counts >64 cores across multiple processor groups
  /// Fallback: std::thread::hardware_concurrency(), minimum 1.
  static unsigned hardware_concurrency() {
#if defined(__APPLE__)
    uint32_t cnt = 0;
    size_t len = sizeof(cnt);
    if (sysctlbyname("hw.logicalcpu", &cnt, &len, nullptr, 0) == 0 && cnt > 0)
      return cnt;
#elif defined(__linux__)
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
      int n = CPU_COUNT(&set);
      if (n > 0)
        return static_cast<unsigned>(n);
    }
#elif defined(__FreeBSD__)
    cpuset_t mask;
    CPU_ZERO(&mask);
    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(mask),
                           &mask) == 0) {
      int n = CPU_COUNT(&mask);
      if (n > 0)
        return static_cast<unsigned>(n);
    }
#elif defined(_WIN32)
    {
      DWORD n = GetActiveProcessorCount(LLVM_ALL_PROCESSOR_GROUPS);
      if (n > 0)
        return static_cast<unsigned>(n);
    }
#endif
    unsigned n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
  }

  inline void join();
  inline void detach();

  void swap(llvm::thread &Other) noexcept { std::swap(Thread, Other.Thread); }

private:
  native_handle_type Thread;
};

#if defined(__APPLE__)
inline const unsigned thread::DefaultStackSize = 8 * 1024 * 1024;
#elif defined(_AIX)
inline const unsigned thread::DefaultStackSize = 4 * 1024 * 1024;
#else
inline const unsigned thread::DefaultStackSize = 0;
#endif

extern "C" uint64_t csupport_thread_execute(void *(*)(void *), void *,
                                            unsigned);
extern "C" void csupport_thread_detach(uint64_t);
extern "C" void csupport_thread_join(uint64_t);
extern "C" uint64_t csupport_thread_get_id(uint64_t);
extern "C" uint64_t csupport_thread_get_current_id(void);

inline thread::native_handle_type
llvm_execute_on_thread_impl(thread::start_routine_type ThreadFunc, void *Arg,
                            unsigned StackSizeInBytes) {
  return (thread::native_handle_type)(uintptr_t)csupport_thread_execute(
      (void *(*)(void *))ThreadFunc, Arg, StackSizeInBytes);
}
inline void llvm_thread_join_impl(thread::native_handle_type Thread) {
  csupport_thread_join((uint64_t)(uintptr_t)Thread);
}
inline void llvm_thread_detach_impl(thread::native_handle_type Thread) {
  csupport_thread_detach((uint64_t)(uintptr_t)Thread);
}
inline thread::id llvm_thread_get_id_impl(thread::native_handle_type Thread) {
  return (thread::id)(uintptr_t)csupport_thread_get_id(
      (uint64_t)(uintptr_t)Thread);
}
inline thread::id llvm_thread_get_current_id_impl() {
  return (thread::id)(uintptr_t)csupport_thread_get_current_id();
}

template <class Function, class... Args>
thread::thread(unsigned StackSizeInBytes, Function &&f, Args &&...args) {
  typedef std::tuple<std::decay_t<Function>, std::decay_t<Args>...> CalleeTuple;
  std::unique_ptr<CalleeTuple> Callee(
      new CalleeTuple(std::forward<Function>(f), std::forward<Args>(args)...));

  Thread = llvm_execute_on_thread_impl(ThreadProxy<CalleeTuple>, Callee.get(),
                                       StackSizeInBytes);
  if (Thread != native_handle_type())
    Callee.release();
}

thread::id thread::get_id() const noexcept {
  return llvm_thread_get_id_impl(Thread);
}

void thread::join() {
  llvm_thread_join_impl(Thread);
  Thread = native_handle_type();
}

void thread::detach() {
  llvm_thread_detach_impl(Thread);
  Thread = native_handle_type();
}

namespace this_thread {
inline thread::id get_id() { return llvm_thread_get_current_id_impl(); }
} // namespace this_thread

#else // !LLVM_ON_UNIX && !_WIN32

/// std::thread backed implementation of llvm::thread interface that ignores the
/// stack size request.
class thread {
public:
  using native_handle_type = std::thread::native_handle_type;
  using id = std::thread::id;

  thread() : Thread(std::thread()) {}
  thread(thread &&Other) noexcept
      : Thread(std::exchange(Other.Thread, std::thread())) {}

  template <class Function, class... Args>
  explicit thread(unsigned StackSizeInBytes, Function &&f, Args &&...args)
      : Thread(std::forward<Function>(f), std::forward<Args>(args)...) {}

  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args) : Thread(f, args...) {}

  thread(const thread &) = delete;

  ~thread() {}

  thread &operator=(thread &&Other) noexcept {
    Thread = std::exchange(Other.Thread, std::thread());
    return *this;
  }

  bool joinable() const noexcept { return Thread.joinable(); }

  id get_id() const noexcept { return Thread.get_id(); }

  native_handle_type native_handle() noexcept { return Thread.native_handle(); }

  static unsigned hardware_concurrency() {
    unsigned n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
  }

  inline void join() { Thread.join(); }
  inline void detach() { Thread.detach(); }

  void swap(llvm::thread &Other) noexcept { std::swap(Thread, Other.Thread); }

private:
  std::thread Thread;
};

namespace this_thread {
inline thread::id get_id() { return std::this_thread::get_id(); }
} // namespace this_thread

#endif // LLVM_ON_UNIX || _WIN32

} // namespace llvm

#else // !LLVM_ENABLE_THREADS

#include <utility>

namespace llvm {

struct thread {
  thread() {}
  thread(thread &&other) {}
  template <class Function, class... Args>
  explicit thread(unsigned StackSizeInBytes, Function &&f, Args &&...args) {
    f(std::forward<Args>(args)...);
  }
  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args) {
    f(std::forward<Args>(args)...);
  }
  thread(const thread &) = delete;

  void detach() {
    report_fatal_error("Detaching from a thread does not make sense with no "
                       "threading support");
  }
  void join() {}
  static unsigned hardware_concurrency() { return 1; }
};

} // namespace llvm

#endif // LLVM_ENABLE_THREADS

#endif // LLVM_SUPPORT_THREAD_H
