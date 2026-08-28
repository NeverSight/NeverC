// A deliberately tiny opt-like client.  It links only LLVMSupport and does
// not reference NeverC's capture API, so a static archive must pull the
// option-registration translation unit through the command-line entry point.

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iterator>
#include <string>

int main() {
  const char *Argv[] = {
      "neverc-pipeline-option-registration-smoke",
      "-neverc-module-inliner-threshold=701",
      "-neverc-auto-lto-inline-threshold=713",
      "-neverc-inliner-lite-fsimpl=0",
      "-neverc-inline-max-caller-loops=727",
      "-neverc-full-unroll-max-loops-per-function=733",
  };
  std::string Diagnostics;
  llvm::raw_string_ostream Errors(Diagnostics);
  if (!llvm::cl::ParseCommandLineOptions(static_cast<int>(std::size(Argv)),
                                         Argv, "", &Errors)) {
    llvm::errs() << Errors.str();
    return 1;
  }

  auto &Options = llvm::cl::getRegisteredOptions();
  const char *Spellings[] = {
      "neverc-module-inliner-threshold",
      "neverc-auto-lto-inline-threshold",
      "neverc-inliner-lite-fsimpl",
      "neverc-inline-max-caller-loops",
      "neverc-full-unroll-max-loops-per-function",
  };
  for (const char *Spelling : Spellings) {
    auto It = Options.find(Spelling);
    if (It == Options.end() || It->second->getNumOccurrences() != 1)
      return 2;
  }
  return 0;
}
