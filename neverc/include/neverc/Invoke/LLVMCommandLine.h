#ifndef NEVERC_INVOKE_LLVMCOMMANDLINE_H
#define NEVERC_INVOKE_LLVMCOMMANDLINE_H

#include "llvm/Support/CommandLine.h"
#include <mutex>

namespace neverc {

/// Parse LLVM cl::opt flags in a process-global, thread-safe way.
///
/// LLVM's ParseCommandLineOptions re-registers default options (including the
/// short-name help alias "h") on every call. Concurrent calls from parallel
/// in-process frontend jobs race on that registration and abort. Serial calls
/// are safe only when preceded by ResetAllOptionOccurrences, which removes the
/// default options before they are re-added.
inline void parseLLVMCommandLineOptions(int argc, const char *const *argv) {
  static std::mutex ParseMutex;
  std::lock_guard<std::mutex> Lock(ParseMutex);
  llvm::cl::ResetAllOptionOccurrences();
  llvm::cl::ParseCommandLineOptions(argc, argv);
}

} // namespace neverc

#endif // NEVERC_INVOKE_LLVMCOMMANDLINE_H
