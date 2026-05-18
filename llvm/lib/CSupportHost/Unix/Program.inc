//===- llvm/Support/Unix/Program.inc ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific portion of the Program class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX
//===          code that is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "llvm/Support/Program.h"
#include "csupport/lprogram.h"

#include "Unix/Unix.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Support/AutoConvert.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
/* posix_spawn/environ includes now in CSupport/Program.c */

using namespace llvm;
using namespace sys;

/* findProgramByName core moved to CSupport/Path.c → csupport_find_program */
ErrorOr<SmallString<256>> sys::findProgramByName(StringRef Name,
                                                 ArrayRef<StringRef> Paths) {
  assert(!Name.empty() && "Must have a name!");

  SmallVector<SmallString<128>, 16> PathStorage;
  SmallVector<const char *, 16> CStringPaths;

  if (!Paths.empty()) {
    for (auto P : Paths) {
      PathStorage.emplace_back(P);
      CStringPaths.push_back(PathStorage.back().c_str());
    }
  }

  char buf[PATH_MAX];
  size_t len = csupport_find_program(
      Name.data(), Name.size(),
      CStringPaths.empty() ? nullptr : CStringPaths.data(),
      CStringPaths.size(), buf, sizeof(buf));
  if (len > 0)
    return SmallString<256>(StringRef(buf, len));
  return errc::no_such_file_or_directory;
}

/* RedirectIO removed — now handled by csupport_execute_child() in Program.c */

/* Execute() moved to CSupport/Program.c → csupport_execute_child() */

static std::vector<const char *>
toNullTerminatedCStringArray(ArrayRef<StringRef> Strings, StringSaver &Saver) {
  std::vector<const char *> Result;
  for (StringRef S : Strings)
    Result.push_back(Saver.save(S).data());
  Result.push_back(nullptr);
  return Result;
}

bool Execute(ProcessInfo &PI, StringRef Program,
                    ArrayRef<StringRef> Args, ArrayRef<StringRef> Env,
                    ArrayRef<StringRef> Redirects,
                    unsigned MemoryLimit, SmallVectorImpl<char> *ErrMsg,
                    BitVector *AffinityMask) {
  if (!llvm::sys::fs::exists(Program)) {
    if (ErrMsg) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      OS << "Executable \"" << Program << "\" doesn't exist!";
      ErrMsg->assign(Msg.begin(), Msg.end());
    }
    return false;
  }

  assert(!AffinityMask && "Starting a process with an affinity mask is "
                          "currently not supported on Unix!");

  BumpPtrAllocator Allocator;
  StringSaver Saver(Allocator);
  std::vector<const char *> ArgVector, EnvVector;
  ArgVector = toNullTerminatedCStringArray(Args, Saver);
  if (Env.data())
    EnvVector = toNullTerminatedCStringArray(Env, Saver);

  csupport_redirect_t c_redirects[3] = {{NULL}, {NULL}, {NULL}};
  SmallString<256> RedirectStorage[3];
  if (!Redirects.empty()) {
    assert(Redirects.size() == 3);
    for (int i = 0; i < 3; ++i) {
      if (Redirects[i].data()) {
        RedirectStorage[i] = Redirects[i];
        c_redirects[i].path = RedirectStorage[i].c_str();
      }
    }
  }

  char errbuf[512] = {0};
  SmallString<256> ProgramStr(Program);
  int child = csupport_execute_child(
      ProgramStr.c_str(),
      ArgVector.data(),
      Env.data() ? EnvVector.data() : nullptr,
      Redirects.empty() ? nullptr : c_redirects,
      Redirects.empty() ? 0 : 3,
      MemoryLimit,
      errbuf, sizeof(errbuf));

  if (child < 0) {
    if (ErrMsg && errbuf[0]) {
      StringRef msg(errbuf);
      ErrMsg->assign(msg.begin(), msg.end());
    }
    return false;
  }

  PI.Pid = child;
  PI.Process = child;
  return true;
}

/* Wait/wait4 moved to CSupport/Program.c → csupport_wait_process() */

ProcessInfo llvm::sys::Wait(const ProcessInfo &PI,
                            unsigned SecondsToWait,
                            SmallVectorImpl<char> *ErrMsg,
                            ProcessStatistics *ProcStat,
                            bool Polling) {
  assert(PI.Pid && "invalid pid to wait on, process not started?");

  ProcessInfo WaitResult;
  int result_pid = 0, return_code = 0;
  int64_t user_us = 0, kernel_us = 0;
  uint64_t peak_mem = 0;
  char errbuf[512] = {0};

  if (ProcStat)
    memset(ProcStat, 0, sizeof(*ProcStat));

  int rc = csupport_wait_process(
      PI.Pid, SecondsToWait, Polling ? 1 : 0,
      &result_pid, &return_code,
      ProcStat ? &user_us : nullptr,
      ProcStat ? &kernel_us : nullptr,
      ProcStat ? &peak_mem : nullptr,
      ErrMsg ? errbuf : nullptr,
      ErrMsg ? sizeof(errbuf) : 0);

  WaitResult.Pid = result_pid;
  WaitResult.ReturnCode = return_code;

  if (ErrMsg && errbuf[0]) {
    StringRef msg(errbuf);
    ErrMsg->assign(msg.begin(), msg.end());
  }

  if (ProcStat && rc == 1) {
    auto UserT = std::chrono::microseconds(user_us);
    auto KernelT = std::chrono::microseconds(kernel_us);
    *ProcStat = ProcessStatistics{UserT + KernelT, UserT, peak_mem};
  }

  return WaitResult;
}

/* commandLineFitsWithinSystemLimits moved to CSupport/Program.c → csupport_cmd_args_fit() */
bool llvm::sys::commandLineFitsWithinSystemLimits(StringRef Program,
                                                  ArrayRef<StringRef> Args) {
  SmallVector<const char *, 64> CArgs;
  SmallVector<SmallString<128>, 64> Storage;
  Storage.resize(Args.size());
  for (size_t i = 0; i < Args.size(); ++i) {
    Storage[i] = Args[i];
    CArgs.push_back(Storage[i].c_str());
  }
  return csupport_cmd_args_fit(Program.size(), CArgs.data(), CArgs.size()) != 0;
}
