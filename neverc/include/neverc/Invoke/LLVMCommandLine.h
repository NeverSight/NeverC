#ifndef NEVERC_INVOKE_LLVMCOMMANDLINE_H
#define NEVERC_INVOKE_LLVMCOMMANDLINE_H

#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

namespace neverc {

/// Parse LLVM cl::opt flags in a process-global, thread-safe way.
///
/// LLVM's ParseCommandLineOptions re-registers default options (including the
/// short-name help alias "h") on every call. Concurrent calls from parallel
/// in-process frontend jobs race on that registration and abort. Serial calls
/// are safe only when preceded by ResetAllOptionOccurrences, which removes the
/// default options before they are re-added.
inline void parseLLVMCommandLineOptions(int argc, const char *const *argv) {
  if (plugin::pluginLLVMOptionGateHeldSharedByCurrentThread() &&
      !plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread())
    llvm::report_fatal_error(
        "cannot mutate LLVM options under a shared option lease");
  if (plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread()) {
    llvm::cl::ResetAllOptionOccurrences();
    llvm::cl::ParseCommandLineOptions(argc, argv);
    return;
  }
  plugin::PluginLLVMOptionExclusiveLease Lock(plugin::pluginLLVMOptionGate());
  llvm::cl::ResetAllOptionOccurrences();
  llvm::cl::ParseCommandLineOptions(argc, argv);
}

} // namespace neverc

#endif // NEVERC_INVOKE_LLVMCOMMANDLINE_H
