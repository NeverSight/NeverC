//===- llvm/Support/Program.h ------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Program class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PROGRAM_H
#define LLVM_SUPPORT_PROGRAM_H

#include "csupport/lprogram.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include <chrono>
#include <optional>

namespace llvm {
class BitVector;
namespace sys {

/// This is the OS-specific separator for PATH like environment variables:
// a colon on Unix or a semicolon on Windows.
#if defined(LLVM_ON_UNIX)
const char EnvPathSeparator = ':';
#elif defined(_WIN32)
const char EnvPathSeparator = ';';
#endif

#if defined(_WIN32)
typedef unsigned long procid_t; // Must match the type of DWORD on Windows.
typedef void *process_t;        // Must match the type of HANDLE on Windows.
#else
typedef ::pid_t procid_t;
typedef procid_t process_t;
#endif

/// This struct encapsulates information about a process.
struct ProcessInfo {
  enum : procid_t { InvalidPid = 0 };

  procid_t Pid = 0;      /// The process identifier.
  process_t Process = 0; /// Platform-dependent process object.

  /// The return code, set after execution.
  int ReturnCode = 0;

  /// The process is running?
  bool IsRunning = false;
};

/// This struct encapsulates information about a process execution.
struct ProcessStatistics {
  std::chrono::microseconds TotalTime;
  std::chrono::microseconds UserTime;
  uint64_t PeakMemory = 0; ///< Maximum resident set size in KiB.
};

/// Find the first executable file \p Name in \p Paths.
///
/// This does not perform hashing as a shell would but instead stats each PATH
/// entry individually so should generally be avoided. Core LLVM library
/// functions and options should instead require fully specified paths.
///
/// \param Name name of the executable to find. If it contains any system
///   slashes, it will be returned as is.
/// \param Paths optional list of paths to search for \p Name. If empty it
///   will use the system PATH environment instead.
///
/// \returns The fully qualified path to the first \p Name in \p Paths if it
///   exists. \p Name if \p Name has slashes in it. Otherwise an error.
ErrorOr<SmallString<256>> findProgramByName(StringRef Name,
                                            ArrayRef<StringRef> Paths = {});

inline int ChangeStdinToBinary() { return csupport_change_stdin_to_binary(); }
inline int ChangeStdoutToBinary() { return csupport_change_stdout_to_binary(); }
#ifdef _WIN32
inline int ChangeStdinMode(fs::OpenFlags Flags) {
  return (Flags & fs::OF_CRLF) ? 0 : ChangeStdinToBinary();
}
inline int ChangeStdoutMode(fs::OpenFlags Flags) {
  return (Flags & fs::OF_CRLF) ? 0 : ChangeStdoutToBinary();
}
#else
inline int ChangeStdinMode(fs::OpenFlags Flags) {
  return (Flags & fs::OF_Text) ? 0 : ChangeStdinToBinary();
}
inline int ChangeStdoutMode(fs::OpenFlags Flags) {
  return (Flags & fs::OF_Text) ? 0 : ChangeStdoutToBinary();
}
#endif

/// This function executes the program using the arguments provided.  The
/// invoked program will inherit the stdin, stdout, and stderr file
/// descriptors, the environment and other configuration settings of the
/// invoking program.
/// This function waits for the program to finish, so should be avoided in
/// library functions that aren't expected to block. Consider using
/// ExecuteNoWait() instead.
/// \returns an integer result code indicating the status of the program.
/// A zero or positive value indicates the result code of the program.
/// -1 indicates failure to execute
/// -2 indicates a crash during execution or timeout
int ExecuteAndWait(
    StringRef Program, ///< Path of the program to be executed. It is
    ///< presumed this is the result of the findProgramByName method.
    ArrayRef<StringRef> Args, ///< An array of strings that are passed to the
    ///< program.  The first element should be the name of the program.
    ///< The array should **not** be terminated by an empty StringRef.
    ArrayRef<StringRef> Env = {}, ///< Environment for the program.
    ///< If Env.data()==nullptr (default), the current process environment
    ///< is inherited. Otherwise, the provided array is used as the
    ///< environment. The array should **not** be terminated by an empty
    ///< StringRef.
    ArrayRef<StringRef> Redirects = {}, ///<
    ///< An array of paths. Should have a size of zero or three.
    ///< If the array is empty, no redirections are performed.
    ///< Otherwise, the inferior process's stdin(0), stdout(1), and stderr(2)
    ///< will be redirected to the corresponding paths. A StringRef with
    ///< data()==nullptr means no redirect for that fd.
    ///< An empty path ("") disconnects the fd (/dev/null).
    unsigned SecondsToWait = 0, ///< If non-zero, this specifies the amount
    ///< of time to wait for the child process to exit. If the time
    ///< expires, the child is killed and this call returns. If zero,
    ///< this function will wait until the child finishes or forever if
    ///< it doesn't.
    unsigned MemoryLimit = 0, ///< If non-zero, this specifies max. amount
    ///< of memory can be allocated by process. If memory usage will be
    ///< higher limit, the child is killed and this call returns. If zero
    ///< - no memory limit.
    SmallVectorImpl<char> *ErrMsg = nullptr, ///< If non-zero, provides a
    ///< pointer to a buffer in which error messages will be returned.
    bool *ExecutionFailed = nullptr,
    ProcessStatistics *ProcStat = nullptr, ///< If non-zero,
    /// provides a pointer to a structure in which process execution
    /// statistics will be stored.
    BitVector *AffinityMask = nullptr, ///< CPUs or processors the new
                                       /// program shall run on.
    ProcessInfo *PI = nullptr, ///< The child process that should be waited on.
    bool SupportMP = true      ///< Support MP?
);

/// This is a MP version.
int ExecuteAndWaitMP(
    StringRef Program, ///< Path of the program to be executed. It is
    ///< presumed this is the result of the findProgramByName method.
    ArrayRef<StringRef> Args, ///< An array of strings that are passed to the
    ///< program.  The first element should be the name of the program.
    ///< The array should **not** be terminated by an empty StringRef.
    ArrayRef<StringRef> Env = {},       ///< Same semantics as ExecuteAndWait.
    ArrayRef<StringRef> Redirects = {}, ///< Same semantics as ExecuteAndWait.
    unsigned SecondsToWait = 0, ///< If non-zero, this specifies the amount
    ///< of time to wait for the child process to exit. If the time
    ///< expires, the child is killed and this call returns. If zero,
    ///< this function will wait until the child finishes or forever if
    ///< it doesn't.
    unsigned MemoryLimit = 0, ///< If non-zero, this specifies max. amount
    ///< of memory can be allocated by process. If memory usage will be
    ///< higher limit, the child is killed and this call returns. If zero
    ///< - no memory limit.
    SmallVectorImpl<char> *ErrMsg = nullptr, ///< If non-zero, provides a
    ///< pointer to a buffer in which error messages will be returned.
    bool *ExecutionFailed = nullptr,
    ProcessStatistics *ProcStat = nullptr, ///< If non-zero,
    /// provides a pointer to a structure in which process execution
    /// statistics will be stored.
    BitVector *AffinityMask = nullptr, ///< CPUs or processors the new
                                       /// program shall run on.
    ProcessInfo *PI = nullptr ///< The child process that should be waited on.
);

/// Similar to ExecuteAndWait, but returns immediately.
/// @returns The \see ProcessInfo of the newly launched process.
/// \note On Microsoft Windows systems, users will need to either call
/// \see Wait until the process finished execution or win32 CloseHandle() API
/// on ProcessInfo.ProcessHandle to avoid memory leaks.
ProcessInfo ExecuteNoWait(StringRef Program, ArrayRef<StringRef> Args,
                          ArrayRef<StringRef> Env,
                          ArrayRef<StringRef> Redirects = {},
                          unsigned MemoryLimit = 0,
                          SmallVectorImpl<char> *ErrMsg = nullptr,
                          bool *ExecutionFailed = nullptr,
                          BitVector *AffinityMask = nullptr);

/// Return true if the given arguments fit within system-specific
/// argument length limits.
bool commandLineFitsWithinSystemLimits(StringRef Program,
                                       ArrayRef<StringRef> Args);

/// Return true if the given arguments fit within system-specific
/// argument length limits.
bool commandLineFitsWithinSystemLimits(StringRef Program,
                                       ArrayRef<const char *> Args);

/// File encoding options when writing contents that a non-UTF8 tool will
/// read (on Windows systems). For UNIX, we always use UTF-8.
enum WindowsEncodingMethod {
  /// UTF-8 is the LLVM native encoding, being the same as "do not perform
  /// encoding conversion".
  WEM_UTF8,
  WEM_CurrentCodePage,
  WEM_UTF16
};

/// Saves the UTF8-encoded \p contents string into the file \p FileName
/// using a specific encoding.
///
/// This write file function adds the possibility to choose which encoding
/// to use when writing a text file. On Windows, this is important when
/// writing files with internationalization support with an encoding that is
/// different from the one used in LLVM (UTF-8). We use this when writing
/// response files, since GCC tools on MinGW only understand legacy code
/// pages, and VisualStudio tools only understand UTF-16.
/// For UNIX, using different encodings is silently ignored, since all tools
/// work well with UTF-8.
/// This function assumes that you only use UTF-8 *text* data and will convert
/// it to your desired encoding before writing to the file.
///
/// FIXME: We use EM_CurrentCodePage to write response files for GNU tools in
/// a MinGW/MinGW-w64 environment, which has serious flaws but currently is
/// our best shot to make gcc/ld understand international characters. This
/// should be changed as soon as binutils fix this to support UTF16 on mingw.
///
/// \returns 0 on success, errno-style value on failure
#ifdef LLVM_ON_UNIX
inline int writeFileWithEncoding(StringRef FileName, StringRef Contents,
                                 WindowsEncodingMethod Encoding = WEM_UTF8) {
  (void)Encoding;
  return csupport_write_file_contents(FileName.data(), FileName.size(),
                                      Contents.data(), Contents.size());
}
#else
int writeFileWithEncoding(StringRef FileName, StringRef Contents,
                          WindowsEncodingMethod Encoding = WEM_UTF8);
#endif

/// This function waits for the process specified by \p PI to finish.
/// \returns A \see ProcessInfo struct with Pid set to:
/// \li The process id of the child process if the child process has changed
/// state.
/// \li 0 if the child process has not changed state.
/// \note Users of this function should always check the ReturnCode member of
/// the \see ProcessInfo returned from this function.
ProcessInfo
Wait(const ProcessInfo &PI, ///< The child process that should be waited on.
     unsigned SecondsToWait = UINT_MAX, ///< If UINT_MAX, waits until
     ///< child has terminated.
     ///< Otherwise, this specifies the amount of time to wait for the child
     ///< process. If the time expires, and \p Polling is false, the child is
     ///< killed and this < function returns. If the time expires and \p
     ///< Polling is true, the child is resumed.
     ///<
     ///< If zero, this function will perform a non-blocking
     ///< wait on the child process.
     SmallVectorImpl<char> *ErrMsg = nullptr, ///< If non-zero, provides a
     ///< pointer to a buffer in which error messages will be returned.
     ProcessStatistics *ProcStat = nullptr, ///< If non-zero, provides
     /// a pointer to a structure in which process execution statistics will
     /// be stored.

     bool Polling = false ///< If true, do not kill the process on timeout.
);

/// This function waits for the process specified by \p PIs to finish.
bool WaitMP(ArrayRef<ProcessInfo *> PIs, bool WaitAllMP,
            unsigned SecondsToWait = UINT_MAX,
            SmallVectorImpl<char> *ErrMsg = nullptr,
            ProcessStatistics *ProcStat = nullptr, bool Polling = false);

/// This function clean up the process specified by \p PIs to finish.
bool CleanUpMP(ArrayRef<ProcessInfo *> PIs);

/// Print a command argument, and optionally quote it.
void printArg(llvm::raw_ostream &OS, StringRef Arg, bool Quote);

#if defined(_WIN32)
/// Given a list of command line arguments, quote and escape them as necessary
/// to build a single flat command line appropriate for calling CreateProcess
/// on
/// Windows.
ErrorOr<SmallVector<wchar_t, 256>>
flattenWindowsCommandLine(ArrayRef<StringRef> Args);
#endif
inline bool commandLineFitsWithinSystemLimits(StringRef Program,
                                              ArrayRef<const char *> Args) {
  SmallVector<StringRef, 16> StringRefArgs;
  StringRefArgs.reserve(Args.size());
  for (const char *A : Args)
    StringRefArgs.emplace_back(A);
  return commandLineFitsWithinSystemLimits(Program, StringRefArgs);
}

inline void printArg(raw_ostream &OS, StringRef Arg, bool Quote) {
  const bool Escape = Arg.find_first_of(" \"\\$") != StringRef::npos;
  if (!Quote && !Escape) {
    OS << Arg;
    return;
  }
  OS << '"';
  for (const auto c : Arg) {
    if (c == '"' || c == '\\' || c == '$')
      OS << '\\';
    OS << c;
  }
  OS << '"';
}

} // namespace sys
} // namespace llvm

// Out-of-line definition in lib/CSupport/Unix/Program.inc (via
// support_cpp.cpp).
bool Execute(llvm::sys::ProcessInfo &PI, llvm::StringRef Program,
             llvm::ArrayRef<llvm::StringRef> Args,
             llvm::ArrayRef<llvm::StringRef> Env,
             llvm::ArrayRef<llvm::StringRef> Redirects, unsigned MemoryLimit,
             llvm::SmallVectorImpl<char> *ErrMsg,
             llvm::BitVector *AffinityMask);

inline int llvm::sys::ExecuteAndWait(
    StringRef Program, ArrayRef<StringRef> Args, ArrayRef<StringRef> Env,
    ArrayRef<StringRef> Redirects, unsigned SecondsToWait, unsigned MemoryLimit,
    SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
    ProcessStatistics *ProcStat, BitVector *AffinityMask, ProcessInfo *PI,
    bool SupportMP) {
#ifdef _WIN32
  if (SupportMP) {
    return ExecuteAndWaitMP(Program, Args, Env, Redirects, SecondsToWait,
                            MemoryLimit, ErrMsg, ExecutionFailed, ProcStat,
                            AffinityMask, PI);
  }
#endif
  assert(Redirects.empty() || Redirects.size() == 3);
  ProcessInfo PITemp;
  if (!PI) {
    PI = &PITemp;
  }
  if (Execute(*PI, Program, Args, Env, Redirects, MemoryLimit, ErrMsg,
              AffinityMask)) {
    if (ExecutionFailed)
      *ExecutionFailed = false;
    ProcessInfo Result = Wait(
        *PI, SecondsToWait == 0 ? UINT_MAX : SecondsToWait, ErrMsg, ProcStat);
    return Result.ReturnCode;
  }
  if (ExecutionFailed)
    *ExecutionFailed = true;
  return -1;
}
inline int llvm::sys::ExecuteAndWaitMP(
    StringRef Program, ArrayRef<StringRef> Args, ArrayRef<StringRef> Env,
    ArrayRef<StringRef> Redirects, unsigned SecondsToWait, unsigned MemoryLimit,
    SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
    ProcessStatistics *ProcStat, BitVector *AffinityMask, ProcessInfo *PI) {
#ifdef _WIN32
  ProcessInfo PITemp;
  if (!PI) {
    PI = &PITemp;
  }
  if (Execute(*PI, Program, Args, Env, Redirects, MemoryLimit, ErrMsg,
              AffinityMask)) {
    if (ExecutionFailed)
      *ExecutionFailed = false;
    bool Ret = WaitMP({PI}, true, SecondsToWait == 0 ? UINT_MAX : SecondsToWait,
                      ErrMsg, ProcStat);
    return Ret ? 0 : 1;
  }
  if (ExecutionFailed)
    *ExecutionFailed = true;
  return -1;
#else
  return -1;
#endif
}
inline llvm::sys::ProcessInfo
llvm::sys::ExecuteNoWait(StringRef Program, ArrayRef<StringRef> Args,
                         ArrayRef<StringRef> Env, ArrayRef<StringRef> Redirects,
                         unsigned MemoryLimit, SmallVectorImpl<char> *ErrMsg,
                         bool *ExecutionFailed, BitVector *AffinityMask) {
  assert(Redirects.empty() || Redirects.size() == 3);
  ProcessInfo PI;
  if (ExecutionFailed)
    *ExecutionFailed = false;
  if (!Execute(PI, Program, Args, Env, Redirects, MemoryLimit, ErrMsg,
               AffinityMask))
    if (ExecutionFailed)
      *ExecutionFailed = true;
  return PI;
}

#endif
