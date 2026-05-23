//===- InitLLVM.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_INITLLVM_H
#define LLVM_SUPPORT_INITLLVM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include <optional>
#ifdef __MVS__
#include "llvm/Support/AutoConvert.h"
#include <unistd.h>
#endif
#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#endif

// The main() functions in typical LLVM tools start with InitLLVM which does
// the following one-time initializations:
//
//  1. Setting up a signal handler so that pretty stack trace is printed out
//     if a process crashes. A signal handler that exits when a failed write to
//     a pipe occurs may optionally be installed: this is on-by-default.
//
//  2. Set up the global new-handler which is called when a memory allocation
//     attempt fails.
//
//  3. If running on Windows, obtain command line arguments using a
//     multibyte character-aware API and convert arguments into UTF-8
//     encoding, so that you can assume that command line arguments are
//     always encoded in UTF-8 on any platform.
//
// InitLLVM calls llvm_shutdown() on destruction, which cleans up
// ManagedStatic objects.
namespace llvm {
class InitLLVM {
public:
  InitLLVM(int &Argc, const char **&Argv,
           bool InstallPipeSignalExitHandler = true);
  InitLLVM(int &Argc, char **&Argv, bool InstallPipeSignalExitHandler = true)
      : InitLLVM(Argc, const_cast<const char **&>(Argv),
                 InstallPipeSignalExitHandler) {}

  ~InitLLVM();

private:
  BumpPtrAllocator Alloc;
  SmallVector<const char *, 0> Args;
  std::optional<PrettyStackTraceProgram> StackPrinter;
};
#ifdef __MVS__
inline void CleanupStdHandles(void *Cookie) {
  raw_ostream *Outs = &outs(), *Errs = &errs();
  Outs->flush();
  Errs->flush();
  restoreStdHandleAutoConversion(STDIN_FILENO);
  restoreStdHandleAutoConversion(STDOUT_FILENO);
  restoreStdHandleAutoConversion(STDERR_FILENO);
}
#endif

inline InitLLVM::InitLLVM(int &Argc, const char **&Argv,
                          bool InstallPipeSignalExitHandler) {
#ifdef __MVS__
  sys::AddSignalHandler(CleanupStdHandles, 0);
#endif
  if (InstallPipeSignalExitHandler)
    sys::SetOneShotPipeSignalFunction(sys::DefaultOneShotPipeSignalHandler);
  StackPrinter.emplace(Argc, Argv);
  sys::PrintStackTraceOnErrorSignal(Argv[0]);
  install_out_of_memory_new_handler();
#ifdef __MVS__
  SmallString<128> Banner(Argv[0]);
  Banner += ": ";
  ExitOnError ExitOnErr(std::string(Banner.str()));
  ExitOnErr(errorCodeToError(enableAutoConversion(STDERR_FILENO)));
  ExitOnErr(errorCodeToError(enableAutoConversion(STDOUT_FILENO)));
#endif
#ifdef _WIN32
  SmallString<128> Banner(Argv[0]);
  Banner += ": ";
  ExitOnError ExitOnErr(std::string(Banner.str()));
  ExitOnErr(
      errorCodeToError(sys::windows::GetCommandLineArguments(Args, Alloc)));
  Args.push_back(0);
  Argc = Args.size() - 1;
  Argv = Args.data();
#endif
}

inline InitLLVM::~InitLLVM() {
#ifdef __MVS__
  CleanupStdHandles(0);
#endif
  llvm_shutdown();
}

} // namespace llvm

#endif
