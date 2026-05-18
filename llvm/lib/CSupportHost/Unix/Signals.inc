//===- Signals.cpp - Generic Unix Signals Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Unix/Unix.h"
#include "csupport/lpath.h"
#include "csupport/lsignals.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ExitCodes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "../SignalsSymbolize.inc"
#include <string>
#ifdef HAVE_BACKTRACE
#include BACKTRACE_HEADER
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#if HAVE_MACH_MACH_H
#include <mach/mach.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#if HAVE_LINK_H
#include <link.h>
#endif

using namespace llvm;

static void raw_ostream_write_cb(void *ctx, const char *data, size_t len) {
  static_cast<raw_ostream *>(ctx)->write(data, len);
}

static StringRef Argv0;

/* RegisterHandlers/unregisterHandlers/SignalHandler/InfoSignalHandler/
 * CleanupOnSignal/signal arrays all moved to CSupport/Signals.c.
 * C++ side only provides thin wrappers that delegate to csupport_* C funcs. */

void sys::unregisterHandlers() {
  csupport_unix_unregister_all_handlers();
}

void sys::CleanupOnSignal(uintptr_t Context) {
  csupport_unix_cleanup_on_signal((int)Context);
}

void llvm::sys::RunInterruptHandlers() {
  csupport_file_remove_list_remove_all();
}

void llvm::sys::SetInterruptFunction(void (*IF)()) {
  csupport_set_atomic_interrupt_fn(IF);
  csupport_unix_register_all_handlers();
}

void llvm::sys::SetInfoSignalFunction(void (*Handler)()) {
  csupport_set_atomic_info_fn(Handler);
  csupport_unix_register_all_handlers();
}

void llvm::sys::SetOneShotPipeSignalFunction(void (*Handler)()) {
  csupport_set_atomic_pipe_fn(Handler);
  csupport_unix_register_all_handlers();
}

void llvm::sys::DefaultOneShotPipeSignalHandler() {
  exit(EX_IOERR);
}

bool llvm::sys::RemoveFileOnSignal(StringRef Filename, std::string *ErrMsg) {
  csupport_file_remove_list_insert(Filename.str().c_str());
  csupport_unix_register_all_handlers();
  return false;
}

void llvm::sys::DontRemoveFileOnSignal(StringRef Filename) {
  csupport_file_remove_list_erase(Filename.str().c_str());
}

void llvm::sys::AddSignalHandler(sys::SignalHandlerCallback FnPtr,
                                 void *Cookie) {
  csupport_insert_signal_callback(FnPtr, Cookie);
  csupport_unix_register_all_handlers();
}

namespace llvm {
#if ENABLE_BACKTRACES && defined(HAVE_BACKTRACE) && HAVE_LINK_H &&             \
    (defined(__linux__) || defined(__FreeBSD__) ||                             \
     defined(__FreeBSD_kernel__) || defined(__NetBSD__))
static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  (void)StrPool;
  return csupport_find_modules_offsets_elf(StackTrace, Depth, Modules, Offsets,
                                           MainExecutableName) != 0;
}

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  csupport_print_dso_markup_all(&OS, raw_ostream_write_cb, MainExecutableName);
  return true;
}

#elif ENABLE_BACKTRACES && defined(__APPLE__) && defined(__LP64__)
static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  return csupport_find_modules_offsets_apple(StackTrace, Depth, Modules,
                                             Offsets) != 0;
}

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  return false;
}
#else
static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  return false;
}

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  return false;
}
#endif
} // namespace llvm

void llvm::sys::PrintStackTrace(raw_ostream &OS, int Depth) {
#if ENABLE_BACKTRACES
  static void *StackTrace[256];
  int depth = 0;
#if defined(HAVE_BACKTRACE)
  if (!depth)
    depth = backtrace(StackTrace, static_cast<int>(std::size(StackTrace)));
#endif
  if (!depth)
    depth = csupport_unwind_backtrace(StackTrace, static_cast<int>(std::size(StackTrace)));
  if (!depth)
    return;
  if (!Depth)
    Depth = depth;
  if (printMarkupStackTrace(Argv0, StackTrace, Depth, OS))
    return;
  if (printSymbolizedStackTrace(Argv0, StackTrace, Depth, OS))
    return;
  OS << "Stack dump without symbol names (ensure you have llvm-symbolizer in "
        "your PATH or set the environment var `LLVM_SYMBOLIZER_PATH` to point "
        "to it):\n";
#if HAVE_DLFCN_H && HAVE_DLADDR
  // NeverC: no Itanium demangler, symbols are printed as-is.
  csupport_print_stack_trace_dladdr(&OS, raw_ostream_write_cb, StackTrace,
                                    depth, /*demangle_fn=*/nullptr);
#elif defined(HAVE_BACKTRACE)
  backtrace_symbols_fd(StackTrace, Depth, STDERR_FILENO);
#endif
#endif
}

static void PrintStackTraceSignalHandler(void *) {
  sys::PrintStackTrace(llvm::errs());
}

void llvm::sys::DisableSystemDialogsOnCrash() {}

void llvm::sys::PrintStackTraceOnErrorSignal(StringRef Argv0,
                                             bool DisableCrashReporting) {
  ::Argv0 = Argv0;

  AddSignalHandler(PrintStackTraceSignalHandler, nullptr);

#if defined(__APPLE__) && ENABLE_CRASH_OVERRIDES
  if (DisableCrashReporting || getenv("LLVM_DISABLE_CRASH_REPORT")) {
    mach_port_t self = mach_task_self();
    exception_mask_t mask = EXC_MASK_CRASH;
    kern_return_t ret = task_set_exception_ports(
        self, mask, MACH_PORT_NULL,
        EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);
    (void)ret;
  }
#endif
}
