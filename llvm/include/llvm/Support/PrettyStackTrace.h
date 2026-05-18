//===- llvm/Support/PrettyStackTrace.h - Pretty Crash Handling --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PrettyStackTraceEntry class, which is used to make
// crashes give more contextual information about what the program was doing
// when it crashed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PRETTYSTACKTRACE_H
#define LLVM_SUPPORT_PRETTYSTACKTRACE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Watchdog.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdarg>
#include <cstring>

namespace llvm {

/// Enables dumping a "pretty" stack trace when the program crashes.
///
/// \see PrettyStackTraceEntry
void EnablePrettyStackTrace();

/// Enables (or disables) dumping a "pretty" stack trace when the user sends
/// SIGINFO or SIGUSR1 to the current process.
///
/// This is a per-thread decision so that a program can choose to print stack
/// traces only on a primary thread, or on all threads that use
/// PrettyStackTraceEntry.
///
/// \see EnablePrettyStackTrace
/// \see PrettyStackTraceEntry
void EnablePrettyStackTraceOnSigInfoForThisThread(bool ShouldEnable = true);

/// Replaces the generic bug report message that is output upon
/// a crash.
void setBugReportMsg(const char *Msg);

/// Get the bug report message that will be output upon a crash.
const char *getBugReportMsg();

/// PrettyStackTraceEntry - This class is used to represent a frame of the
/// "pretty" stack trace that is dumped when a program crashes. You can define
/// subclasses of this and declare them on the program stack: when they are
/// constructed and destructed, they will add their symbolic frames to a
/// virtual stack trace.  This gets dumped out if the program crashes.
class PrettyStackTraceEntry {
  friend PrettyStackTraceEntry *ReverseStackTrace(PrettyStackTraceEntry *);

  PrettyStackTraceEntry *NextEntry;
  PrettyStackTraceEntry(const PrettyStackTraceEntry &) = delete;
  void operator=(const PrettyStackTraceEntry &) = delete;

public:
  PrettyStackTraceEntry();
  virtual ~PrettyStackTraceEntry();

  /// print - Emit information about this stack frame to OS.
  virtual void print(raw_ostream &OS) const = 0;

  /// getNextEntry - Return the next entry in the list of frames.
  const PrettyStackTraceEntry *getNextEntry() const { return NextEntry; }
};

/// PrettyStackTraceString - This object prints a specified string (which
/// should not contain newlines) to the stream as the stack trace when a crash
/// occurs.
class PrettyStackTraceString : public PrettyStackTraceEntry {
  const char *Str;

public:
  PrettyStackTraceString(const char *str) : Str(str) {}
  void print(raw_ostream &OS) const override;
};

/// PrettyStackTraceFormat - This object prints a string (which may use
/// printf-style formatting but should not contain newlines) to the stream
/// as the stack trace when a crash occurs.
class PrettyStackTraceFormat : public PrettyStackTraceEntry {
  llvm::SmallVector<char, 32> Str;

public:
  PrettyStackTraceFormat(const char *Format, ...);
  void print(raw_ostream &OS) const override;
};

/// PrettyStackTraceProgram - This object prints a specified program arguments
/// to the stream as the stack trace when a crash occurs.
class PrettyStackTraceProgram : public PrettyStackTraceEntry {
  int ArgC;
  const char *const *ArgV;

public:
  PrettyStackTraceProgram(int argc, const char *const *argv)
      : ArgC(argc), ArgV(argv) {
    EnablePrettyStackTrace();
  }
  void print(raw_ostream &OS) const override;
};

/// Returns the topmost element of the "pretty" stack state.
const void *SavePrettyStackState();

/// Restores the topmost element of the "pretty" stack state to State, which
/// should come from a previous call to SavePrettyStackState().  This is
/// useful when using a CrashRecoveryContext in code that also uses
/// PrettyStackTraceEntries, to make sure the stack that's printed if a crash
/// happens after a crash that's been recovered by CrashRecoveryContext
/// doesn't have frames on it that were added in code unwound by the
/// CrashRecoveryContext.
void RestorePrettyStackState(const void *State);

} // end namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

#ifdef HAVE_CRASHREPORTERCLIENT_H
#include <CrashReporterClient.h>
#endif

namespace llvm {

inline static const char *BugReportMsg =
    "PLEASE submit a bug report to " BUG_REPORT_URL
    " and include the crash backtrace.\n";

// If backtrace support is not enabled, compile out support for pretty stack
// traces.  This has the secondary effect of not requiring thread local storage
// when backtrace support is disabled.
#if ENABLE_BACKTRACES

// We need a thread local pointer to manage the stack of our stack trace
// objects, but we *really* cannot tolerate destructors running and do not want
// to pay any overhead of synchronizing. As a consequence, we use a raw
// thread-local variable.
inline static LLVM_THREAD_LOCAL PrettyStackTraceEntry *PrettyStackTraceHead = 0;

// The use of 'volatile' here is to ensure that any particular thread always
// reloads the value of the counter. The atomic type allows us to specify that
// this variable is accessed in an unsychronized way (it's not actually
// synchronizing). This does technically mean that the value may not appear to
// be the same across threads running simultaneously on different CPUs, but in
// practice the worst that will happen is that we won't print a stack trace when
// we could have.
//
// This is initialized to 1 because 0 is used as a sentinel for "not enabled on
// the current thread". If the user happens to overflow an 'unsigned' with
// SIGINFO requests, it's possible that some threads will stop responding to it,
// but the program won't crash.
inline static volatile unsigned GlobalSigInfoGenerationCounter = 1;
inline static LLVM_THREAD_LOCAL unsigned ThreadLocalSigInfoGenerationCounter =
    0;

inline PrettyStackTraceEntry *ReverseStackTrace(PrettyStackTraceEntry *Head) {
  PrettyStackTraceEntry *Prev = 0;
  while (Head) {
    PrettyStackTraceEntry *Next = Head->NextEntry;
    Head->NextEntry = Prev;
    Prev = Head;
    Head = Next;
  }
  return Prev;
}

inline static void PrintStack(raw_ostream &OS) {
  // Print out the stack in reverse order. To avoid recursion (which is likely
  // to fail if we crashed due to stack overflow), we do an up-front pass to
  // reverse the stack, then print it, then reverse it again.
  unsigned ID = 0;
  SaveAndRestore<PrettyStackTraceEntry *> SavedStack{PrettyStackTraceHead, 0};
  PrettyStackTraceEntry *ReversedStack = ReverseStackTrace(SavedStack.get());
  for (const PrettyStackTraceEntry *Entry = ReversedStack; Entry;
       Entry = Entry->getNextEntry()) {
    OS << ID++ << ".\t";
    sys::Watchdog W(5);
    Entry->print(OS);
  }
  ReverseStackTrace(ReversedStack);
}

/// Print the current stack trace to the specified stream.
///
/// Marked NOINLINE so it can be called from debuggers.
inline LLVM_ATTRIBUTE_NOINLINE static void PrintCurStackTrace(raw_ostream &OS) {
  // Don't print an empty trace.
  if (!PrettyStackTraceHead)
    return;

  // If there are pretty stack frames registered, walk and emit them.
  OS << "Stack dump:\n";

  PrintStack(OS);
  OS.flush();
}

// Integrate with crash reporter libraries.
#if defined(__APPLE__) && defined(HAVE_CRASHREPORTERCLIENT_H)
//  If any clients of llvm try to link to libCrashReporterClient.a themselves,
//  only one crash info struct will be used.
extern "C" {
CRASH_REPORTER_CLIENT_HIDDEN
struct crashreporter_annotations_t gCRAnnotations
    __attribute__((section("__DATA," CRASHREPORTER_ANNOTATIONS_SECTION)))
#if CRASHREPORTER_ANNOTATIONS_VERSION < 5
    = {CRASHREPORTER_ANNOTATIONS_VERSION, 0, 0, 0, 0, 0, 0};
#else
    = {CRASHREPORTER_ANNOTATIONS_VERSION, 0, 0, 0, 0, 0, 0, 0};
#endif
}
#elif defined(__APPLE__) && HAVE_CRASHREPORTER_INFO
// Header is included in many TUs; weak avoids duplicate symbol at link time.
extern "C" __attribute__((weak)) const char *__crashreporter_info__
    __attribute__((visibility("hidden"))) = 0;
asm(".desc ___crashreporter_info__, 0x10");
#endif

inline static void setCrashLogMessage(const char *msg) LLVM_ATTRIBUTE_UNUSED;
inline static void setCrashLogMessage(const char *msg) {
#ifdef HAVE_CRASHREPORTERCLIENT_H
  (void)CRSetCrashLogMessage(msg);
#elif HAVE_CRASHREPORTER_INFO
  __crashreporter_info__ = msg;
#endif
  // Don't reorder subsequent operations: whatever comes after might crash and
  // we want the system crash handling to see the message we just set.
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
}

#ifdef __APPLE__
using CrashHandlerString = SmallString<2048>;
typedef struct {
  alignas(alignof(CrashHandlerString)) char buf[sizeof(CrashHandlerString)];
} CrashHandlerStringStorage;
inline static CrashHandlerStringStorage crashHandlerStringStorage;
#endif

/// This callback is run if a fatal signal is delivered to the process, it
/// prints the pretty stack trace.
inline static void CrashHandler(void *) {
  errs() << BugReportMsg;

#ifndef __APPLE__
  // On non-apple systems, just emit the crash stack trace to stderr.
  PrintCurStackTrace(errs());
#else
  // Emit the crash stack trace to a SmallString, put it where the system crash
  // handling will find it, and also send it to stderr.
  //
  // The SmallString is fairly large in the hope that we don't allocate (we're
  // handling a fatal signal, something is already pretty wrong, allocation
  // might not work). Further, we don't use a magic static in case that's also
  // borked. We leak any allocation that does occur because the program is about
  // to die anyways. This is technically racy if we were handling two fatal
  // signals, however if we're in that situation a race is the least of our
  // worries.
  auto &crashHandlerString =
      *new (&crashHandlerStringStorage) CrashHandlerString;

  // If we crash while trying to print the stack trace, we still want the system
  // crash handling to have some partial information. That'll work out as long
  // as the SmallString doesn't allocate. If it does allocate then the system
  // crash handling will see some garbage because the inline buffer now contains
  // a pointer.
  setCrashLogMessage(crashHandlerString.c_str());

  {
    raw_svector_ostream Stream(crashHandlerString);
    PrintCurStackTrace(Stream);
  }

  if (!crashHandlerString.empty()) {
    setCrashLogMessage(crashHandlerString.c_str());
    errs() << crashHandlerString.str();
  } else
    setCrashLogMessage("No crash information.");
#endif
}

inline static void printForSigInfoIfNeeded() {
  unsigned CurrentSigInfoGeneration =
      __atomic_load_n(&GlobalSigInfoGenerationCounter, __ATOMIC_RELAXED);
  if (ThreadLocalSigInfoGenerationCounter == 0 ||
      ThreadLocalSigInfoGenerationCounter == CurrentSigInfoGeneration) {
    return;
  }

  PrintCurStackTrace(errs());
  ThreadLocalSigInfoGenerationCounter = CurrentSigInfoGeneration;
}

#endif // ENABLE_BACKTRACES

inline void setBugReportMsg(const char *Msg) { BugReportMsg = Msg; }

inline const char *getBugReportMsg() { return BugReportMsg; }

inline PrettyStackTraceEntry::PrettyStackTraceEntry() {
#if ENABLE_BACKTRACES
  // Handle SIGINFO first, because we haven't finished constructing yet.
  printForSigInfoIfNeeded();
  // Link ourselves.
  NextEntry = PrettyStackTraceHead;
  PrettyStackTraceHead = this;
#endif
}

inline PrettyStackTraceEntry::~PrettyStackTraceEntry() {
#if ENABLE_BACKTRACES
  assert(PrettyStackTraceHead == this &&
         "Pretty stack trace entry destruction is out of order");
  PrettyStackTraceHead = NextEntry;
  // Handle SIGINFO first, because we already started destructing.
  printForSigInfoIfNeeded();
#endif
}

inline void PrettyStackTraceString::print(raw_ostream &OS) const {
  OS << Str << "\n";
}

inline PrettyStackTraceFormat::PrettyStackTraceFormat(const char *Format, ...) {
  va_list AP;
  va_start(AP, Format);
  const int SizeOrError = vsnprintf(0, 0, Format, AP);
  va_end(AP);
  if (SizeOrError < 0) {
    return;
  }

  const int Size = SizeOrError + 1; // '\0'
  Str.resize(Size);
  va_start(AP, Format);
  vsnprintf(Str.data(), Size, Format, AP);
  va_end(AP);
}

inline void PrettyStackTraceFormat::print(raw_ostream &OS) const {
  OS << Str << "\n";
}

inline void PrettyStackTraceProgram::print(raw_ostream &OS) const {
  OS << "Program arguments: ";
  // Print the argument list.
  for (int I = 0; I < ArgC; ++I) {
    const bool HaveSpace = ::strchr(ArgV[I], ' ');
    if (I)
      OS << ' ';
    if (HaveSpace)
      OS << '"';
    OS.write_escaped(ArgV[I]);
    if (HaveSpace)
      OS << '"';
  }
  OS << '\n';
}

#if ENABLE_BACKTRACES
inline static bool RegisterCrashPrinter() {
  sys::AddSignalHandler(CrashHandler, 0);
  return false;
}
#endif

inline void EnablePrettyStackTrace() {
#if ENABLE_BACKTRACES
  // The first time this is called, we register the crash printer.
  static bool HandlerRegistered = RegisterCrashPrinter();
  (void)HandlerRegistered;
#endif
}

inline void EnablePrettyStackTraceOnSigInfoForThisThread(bool ShouldEnable) {
#if ENABLE_BACKTRACES
  if (!ShouldEnable) {
    ThreadLocalSigInfoGenerationCounter = 0;
    return;
  }

  // The first time this is called, we register the SIGINFO handler.
  static bool HandlerRegistered = [] {
    sys::SetInfoSignalFunction([] {
      __atomic_fetch_add(&GlobalSigInfoGenerationCounter, 1, __ATOMIC_RELAXED);
    });
    return false;
  }();
  (void)HandlerRegistered;

  // Next, enable it for the current thread.
  ThreadLocalSigInfoGenerationCounter =
      __atomic_load_n(&GlobalSigInfoGenerationCounter, __ATOMIC_RELAXED);
#endif
}

inline const void *SavePrettyStackState() {
#if ENABLE_BACKTRACES
  return PrettyStackTraceHead;
#else
  return 0;
#endif
}

inline void RestorePrettyStackState(const void *Top) {
#if ENABLE_BACKTRACES
  PrettyStackTraceHead =
      static_cast<PrettyStackTraceEntry *>(const_cast<void *>(Top));
#endif
}

inline void LLVMEnablePrettyStackTrace() { EnablePrettyStackTrace(); }

} // namespace llvm

#endif
