//===- llvm/Support/Debug.h - Easy way to add debug output ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a handy way of adding debugging information to your
// code, without it being enabled all of the time, and without having to add
// command line options to enable it.
//
// In particular, just wrap your code with the LLVM_DEBUG() macro, and it will
// be enabled automatically if you specify '-debug' on the command-line.
// LLVM_DEBUG() requires the DEBUG_TYPE macro to be defined. Set it to "foo"
// specify that your debug code belongs to class "foo". Be careful that you only
// do this after including Debug.h and not around any #include of headers.
// Headers should define and undef the macro acround the code that needs to use
// the LLVM_DEBUG() macro. Then, on the command line, you can specify
// '-debug-only=foo' to enable JUST the debug information for the foo class.
//
// When compiling without assertions, the -debug-* options and all code in
// LLVM_DEBUG() statements disappears, so it does not affect the runtime of the
// code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DEBUG_H
#define LLVM_SUPPORT_DEBUG_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/circular_raw_ostream.h"

#ifndef NDEBUG
#include "llvm/Support/CommandLine.h"
#endif

namespace llvm {

class raw_ostream;

#ifndef NDEBUG

/// isCurrentDebugType - Return true if the specified string is the debug type
/// specified on the command line, or if none was specified on the command line
/// with the -debug-only=X option.
///
bool isCurrentDebugType(const char *Type);

/// setCurrentDebugType - Set the current debug type, as if the -debug-only=X
/// option were specified.  Note that DebugFlag also needs to be set to true for
/// debug output to be produced.
///
void setCurrentDebugType(const char *Type);

/// setCurrentDebugTypes - Set the current debug type, as if the
/// -debug-only=X,Y,Z option were specified. Note that DebugFlag
/// also needs to be set to true for debug output to be produced.
///
void setCurrentDebugTypes(const char **Types, unsigned Count);

/// DEBUG_WITH_TYPE macro - This macro should be used by passes to emit debug
/// information.  If the '-debug' option is specified on the commandline, and if
/// this is a debug build, then the code specified as the option to the macro
/// will be executed.  Otherwise it will not be.  Example:
///
/// DEBUG_WITH_TYPE("bitset", dbgs() << "Bitset contains: " << Bitset << "\n");
///
/// This will emit the debug information if -debug is present, and -debug-only
/// is not specified, or is specified as "bitset".
#define DEBUG_WITH_TYPE(TYPE, X)                                               \
  do {                                                                         \
    if (::llvm::DebugFlag && ::llvm::isCurrentDebugType(TYPE)) {               \
      X;                                                                       \
    }                                                                          \
  } while (false)

#else
#define isCurrentDebugType(X) (false)
#define setCurrentDebugType(X)                                                 \
  do {                                                                         \
    (void)(X);                                                                 \
  } while (false)
#define setCurrentDebugTypes(X, N)                                             \
  do {                                                                         \
    (void)(X);                                                                 \
    (void)(N);                                                                 \
  } while (false)
#define DEBUG_WITH_TYPE(TYPE, X)                                               \
  do {                                                                         \
  } while (false)
#endif

/// This boolean is set to true if the '-debug' command line option
/// is specified.  This should probably not be referenced directly, instead, use
/// the DEBUG macro below.
///
extern bool DebugFlag;

/// EnableDebugBuffering - This defaults to false.  If true, the debug
/// stream will install signal handlers to dump any buffered debug
/// output.  It allows clients to selectively allow the debug stream
/// to install signal handlers if they are certain there will be no
/// conflict.
///
extern bool EnableDebugBuffering;

/// dbgs() - This returns a reference to a raw_ostream for debugging
/// messages.  If debugging is disabled it returns errs().  Use it
/// like: dbgs() << "foo" << "bar";
::llvm::raw_ostream &dbgs();

// DEBUG macro - This macro should be used by passes to emit debug information.
// If the '-debug' option is specified on the commandline, and if this is a
// debug build, then the code specified as the option to the macro will be
// executed.  Otherwise it will not be.  Example:
//
// LLVM_DEBUG(dbgs() << "Bitset contains: " << Bitset << "\n");
//
#define LLVM_DEBUG(X) DEBUG_WITH_TYPE(DEBUG_TYPE, X)

inline bool DebugFlag = false;
inline bool EnableDebugBuffering = false;

namespace detail {
inline ManagedStatic<SmallVector<SmallString<32>, 4>> &getCurrentDebugType() {
  static ManagedStatic<SmallVector<SmallString<32>, 4>> Inst;
  return Inst;
}
} // namespace detail

#ifndef NDEBUG
#undef isCurrentDebugType
#undef setCurrentDebugType
#undef setCurrentDebugTypes
inline bool isCurrentDebugType(const char *DebugType) {
  auto &CurrentDebugType = detail::getCurrentDebugType();
  if (CurrentDebugType->empty())
    return true;
  for (auto &d : *CurrentDebugType) {
    if (d == DebugType)
      return true;
  }
  return false;
}
inline void setCurrentDebugTypes(const char **Types, unsigned Count) {
  auto &CurrentDebugType = detail::getCurrentDebugType();
  CurrentDebugType->clear();
  for (size_t T = 0; T < Count; ++T)
    CurrentDebugType->push_back(SmallString<32>(Types[T]));
}
inline void setCurrentDebugType(const char *Type) {
  setCurrentDebugTypes(&Type, 1);
}

namespace detail {
struct CreateDebug {
  static void *call() {
    return new cl::opt<bool, true>("debug", cl::desc("Enable debug output"),
                                   cl::Hidden, cl::location(DebugFlag));
  }
};
struct CreateDebugBufferSize {
  static void *call() {
    return new cl::opt<unsigned>(
        "debug-buffer-size",
        cl::desc("Buffer the last N characters of debug output until program "
                 "termination. [default 0 -- immediate print-out]"),
        cl::Hidden, cl::init(0));
  }
};
struct DebugOnlyOpt {
  void operator=(StringRef Val) const {
    if (Val.empty())
      return;
    DebugFlag = true;
    SmallVector<StringRef, 8> dbgTypes;
    StringRef(Val).split(dbgTypes, ',', -1, false);
    auto &CurrentDebugType = getCurrentDebugType();
    for (auto dbgType : dbgTypes)
      CurrentDebugType->push_back(SmallString<32>(dbgType));
  }
};
struct CreateDebugOnly {
  static void *call() {
    static DebugOnlyOpt DebugOnlyOptLoc;
    return new cl::opt<DebugOnlyOpt, true, cl::parser<std::string>>(
        "debug-only",
        cl::desc("Enable a specific type of debug output (comma separated list "
                 "of types)"),
        cl::Hidden, cl::value_desc("debug string"),
        cl::location(DebugOnlyOptLoc), cl::ValueRequired);
  }
};
inline void getDebugStatics(
    ManagedStatic<cl::opt<bool, true>, CreateDebug> *&Debug,
    ManagedStatic<cl::opt<unsigned>, CreateDebugBufferSize> *&DebugBufferSize,
    ManagedStatic<cl::opt<DebugOnlyOpt, true, cl::parser<std::string>>,
                  CreateDebugOnly> *&DebugOnly) {
  static ManagedStatic<cl::opt<bool, true>, CreateDebug> D;
  static ManagedStatic<cl::opt<unsigned>, CreateDebugBufferSize> DBS;
  static ManagedStatic<cl::opt<DebugOnlyOpt, true, cl::parser<std::string>>,
                       CreateDebugOnly>
      DO;
  Debug = &D;
  DebugBufferSize = &DBS;
  DebugOnly = &DO;
}
} // namespace detail

inline void initDebugOptions() {
  ManagedStatic<cl::opt<bool, true>, detail::CreateDebug> *Debug;
  ManagedStatic<cl::opt<unsigned>, detail::CreateDebugBufferSize>
      *DebugBufferSize;
  ManagedStatic<cl::opt<detail::DebugOnlyOpt, true, cl::parser<std::string>>,
                detail::CreateDebugOnly> *DebugOnly;
  detail::getDebugStatics(Debug, DebugBufferSize, DebugOnly);
  **Debug;
  **DebugBufferSize;
  **DebugOnly;
}

namespace detail {
inline void debug_user_sig_handler(void *Cookie) {
  circular_raw_ostream &dbgout = (circular_raw_ostream &)(llvm::dbgs());
  dbgout.flushBufferWithBanner();
}
} // namespace detail

inline ::llvm::raw_ostream &dbgs() {
  ManagedStatic<cl::opt<bool, true>, detail::CreateDebug> *Debug;
  ManagedStatic<cl::opt<unsigned>, detail::CreateDebugBufferSize>
      *DebugBufferSize;
  ManagedStatic<cl::opt<detail::DebugOnlyOpt, true, cl::parser<std::string>>,
                detail::CreateDebugOnly> *DebugOnly;
  detail::getDebugStatics(Debug, DebugBufferSize, DebugOnly);
  static struct dbgstream {
    circular_raw_ostream strm;
    dbgstream(
        ManagedStatic<cl::opt<unsigned>, detail::CreateDebugBufferSize> *DBS)
        : strm(errs(), "*** Debug Log Output ***\n",
               (!EnableDebugBuffering || !DebugFlag) ? 0 : **DBS) {
      if (EnableDebugBuffering && DebugFlag && **DBS != 0)
        sys::AddSignalHandler(&detail::debug_user_sig_handler, 0);
    }
  } thestrm(DebugBufferSize);
  return thestrm.strm;
}
#else
inline ::llvm::raw_ostream &dbgs() { return errs(); }
inline void initDebugOptions() {}
#endif

} // end namespace llvm

#endif // LLVM_SUPPORT_DEBUG_H
