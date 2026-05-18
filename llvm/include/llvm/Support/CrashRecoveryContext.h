//===--- CrashRecoveryContext.h - Crash Recovery ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H
#define LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H

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
#include <pthread.h>
#endif

#ifndef STRIP_CONST
#define STRIP_CONST(T, x) const_cast<T>(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif
void csupport_set_thread_background_priority(void);
int csupport_has_thread_background_priority(void);
#ifdef __cplusplus
}
#endif

namespace {

struct CrashRecoveryContextImpl;
static LLVM_THREAD_LOCAL const CrashRecoveryContextImpl *CurrentContext;

struct CrashRecoveryContextImpl {
  const CrashRecoveryContextImpl *Next;
  llvm::CrashRecoveryContext *CRC;
  ::jmp_buf JumpBuffer;
  volatile unsigned Failed : 1;
  unsigned SwitchedThread : 1;
  unsigned ValidJumpBuffer : 1;
};

inline static CrashRecoveryContextImpl *
CrashRecoveryContextImpl_create(llvm::CrashRecoveryContext *CRC) {
  CrashRecoveryContextImpl *impl =
      (CrashRecoveryContextImpl *)calloc(1, sizeof(CrashRecoveryContextImpl));
  impl->CRC = CRC;
  impl->Failed = 0;
  impl->SwitchedThread = 0;
  impl->ValidJumpBuffer = 0;
  impl->Next = CurrentContext;
  CurrentContext = impl;
  return impl;
}

inline static void
CrashRecoveryContextImpl_destroy(CrashRecoveryContextImpl *impl) {
  if (!impl->SwitchedThread)
    CurrentContext = impl->Next;
  free(impl);
}

inline static void
CrashRecoveryContextImpl_setSwitchedThread(CrashRecoveryContextImpl *impl) {
#if defined(LLVM_ENABLE_THREADS) && LLVM_ENABLE_THREADS != 0
  impl->SwitchedThread = true;
#else
  (void)impl;
#endif
}

inline static void
CrashRecoveryContextImpl_HandleCrash(CrashRecoveryContextImpl *impl,
                                     int RetCode, uintptr_t Context) {
  CurrentContext = impl->Next;

  assert(!impl->Failed && "Crash recovery context already failed!");
  impl->Failed = true;

  if (impl->CRC->DumpStackAndCleanupOnFailure)
    llvm::sys::CleanupOnSignal(Context);

  impl->CRC->RetCode = RetCode;

  if (impl->ValidJumpBuffer)
    longjmp(impl->JumpBuffer, 1);
}

inline pthread_mutex_t *getCrashRecoveryContextMutex() {
  static pthread_mutex_t CrashRecoveryContextMutex = PTHREAD_MUTEX_INITIALIZER;
  return &CrashRecoveryContextMutex;
}

static bool gCrashRecoveryEnabled = false;

static LLVM_THREAD_LOCAL const llvm::CrashRecoveryContext
    *IsRecoveringFromCrash;

} // namespace

static void installExceptionOrSignalHandlers();
static void uninstallExceptionOrSignalHandlers();

namespace llvm {

inline CrashRecoveryContextCleanup::~CrashRecoveryContextCleanup() = default;

inline CrashRecoveryContext::CrashRecoveryContext() {
  sys::DisableSystemDialogsOnCrash();
}

inline CrashRecoveryContext::~CrashRecoveryContext() {
  CrashRecoveryContextCleanup *i = head;
  const CrashRecoveryContext *PC = IsRecoveringFromCrash;
  IsRecoveringFromCrash = this;
  while (i) {
    CrashRecoveryContextCleanup *tmp = i;
    i = tmp->next;
    tmp->cleanupFired = true;
    tmp->recoverResources();
    delete tmp;
  }
  IsRecoveringFromCrash = PC;

  CrashRecoveryContextImpl *CRCI = (CrashRecoveryContextImpl *)Impl;
  CrashRecoveryContextImpl_destroy(CRCI);
}

inline bool CrashRecoveryContext::isRecoveringFromCrash() {
  return IsRecoveringFromCrash != 0;
}

inline CrashRecoveryContext *CrashRecoveryContext::GetCurrent() {
  if (!gCrashRecoveryEnabled)
    return 0;

  const CrashRecoveryContextImpl *CRCI = CurrentContext;
  if (!CRCI)
    return 0;

  return CRCI->CRC;
}

inline void CrashRecoveryContext::Enable() {
  pthread_mutex_t *_crc_mtx = getCrashRecoveryContextMutex();
  pthread_mutex_lock(_crc_mtx);
  if (!gCrashRecoveryEnabled) {
    gCrashRecoveryEnabled = true;
    installExceptionOrSignalHandlers();
  }
  pthread_mutex_unlock(_crc_mtx);
}

inline void CrashRecoveryContext::Disable() {
  pthread_mutex_t *_crc_mtx = getCrashRecoveryContextMutex();
  pthread_mutex_lock(_crc_mtx);
  if (gCrashRecoveryEnabled) {
    gCrashRecoveryEnabled = false;
    uninstallExceptionOrSignalHandlers();
  }
  pthread_mutex_unlock(_crc_mtx);
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
  if (!cleanup)
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

static void installExceptionOrSignalHandlers() {}
static void uninstallExceptionOrSignalHandlers() {}

static int ExceptionFilter(_EXCEPTION_POINTERS *Except) {
  const CrashRecoveryContextImpl *CRCI = CurrentContext;

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

inline bool
llvm::CrashRecoveryContext::RunSafely(llvm::function_ref<void()> Fn) {
  if (!gCrashRecoveryEnabled) {
    Fn();
    return true;
  }
  assert(!Impl && "Crash recovery context already initialized!");
  Impl = CrashRecoveryContextImpl_create(this);
  __try {
    Fn();
  } __except (ExceptionFilter(GetExceptionInformation())) {
    return false;
  }
  return true;
}

#else // !_MSC_VER

#if defined(_WIN32)

#include "llvm/Support/Windows/WindowsSupport.h"

static LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo) {
  const ULONG DbgPrintExceptionWideC = 0x4001000AL;
  switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
  case DBG_PRINTEXCEPTION_C:
  case DbgPrintExceptionWideC:
  case 0x406D1388:
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  const CrashRecoveryContextImpl *CRCI = CurrentContext;

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

static LLVM_THREAD_LOCAL const void *sCurrentExceptionHandle;

static void installExceptionOrSignalHandlers() {
  PVOID handle = ::AddVectoredExceptionHandler(1, ExceptionHandler);
  sCurrentExceptionHandle = handle;
}

static void uninstallExceptionOrSignalHandlers() {
  PVOID currentHandle = (PVOID)(sCurrentExceptionHandle);
  if (currentHandle) {
    ::RemoveVectoredExceptionHandler(currentHandle);
    sCurrentExceptionHandle = NULL;
  }
}

#else // !_WIN32

#include <signal.h>

static const int Signals[] = {SIGABRT, SIGBUS,  SIGFPE,
                              SIGILL,  SIGSEGV, SIGTRAP};
static const unsigned NumSignals = (sizeof(Signals) / sizeof(Signals[0]));
static struct sigaction PrevActions[NumSignals];

static void CrashRecoverySignalHandler(int Signal) {
  const CrashRecoveryContextImpl *CRCI = CurrentContext;

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

static void installExceptionOrSignalHandlers() {
  struct sigaction Handler;
  Handler.sa_handler = CrashRecoverySignalHandler;
  Handler.sa_flags = 0;
  sigemptyset(&Handler.sa_mask);

  for (unsigned i = 0; i != NumSignals; ++i) {
    sigaction(Signals[i], &Handler, &PrevActions[i]);
  }
}

static void uninstallExceptionOrSignalHandlers() {
  for (unsigned i = 0; i != NumSignals; ++i)
    sigaction(Signals[i], &PrevActions[i], 0);
}

#endif // !_WIN32

inline bool
llvm::CrashRecoveryContext::RunSafely(llvm::function_ref<void()> Fn) {
  if (gCrashRecoveryEnabled) {
    assert(!Impl && "Crash recovery context already initialized!");
    CrashRecoveryContextImpl *CRCI = CrashRecoveryContextImpl_create(this);
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
  CrashRecoveryContextImpl *CRCI = (CrashRecoveryContextImpl *)Impl;
  assert(CRCI && "Crash recovery context never initialized!");
  CrashRecoveryContextImpl_HandleCrash(CRCI, RetCode, 0);
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

namespace {
struct RunSafelyOnThreadInfo {
  llvm::function_ref<void()> Fn;
  llvm::CrashRecoveryContext *CRC;
  bool UseBackgroundPriority;
  bool Result;
};
} // namespace

inline static void RunSafelyOnThread_Dispatch(void *UserData) {
  RunSafelyOnThreadInfo *Info = (RunSafelyOnThreadInfo *)(UserData);

  if (Info->UseBackgroundPriority)
    setThreadBackgroundPriority();

  Info->Result = Info->CRC->RunSafely(Info->Fn);
}

namespace llvm {

inline bool
CrashRecoveryContext::RunSafelyOnThread(function_ref<void()> Fn,
                                        unsigned RequestedStackSize) {
  bool UseBackgroundPriority = hasThreadBackgroundPriority();
  RunSafelyOnThreadInfo Info = {Fn, this, UseBackgroundPriority, false};
  llvm::thread Thread(RequestedStackSize, RunSafelyOnThread_Dispatch, &Info);
  Thread.join();

  if (CrashRecoveryContextImpl *CRC = (CrashRecoveryContextImpl *)Impl)
    CrashRecoveryContextImpl_setSwitchedThread(CRC);
  return Info.Result;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_CRASHRECOVERYCONTEXT_H
