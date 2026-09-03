/*===- support_cpp.cpp - Irreducible C++ residual (188 lines) -------*- C++
-*-===*\
|* *|
|* Contains ONLY C++ that cannot be made inline or converted to C: *|
|*   1. ManagedStatic<cl::opt<...>> singletons (C++ template + RAII) *|
|*   2. Platform .inc includes (Program/Signals/Path — use LLVM C++ APIs) *|
|*   3. formatv helpers (Twine↔FormatVariadic circular include dependency) *|
|*   4. cl::initCommonOptions (calls C++ init functions) *|
|* *|
\*===-----------------------------------------------------------------------===*/
#ifdef __cplusplus

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/NevercPipelineTuning.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>

#include <sys/stat.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif

using namespace llvm;

namespace llvm {

void report_fatal_error(const Twine &Reason, bool GenCrashDiag) {
  llvm::fatal_error_handler_t Handler = nullptr;
  void *HandlerData = nullptr;
  if (ScopedThreadLocalFatalErrorHandler::currentFrame()) {
    Handler = ScopedThreadLocalFatalErrorHandler::currentFrame()->Handler;
    HandlerData =
        ScopedThreadLocalFatalErrorHandler::currentFrame()->UserData;
  } else {
#if LLVM_ENABLE_THREADS == 1
    LLVM_MUTEX_LOCK(&ErrorHandlerMutex);
#endif
    Handler = ErrorHandler;
    HandlerData = ErrorHandlerUserData;
#if LLVM_ENABLE_THREADS == 1
    LLVM_MUTEX_UNLOCK(&ErrorHandlerMutex);
#endif
  }

  if (Handler) {
    // A recovery handler may transfer control with setjmp/longjmp, bypassing
    // this frame's automatic destructors. Give the rendered message an
    // explicit CRC owner for that path; if the handler returns normally, first
    // unregister that owner and then release the message after the callback
    // has finished consuming its C string.
    auto Message = std::make_unique<std::string>(Reason.str());
    CrashRecoveryContextCleanupRegistrar<std::string> MessageCleanup(
        Message.get());
    Handler(HandlerData, Message->c_str(), GenCrashDiag);
    MessageCleanup.unregister();
    Message.reset();
  } else {
    const char Prefix[] = "LLVM ERROR: ";
    (void)!::write(2, Prefix, sizeof(Prefix) - 1);
    std::string Message = Reason.str();
    (void)!::write(2, Message.data(), Message.size());
    (void)!::write(2, "\n", 1);
  }

  sys::RunInterruptHandlers();

  if (GenCrashDiag)
    abort();
  else
    exit(1);
}

namespace cl {

template class basic_parser<bool>;
template class basic_parser<boolOrDefault>;
template class basic_parser<int>;
template class basic_parser<long>;
template class basic_parser<long long>;
template class basic_parser<unsigned>;
template class basic_parser<unsigned long>;
template class basic_parser<unsigned long long>;
template class basic_parser<double>;
template class basic_parser<float>;
template class basic_parser<string_t>;
template class basic_parser<char>;

template class opt<unsigned>;
template class opt<int>;
template class opt<string_t>;
template class opt<char>;
template class opt<bool>;

// These are key virtual functions: keep their definitions in one translation
// unit so every client uses the same vtable and RTTI identity.  Defining them
// inline in the public header leaves one vtable in every translation unit and
// breaks UBSan's polymorphic lifetime checks.
void GenericOptionValue::anchor() {}
void OptionValue<boolOrDefault>::anchor() {}
void OptionValue<string_t>::anchor() {}
void Option::anchor() {}
void basic_parser_impl::anchor() {}
void parser<bool>::anchor() {}
void parser<boolOrDefault>::anchor() {}
void parser<int>::anchor() {}
void parser<long>::anchor() {}
void parser<long long>::anchor() {}
void parser<unsigned>::anchor() {}
void parser<unsigned long>::anchor() {}
void parser<unsigned long long>::anchor() {}
void parser<double>::anchor() {}
void parser<float>::anchor() {}
void parser<string_t>::anchor() {}
void parser<char>::anchor() {}

} // namespace cl

namespace {
// Error.h must only forward-declare dbgs() to avoid a Support include cycle,
// while Debug.h provides its implementation inline.  Make LLVMSupport itself
// emit one weak copy so clients that instantiate Error/Expected diagnostics do
// not depend on some unrelated higher-level LLVM library doing so by accident.
LLVM_ATTRIBUTE_USED raw_ostream &(*const ForceDbgsEmission)() = &dbgs;

cl::OptionCategory &colorCategorySingleton() {
  static cl::OptionCategory ColorCategory("Color Options");
  return ColorCategory;
}

struct CreateUseColorTy {
  static void *call() {
    return new cl::opt<cl::boolOrDefault>(
        "color", cl::cat(colorCategorySingleton()),
        cl::desc("Use colors in output (default=autodetect)"),
        cl::init(cl::BOU_UNSET));
  }
};
ManagedStatic<cl::opt<cl::boolOrDefault>, CreateUseColorTy> UseColorOpt;
} // namespace

cl::OptionCategory &getColorCategory() { return colorCategorySingleton(); }

void initWithColorOptions() { (void)*UseColorOpt; }

namespace detail {
bool withColorDefaultAutoDetect(const raw_ostream &OS) {
  return *UseColorOpt == cl::BOU_UNSET ? OS.has_colors()
                                       : *UseColorOpt == cl::BOU_TRUE;
}
} // namespace detail
} // namespace llvm

/* --- Program platform includes --- */
#ifdef LLVM_ON_UNIX
#include "Unix/Program.inc"
#endif
#ifdef _WIN32
#include "Windows/Program.inc"
#include "Windows/ProcessCmdline.inc"
#endif

/* --- Signal handler platform includes --- */
#ifdef LLVM_ON_UNIX
#include "Unix/Signals.inc"
#endif
#ifdef _WIN32
#include "Windows/Signals.inc"
#endif

#include "csupport/lpath.h"

#if defined(LLVM_ON_UNIX)
#include "Unix/Path.inc"
#include "llvm/Support/Signals.h"
#endif
#if defined(_WIN32)
#include "Windows/Path.inc"
#endif

namespace llvm {
namespace {
struct CreateDisableSymbolication {
  static void *call() {
    return new cl::opt<bool, true>(
        "disable-symbolication",
        cl::desc("Disable symbolizing crash backtraces."),
        cl::location(DisableSymbolicationFlag), cl::Hidden);
  }
};
struct CreateCrashDiagnosticsDir {
  static void *call() {
    return new cl::opt<SmallString<256>, true>(
        "crash-diagnostics-dir", cl::value_desc("directory"),
        cl::desc("Directory for crash diagnostic files."),
        cl::location(*CrashDiagnosticsDirectory), cl::Hidden);
  }
};
} // namespace

void initSignalsOptions() {
  static ManagedStatic<cl::opt<bool, true>, CreateDisableSymbolication>
      DisableSymbolication;
  static ManagedStatic<cl::opt<SmallString<256>, true>,
                       CreateCrashDiagnosticsDir>
      CrashDiagnosticsDir;
  *DisableSymbolication;
  *CrashDiagnosticsDir;
}

} // namespace llvm

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/TypeSize.h"

#ifndef STRICT_FIXED_SIZE_VECTORS
namespace llvm {
namespace {
struct CreateScalableErrorAsWarningTy {
  static void *call() {
    return new cl::opt<bool>(
        "treat-scalable-fixed-error-as-warning", cl::Hidden,
        cl::desc(
            "Treat issues where a fixed-width property is requested from a "
            "scalable type as a warning, instead of an error"));
  }
};
ManagedStatic<cl::opt<bool>, CreateScalableErrorAsWarningTy>
    ScalableErrorAsWarningOpt;
} // namespace

namespace detail {
bool scalableFixedErrorAsWarningEnabled() { return *ScalableErrorAsWarningOpt; }
} // namespace detail

void initTypeSizeOptions() { (void)*ScalableErrorAsWarningOpt; }
} // namespace llvm
#endif

#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/WithColor.h"

namespace llvm {

void Twine::toVector(SmallVectorImpl<char> &Out) const {
  raw_svector_ostream OS(Out);
  print(OS);
}

std::string twine_str_from_formatv(const formatv_object_base *O) {
  return O->str();
}
void twine_print_formatv_to_stream(raw_ostream &OS,
                                   const formatv_object_base *O) {
  O->format(OS);
}
void twine_print_formatv_repr_to_stream(raw_ostream &OS,
                                        const formatv_object_base *O) {
  OS << "formatv:\"";
  O->format(OS);
  OS << '"';
}

namespace cl {
void initCommonOptions() {
  llvm::detail::anchorNevercPipelineTuningOptions();
  *CommonOptions;
  initDebugCounterOptions();
  initGraphWriterOptions();
  initSignalsOptions();
  initStatisticOptions();
  initTimerOptions();
  initTypeSizeOptions();
  initWithColorOptions();
  initDebugOptions();
  initRandomSeedOptions();
}
} // namespace cl
} // namespace llvm

#endif /* __cplusplus */
