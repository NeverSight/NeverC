//===- DiagnosticHandler.h - DiagnosticHandler class for LLVM -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"
#include <utility>

using namespace llvm;

namespace {

/// Regular expression corresponding to the value given in one of the
/// -pass-remarks* command line flags. Passes whose name matches this regexp
/// will emit a diagnostic when calling the associated diagnostic function
/// (emitOptimizationRemark, emitOptimizationRemarkMissed or
/// emitOptimizationRemarkAnalysis).
struct PassRemarksOpt {
  std::shared_ptr<Regex> Pattern;

  void operator=(const std::string &Val) {
    // Create a regexp object to match pass names for emitOptimizationRemark.
    if (!Val.empty()) {
      auto Candidate = std::make_shared<Regex>(Val);
      SmallString<256> RegexError;
      if (!Candidate->isValid(RegexError))
        return;
      Pattern = std::move(Candidate);
    }
  }
};

struct PassRemarksParser final : cl::parser<std::string> {
  explicit PassRemarksParser(cl::Option &O) : cl::parser<std::string>(O) {}

  bool parse(cl::Option &O, StringRef ArgName, StringRef Arg,
             std::string &Value) {
    if (cl::parser<std::string>::parse(O, ArgName, Arg, Value))
      return true;
    if (Value.empty())
      return false;
    Regex Candidate(Value);
    SmallString<256> RegexError;
    if (Candidate.isValid(RegexError))
      return false;
    return O.error((Twine("invalid regular expression '") + Arg + "': " +
                    RegexError)
                       .str(),
                   ArgName);
  }
};

static PassRemarksOpt PassRemarksPassedOptLoc;
static PassRemarksOpt PassRemarksMissedOptLoc;
static PassRemarksOpt PassRemarksAnalysisOptLoc;

// -pass-remarks
//    Command line flag to enable emitOptimizationRemark()
static cl::opt<PassRemarksOpt, true, PassRemarksParser> PassRemarks(
    "pass-remarks", cl::value_desc("pattern"),
    cl::desc("Enable optimization remarks from passes whose name match "
             "the given regular expression"),
    cl::Hidden, cl::location(PassRemarksPassedOptLoc), cl::ValueRequired);

// -pass-remarks-missed
//    Command line flag to enable emitOptimizationRemarkMissed()
static cl::opt<PassRemarksOpt, true, PassRemarksParser> PassRemarksMissed(
    "pass-remarks-missed", cl::value_desc("pattern"),
    cl::desc("Enable missed optimization remarks from passes whose name match "
             "the given regular expression"),
    cl::Hidden, cl::location(PassRemarksMissedOptLoc), cl::ValueRequired);

// -pass-remarks-analysis
//    Command line flag to enable emitOptimizationRemarkAnalysis()
static cl::opt<PassRemarksOpt, true, PassRemarksParser>
    PassRemarksAnalysis(
        "pass-remarks-analysis", cl::value_desc("pattern"),
        cl::desc(
            "Enable optimization analysis remarks from passes whose name match "
            "the given regular expression"),
        cl::Hidden, cl::location(PassRemarksAnalysisOptLoc), cl::ValueRequired);
} // namespace

bool DiagnosticHandler::isAnalysisRemarkEnabled(StringRef PassName) const {
  return (PassRemarksAnalysisOptLoc.Pattern &&
          PassRemarksAnalysisOptLoc.Pattern->match(PassName));
}
bool DiagnosticHandler::isMissedOptRemarkEnabled(StringRef PassName) const {
  return (PassRemarksMissedOptLoc.Pattern &&
          PassRemarksMissedOptLoc.Pattern->match(PassName));
}
bool DiagnosticHandler::isPassedOptRemarkEnabled(StringRef PassName) const {
  return (PassRemarksPassedOptLoc.Pattern &&
          PassRemarksPassedOptLoc.Pattern->match(PassName));
}

bool DiagnosticHandler::isAnyRemarkEnabled() const {
  return (PassRemarksPassedOptLoc.Pattern || PassRemarksMissedOptLoc.Pattern ||
          PassRemarksAnalysisOptLoc.Pattern);
}
