#include "neverc/Compiler/FrontendTool.h"
#include "neverc/Invoke/DirectInvocationOpts.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {
unsigned getAArch64CodeLayoutFunctionAlignmentForTesting();
} // namespace llvm

namespace {

template <typename T>
llvm::cl::opt<T> *findTopLevelOption(llvm::StringRef Name) {
  const auto &Options = llvm::cl::getRegisteredOptions();
  auto It = Options.find(Name);
  return It == Options.end() ? nullptr
                             : static_cast<llvm::cl::opt<T> *>(It->second);
}

struct AArchAlignmentMarkerState {
  unsigned Calls = 0;
  unsigned LastValue = 0;
};

thread_local AArchAlignmentMarkerState AArchAlignmentMarker;

llvm::cl::opt<unsigned> AArchAlignmentResponseMarker(
    "neverc-test-aarch-alignment-response-marker-6d1305a1", llvm::cl::Hidden,
    llvm::cl::ZeroOrMore, llvm::cl::init(17u),
    llvm::cl::desc("Test-only AArch64 response expansion marker"),
    llvm::cl::callback([](const unsigned &Value) {
      ++AArchAlignmentMarker.Calls;
      AArchAlignmentMarker.LastValue = Value;
    }));

struct AArchAlignmentOptionState {
  unsigned MarkerValue = 0;
  int MarkerOccurrences = 0;
  unsigned MarkerPosition = 0;
  int AlignmentOccurrences = 0;
  unsigned AlignmentPosition = 0;
};

int aarchAlignmentProbeFailure(int Code, llvm::StringRef Message) {
  llvm::errs() << "AArch64 alignment recovery probe " << Code << ": "
               << Message << '\n';
  return Code;
}

void *aarchAlignmentFrontendMainAddress() {
  return reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(
      &aarchAlignmentFrontendMainAddress));
}

int runAArchAlignmentOrdinaryErrorProbe(bool UseResponseFile) {
  if (llvm::timeTraceProfilerEnabled())
    return aarchAlignmentProbeFailure(1, "ambient time-trace profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return aarchAlignmentProbeFailure(2, "ambient LLVM fatal handler");

  const auto &Options = llvm::cl::getRegisteredOptions();
  auto AlignmentIt =
      Options.find("aarch64-code-layout-opt-align-functions");
  if (AlignmentIt == Options.end())
    return aarchAlignmentProbeFailure(3,
                                      "alignment option was not registered");
  llvm::cl::Option *const AlignmentOption = AlignmentIt->second;

  AlignmentOption->reset();
  AArchAlignmentResponseMarker.reset();
  if (AlignmentOption->addOccurrence(
          /*pos=*/41, AlignmentOption->ArgStr, "64"))
    return aarchAlignmentProbeFailure(4,
                                      "could not seed alignment option");
  if (AArchAlignmentResponseMarker.addOccurrence(
          /*pos=*/43, AArchAlignmentResponseMarker.ArgStr, "101"))
    return aarchAlignmentProbeFailure(5, "could not seed response marker");
  const AArchAlignmentOptionState Baseline{
      AArchAlignmentResponseMarker.getValue(),
      AArchAlignmentResponseMarker.getNumOccurrences(),
      AArchAlignmentResponseMarker.getPosition(),
      AlignmentOption->getNumOccurrences(), AlignmentOption->getPosition()};
  if (llvm::getAArch64CodeLayoutFunctionAlignmentForTesting() != 64 ||
      Baseline.MarkerValue != 101 || Baseline.MarkerOccurrences != 1 ||
      Baseline.MarkerPosition != 43 || Baseline.AlignmentOccurrences != 1 ||
      Baseline.AlignmentPosition != 41)
    return aarchAlignmentProbeFailure(6, "seed state mismatch");
  AArchAlignmentMarker = {};

  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-aarch-alignment-recovery", "c", SourcePath))
    return aarchAlignmentProbeFailure(7, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return aarchAlignmentProbeFailure(8, "could not open source");
    Source << "int neverc_aarch_alignment_recovery(void) { return 31; }\n";
    Source.close();
    if (Source.has_error()) {
      Source.clear_error();
      return aarchAlignmentProbeFailure(9, "could not close source");
    }
  }

  llvm::SmallString<128> OutputPath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-aarch-alignment-recovery", "out", OutputPath))
    return aarchAlignmentProbeFailure(10, "could not create output path");
  llvm::FileRemover RemoveOutput(OutputPath);
  if (std::error_code Error = llvm::sys::fs::remove(OutputPath))
    return aarchAlignmentProbeFailure(11, Error.message());

  llvm::SmallString<128> ResponsePath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-aarch-alignment-recovery", "rsp", ResponsePath))
    return aarchAlignmentProbeFailure(12, "could not create response file");
  llvm::FileRemover RemoveResponse(ResponsePath);
  if (std::error_code Error = llvm::sys::fs::make_absolute(ResponsePath))
    return aarchAlignmentProbeFailure(13, Error.message());
  std::error_code ResponseError;
  {
    llvm::raw_fd_ostream Response(ResponsePath, ResponseError);
    if (ResponseError)
      return aarchAlignmentProbeFailure(14, "could not open response file");
    Response << '-' << AArchAlignmentResponseMarker.ArgStr << "=7\n"
             << "-aarch64-code-layout-opt-align-functions=3\n";
    Response.close();
    if (Response.has_error()) {
      Response.clear_error();
      return aarchAlignmentProbeFailure(15, "could not close response file");
    }
  }

  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  auto Run = [&](llvm::ArrayRef<std::string> LLVMArguments) {
    std::vector<const char *> Args = {
        "-triple", HostTriple.c_str(), "-fsyntax-only", "-o",
        OutputPath.c_str()};
    Args.reserve(Args.size() + LLVMArguments.size() * 2 + 1);
    for (const std::string &Argument : LLVMArguments) {
      Args.push_back("-mllvm");
      Args.push_back(Argument.c_str());
    }
    Args.push_back(SourcePath.c_str());
    return neverc::ExecuteFrontendDirect(
        Args, "neverc-test-frontend", aarchAlignmentFrontendMainAddress(),
        &DirectOpts);
  };
  auto StateMatches = [&] {
    return llvm::getAArch64CodeLayoutFunctionAlignmentForTesting() == 64 &&
           AlignmentOption->getNumOccurrences() ==
               Baseline.AlignmentOccurrences &&
           AlignmentOption->getPosition() == Baseline.AlignmentPosition &&
           AArchAlignmentResponseMarker.getValue() == Baseline.MarkerValue &&
           AArchAlignmentResponseMarker.getNumOccurrences() ==
               Baseline.MarkerOccurrences &&
           AArchAlignmentResponseMarker.getPosition() ==
               Baseline.MarkerPosition;
  };

  const std::string InvalidArgument =
      UseResponseFile ? "@" + ResponsePath.str().str()
                      : "-aarch64-code-layout-opt-align-functions=3";
  if (Run({InvalidArgument}) != 1)
    return aarchAlignmentProbeFailure(16,
                                      "invalid option did not return one");
  if (!StateMatches())
    return aarchAlignmentProbeFailure(17,
                                      "invalid option changed outer state");
  if (llvm::sys::fs::exists(OutputPath))
    return aarchAlignmentProbeFailure(18,
                                      "invalid option produced output");
  const unsigned ExpectedMarkerCalls = UseResponseFile ? 1u : 0u;
  if (AArchAlignmentMarker.Calls != ExpectedMarkerCalls ||
      (UseResponseFile && AArchAlignmentMarker.LastValue != 7))
    return aarchAlignmentProbeFailure(19,
                                      "response expansion marker mismatch");

  const std::string ValidMarker =
      "-" + AArchAlignmentResponseMarker.ArgStr.str() + "=9";
  const std::string ValidAlignment =
      "-aarch64-code-layout-opt-align-functions=128";
  if (Run({ValidMarker, ValidAlignment}) != 0)
    return aarchAlignmentProbeFailure(20, "valid retry failed");
  if (!StateMatches())
    return aarchAlignmentProbeFailure(21, "valid retry changed outer state");
  if (AArchAlignmentMarker.Calls != ExpectedMarkerCalls + 1 ||
      AArchAlignmentMarker.LastValue != 9)
    return aarchAlignmentProbeFailure(22, "valid retry marker mismatch");

  const unsigned MarkerCallsBeforeNoMllvm = AArchAlignmentMarker.Calls;
  if (Run({}) != 0)
    return aarchAlignmentProbeFailure(23, "no-mllvm retry failed");
  if (!StateMatches() ||
      AArchAlignmentMarker.Calls != MarkerCallsBeforeNoMllvm)
    return aarchAlignmentProbeFailure(24,
                                      "no-mllvm retry changed option state");
  if (llvm::sys::fs::exists(OutputPath))
    return aarchAlignmentProbeFailure(25, "retry produced output");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return aarchAlignmentProbeFailure(26, "retry retained a fatal handler");

  llvm::errs() << "AArch64 alignment option rejection recovered\n";
  return 0;
}

} // namespace

TEST(PluginLLVMOptionOrdinaryErrorTest,
     AArch64FunctionAlignmentInvalidDirectAndResponseReturnNormally) {
  EXPECT_EXIT(std::exit(runAArchAlignmentOrdinaryErrorProbe(
                  /*UseResponseFile=*/false)),
              ::testing::ExitedWithCode(0),
              "AArch64 alignment option rejection recovered");
  EXPECT_EXIT(std::exit(runAArchAlignmentOrdinaryErrorProbe(
                  /*UseResponseFile=*/true)),
              ::testing::ExitedWithCode(0),
              "AArch64 alignment option rejection recovered");
}
