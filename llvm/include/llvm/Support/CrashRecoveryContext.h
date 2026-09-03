//===--- CrashRecoveryContext.h - Crash Recovery ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H
#define LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H

#include "csupport/lsignals.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/thread.h"

namespace llvm {
class CrashRecoveryContextCleanup;

/// Crash recovery helper object.
///
/// This class implements support for running operations in a safe context so
/// that crashes (memory errors, stack overflow, assertion violations) can be
/// detected and control restored to the crashing thread. Crash detection is
/// purely "best effort", the exact set of failures which can be recovered from
/// is platform dependent.
///
/// Clients make use of this code by first calling
/// CrashRecoveryContext::Enable(), and then executing unsafe operations via a
/// CrashRecoveryContext object. For example:
///
/// \code
///    void actual_work(void *);
///
///    void foo() {
///      CrashRecoveryContext CRC;
///
///      if (!CRC.RunSafely(actual_work, 0)) {
///         ... a crash was detected, report error to user ...
///      }
///
///      ... no crash was detected ...
///    }
/// \endcode
///
/// To assist recovery the class allows specifying set of actions that will be
/// executed in any case, whether crash occurs or not. These actions may be used
/// to reclaim resources in the case of crash.
class CrashRecoveryContext {
  void *Impl = nullptr;
  CrashRecoveryContextCleanup *head = nullptr;

public:
  CrashRecoveryContext();
  ~CrashRecoveryContext();

  /// Register cleanup handler, which is used when the recovery context is
  /// finished.
  /// The recovery context owns the handler.
  void registerCleanup(CrashRecoveryContextCleanup *cleanup);

  void unregisterCleanup(CrashRecoveryContextCleanup *cleanup);

  /// Enable crash recovery.
  static void Enable();

  /// Disable crash recovery.
  static void Disable();

  /// Return the active context, if the code is currently executing in a
  /// thread which is in a protected context.
  static CrashRecoveryContext *GetCurrent();

  /// Return true if the current thread is recovering from a crash.
  static bool isRecoveringFromCrash();

  /// Execute the provided callback function (with the given arguments) in
  /// a protected context.
  ///
  /// \return True if the function completed successfully, and false if the
  /// function crashed (or HandleCrash was called explicitly). Clients should
  /// make as little assumptions as possible about the program state when
  /// RunSafely has returned false.
  bool RunSafely(function_ref<void()> Fn);
  bool RunSafely(void (*Fn)(void *), void *UserData) {
    return RunSafely([&]() { Fn(UserData); });
  }

  /// Execute the provide callback function (with the given arguments) in
  /// a protected context which is run in another thread (optionally with a
  /// requested stack size).
  ///
  /// See RunSafely().
  ///
  /// On Darwin, if PRIO_DARWIN_BG is set on the calling thread, it will be
  /// propagated to the new thread as well.
  bool RunSafelyOnThread(function_ref<void()>, unsigned RequestedStackSize = 0);
  bool RunSafelyOnThread(void (*Fn)(void *), void *UserData,
                         unsigned RequestedStackSize = 0) {
    return RunSafelyOnThread([&]() { Fn(UserData); }, RequestedStackSize);
  }

  /// Explicitly trigger a crash recovery in the current process, and
  /// return failure from RunSafely(). This function does not return.
  [[noreturn]] void HandleExit(int RetCode);

  /// Return true if RetCode indicates that a signal or an exception occurred.
  static bool isCrash(int RetCode);

  /// Throw again a signal or an exception, after it was catched once by a
  /// CrashRecoveryContext.
  static bool throwIfCrash(int RetCode);

  /// In case of a crash, this is the crash identifier.
  int RetCode = 0;

  /// Selects whether handling of failures should be done in the same way as
  /// for regular crashes. When this is active, a crash would print the
  /// callstack, clean-up any temporary files and create a coredump/minidump.
  bool DumpStackAndCleanupOnFailure = false;
};

/// Abstract base class of cleanup handlers.
///
/// Derived classes override method recoverResources, which makes actual work on
/// resource recovery.
///
/// Cleanup handlers are stored in a double list, which is owned and managed by
/// a crash recovery context.
class CrashRecoveryContextCleanup {
protected:
  CrashRecoveryContext *context = nullptr;
  CrashRecoveryContextCleanup(CrashRecoveryContext *context)
      : context(context) {}

public:
  bool cleanupFired = false;

  virtual ~CrashRecoveryContextCleanup();
  virtual void recoverResources() = 0;

  CrashRecoveryContext *getContext() const { return context; }

private:
  friend class CrashRecoveryContext;
  CrashRecoveryContextCleanup *prev = nullptr, *next = nullptr;
};

/// Base class of cleanup handler that controls recovery of resources of the
/// given type.
///
/// \tparam Derived Class that uses this class as a base.
/// \tparam T Type of controlled resource.
///
/// This class serves as a base for its template parameter as implied by
/// Curiously Recurring Template Pattern.
///
/// This class factors out creation of a cleanup handler. The latter requires
/// knowledge of the current recovery context, which is provided by this class.
template <typename Derived, typename T>
class CrashRecoveryContextCleanupBase : public CrashRecoveryContextCleanup {
protected:
  T *resource;
  CrashRecoveryContextCleanupBase(CrashRecoveryContext *context, T *resource)
      : CrashRecoveryContextCleanup(context), resource(resource) {}

public:
  /// Creates cleanup handler.
  /// \param x Pointer to the resource recovered by this handler.
  /// \return New handler or null if the method was called outside a recovery
  ///         context.
  static Derived *create(T *x) {
    if (x) {
      if (CrashRecoveryContext *context = CrashRecoveryContext::GetCurrent())
        return new Derived(context, x);
    }
    return nullptr;
  }
};

/// Cleanup handler that reclaims resource by calling destructor on it.
template <typename T>
class CrashRecoveryContextDestructorCleanup
    : public CrashRecoveryContextCleanupBase<
          CrashRecoveryContextDestructorCleanup<T>, T> {
public:
  CrashRecoveryContextDestructorCleanup(CrashRecoveryContext *context,
                                        T *resource)
      : CrashRecoveryContextCleanupBase<
            CrashRecoveryContextDestructorCleanup<T>, T>(context, resource) {}

  void recoverResources() override { this->resource->~T(); }
};

/// Cleanup handler that reclaims resource by calling 'delete' on it.
template <typename T>
class CrashRecoveryContextDeleteCleanup
    : public CrashRecoveryContextCleanupBase<
          CrashRecoveryContextDeleteCleanup<T>, T> {
public:
  CrashRecoveryContextDeleteCleanup(CrashRecoveryContext *context, T *resource)
      : CrashRecoveryContextCleanupBase<CrashRecoveryContextDeleteCleanup<T>,
                                        T>(context, resource) {}

  void recoverResources() override { delete this->resource; }
};

/// Cleanup handler that reclaims resource by calling its method 'Release'.
template <typename T>
class CrashRecoveryContextReleaseRefCleanup
    : public CrashRecoveryContextCleanupBase<
          CrashRecoveryContextReleaseRefCleanup<T>, T> {
public:
  CrashRecoveryContextReleaseRefCleanup(CrashRecoveryContext *context,
                                        T *resource)
      : CrashRecoveryContextCleanupBase<
            CrashRecoveryContextReleaseRefCleanup<T>, T>(context, resource) {}

  void recoverResources() override { this->resource->Release(); }
};

/// Helper class for managing resource cleanups.
///
/// \tparam T Type of resource been reclaimed.
/// \tparam Cleanup Class that defines how the resource is reclaimed.
///
/// Clients create objects of this type in the code executed in a crash recovery
/// context to ensure that the resource will be reclaimed even in the case of
/// crash. For example:
///
/// \code
///    void actual_work(void *) {
///      ...
///      std::unique_ptr<Resource> R(new Resource());
///      CrashRecoveryContextCleanupRegistrar D(R.get());
///      ...
///    }
///
///    void foo() {
///      CrashRecoveryContext CRC;
///
///      if (!CRC.RunSafely(actual_work, 0)) {
///         ... a crash was detected, report error to user ...
///      }
/// \endcode
///
/// If the code of `actual_work` in the example above does not crash, the
/// destructor of CrashRecoveryContextCleanupRegistrar removes cleanup code from
/// the current CrashRecoveryContext and the resource is reclaimed by the
/// destructor of std::unique_ptr. If crash happens, destructors are not called
/// and the resource is reclaimed by cleanup object registered in the recovery
/// context by the constructor of CrashRecoveryContextCleanupRegistrar.
template <typename T, typename Cleanup = CrashRecoveryContextDeleteCleanup<T>>
class CrashRecoveryContextCleanupRegistrar {
  CrashRecoveryContextCleanup *cleanup;

public:
  CrashRecoveryContextCleanupRegistrar(T *x) : cleanup(Cleanup::create(x)) {
    if (cleanup)
      cleanup->getContext()->registerCleanup(cleanup);
  }

  ~CrashRecoveryContextCleanupRegistrar() { unregister(); }

  bool cleanupFired() const noexcept {
    return cleanup && cleanup->cleanupFired;
  }

  void unregister() {
    if (cleanup && !cleanup->cleanupFired)
      cleanup->getContext()->unregisterCleanup(cleanup);
    cleanup = nullptr;
  }
};
} // end namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

#include "llvm/Support/ExitCodes.h"
#include "llvm/Support/Signals.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#if LLVM_ENABLE_THREADS == 1
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define LLVM_CRC_MUTEX_T SRWLOCK
#define LLVM_CRC_MUTEX_INITIALIZER SRWLOCK_INIT
#define LLVM_CRC_MUTEX_LOCK(m) AcquireSRWLockExclusive(m)
#define LLVM_CRC_MUTEX_UNLOCK(m) ReleaseSRWLockExclusive(m)
#else
#include <pthread.h>
#define LLVM_CRC_MUTEX_T pthread_mutex_t
#define LLVM_CRC_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define LLVM_CRC_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define LLVM_CRC_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#endif
#endif

#ifndef STRIP_CONST
#define STRIP_CONST(T, x) const_cast<T>(x)
#endif

// Upstream keeps this block in CrashRecoveryContext.cpp, where an anonymous
// namespace is what confines it to that one translation unit.  This
// header-only port must name the namespace instead: an anonymous one here
// would hand every including translation unit its own enable flag, handler
// mutex, saved sigactions and context stack, so a context entered through one
// of them would be invisible to the handler reached from another -- and
// Disable() would restore sigactions it never saved.
namespace llvm::crc_detail {

struct CrashRecoveryContextImpl;

/// The innermost recovery context on the calling thread.
///
/// Wrapped in a function because LLVM_THREAD_LOCAL may expand to `__thread`,
/// which cannot carry `inline`. A function-local static can, and an inline
/// function still yields one object per thread for the whole program.
inline const CrashRecoveryContextImpl *&currentContext() {
  static LLVM_THREAD_LOCAL const CrashRecoveryContextImpl *C = nullptr;
  return C;
}

struct CrashRecoveryContextImpl {
  const CrashRecoveryContextImpl *Next;
  llvm::CrashRecoveryContext *CRC;
  ::jmp_buf JumpBuffer;
  volatile unsigned Failed : 1;
  unsigned SwitchedThread : 1;
  unsigned ValidJumpBuffer : 1;
};

inline CrashRecoveryContextImpl *
CrashRecoveryContextImpl_create(llvm::CrashRecoveryContext *CRC) {
  CrashRecoveryContextImpl *impl =
      (CrashRecoveryContextImpl *)calloc(1, sizeof(CrashRecoveryContextImpl));
  impl->CRC = CRC;
  impl->Failed = 0;
  impl->SwitchedThread = 0;
  impl->ValidJumpBuffer = 0;
  impl->Next = currentContext();
  currentContext() = impl;
  return impl;
}

inline void
CrashRecoveryContextImpl_destroy(CrashRecoveryContextImpl *impl) {
  // Replaces the upstream `delete CRCI` and must share its null-safety: a
  // CrashRecoveryContext that never ran keeps a null Impl, so the destructor
  // calls this with impl == nullptr. Dereferencing it before the guard is
  // undefined behavior (caught by UBSan).
  if (!impl)
    return;
  if (!impl->SwitchedThread)
    currentContext() = impl->Next;
  free(impl);
}

inline void
CrashRecoveryContextImpl_setSwitchedThread(CrashRecoveryContextImpl *impl) {
#if defined(LLVM_ENABLE_THREADS) && LLVM_ENABLE_THREADS != 0
  impl->SwitchedThread = true;
#else
  (void)impl;
#endif
}

inline void
CrashRecoveryContextImpl_HandleCrash(CrashRecoveryContextImpl *impl,
                                     int RetCode, uintptr_t Context) {
  currentContext() = impl->Next;

  assert(!impl->Failed && "Crash recovery context already failed!");
  impl->Failed = true;

  if (impl->CRC->DumpStackAndCleanupOnFailure)
    llvm::sys::CleanupOnSignal(Context);

  impl->CRC->RetCode = RetCode;

  if (impl->ValidJumpBuffer)
    longjmp(impl->JumpBuffer, 1);
}

#if LLVM_ENABLE_THREADS == 1
inline LLVM_CRC_MUTEX_T *getCrashRecoveryContextMutex() {
  static LLVM_CRC_MUTEX_T CrashRecoveryContextMutex = LLVM_CRC_MUTEX_INITIALIZER;
  return &CrashRecoveryContextMutex;
}
#endif

inline bool gCrashRecoveryEnabled = false;

/// The context whose cleanups are currently running on the calling thread.
/// Wrapped for the same reason as currentContext().
inline const llvm::CrashRecoveryContext *&recoveringFromCrashSlot() {
  static LLVM_THREAD_LOCAL const llvm::CrashRecoveryContext *C = nullptr;
  return C;
}

inline void installExceptionOrSignalHandlers();
inline void uninstallExceptionOrSignalHandlers();

} // namespace llvm::crc_detail

namespace llvm {

inline CrashRecoveryContextCleanup::~CrashRecoveryContextCleanup() = default;

inline CrashRecoveryContext::CrashRecoveryContext() {
  sys::DisableSystemDialogsOnCrash();
}

inline CrashRecoveryContext::~CrashRecoveryContext() {
  // A cleanup may destroy an object that owns cleanup registrars of its own.
  // Keep fired nodes alive until every recovery callback has finished so such
  // registrars can observe cleanupFired without dereferencing freed storage.
  // Also unlink one node at a time and re-read head after recoverResources():
  // the callback may unregister another active node or register a new one.
  CrashRecoveryContextCleanup *RetiredHead = nullptr;
  CrashRecoveryContextCleanup **RetiredTail = &RetiredHead;
  const CrashRecoveryContext *PC = crc_detail::recoveringFromCrashSlot();
  crc_detail::recoveringFromCrashSlot() = this;
  while (head) {
    CrashRecoveryContextCleanup *tmp = head;
    head = tmp->next;
    if (head)
      head->prev = nullptr;
    tmp->prev = nullptr;
    tmp->next = nullptr;
    *RetiredTail = tmp;
    RetiredTail = &tmp->next;
    tmp->cleanupFired = true;
    tmp->recoverResources();
  }
  while (RetiredHead) {
    CrashRecoveryContextCleanup *tmp = RetiredHead;
    RetiredHead = tmp->next;
    delete tmp;
  }
  crc_detail::recoveringFromCrashSlot() = PC;

  crc_detail::CrashRecoveryContextImpl *CRCI =
      (crc_detail::CrashRecoveryContextImpl *)Impl;
  crc_detail::CrashRecoveryContextImpl_destroy(CRCI);
}

inline bool CrashRecoveryContext::isRecoveringFromCrash() {
  return crc_detail::recoveringFromCrashSlot() != 0;
}

inline CrashRecoveryContext *CrashRecoveryContext::GetCurrent() {
  if (!crc_detail::gCrashRecoveryEnabled)
    return 0;

  const crc_detail::CrashRecoveryContextImpl *CRCI =
      crc_detail::currentContext();
  if (!CRCI)
    return 0;

  return CRCI->CRC;
}

inline void CrashRecoveryContext::Enable() {
#if LLVM_ENABLE_THREADS == 1
  LLVM_CRC_MUTEX_T *_crc_mtx = crc_detail::getCrashRecoveryContextMutex();
  LLVM_CRC_MUTEX_LOCK(_crc_mtx);
#endif
  if (!crc_detail::gCrashRecoveryEnabled) {
    crc_detail::gCrashRecoveryEnabled = true;
    crc_detail::installExceptionOrSignalHandlers();
  }
#if LLVM_ENABLE_THREADS == 1
  LLVM_CRC_MUTEX_UNLOCK(_crc_mtx);
#endif
}

inline void CrashRecoveryContext::Disable() {
#if LLVM_ENABLE_THREADS == 1
  LLVM_CRC_MUTEX_T *_crc_mtx = crc_detail::getCrashRecoveryContextMutex();
  LLVM_CRC_MUTEX_LOCK(_crc_mtx);
#endif
  if (crc_detail::gCrashRecoveryEnabled) {
    crc_detail::gCrashRecoveryEnabled = false;
    crc_detail::uninstallExceptionOrSignalHandlers();
  }
#if LLVM_ENABLE_THREADS == 1
  LLVM_CRC_MUTEX_UNLOCK(_crc_mtx);
#endif
}

inline void
CrashRecoveryContext::registerCleanup(CrashRecoveryContextCleanup *cleanup) {
  if (!cleanup)
    return;
  if (head)
    head->prev = cleanup;
  cleanup->next = head;
  head = cleanup;
}

inline void
CrashRecoveryContext::unregisterCleanup(CrashRecoveryContextCleanup *cleanup) {
  // Once recovery begins the context retains ownership until the retired list
  // is drained. A registrar (or a defensive callback) may still try to detach
  // that node while destroying an enclosing owner; make that a safe no-op.
  if (!cleanup || cleanup->cleanupFired)
    return;
  if (cleanup == head) {
    head = cleanup->next;
    if (head)
      head->prev = 0;
  } else {
    cleanup->prev->next = cleanup->next;
    if (cleanup->next)
      cleanup->next->prev = cleanup->prev;
  }
  delete cleanup;
}

} // end namespace llvm

#if defined(_MSC_VER)

#include <windows.h>

namespace llvm::crc_detail {

inline void installExceptionOrSignalHandlers() {}
inline void uninstallExceptionOrSignalHandlers() {}

inline int ExceptionFilter(_EXCEPTION_POINTERS *Except) {
  const CrashRecoveryContextImpl *CRCI = currentContext();

  if (!CRCI) {
    llvm::CrashRecoveryContext::Disable();
    return EXCEPTION_CONTINUE_SEARCH;
  }

  int RetCode = (int)Except->ExceptionRecord->ExceptionCode;
  if ((RetCode & 0xF0000000) == 0xE0000000)
    RetCode &= ~0xF0000000;

  CrashRecoveryContextImpl_HandleCrash(
      (CrashRecoveryContextImpl *)(const void *)CRCI, RetCode,
      (uintptr_t)(Except));

  return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace llvm::crc_detail

inline bool
llvm::CrashRecoveryContext::RunSafely(llvm::function_ref<void()> Fn) {
  if (!crc_detail::gCrashRecoveryEnabled) {
    Fn();
    return true;
  }
  assert(!Impl && "Crash recovery context already initialized!");
  Impl = crc_detail::CrashRecoveryContextImpl_create(this);
  __try {
    Fn();
  } __except (crc_detail::ExceptionFilter(GetExceptionInformation())) {
    return false;
  }
  return true;
}

#else // !_MSC_VER

#if defined(_WIN32)

#include "llvm/Support/Windows/WindowsSupport.h"

namespace llvm::crc_detail {

inline LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo) {
  const ULONG DbgPrintExceptionWideC = 0x4001000AL;
  switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
  case DBG_PRINTEXCEPTION_C:
  case DbgPrintExceptionWideC:
  case 0x406D1388:
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  const CrashRecoveryContextImpl *CRCI = currentContext();

  if (!CRCI) {
    llvm::CrashRecoveryContext::Disable();
    return EXCEPTION_CONTINUE_SEARCH;
  }

  int RetCode = (int)ExceptionInfo->ExceptionRecord->ExceptionCode;
  if ((RetCode & 0xF0000000) == 0xE0000000)
    RetCode &= ~0xF0000000;

  CrashRecoveryContextImpl_HandleCrash(
      (CrashRecoveryContextImpl *)(const void *)CRCI, RetCode,
      (uintptr_t)(ExceptionInfo));

  llvm_unreachable("Handled the crash, should have longjmp'ed out of here");
}

/// Wrapped for the same reason as currentContext().
inline const void *&currentExceptionHandle() {
  static LLVM_THREAD_LOCAL const void *H = nullptr;
  return H;
}

inline void installExceptionOrSignalHandlers() {
  PVOID handle = ::AddVectoredExceptionHandler(1, ExceptionHandler);
  currentExceptionHandle() = handle;
}

inline void uninstallExceptionOrSignalHandlers() {
  PVOID currentHandle = (PVOID)(currentExceptionHandle());
  if (currentHandle) {
    ::RemoveVectoredExceptionHandler(currentHandle);
    currentExceptionHandle() = NULL;
  }
}

} // namespace llvm::crc_detail

#else // !_WIN32

#include <signal.h>

namespace llvm::crc_detail {

inline constexpr int Signals[] = {SIGABRT, SIGBUS,  SIGFPE,
                                  SIGILL,  SIGSEGV, SIGTRAP};
inline constexpr unsigned NumSignals = (sizeof(Signals) / sizeof(Signals[0]));

/// The sigactions displaced by installExceptionOrSignalHandlers(), restored by
/// uninstallExceptionOrSignalHandlers().  One array for the whole program: a
/// per-translation-unit copy would let Disable() install a zeroed sigaction
/// over a handler it never saved.
inline struct sigaction PrevActions[NumSignals];

inline void CrashRecoverySignalHandler(int Signal) {
  const CrashRecoveryContextImpl *CRCI = currentContext();

  if (!CRCI) {
    llvm::CrashRecoveryContext::Disable();
    raise(Signal);
    return;
  }

  sigset_t SigMask;
  sigemptyset(&SigMask);
  sigaddset(&SigMask, Signal);
  sigprocmask(SIG_UNBLOCK, &SigMask, 0);

  int RetCode = 128 + Signal;

  if (Signal == SIGPIPE)
    RetCode = EX_IOERR;

  if (CRCI)
    CrashRecoveryContextImpl_HandleCrash(
        STRIP_CONST(CrashRecoveryContextImpl *, CRCI), RetCode, Signal);
}

inline void installExceptionOrSignalHandlers() {
  struct sigaction Handler;
  Handler.sa_handler = CrashRecoverySignalHandler;
  Handler.sa_flags = 0;
  sigemptyset(&Handler.sa_mask);

  for (unsigned i = 0; i != NumSignals; ++i) {
    sigaction(Signals[i], &Handler, &PrevActions[i]);
  }
}

inline void uninstallExceptionOrSignalHandlers() {
  for (unsigned i = 0; i != NumSignals; ++i)
    sigaction(Signals[i], &PrevActions[i], 0);
}

} // namespace llvm::crc_detail

#endif // !_WIN32

inline bool
llvm::CrashRecoveryContext::RunSafely(llvm::function_ref<void()> Fn) {
  if (crc_detail::gCrashRecoveryEnabled) {
    assert(!Impl && "Crash recovery context already initialized!");
    crc_detail::CrashRecoveryContextImpl *CRCI =
        crc_detail::CrashRecoveryContextImpl_create(this);
    Impl = CRCI;

    CRCI->ValidJumpBuffer = true;
    if (setjmp(CRCI->JumpBuffer) != 0) {
      return false;
    }
  }

  Fn();
  return true;
}

#endif // !_MSC_VER

namespace llvm {

[[noreturn]] inline void CrashRecoveryContext::HandleExit(int RetCode) {
#if defined(_WIN32)
  ::RaiseException(0xE0000000 | RetCode, 0, 0, NULL);
#else
  crc_detail::CrashRecoveryContextImpl *CRCI =
      (crc_detail::CrashRecoveryContextImpl *)Impl;
  assert(CRCI && "Crash recovery context never initialized!");
  crc_detail::CrashRecoveryContextImpl_HandleCrash(CRCI, RetCode, 0);
#endif
  llvm_unreachable("Most likely setjmp wasn't called!");
}

inline bool CrashRecoveryContext::isCrash(int RetCode) {
#if defined(_WIN32)
  unsigned Code = ((unsigned)RetCode & 0xF0000000) >> 28;
  if (Code != 0xC && Code != 8)
    return false;
#else
  if (RetCode <= 128)
    return false;
#endif
  return true;
}

inline bool CrashRecoveryContext::throwIfCrash(int RetCode) {
  if (!isCrash(RetCode))
    return false;
#if defined(_WIN32)
  ::RaiseException(RetCode, 0, 0, NULL);
#else
  llvm::sys::unregisterHandlers();
  raise(RetCode - 128);
#endif
  return true;
}

} // end namespace llvm

#define setThreadBackgroundPriority() csupport_set_thread_background_priority()
#define hasThreadBackgroundPriority()                                          \
  (csupport_has_thread_background_priority() != 0)

namespace llvm::crc_detail {

struct RunSafelyOnThreadInfo {
  llvm::function_ref<void()> Fn;
  llvm::CrashRecoveryContext *CRC;
  bool UseBackgroundPriority;
  bool Result;
};

inline void RunSafelyOnThread_Dispatch(void *UserData) {
  RunSafelyOnThreadInfo *Info = (RunSafelyOnThreadInfo *)(UserData);

  if (Info->UseBackgroundPriority)
    setThreadBackgroundPriority();

  Info->Result = Info->CRC->RunSafely(Info->Fn);
}

} // namespace llvm::crc_detail

namespace llvm {

inline bool
CrashRecoveryContext::RunSafelyOnThread(function_ref<void()> Fn,
                                        unsigned RequestedStackSize) {
  bool UseBackgroundPriority = hasThreadBackgroundPriority();
  crc_detail::RunSafelyOnThreadInfo Info = {Fn, this, UseBackgroundPriority,
                                            false};
  llvm::thread Thread(RequestedStackSize,
                      crc_detail::RunSafelyOnThread_Dispatch, &Info);
  Thread.join();

  if (crc_detail::CrashRecoveryContextImpl *CRC =
          (crc_detail::CrashRecoveryContextImpl *)Impl)
    crc_detail::CrashRecoveryContextImpl_setSwitchedThread(CRC);
  return Info.Result;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H
