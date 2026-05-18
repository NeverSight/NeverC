//===- llvm/Support/Process.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Provides a library for accessing information about this process and other
/// processes on the operating system. Also provides means of spawning
/// subprocess for commands. The design of this library is modeled after the
/// proposed design of the Boost.Process library, and is design specifically to
/// follow the style of standard libraries and potentially become a proposal
/// for a standard library.
///
/// This file declares the llvm::sys::Process class which contains a collection
/// of legacy static interfaces for extracting various information about the
/// current process. The goal is to migrate users of this API over to the new
/// interfaces.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PROCESS_H
#define LLVM_SUPPORT_PROCESS_H

#include "csupport/lprocess.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include <optional>

namespace llvm {
template <typename T> class ArrayRef;
class StringRef;

namespace sys {

/// A collection of legacy interfaces for querying information about the
/// current executing process.
class Process {
public:
  using Pid = int32_t;

  static Pid getProcessId() {
    return static_cast<Pid>(csupport_get_process_id());
  }

  static Expected<unsigned> getPageSize() {
    unsigned ps = csupport_get_page_size();
    if (ps == 0)
      return createStringError(std::errc::not_supported,
                               "cannot get page size");
    return ps;
  }

  static unsigned getPageSizeEstimate() {
    if (auto PageSize = getPageSize())
      return *PageSize;
    else {
      consumeError(PageSize.takeError());
      return 4096;
    }
  }

  static size_t GetMallocUsage() { return csupport_get_malloc_usage(); }

  static void GetTimeUsage(TimePoint<> &elapsed,
                           std::chrono::nanoseconds &user_time,
                           std::chrono::nanoseconds &sys_time) {
    int64_t e, u, s;
    csupport_get_time_usage(&e, &u, &s);
    auto us = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::nanoseconds(e));
    elapsed = std::chrono::system_clock::time_point(us);
    user_time = std::chrono::nanoseconds(u);
    sys_time = std::chrono::nanoseconds(s);
  }

  static void PreventCoreFiles() { csupport_prevent_core_files(); }
  static bool AreCoreFilesPrevented() { return csupport_core_files_prevented; }

  static std::optional<SmallString<256>> GetEnv(StringRef name) {
    SmallString<128> NameStr(name);
    const char *Val = csupport_get_env(NameStr.c_str());
    if (!Val)
      return std::nullopt;
    return SmallString<256>(Val);
  }

  static SmallString<256> FindInEnvPath(StringRef EnvName, StringRef FileName,
                                        ArrayRef<SmallString<256>> IgnoreList,
                                        char Separator = EnvPathSeparator);

  static SmallString<256> FindInEnvPath(StringRef EnvName, StringRef FileName,
                                        char Separator = EnvPathSeparator);

  static int FixupStandardFileDescriptors() { return csupport_fixup_std_fds(); }
  static int SafelyCloseFileDescriptor(int FD) {
    return csupport_safely_close_fd(FD);
  }
  static bool StandardInIsUserInput() { return csupport_fd_is_displayed(0); }
  static bool StandardOutIsDisplayed() {
    return csupport_fd_is_displayed(csupport_stdout_fileno());
  }
  static bool StandardErrIsDisplayed() {
    return csupport_fd_is_displayed(csupport_stderr_fileno());
  }
  static bool FileDescriptorIsDisplayed(int fd) {
    return csupport_fd_is_displayed(fd);
  }
  static bool FileDescriptorHasColors(int fd) {
    return csupport_fd_has_terminal_colors(fd);
  }
  static unsigned StandardOutColumns() {
    return csupport_fd_columns(csupport_stdout_fileno());
  }
  static unsigned StandardErrColumns() {
    return csupport_fd_columns(csupport_stderr_fileno());
  }
  static bool StandardOutHasColors() {
    return csupport_fd_has_terminal_colors(csupport_stdout_fileno());
  }
  static bool StandardErrHasColors() {
    return csupport_fd_has_terminal_colors(csupport_stderr_fileno());
  }
  static void UseANSIEscapeCodes(bool enable) {
    csupport_use_ansi_escape_codes(enable ? 1 : 0);
  }
  static bool ColorNeedsFlush() { return csupport_color_needs_flush(); }
  static const char *OutputColor(char c, bool bold, bool bg) {
    return csupport_output_color(c, bold ? 1 : 0, bg ? 1 : 0);
  }
  static const char *OutputBold(bool bg) {
    return csupport_output_bold(bg ? 1 : 0);
  }
  static const char *OutputReverse() { return csupport_output_reverse(); }
  static const char *ResetColor() { return csupport_reset_color(); }
  static unsigned GetRandomNumber() { return csupport_get_random_number(); }

  [[noreturn]] static void Exit(int RetCode, bool NoCleanup = false);
  [[noreturn]] static void ExitNoCleanup(int RetCode) {
    csupport_exit_no_cleanup(RetCode);
    __builtin_unreachable();
  }

private:
};

} // namespace sys

inline SmallString<256> sys::Process::FindInEnvPath(StringRef EnvName,
                                                    StringRef FileName,
                                                    char Separator) {
  return FindInEnvPath(EnvName, FileName, {}, Separator);
}
inline SmallString<256>
sys::Process::FindInEnvPath(StringRef EnvName, StringRef FileName,
                            ArrayRef<SmallString<256>> IgnoreList,
                            char Separator) {
  assert(!sys::path::is_absolute(FileName));
  SmallString<256> FoundPath;
  auto OptPath = sys::Process::GetEnv(EnvName);
  if (!OptPath)
    return FoundPath;
  const char EnvPathSeparatorStr[] = {Separator, '\0'};
  SmallVector<StringRef, 16> Dirs;
  SplitString(*OptPath, Dirs, EnvPathSeparatorStr);
  for (StringRef Dir : Dirs) {
    if (Dir.empty())
      continue;
    if (any_of(IgnoreList,
               [&](StringRef S) { return sys::fs::equivalent(S, Dir); }))
      continue;
    SmallString<128> FilePath(Dir);
    sys::path::append(FilePath, FileName);
    if (sys::fs::exists(Twine(FilePath))) {
      FoundPath = SmallString<256>(FilePath);
      break;
    }
  }
  return FoundPath;
}

inline void sys::Process::Exit(int RetCode, bool NoCleanup) {
  if (CrashRecoveryContext *CRC = CrashRecoveryContext::GetCurrent())
    CRC->HandleExit(RetCode);
  if (NoCleanup)
    ExitNoCleanup(RetCode);
  else
    ::exit(RetCode);
}

} // namespace llvm

#define COLOR(FGBG, CODE, BOLD) "\033[0;" BOLD FGBG CODE "m"
#define ALLCOLORS(FGBG, BOLD)                                                  \
  {COLOR(FGBG, "0", BOLD), COLOR(FGBG, "1", BOLD), COLOR(FGBG, "2", BOLD),     \
   COLOR(FGBG, "3", BOLD), COLOR(FGBG, "4", BOLD), COLOR(FGBG, "5", BOLD),     \
   COLOR(FGBG, "6", BOLD), COLOR(FGBG, "7", BOLD)}
static const char colorcodes[2][2][8][10] = {
    {ALLCOLORS("3", ""), ALLCOLORS("3", "1;")},
    {ALLCOLORS("4", ""), ALLCOLORS("4", "1;")}};

#endif
