//===- llvm/Support/ErrorHandling.h - Fatal error handling ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an API used to indicate fatal error conditions.  Non-fatal
// errors (most of them) should be handled through LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERRORHANDLING_H
#define LLVM_SUPPORT_ERRORHANDLING_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Compiler.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

/// An error handler callback.
typedef void (*fatal_error_handler_t)(void *user_data, const char *reason,
                                      bool gen_crash_diag);

/// install_fatal_error_handler - Installs a new error handler to be used
/// whenever a serious (non-recoverable) error is encountered by LLVM.
///
/// If no error handler is installed the default is to print the error message
/// to stderr, and call exit(1).  If an error handler is installed then it is
/// the handler's responsibility to log the message, it will no longer be
/// printed to stderr.  If the error handler returns, then exit(1) will be
/// called.
///
/// It is dangerous to naively use an error handler which throws an exception.
/// Even though some applications desire to gracefully recover from arbitrary
/// faults, blindly throwing exceptions through unfamiliar code isn't a way to
/// achieve this.
///
/// \param user_data - An argument which will be passed to the install error
/// handler.
void install_fatal_error_handler(fatal_error_handler_t handler,
                                 void *user_data = nullptr);

/// Restores default error handling behaviour.
void remove_fatal_error_handler();

/// ScopedFatalErrorHandler - This is a simple helper class which just
/// calls install_fatal_error_handler in its constructor and
/// remove_fatal_error_handler in its destructor.
struct ScopedFatalErrorHandler {
  explicit ScopedFatalErrorHandler(fatal_error_handler_t handler,
                                   void *user_data = nullptr) {
    install_fatal_error_handler(handler, user_data);
  }

  ~ScopedFatalErrorHandler() { remove_fatal_error_handler(); }
};

/// Reports a serious error, calling any installed error handler. These
/// functions are intended to be used for error conditions which are outside
/// the control of the compiler (I/O errors, invalid user input, etc.)
///
/// If no error handler is installed the default is to print the message to
/// standard error, followed by a newline.
/// After the error handler is called this function will call abort(), it
/// does not return.
/// NOTE: The std::string variant was removed to avoid a <string> dependency.
[[noreturn]] void report_fatal_error(const char *reason,
                                     bool gen_crash_diag = true);
[[noreturn]] void report_fatal_error(StringRef reason,
                                     bool gen_crash_diag = true);
[[noreturn]] void report_fatal_error(const Twine &reason,
                                     bool gen_crash_diag = true);

/// A same-thread, nestable fatal-error handler. Thread-local handlers shadow
/// the process-wide handler installed by install_fatal_error_handler(), but do
/// not mutate it. This is intended for independent in-process invocations
/// whose recovery transfer and diagnostics are owned by the calling thread.
///
/// The object must be reset or destroyed on the constructing thread. A reset
/// token identifies the exact registration generation, so an abandoned or
/// stale cleanup cannot detach a later handler that reuses the same address.
class ScopedThreadLocalFatalErrorHandler {
  struct Frame {
    fatal_error_handler_t Handler = nullptr;
    void *UserData = nullptr;
    Frame *Previous = nullptr;
    uint64_t Generation = 0;
    bool Active = false;
  };

public:
  class ResetToken {
  public:
    ResetToken() = default;

  private:
    friend class ScopedThreadLocalFatalErrorHandler;
    ResetToken(Frame *RegisteredFrame, Frame **RegisteredOwner,
               uint64_t RegisteredGeneration)
        : RegisteredFrame(RegisteredFrame), RegisteredOwner(RegisteredOwner),
          RegisteredGeneration(RegisteredGeneration) {}

    Frame *RegisteredFrame = nullptr;
    Frame **RegisteredOwner = nullptr;
    uint64_t RegisteredGeneration = 0;
  };

  explicit ScopedThreadLocalFatalErrorHandler(
      fatal_error_handler_t Handler, void *UserData = nullptr);
  ~ScopedThreadLocalFatalErrorHandler();

  ScopedThreadLocalFatalErrorHandler(
      const ScopedThreadLocalFatalErrorHandler &) = delete;
  ScopedThreadLocalFatalErrorHandler &
  operator=(const ScopedThreadLocalFatalErrorHandler &) = delete;
  ScopedThreadLocalFatalErrorHandler(
      ScopedThreadLocalFatalErrorHandler &&) = delete;
  ScopedThreadLocalFatalErrorHandler &
  operator=(ScopedThreadLocalFatalErrorHandler &&) = delete;

  ResetToken resetToken() const noexcept;
  void reset() noexcept;
  static void reset(ResetToken Token) noexcept;

private:
  static uint64_t allocateGeneration();
  static Frame *&currentFrame() noexcept;

  inline static std::atomic<uint64_t> NextGeneration{1};

  Frame Registration;
  Frame **Owner = nullptr;
  uint64_t Generation = 0;

  friend bool has_thread_local_fatal_error_handler();
  friend bool has_fatal_error_handler();
  friend void report_fatal_error(const Twine &, bool);
};

/// Whether the calling thread has a scoped handler.
bool has_thread_local_fatal_error_handler();

/// Whether the calling thread has a scoped handler or a process-wide fallback.
bool has_fatal_error_handler();

/// Installs a new bad alloc error handler that should be used whenever a
/// bad alloc error, e.g. failing malloc/calloc, is encountered by LLVM.
///
/// The user can install a bad alloc handler, in order to define the behavior
/// in case of failing allocations, e.g. throwing an exception. Note that this
/// handler must not trigger any additional allocations itself.
///
/// If no error handler is installed the default is to print the error message
/// to stderr, and call exit(1).  If an error handler is installed then it is
/// the handler's responsibility to log the message, it will no longer be
/// printed to stderr.  If the error handler returns, then exit(1) will be
/// called.
///
///
/// \param user_data - An argument which will be passed to the installed error
/// handler.
void install_bad_alloc_error_handler(fatal_error_handler_t handler,
                                     void *user_data = nullptr);

/// Restores default bad alloc error handling behavior.
void remove_bad_alloc_error_handler();

void install_out_of_memory_new_handler();

/// Reports a bad alloc error, calling any user defined bad alloc
/// error handler. In contrast to the generic 'report_fatal_error'
/// functions, this function might not terminate, e.g. the user
/// defined error handler throws an exception, but it won't return.
///
/// Note: When throwing an exception in the bad alloc handler, make sure that
/// the following unwind succeeds, e.g. do not trigger additional allocations
/// in the unwind chain.
///
/// If no error handler is installed (default), throws a bad_alloc exception
/// if LLVM is compiled with exception support. Otherwise prints the error
/// to standard error and calls abort().
[[noreturn]] void report_bad_alloc_error(const char *Reason,
                                         bool GenCrashDiag = true);

/// This function calls abort(), and prints the optional message to stderr.
/// Use the llvm_unreachable macro (that adds location info), instead of
/// calling this function directly.
[[noreturn]] void llvm_unreachable_internal(const char *msg = nullptr,
                                            const char *file = nullptr,
                                            unsigned line = 0);
} // namespace llvm

/// Marks that the current location is not supposed to be reachable.
/// In !NDEBUG builds, prints the message and location info to stderr.
/// In NDEBUG builds, if the platform does not support a builtin unreachable
/// then we call an internal LLVM runtime function. Otherwise the behavior is
/// controlled by the CMake flag
///   -DLLVM_UNREACHABLE_OPTIMIZE
/// * When "ON" (default) llvm_unreachable() becomes an optimizer hint
///   that the current location is not supposed to be reachable: the hint
///   turns such code path into undefined behavior.  On compilers that don't
///   support such hints, prints a reduced message instead and aborts the
///   program.
/// * When "OFF", a builtin_trap is emitted instead of an
//    optimizer hint or printing a reduced message.
///
/// Use this instead of assert(0). It conveys intent more clearly, suppresses
/// diagnostics for unreachable code paths, and allows compilers to omit
/// unnecessary code.
#ifndef NDEBUG
#define llvm_unreachable(msg)                                                  \
  ::llvm::llvm_unreachable_internal(msg, __FILE__, __LINE__)
#elif !defined(LLVM_BUILTIN_UNREACHABLE)
#define llvm_unreachable(msg) ::llvm::llvm_unreachable_internal()
#elif LLVM_UNREACHABLE_OPTIMIZE
#define llvm_unreachable(msg) LLVM_BUILTIN_UNREACHABLE
#else
#define llvm_unreachable(msg)                                                  \
  do {                                                                         \
    LLVM_BUILTIN_TRAP;                                                         \
    LLVM_BUILTIN_UNREACHABLE;                                                  \
  } while (false)
#endif

#if LLVM_ENABLE_THREADS == 1
// std::mutex is the stable choice here: OS-level primitive that yields on
// contention, constexpr default constructor (no static-init-order worry),
// and — critically — neither libc++ nor MSVC STL pulls <windows.h> in
// through <mutex> (they use opaque handle types), so we don't reintroduce
// the IMAGE_*/PASSTHROUGH/min/max macro pollution that motivated this
// header's earlier hand-rolled lock.
#include <mutex>
#define LLVM_MUTEX_T std::mutex
#define LLVM_MUTEX_LOCK(m) (m)->lock()
#define LLVM_MUTEX_UNLOCK(m) (m)->unlock()
#endif

#ifdef __cplusplus
extern "C" {
typedef void (*LLVMFatalErrorHandler)(const char *Reason);
}
#endif

namespace llvm {
class raw_ostream;
namespace sys {
void RunInterruptHandlers();
void PrintStackTrace(raw_ostream &OS, int Depth);
} // namespace sys
} // namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

#include "llvm/Support/CrashRecoveryContext.h"
#include <new>
#include <string.h>
#if defined(_MSC_VER)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

inline llvm::fatal_error_handler_t ErrorHandler = 0;
inline void *ErrorHandlerUserData = 0;

inline llvm::fatal_error_handler_t BadAllocErrorHandler = 0;
inline void *BadAllocErrorHandlerUserData = 0;

#if LLVM_ENABLE_THREADS == 1
inline LLVM_MUTEX_T ErrorHandlerMutex;
inline LLVM_MUTEX_T BadAllocErrorHandlerMutex;
#endif

namespace llvm {

inline uint64_t ScopedThreadLocalFatalErrorHandler::allocateGeneration() {
  uint64_t Observed = NextGeneration.load(std::memory_order_relaxed);
  for (;;) {
    if (Observed == 0 || Observed == ~uint64_t{0})
      report_fatal_error(
          "thread-local fatal-error handler generations are exhausted");
    if (NextGeneration.compare_exchange_weak(Observed, Observed + 1,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed))
      return Observed;
  }
}

inline ScopedThreadLocalFatalErrorHandler::Frame *&
ScopedThreadLocalFatalErrorHandler::currentFrame() noexcept {
  // LLVM_THREAD_LOCAL may be GCC's __thread, which cannot be combined with
  // an inline variable. An inline function-local TLS object still denotes one
  // slot per thread for the whole program, matching CrashRecoveryContext.
  static LLVM_THREAD_LOCAL Frame *Value = nullptr;
  return Value;
}

inline ScopedThreadLocalFatalErrorHandler::ScopedThreadLocalFatalErrorHandler(
    fatal_error_handler_t Handler, void *UserData) {
  if (!Handler)
    report_fatal_error(
        "cannot install a null thread-local fatal-error handler");
  Generation = allocateGeneration();
  Owner = &currentFrame();
  Registration = {Handler, UserData, currentFrame(), Generation, true};
  currentFrame() = &Registration;
}

inline ScopedThreadLocalFatalErrorHandler::
    ~ScopedThreadLocalFatalErrorHandler() {
  reset();
}

inline ScopedThreadLocalFatalErrorHandler::ResetToken
ScopedThreadLocalFatalErrorHandler::resetToken() const noexcept {
  return ResetToken(&const_cast<Frame &>(Registration), Owner, Generation);
}

inline void
ScopedThreadLocalFatalErrorHandler::reset(ResetToken Token) noexcept {
  if (!Token.RegisteredFrame || Token.RegisteredGeneration == 0 ||
      Token.RegisteredOwner != &currentFrame())
    return;

  Frame **Link = &currentFrame();
  while (*Link && *Link != Token.RegisteredFrame)
    Link = &(*Link)->Previous;
  if (!*Link || (*Link)->Generation != Token.RegisteredGeneration)
    return;

  Frame *Found = *Link;
  *Link = Found->Previous;
  Found->Handler = nullptr;
  Found->UserData = nullptr;
  Found->Previous = nullptr;
  Found->Active = false;
}

inline void ScopedThreadLocalFatalErrorHandler::reset() noexcept {
  if (!Registration.Active)
    return;
  if (Owner != &currentFrame())
    report_fatal_error(
        "thread-local fatal-error handler reset on a different thread");
  reset(resetToken());
  if (Registration.Active)
    report_fatal_error(
        "thread-local fatal-error handler registration changed before reset");
}

inline bool has_thread_local_fatal_error_handler() {
  return ScopedThreadLocalFatalErrorHandler::currentFrame() != nullptr;
}

inline bool has_fatal_error_handler() {
  if (has_thread_local_fatal_error_handler())
    return true;
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_LOCK(&ErrorHandlerMutex);
#endif
  const bool Installed = ErrorHandler != nullptr;
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_UNLOCK(&ErrorHandlerMutex);
#endif
  return Installed;
}

inline void install_fatal_error_handler(fatal_error_handler_t handler,
                                        void *user_data) {
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_LOCK(&ErrorHandlerMutex);
#endif
  assert(!ErrorHandler && "Error handler already registered!\n");
  ErrorHandler = handler;
  ErrorHandlerUserData = user_data;
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_UNLOCK(&ErrorHandlerMutex);
#endif
}

inline void remove_fatal_error_handler() {
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_LOCK(&ErrorHandlerMutex);
#endif
  ErrorHandler = 0;
  ErrorHandlerUserData = 0;
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_UNLOCK(&ErrorHandlerMutex);
#endif
}

inline void report_fatal_error(const char *Reason, bool GenCrashDiag) {
  report_fatal_error(Twine(Reason), GenCrashDiag);
}

inline void report_fatal_error(StringRef Reason, bool GenCrashDiag) {
  report_fatal_error(Twine(Reason), GenCrashDiag);
}

inline void report_fatal_error(const Twine &Reason, bool GenCrashDiag) {
  llvm::fatal_error_handler_t handler = 0;
  void *handlerData = 0;
  if (ScopedThreadLocalFatalErrorHandler::currentFrame()) {
    handler = ScopedThreadLocalFatalErrorHandler::currentFrame()->Handler;
    handlerData = ScopedThreadLocalFatalErrorHandler::currentFrame()->UserData;
  } else {
#if LLVM_ENABLE_THREADS == 1
    LLVM_MUTEX_LOCK(&ErrorHandlerMutex);
#endif
    handler = ErrorHandler;
    handlerData = ErrorHandlerUserData;
#if LLVM_ENABLE_THREADS == 1
    LLVM_MUTEX_UNLOCK(&ErrorHandlerMutex);
#endif
  }

  if (handler) {
    // A recovery handler may transfer control with setjmp/longjmp, bypassing
    // this frame's automatic destructors. Give the rendered message an
    // explicit CRC owner for that path; if the handler returns normally, first
    // unregister that owner and then release the message after the callback
    // has finished consuming its C string.
    auto Message = std::make_unique<std::string>(Reason.str());
    CrashRecoveryContextCleanupRegistrar<std::string> MessageCleanup(
        Message.get());
    handler(handlerData, Message->c_str(), GenCrashDiag);
    MessageCleanup.unregister();
    Message.reset();
  } else {
    const char prefix[] = "LLVM ERROR: ";
    (void)!::write(2, prefix, sizeof(prefix) - 1);
    std::string msg = Reason.str();
    (void)!::write(2, msg.data(), msg.size());
    (void)!::write(2, "\n", 1);
  }

  sys::RunInterruptHandlers();

  if (GenCrashDiag)
    abort();
  else
    exit(1);
}

inline void install_bad_alloc_error_handler(fatal_error_handler_t handler,
                                            void *user_data) {
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_LOCK(&BadAllocErrorHandlerMutex);
#endif
  assert(!ErrorHandler && "Bad alloc error handler already registered!\n");
  BadAllocErrorHandler = handler;
  BadAllocErrorHandlerUserData = user_data;
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_UNLOCK(&BadAllocErrorHandlerMutex);
#endif
}

inline void remove_bad_alloc_error_handler() {
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_LOCK(&BadAllocErrorHandlerMutex);
#endif
  BadAllocErrorHandler = 0;
  BadAllocErrorHandlerUserData = 0;
#if LLVM_ENABLE_THREADS == 1
  LLVM_MUTEX_UNLOCK(&BadAllocErrorHandlerMutex);
#endif
}

inline void report_bad_alloc_error(const char *Reason, bool GenCrashDiag) {
  fatal_error_handler_t Handler = 0;
  void *HandlerData = 0;
  {
#if LLVM_ENABLE_THREADS == 1
    LLVM_MUTEX_LOCK(&BadAllocErrorHandlerMutex);
#endif
    Handler = BadAllocErrorHandler;
    HandlerData = BadAllocErrorHandlerUserData;
#if LLVM_ENABLE_THREADS == 1
    LLVM_MUTEX_UNLOCK(&BadAllocErrorHandlerMutex);
#endif
  }

  if (Handler) {
    Handler(HandlerData, Reason, GenCrashDiag);
    llvm_unreachable("bad alloc handler should not return");
  }

#ifdef LLVM_ENABLE_EXCEPTIONS
  llvm::report_bad_alloc_error("out of memory", true);
#else
  const char *OOMMessage = "LLVM ERROR: out of memory\n";
  const char *Newline = "\n";
  (void)!::write(2, OOMMessage, strlen(OOMMessage));
  (void)!::write(2, Reason, strlen(Reason));
  (void)!::write(2, Newline, strlen(Newline));
  /* sys::PrintStackTrace omitted to avoid raw_ostream dependency */
  ::_Exit(235);
#endif
}

#ifdef LLVM_ENABLE_EXCEPTIONS
inline void install_out_of_memory_new_handler() {}
#else
inline static void out_of_memory_new_handler() {
  llvm::report_bad_alloc_error("Allocation failed");
}

inline void install_out_of_memory_new_handler() {
  auto old = std::set_new_handler(out_of_memory_new_handler);
  (void)old;
  assert((old == 0 || old == out_of_memory_new_handler) &&
         "new-handler already installed");
}
#endif

inline void llvm_unreachable_internal(const char *msg, const char *file,
                                      unsigned line) {
  if (msg) {
    (void)!::write(2, msg, strlen(msg));
    (void)!::write(2, "\n", 1);
  }
  (void)!::write(2, "UNREACHABLE executed", 20);
  if (file) {
    (void)!::write(2, " at ", 4);
    (void)!::write(2, file, strlen(file));
    (void)!::write(2, ":", 1);
    char linebuf[16];
    int pos = 0;
    unsigned l = line;
    do {
      linebuf[pos++] = '0' + (l % 10);
      l /= 10;
    } while (l);
    for (int i = 0; i < pos / 2; i++) {
      char t = linebuf[i];
      linebuf[i] = linebuf[pos - 1 - i];
      linebuf[pos - 1 - i] = t;
    }
    (void)!::write(2, linebuf, pos);
  }
  (void)!::write(2, "!\n", 2);
  abort();
#ifdef LLVM_BUILTIN_UNREACHABLE
  LLVM_BUILTIN_UNREACHABLE;
#endif
}

} // namespace llvm

inline static void bindingsErrorHandler(void *user_data, const char *reason,
                                        bool gen_crash_diag) {
  LLVMFatalErrorHandler handler =
      LLVM_EXTENSION(LLVMFatalErrorHandler)(user_data);
  handler(reason);
}

inline void LLVMInstallFatalErrorHandler(LLVMFatalErrorHandler Handler) {
  llvm::install_fatal_error_handler(bindingsErrorHandler,
                                    LLVM_EXTENSION(void *)(Handler));
}

inline void LLVMResetFatalErrorHandler() { llvm::remove_fatal_error_handler(); }

#ifdef _WIN32

#include "llvm/Support/Errc.h"
#include <winerror.h>

#define MAP_ERR_TO_COND(x, y)                                                  \
  case x:                                                                      \
    return llvm::make_error_code(llvm::errc::y)

namespace llvm {

inline std::error_code mapWindowsError(unsigned EV) {
  switch (EV) {
    MAP_ERR_TO_COND(ERROR_ACCESS_DENIED, permission_denied);
    MAP_ERR_TO_COND(ERROR_ALREADY_EXISTS, file_exists);
    MAP_ERR_TO_COND(ERROR_BAD_NETPATH, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_BAD_PATHNAME, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_BAD_UNIT, no_such_device);
    MAP_ERR_TO_COND(ERROR_BROKEN_PIPE, broken_pipe);
    MAP_ERR_TO_COND(ERROR_BUFFER_OVERFLOW, filename_too_long);
    MAP_ERR_TO_COND(ERROR_BUSY, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_BUSY_DRIVE, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_CANNOT_MAKE, permission_denied);
    MAP_ERR_TO_COND(ERROR_CANTOPEN, io_error);
    MAP_ERR_TO_COND(ERROR_CANTREAD, io_error);
    MAP_ERR_TO_COND(ERROR_CANTWRITE, io_error);
    MAP_ERR_TO_COND(ERROR_CURRENT_DIRECTORY, permission_denied);
    MAP_ERR_TO_COND(ERROR_DEV_NOT_EXIST, no_such_device);
    MAP_ERR_TO_COND(ERROR_DEVICE_IN_USE, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_DIR_NOT_EMPTY, directory_not_empty);
    MAP_ERR_TO_COND(ERROR_DIRECTORY, invalid_argument);
    MAP_ERR_TO_COND(ERROR_DISK_FULL, no_space_on_device);
    MAP_ERR_TO_COND(ERROR_FILE_EXISTS, file_exists);
    MAP_ERR_TO_COND(ERROR_FILE_NOT_FOUND, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_HANDLE_DISK_FULL, no_space_on_device);
    MAP_ERR_TO_COND(ERROR_INVALID_ACCESS, permission_denied);
    MAP_ERR_TO_COND(ERROR_INVALID_DRIVE, no_such_device);
    MAP_ERR_TO_COND(ERROR_INVALID_FUNCTION, function_not_supported);
    MAP_ERR_TO_COND(ERROR_INVALID_HANDLE, invalid_argument);
    MAP_ERR_TO_COND(ERROR_INVALID_NAME, invalid_argument);
    MAP_ERR_TO_COND(ERROR_INVALID_PARAMETER, invalid_argument);
    MAP_ERR_TO_COND(ERROR_LOCK_VIOLATION, no_lock_available);
    MAP_ERR_TO_COND(ERROR_LOCKED, no_lock_available);
    MAP_ERR_TO_COND(ERROR_NEGATIVE_SEEK, invalid_argument);
    MAP_ERR_TO_COND(ERROR_NOACCESS, permission_denied);
    MAP_ERR_TO_COND(ERROR_NOT_ENOUGH_MEMORY, not_enough_memory);
    MAP_ERR_TO_COND(ERROR_NOT_READY, resource_unavailable_try_again);
    MAP_ERR_TO_COND(ERROR_NOT_SUPPORTED, not_supported);
    MAP_ERR_TO_COND(ERROR_OPEN_FAILED, io_error);
    MAP_ERR_TO_COND(ERROR_OPEN_FILES, device_or_resource_busy);
    MAP_ERR_TO_COND(ERROR_OUTOFMEMORY, not_enough_memory);
    MAP_ERR_TO_COND(ERROR_PATH_NOT_FOUND, no_such_file_or_directory);
    MAP_ERR_TO_COND(ERROR_READ_FAULT, io_error);
    MAP_ERR_TO_COND(ERROR_REPARSE_TAG_INVALID, invalid_argument);
    MAP_ERR_TO_COND(ERROR_RETRY, resource_unavailable_try_again);
    MAP_ERR_TO_COND(ERROR_SEEK, io_error);
    MAP_ERR_TO_COND(ERROR_SHARING_VIOLATION, permission_denied);
    MAP_ERR_TO_COND(ERROR_TOO_MANY_OPEN_FILES, too_many_files_open);
    MAP_ERR_TO_COND(ERROR_WRITE_FAULT, io_error);
    MAP_ERR_TO_COND(ERROR_WRITE_PROTECT, permission_denied);
    MAP_ERR_TO_COND(WSAEACCES, permission_denied);
    MAP_ERR_TO_COND(WSAEBADF, bad_file_descriptor);
    MAP_ERR_TO_COND(WSAEFAULT, bad_address);
    MAP_ERR_TO_COND(WSAEINTR, interrupted);
    MAP_ERR_TO_COND(WSAEINVAL, invalid_argument);
    MAP_ERR_TO_COND(WSAEMFILE, too_many_files_open);
    MAP_ERR_TO_COND(WSAENAMETOOLONG, filename_too_long);
  default:
    return std::error_code(EV, std::system_category());
  }
}

} // namespace llvm

#endif

#endif
