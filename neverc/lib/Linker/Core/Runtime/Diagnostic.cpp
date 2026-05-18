//===----------------------------------------------------------------------===//
//
//  Diagnostic — central `ErrorHandler` + the free-function facade
//  (`error`, `warn`, `log`, `message`, `fatal`).
//
//  One per `CommonLinkerContext`; backends never hold their own.  The
//  same object also drives the Visual-Studio-style `(file:line): error:`
//  reformat and the optional external `--error-handling-script`.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Runtime/Diagnostic.h"

#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace linker;

namespace {
StringRef getSeparator(const Twine &msg) {
  if (StringRef(msg.str()).contains('\n'))
    return "\n";
  return "";
}
} // namespace

linker::ErrorHandler::~ErrorHandler() {
  if (cleanupCallback)
    cleanupCallback();
}

void linker::ErrorHandler::initialize(llvm::raw_ostream &stdoutOS,
                                      llvm::raw_ostream &stderrOS,
                                      bool exitEarly, bool disableOutput) {
  this->stdoutOS = &stdoutOS;
  this->stderrOS = &stderrOS;
  stderrOS.enable_colors(stderrOS.has_colors());
  this->exitEarly = exitEarly;
  this->disableOutput = disableOutput;
}

void linker::ErrorHandler::flushStreams() {
  std::lock_guard<std::mutex> lock(mu);
  outs().flush();
  errs().flush();
}

linker::ErrorHandler &linker::errorHandler() { return context().e; }

void linker::error(const Twine &msg) { errorHandler().error(msg); }
void linker::error(const Twine &msg, ErrorTag tag, ArrayRef<StringRef> args) {
  errorHandler().error(msg, tag, args);
}
void linker::fatal(const Twine &msg) { errorHandler().fatal(msg); }
void linker::log(const Twine &msg) { errorHandler().log(msg); }
void linker::message(const Twine &msg, llvm::raw_ostream &s) {
  errorHandler().message(msg, s);
}
void linker::warn(const Twine &msg) { errorHandler().warn(msg); }
uint64_t linker::errorCount() { return errorHandler().errorCount; }

raw_ostream &linker::outs() { return errorHandler().outs(); }
raw_ostream &linker::errs() { return errorHandler().errs(); }

raw_ostream &linker::ErrorHandler::outs() {
  if (disableOutput)
    return llvm::nulls();
  return stdoutOS ? *stdoutOS : llvm::outs();
}

raw_ostream &linker::ErrorHandler::errs() {
  if (disableOutput)
    return llvm::nulls();
  return stderrOS ? *stderrOS : llvm::errs();
}

void linker::exitLinker(int val) {
  if (hasContext()) {
    linker::ErrorHandler &e = errorHandler();
    // Drop any temp file while keeping the mapping alive for callers.
    if (e.outputBuffer)
      e.outputBuffer->discard();
  }

  // Re-throw a possible signal or exception once/if it was caught by an
  // enclosing `CrashRecoveryContext` (the driver wraps execution in one).
  CrashRecoveryContext::throwIfCrash(val);

  // Tear down ManagedStatic variables before `_exit()`.  In an LTO build
  // that lets us flush `-time-passes`; it also stops the parallel thread
  // pool so Windows does not crash on exit.
  if (!CrashRecoveryContext::GetCurrent())
    llvm_shutdown();

  if (hasContext())
    linker::errorHandler().flushStreams();

  // When called inside a `CrashRecoveryContext`, control flow is restored
  // by `throwIfCrash` above.  Otherwise fall through to `_exit()` to avoid
  // any further destructor-driven crash on shutdown.
  llvm::sys::Process::Exit(val, /*NoCleanup=*/true);
}

void linker::diagnosticHandler(const DiagnosticInfo &di) {
  SmallString<128> s;
  raw_svector_ostream os(s);
  DiagnosticPrinterRawOStream dp(os);

  // For an inline-asm diagnostic, prepend the module name so the output
  // reads "$module <inline asm>:1:5: ".
  if (auto *dism = dyn_cast<DiagnosticInfoSrcMgr>(&di))
    if (dism->isInlineAsmDiag())
      os << dism->getModuleName() << ' ';

  di.print(dp);
  switch (di.getSeverity()) {
  case DS_Error:
    error(s);
    break;
  case DS_Warning:
    warn(s);
    break;
  case DS_Remark:
  case DS_Note:
    message(s);
    break;
  }
}

void linker::checkError(Error e) {
  handleAllErrors(std::move(e),
                  [&](ErrorInfoBase &eib) { error(eib.message()); });
}

std::string linker::ErrorHandler::getLocation(const Twine &msg) {
  return std::string(logName);
}

void linker::ErrorHandler::reportDiagnostic(StringRef location, Colors c,
                                            StringRef diagKind,
                                            const Twine &msg) {
  SmallString<256> buf;
  raw_svector_ostream os(buf);
  os << sep << location << ": ";
  if (!diagKind.empty()) {
    if (linker::errs().colors_enabled()) {
      os.enable_colors(true);
      os << c << diagKind << ": " << Colors::RESET;
    } else {
      os << diagKind << ": ";
    }
  }
  os << msg << '\n';
  linker::errs() << buf;
}

void linker::ErrorHandler::log(const Twine &msg) {
  if (!verbose || disableOutput)
    return;
  std::lock_guard<std::mutex> lock(mu);
  reportDiagnostic(logName, Colors::RESET, "", msg);
}

void linker::ErrorHandler::message(const Twine &msg, llvm::raw_ostream &s) {
  if (disableOutput)
    return;
  std::lock_guard<std::mutex> lock(mu);
  s << msg << "\n";
  s.flush();
}

void linker::ErrorHandler::warn(const Twine &msg) {
  if (fatalWarnings) {
    error(msg);
    return;
  }

  if (suppressWarnings)
    return;

  std::lock_guard<std::mutex> lock(mu);
  reportDiagnostic(getLocation(msg), Colors::MAGENTA, "warning", msg);
  sep = getSeparator(msg);
}

void linker::ErrorHandler::error(const Twine &msg) {
  bool exit = false;
  {
    std::lock_guard<std::mutex> lock(mu);

    if (errorLimit == 0 || errorCount < errorLimit) {
      reportDiagnostic(getLocation(msg), Colors::RED, "error", msg);
    } else if (errorCount == errorLimit) {
      reportDiagnostic(logName, Colors::RED, "error", errorLimitExceededMsg);
      exit = exitEarly;
    }

    sep = getSeparator(msg);
    ++errorCount;
  }

  if (exit)
    exitLinker(1);
}

void linker::ErrorHandler::error(const Twine &msg, ErrorTag tag,
                                 ArrayRef<StringRef> args) {
  if (errorHandlingScript.empty()) {
    error(msg);
    return;
  }
  SmallVector<StringRef, 4> scriptArgs;
  scriptArgs.push_back(errorHandlingScript);
  switch (tag) {
  case ErrorTag::LibNotFound:
    scriptArgs.push_back("missing-lib");
    break;
  case ErrorTag::SymbolNotFound:
    scriptArgs.push_back("undefined-symbol");
    break;
  }
  scriptArgs.insert(scriptArgs.end(), args.begin(), args.end());
  int res = llvm::sys::ExecuteAndWait(errorHandlingScript, scriptArgs);
  if (res == 0) {
    return error(msg);
  } else {
    // Temporarily disable the error limit so the pair of `error()` calls
    // below still counts as a single diagnostic.
    uint64_t currentErrorLimit = errorLimit;
    errorLimit = 0;
    error(msg);
    errorLimit = currentErrorLimit;
    --errorCount;

    switch (res) {
    case -1:
      error("error handling script '" + errorHandlingScript +
            "' failed to execute");
      break;
    case -2:
      error("error handling script '" + errorHandlingScript +
            "' crashed or timeout");
      break;
    default:
      error("error handling script '" + errorHandlingScript +
            "' exited with code " + Twine(res));
    }
  }
}

void linker::ErrorHandler::fatal(const Twine &msg) {
  error(msg);
  exitLinker(1);
}
