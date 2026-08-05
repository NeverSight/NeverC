//===--- ArgumentHandlers.cpp - Early driver argument handling ------------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ArgumentHandlers.h"

#include "neverc/Linker/COFF/TestSign.h"
#include "neverc/Plugin/Host/PluginCapabilityInventory.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>

using llvm::ArrayRef;
using llvm::StringRef;

namespace neverc {
namespace driver {
namespace {

void handlePrintArguments(ArrayRef<const char *> Args, llvm::raw_ostream &Out) {
  for (const char *Arg : Args) {
    if (Arg == nullptr || StringRef(Arg) != "-fprint-arguments")
      continue;

    Out << "compiler arguments:\n";
    for (const char *Argument : Args) {
      if (Argument != nullptr)
        Out << "\"" << Argument << "\",\n";
    }
    return;
  }
}

// Handle the read-only `--print-plugin-capabilities[=json]` query. It dumps the
// host's compiled-in plugin capability inventory as JSON and returns an exit
// code; it loads no plugins and builds no compilation. Returns std::nullopt if
// the query was not requested so normal driver processing continues.
std::optional<int> handlePluginCapabilityQuery(ArrayRef<const char *> Args,
                                               llvm::raw_ostream &Out,
                                               llvm::raw_ostream &Err) {
  for (const char *Arg : Args) {
    if (Arg == nullptr)
      continue;
    StringRef A(Arg);
    if (A == "--print-plugin-capabilities" ||
        A == "--print-plugin-capabilities=json") {
      neverc::plugin::emitCapabilityInventoryJSON(Out);
      Out.flush();
      return 0;
    }
    if (A.starts_with("--print-plugin-capabilities=")) {
      Err << "neverc: error: unsupported --print-plugin-capabilities format '"
          << A.substr(StringRef("--print-plugin-capabilities=").size())
          << "'; only 'json' is supported\n";
      Err.flush();
      return 1;
    }
  }
  return std::nullopt;
}

// Handle the read-only `--print-test-sign-cert` query: write the DER
// certificate that `-ftest-sign` signs with to stdout.
//
// The certificate is emitted from the compiler rather than shipped as a file
// because it is already compiled in, and because that makes it impossible for
// the two to drift: a rebuilt signing identity changes what images are signed
// with and what this prints in the same step. A stale .cer next to a newer
// compiler would install a certificate that trusts nothing the compiler
// produces, which is a miserable thing to debug.
std::optional<int> handleTestSignCertQuery(ArrayRef<const char *> Args,
                                           llvm::raw_ostream &Out,
                                           llvm::raw_ostream &Err,
                                           bool StandardOutputIsDisplayed) {
  for (const char *Arg : Args) {
    if (Arg == nullptr || StringRef(Arg) != "--print-test-sign-cert")
      continue;
    if (StandardOutputIsDisplayed) {
      Err << "neverc: error: --print-test-sign-cert writes binary DER; "
             "redirect it to a file, e.g. neverc "
             "--print-test-sign-cert > neverc-test-signing.cer\n";
      Err.flush();
      return 1;
    }
    Out.write(
        reinterpret_cast<const char *>(linker::coff::testsign::CertificateDer),
        linker::coff::testsign::CertificateDerSize);
    Out.flush();
    return 0;
  }
  return std::nullopt;
}

bool scanCanonicalPrefixes(ArrayRef<const char *> Args) {
  bool CanonicalPrefixes = true;
  for (std::size_t I = 1; I < Args.size(); ++I) {
    if (Args[I] == nullptr)
      continue;
    if (StringRef(Args[I]) == "-canonical-prefixes")
      CanonicalPrefixes = true;
    else if (StringRef(Args[I]) == "-no-canonical-prefixes")
      CanonicalPrefixes = false;
  }
  return CanonicalPrefixes;
}

} // namespace

EarlyArgumentResult
processEarlyDriverArguments(ArrayRef<const char *> Args, llvm::raw_ostream &Out,
                            llvm::raw_ostream &Err,
                            bool StandardOutputIsDisplayed) {
  handlePrintArguments(Args, Out);
  if (std::optional<int> ExitCode = handlePluginCapabilityQuery(Args, Out, Err))
    return {ExitCode, true};
  if (std::optional<int> ExitCode =
          handleTestSignCertQuery(Args, Out, Err, StandardOutputIsDisplayed))
    return {ExitCode, true};
  return {std::nullopt, scanCanonicalPrefixes(Args)};
}

} // namespace driver
} // namespace neverc
