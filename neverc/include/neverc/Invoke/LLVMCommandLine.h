#ifndef NEVERC_INVOKE_LLVMCOMMANDLINE_H
#define NEVERC_INVOKE_LLVMCOMMANDLINE_H

#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <utility>

namespace neverc {

/// Parse LLVM cl::opt flags while holding LLVM's process-global option gate.
///
/// LLVM's ParseCommandLineOptions re-registers default options (including the
/// short-name help alias "h") on every call. Concurrent calls from parallel
/// in-process frontend jobs race on that registration and abort. Serial calls
/// are safe only when preceded by ResetAllOptionOccurrences, which removes the
/// default options before they are re-added. LLVM's help/version handlers exit
/// the host process even when an error stream is supplied, so reject those
/// process-level controls after recursively expanding response files. Option
/// validators must report ordinary parser errors: non-local fatal recovery
/// here would abandon LLVM's parser-owned response and argv scratch storage.
inline bool parseLLVMCommandLineOptions(int argc, const char *const *argv) {
  if (argc <= 0 || !argv || !argv[0]) {
    llvm::errs() << "neverc: invalid in-process LLVM option vector\n";
    return false;
  }
  if (plugin::pluginLLVMOptionGateHeldSharedByCurrentThread() &&
      !plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread())
    llvm::report_fatal_error(
        "cannot mutate LLVM options under a shared option lease");

  auto PrepareAndParse = [&]() -> bool {
    llvm::SmallVector<const char *, 20> Expanded(argv, argv + argc);
    llvm::BumpPtrAllocator Allocator;
    llvm::cl::TokenizerCallback Tokenize =
#ifdef _WIN32
        llvm::cl::TokenizeWindowsCommandLine;
#else
        llvm::cl::TokenizeGNUCommandLine;
#endif
    llvm::cl::ExpansionContext Expansion(Allocator, Tokenize);
    if (llvm::Error Error = Expansion.expandResponseFiles(Expanded)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return false;
    }

    llvm::cl::initCommonOptions();
    const auto &Registered = llvm::cl::getRegisteredOptions();
    for (std::size_t I = 1; I != Expanded.size(); ++I) {
      const char *RawArgument = Expanded[I];
      if (!RawArgument)
        continue;
      llvm::StringRef Name(RawArgument);
      // A missing response file is intentionally left unexpanded by LLVM.
      // Reject it here so the parser cannot re-read a file that appears or is
      // replaced between this preflight and its own expansion pass.
      if (Name.starts_with("@")) {
        llvm::errs() << argv[0]
                     << ": unresolved LLVM response files are not supported "
                        "through an in-process -mllvm invocation\n";
        return false;
      }
      if (!Name.consume_front("-"))
        continue;
      Name.consume_front("-");
      Name = Name.split('=').first;
      const bool HostExitOption =
          Name == "help" || Name == "help-hidden" || Name == "help-list" ||
          Name == "help-list-hidden" || Name == "h" || Name == "version";
      // `h` is an LLVM grouping option. An otherwise unknown group such as
      // `-xh` invokes the help callback before the parser can return an error.
      const bool CouldGroupHelp =
          Name.contains('h') && !Registered.contains(Name);
      if (!HostExitOption && !CouldGroupHelp)
        continue;
      llvm::errs() << (argc > 0 && argv[0] ? argv[0] : "neverc")
                   << ": LLVM help/version options are not supported through "
                      "an in-process -mllvm invocation\n";
      return false;
    }

    llvm::cl::ResetAllOptionOccurrences();
    return llvm::cl::ParseCommandLineOptions(
        static_cast<int>(Expanded.size()), Expanded.data(),
        /*Overview=*/"", &llvm::errs());
  };

  if (plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread()) {
    return PrepareAndParse();
  }
  plugin::PluginLLVMOptionExclusiveLease Lock(plugin::pluginLLVMOptionGate());
  return PrepareAndParse();
}

} // namespace neverc

#endif // NEVERC_INVOKE_LLVMCOMMANDLINE_H
