//===----------------------------------------------------------------------===//
//
//  Diagnostic — the `ErrorHandler` class and the free-function facade
//  (`error`, `warn`, `log`, `message`, `fatal`, `exitLinker`, `checkError`,
//  `check`) that every linker TU uses to emit user-facing output.
//
//  The free functions dispatch to the `ErrorHandler` owned by the active
//  `CommonLinkerContext`; there is no other instance in the process.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_RUNTIME_DIAGNOSTIC_H
#define LINKER_CORE_RUNTIME_DIAGNOSTIC_H

#include "Linker/Core/Support/LlvmAliases.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include <mutex>

namespace llvm {
class DiagnosticInfo;
class raw_ostream;
} // namespace llvm

namespace linker {

llvm::raw_ostream &outs();
llvm::raw_ostream &errs();

enum class ErrorTag { LibNotFound, SymbolNotFound };

class ErrorHandler {
public:
  ~ErrorHandler();

  void initialize(llvm::raw_ostream &stdoutOS, llvm::raw_ostream &stderrOS,
                  bool exitEarly, bool disableOutput);

  uint64_t errorCount = 0;
  uint64_t errorLimit = 20;
  StringRef errorLimitExceededMsg = "too many errors emitted, stopping now";
  StringRef errorHandlingScript;
  StringRef logName = "neverc-linker";
  bool exitEarly = true;
  bool fatalWarnings = false;
  bool suppressWarnings = false;
  bool verbose = false;
  bool disableOutput = false;
  std::function<void()> cleanupCallback;

  void error(const Twine &msg);
  void error(const Twine &msg, ErrorTag tag, ArrayRef<StringRef> args);
  [[noreturn]] void fatal(const Twine &msg);
  void log(const Twine &msg);
  void message(const Twine &msg, llvm::raw_ostream &s);
  void warn(const Twine &msg);

  raw_ostream &outs();
  raw_ostream &errs();
  void flushStreams();

  std::unique_ptr<llvm::FileOutputBuffer> outputBuffer;

private:
  using Colors = raw_ostream::Colors;

  std::string getLocation(const Twine &msg);
  void reportDiagnostic(StringRef location, Colors c, StringRef diagKind,
                        const Twine &msg);

  // "\n" if the previous diagnostic was multi-line, "" otherwise.  Used to
  // visually separate two multi-line messages with a blank line.
  llvm::StringRef sep;

  // `linker::outs()` / `linker::errs()` are reachable from multiple threads
  // through the free-function facade, so we protect the underlying streams
  // with a mutex.  In the future, when several concurrent linker contexts
  // become supported, this mutex will be naturally scoped to this context.
  std::mutex mu;
  llvm::raw_ostream *stdoutOS{};
  llvm::raw_ostream *stderrOS{};
};

/// Returns the `ErrorHandler` attached to the active `CommonLinkerContext`.
ErrorHandler &errorHandler();

void error(const Twine &msg);
void error(const Twine &msg, ErrorTag tag, ArrayRef<StringRef> args);
[[noreturn]] void fatal(const Twine &msg);
void log(const Twine &msg);
void message(const Twine &msg, llvm::raw_ostream &s = outs());
void warn(const Twine &msg);
uint64_t errorCount();

[[noreturn]] void exitLinker(int val);

void diagnosticHandler(const llvm::DiagnosticInfo &di);
void checkError(Error e);

// `check*` helpers strip the error carrier from an `ErrorOr`/`Expected`.
// They call `fatal()` on the failure branch.
template <class T> T check(ErrorOr<T> e) {
  if (auto ec = e.getError())
    fatal(ec.message());
  return std::move(*e);
}

template <class T> T check(Expected<T> e) {
  if (!e)
    fatal(llvm::toString(e.takeError()));
  return std::move(*e);
}

// Overload for reference-returning Expecteds (never move out of a reference).
template <class T> T &check(Expected<T &> e) {
  if (!e)
    fatal(llvm::toString(e.takeError()));
  return *e;
}

template <class T>
T check2(ErrorOr<T> e, llvm::function_ref<std::string()> prefix) {
  if (auto ec = e.getError())
    fatal(prefix() + ": " + ec.message());
  return std::move(*e);
}

template <class T>
T check2(Expected<T> e, llvm::function_ref<std::string()> prefix) {
  if (!e)
    fatal(prefix() + ": " + toString(e.takeError()));
  return std::move(*e);
}

inline std::string toString(const Twine &s) { return s.str(); }

// Macro wrapper so the `S` prefix argument is evaluated lazily — a call
// chain like `CHECK(expr, "prefix " + computeExpensiveString())` pays the
// string-building cost only when `expr` fails.
#define CHECK(E, S) check2((E), [&] { return toString(S); })

} // namespace linker

#endif // LINKER_CORE_RUNTIME_DIAGNOSTIC_H
