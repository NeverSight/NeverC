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
#include "llvm/Support/TargetSelect.h"
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

int runInvocationScopedRelinkOptionProbe() {
  constexpr llvm::StringLiteral OptionName(
      "relink-builtin-bitcode-postop");
  if (llvm::cl::getRegisteredOptions().contains(OptionName))
    return aarchAlignmentProbeFailure(27,
                                      "relink option has global storage");

  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile("neverc-relink-option", "c",
                                         SourcePath))
    return aarchAlignmentProbeFailure(28, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return aarchAlignmentProbeFailure(29, "could not open source");
    Source << "int neverc_relink_option(void) { return 37; }\n";
  }

  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  auto Run = [&](llvm::StringRef Option) {
    std::vector<std::string> Storage;
    std::vector<const char *> Args = {"-triple", HostTriple.c_str(),
                                      "-fsyntax-only"};
    if (!Option.empty()) {
      Storage.push_back(Option.str());
      Args.push_back("-mllvm");
      Args.push_back(Storage.back().c_str());
    }
    Args.push_back(SourcePath.c_str());
    return neverc::ExecuteFrontendDirect(
        Args, "neverc-test-frontend", aarchAlignmentFrontendMainAddress(),
        &DirectOpts);
  };

  if (Run("-relink-builtin-bitcode-postop") != 0 ||
      Run("-relink-builtin-bitcode-postop=false") != 0 ||
      Run("-relink-builtin-bitcode-postop=maybe") != 1 || Run({}) != 0)
    return aarchAlignmentProbeFailure(30,
                                      "invocation-scoped option result mismatch");
  if (llvm::cl::getRegisteredOptions().contains(OptionName))
    return aarchAlignmentProbeFailure(31,
                                      "relink option leaked into registry");

  llvm::errs() << "invocation-scoped relink option recovered\n";
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

TEST(PluginLLVMOptionOrdinaryErrorTest,
     RelinkBuiltinBitcodeOptionIsInvocationScoped) {
  EXPECT_EXIT(std::exit(runInvocationScopedRelinkOptionProbe()),
              ::testing::ExitedWithCode(0),
              "invocation-scoped relink option recovered");
}

namespace llvm {
std::uint32_t getAArch64TailFoldingOptionStateForTesting();
std::uint8_t getX86AlignBranchKindStateForTesting();
} // namespace llvm

namespace {

struct BackendFatalOptionState {
  bool PrintBeforeAll = false;
  int PrintBeforeOccurrences = 0;
  bool PrintAfterAll = false;
  int PrintAfterOccurrences = 0;
  std::string IRDumpDirectory;
  int IRDumpDirectoryOccurrences = 0;
};

std::optional<BackendFatalOptionState> captureBackendFatalOptions() {
  auto *PrintBefore = findTopLevelOption<bool>("print-before-all");
  auto *PrintAfter = findTopLevelOption<bool>("print-after-all");
  auto *DumpDirectory =
      findTopLevelOption<std::string>("ir-dump-directory");
  if (!PrintBefore || !PrintAfter || !DumpDirectory)
    return std::nullopt;
  return BackendFatalOptionState{
      PrintBefore->getValue(), PrintBefore->getNumOccurrences(),
      PrintAfter->getValue(), PrintAfter->getNumOccurrences(),
      DumpDirectory->getValue(), DumpDirectory->getNumOccurrences()};
}

bool backendFatalOptionsMatch(const BackendFatalOptionState &Expected) {
  std::optional<BackendFatalOptionState> Actual =
      captureBackendFatalOptions();
  return Actual && Actual->PrintBeforeAll == Expected.PrintBeforeAll &&
         Actual->PrintBeforeOccurrences == Expected.PrintBeforeOccurrences &&
         Actual->PrintAfterAll == Expected.PrintAfterAll &&
         Actual->PrintAfterOccurrences == Expected.PrintAfterOccurrences &&
         Actual->IRDumpDirectory == Expected.IRDumpDirectory &&
         Actual->IRDumpDirectoryOccurrences ==
             Expected.IRDumpDirectoryOccurrences;
}

constexpr llvm::StringLiteral LLVMFatalValidatorDirectDiagnostic =
    "neverc test LLVM fatal validator direct value 3 (9f2e4c7b)";
constexpr llvm::StringLiteral LLVMFatalValidatorResponseDiagnostic =
    "neverc test LLVM fatal validator response value 5 (9f2e4c7b)";

struct LLVMFatalValidatorCallbackProbe {
  unsigned Calls = 0;
  unsigned LastValue = 0;
};

thread_local LLVMFatalValidatorCallbackProbe LLVMFatalValidatorCallbackState;
thread_local LLVMFatalValidatorCallbackProbe LLVMValidatorParseState;

struct LLVMTestValidatorParser final : llvm::cl::parser<unsigned> {
  explicit LLVMTestValidatorParser(llvm::cl::Option &O)
      : llvm::cl::parser<unsigned>(O) {}

  bool parse(llvm::cl::Option &O, llvm::StringRef ArgName,
             llvm::StringRef Arg, unsigned &Value) {
    if (llvm::cl::parser<unsigned>::parse(O, ArgName, Arg, Value))
      return true;
    ++LLVMValidatorParseState.Calls;
    LLVMValidatorParseState.LastValue = Value;
    if (Value == 3)
      return O.error(LLVMFatalValidatorDirectDiagnostic, ArgName);
    if (Value == 5)
      return O.error(LLVMFatalValidatorResponseDiagnostic, ArgName);
    return false;
  }
};

llvm::cl::opt<unsigned, false, LLVMTestValidatorParser>
    LLVMFatalValidatorOption(
    "neverc-test-llvm-fatal-validator-9f2e4c7b", llvm::cl::Hidden,
    llvm::cl::ZeroOrMore, llvm::cl::init(11u),
    llvm::cl::desc("Test-only recoverable LLVM validator"),
    llvm::cl::callback([](const unsigned &Value) {
      ++LLVMFatalValidatorCallbackState.Calls;
      LLVMFatalValidatorCallbackState.LastValue = Value;
    }));

llvm::cl::opt<unsigned> LLVMFatalValidatorSentinel(
    "neverc-test-llvm-fatal-validator-sentinel-9f2e4c7b", llvm::cl::Hidden,
    llvm::cl::ZeroOrMore, llvm::cl::init(17u),
    llvm::cl::desc("Test-only LLVM option snapshot sentinel"));

struct LLVMTestOptionState {
  unsigned Value = 0;
  int Occurrences = 0;
  unsigned Position = 0;
};

template <typename OptionT>
LLVMTestOptionState captureLLVMTestOption(const OptionT &Option) {
  return LLVMTestOptionState{Option.getValue(), Option.getNumOccurrences(),
                             Option.getPosition()};
}

template <typename OptionT>
bool llvmTestOptionMatches(const OptionT &Option,
                           const LLVMTestOptionState &Expected) {
  return Option.getValue() == Expected.Value &&
         Option.getNumOccurrences() == Expected.Occurrences &&
         Option.getPosition() == Expected.Position;
}

struct TraceFiles {
  std::string Output;
  std::string Trace;
};

std::error_code createTraceFiles(llvm::StringRef Stem, TraceFiles &Files) {
  llvm::SmallString<128> Output;
  if (std::error_code Error =
          llvm::sys::fs::createTemporaryFile(Stem, "image", Output))
    return Error;
  Files.Output = Output.str().str();
  Files.Trace = Files.Output + ".time-trace";
  return llvm::sys::fs::remove(Output);
}

void *frontendMainAddress() {
  return reinterpret_cast<void *>(
      reinterpret_cast<std::uintptr_t>(&frontendMainAddress));
}

int recoveryProbeFailure(int Code, llvm::StringRef Message) {
  llvm::errs() << "frontend fatal recovery probe " << Code << ": " << Message
               << '\n';
  return Code;
}

enum class LLVMOptionFailureKind {
  Unknown,
  GroupedHelp,
  ResponseVersion,
};

int runLLVMOptionParseFailureProbe(LLVMOptionFailureKind Kind) {
  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter())
    return recoveryProbeFailure(39, "could not initialize native target");
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery = llvm::make_scope_exit(
      [] { llvm::CrashRecoveryContext::Disable(); });
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(40, "ambient time-trace profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(41, "ambient LLVM fatal handler");
  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile("neverc-mllvm-recovery", "c",
                                         SourcePath))
    return recoveryProbeFailure(43, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return recoveryProbeFailure(44, "could not open source");
    Source << "int neverc_mllvm_recovery_probe(void) { return 29; }\n";
  }

  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  llvm::SmallString<128> WarmupOutput;
  if (llvm::sys::fs::createTemporaryFile("neverc-mllvm-warmup", "o",
                                         WarmupOutput))
    return recoveryProbeFailure(42, "could not create warmup output");
  llvm::FileRemover RemoveWarmupOutput(WarmupOutput);
  if (std::error_code Error = llvm::sys::fs::remove(WarmupOutput))
    return recoveryProbeFailure(42, Error.message());
  const char *WarmupArgs[] = {"-triple", HostTriple.c_str(), "-emit-obj",
                              "-o", WarmupOutput.c_str(), SourcePath.c_str()};
  if (neverc::ExecuteFrontendDirect(WarmupArgs, "neverc-test-frontend",
                                    frontendMainAddress(), &DirectOpts) != 0 ||
      !llvm::sys::fs::exists(WarmupOutput))
    return recoveryProbeFailure(42, "could not warm frontend state");

  std::optional<BackendFatalOptionState> BaselineOptions =
      captureBackendFatalOptions();
  if (!BaselineOptions)
    return recoveryProbeFailure(42, "could not capture LLVM option baseline");
  llvm::TimerGroup *const BaselineTimerGroups =
      llvm::timer_detail::TimerGroupList;

  TraceFiles FailedFiles;
  if (std::error_code Error =
          createTraceFiles("neverc-mllvm-rejected", FailedFiles))
    return recoveryProbeFailure(45, Error.message());
  llvm::FileRemover RemoveFailedOutput(FailedFiles.Output);
  llvm::FileRemover RemoveFailedTrace(FailedFiles.Trace);

  llvm::SmallString<128> ResponsePath;
  std::optional<llvm::FileRemover> RemoveResponse;
  std::string LLVMArgument;
  auto CreateResponse = [&](llvm::StringRef Contents) -> int {
    if (llvm::sys::fs::createTemporaryFile("neverc-mllvm-control", "rsp",
                                           ResponsePath))
      return 46;
    RemoveResponse.emplace(ResponsePath);
    std::error_code ResponseError;
    llvm::raw_fd_ostream Response(ResponsePath, ResponseError);
    if (ResponseError)
      return 47;
    Response << Contents << '\n';
    Response.close();
    LLVMArgument = "@" + ResponsePath.str().str();
    return 0;
  };
  switch (Kind) {
  case LLVMOptionFailureKind::Unknown:
    LLVMArgument = "-neverc-invalid-llvm-option";
    break;
  case LLVMOptionFailureKind::GroupedHelp:
    LLVMArgument = "-hneverc";
    break;
  case LLVMOptionFailureKind::ResponseVersion: {
    if (int Error = CreateResponse("--version"))
      return recoveryProbeFailure(
          Error, Error == 46 ? "could not create response file"
                            : "could not open response file");
    break;
  }
  }

  const std::string TraceArgument = "-ftime-trace=" + FailedFiles.Trace;
  const char *FailedArgs[] = {
      "-triple", HostTriple.c_str(), "-emit-obj", "-o",
      FailedFiles.Output.c_str(), TraceArgument.c_str(), "-mllvm",
      LLVMArgument.c_str(), SourcePath.c_str()};
  if (neverc::ExecuteFrontendDirect(FailedArgs, "neverc-test-frontend",
                                    frontendMainAddress(), &DirectOpts) != 1)
    return recoveryProbeFailure(48, "invalid LLVM option did not return one");
  if (llvm::sys::fs::exists(FailedFiles.Output) ||
      llvm::sys::fs::exists(FailedFiles.Trace))
    return recoveryProbeFailure(49, "rejected LLVM option produced output");
  if (!backendFatalOptionsMatch(*BaselineOptions))
    return recoveryProbeFailure(50, "LLVM options were not restored");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(51, "timer group registry changed");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(52, "time-trace profiler remained enabled");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(53, "LLVM fatal handler remained installed");

  TraceFiles RetryFiles;
  if (std::error_code Error =
          createTraceFiles("neverc-mllvm-retry", RetryFiles))
    return recoveryProbeFailure(54, Error.message());
  llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
  const char *RetryArgs[] = {
      "-triple", HostTriple.c_str(), "-emit-obj", "-o",
      RetryFiles.Output.c_str(), SourcePath.c_str()};
  if (neverc::ExecuteFrontendDirect(RetryArgs, "neverc-test-frontend",
                                    frontendMainAddress(), &DirectOpts) != 0)
    return recoveryProbeFailure(55, "fresh frontend retry failed");
  if (!llvm::sys::fs::exists(RetryFiles.Output))
    return recoveryProbeFailure(56, "fresh frontend retry emitted no output");
  if (!backendFatalOptionsMatch(*BaselineOptions))
    return recoveryProbeFailure(57, "fresh retry changed LLVM options");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(58, "fresh retry changed timer registry");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(59, "fresh retry retained a profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(60, "fresh retry retained a fatal handler");

  llvm::errs() << "frontend LLVM option rejection recovered\n";
  return 0;
}

enum class LLVMValidatorOrder { DirectFirst, ResponseFirst };
enum class LLVMProductionParserCase {
  None,
  All,
  AArchAlign,
  AArchTail,
  PassRemarksPassed,
  PassRemarksMissed,
  PassRemarksAnalysis,
  X86Align,
};
enum class LLVMProductionInvocation {
  Both,
  DirectOnly,
  ResponseOnly,
};

// LLVM option validation must return through the parser normally so all argv,
// response-file, and diagnostic scratch storage is released. It is not a
// promise that an arbitrary fatal callback can be recovered in-process.
int runLLVMValidatorRejectionProbe(
    LLVMValidatorOrder Order,
    LLVMProductionParserCase ProductionCase =
        LLVMProductionParserCase::None,
    LLVMProductionInvocation ProductionInvocation =
        LLVMProductionInvocation::Both,
    llvm::StringRef InvalidArgumentOverride = {}) {
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(120, "ambient time-trace profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(121, "ambient LLVM fatal handler");
  if (neverc::plugin::pluginLLVMOptionGateHeldSharedByCurrentThread() ||
      neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread())
    return recoveryProbeFailure(152, "ambient LLVM option gate lease");
  const LLVMFatalValidatorCallbackProbe SavedValidatorCallbackState =
      LLVMFatalValidatorCallbackState;
  const LLVMFatalValidatorCallbackProbe SavedValidatorParseState =
      LLVMValidatorParseState;
  auto RestoreValidatorProbeState = llvm::make_scope_exit([&] {
    LLVMFatalValidatorCallbackState = SavedValidatorCallbackState;
    LLVMValidatorParseState = SavedValidatorParseState;
  });
  LLVMFatalValidatorCallbackState = {};
  LLVMValidatorParseState = {};

#ifndef NDEBUG
  // The debug options are lazy. Register them under the option gate before
  // the outer snapshot so nested frontend parses restore their side state as
  // well as the ordinary option metadata.
  {
    neverc::plugin::PluginLLVMOptionExclusiveLease DebugRegistrationLease(
        neverc::plugin::pluginLLVMOptionGate());
    llvm::initDebugOptions();
  }
#endif

  // Ordinary gtests must restore the registry they seed below. Keeping this
  // outer snapshot alive also makes every frontend invocation exercise the
  // option gate's supported same-thread nested-exclusive path.
  neverc::plugin::PluginLLVMOptionSnapshot TestOptionSnapshot(
      neverc::plugin::pluginLLVMOptionGate());
  std::optional<BackendFatalOptionState> BaselineOptions =
      captureBackendFatalOptions();
  if (!BaselineOptions)
    return recoveryProbeFailure(122, "could not capture LLVM option baseline");
  {
    llvm::Timer Warmup("neverc-mllvm-validator-warmup",
                       "NeverC mllvm validator warmup");
  }
  llvm::TimerGroup *const BaselineTimerGroups =
      llvm::timer_detail::TimerGroupList;
  if (!BaselineTimerGroups)
    return recoveryProbeFailure(123,
                                "default timer group was not initialized");
  auto AArchAlignIt = llvm::cl::getRegisteredOptions().find(
      "aarch64-code-layout-opt-align-functions");
  if (AArchAlignIt == llvm::cl::getRegisteredOptions().end())
    return recoveryProbeFailure(167,
                                "AArch64 alignment option was not registered");
  llvm::cl::Option *const AArchAlignOption = AArchAlignIt->second;
  auto FindProductionOption = [&](llvm::StringRef Name,
                                  int MissingCode) -> llvm::cl::Option * {
    auto It = llvm::cl::getRegisteredOptions().find(Name);
    if (It == llvm::cl::getRegisteredOptions().end()) {
      (void)recoveryProbeFailure(
          MissingCode, (llvm::Twine(Name) + " option was not registered").str());
      return nullptr;
    }
    return It->second;
  };
  llvm::cl::Option *const AArchTailOption =
      FindProductionOption("sve-tail-folding", 178);
  llvm::cl::Option *const PassRemarksOption =
      FindProductionOption("pass-remarks", 179);
  llvm::cl::Option *const PassRemarksMissedOption =
      FindProductionOption("pass-remarks-missed", 206);
  llvm::cl::Option *const PassRemarksAnalysisOption =
      FindProductionOption("pass-remarks-analysis", 207);
  llvm::cl::Option *const X86AlignOption =
      FindProductionOption("x86-align-branch", 180);
  auto *const OptBisectOption =
      findTopLevelOption<int>("opt-bisect-limit");
#ifndef NDEBUG
  llvm::cl::Option *const DebugOption =
      FindProductionOption("debug", 213);
  llvm::cl::Option *const DebugOnlyOption =
      FindProductionOption("debug-only", 194);
#endif
  if (!AArchTailOption || !PassRemarksOption || !PassRemarksMissedOption ||
      !PassRemarksAnalysisOption || !X86AlignOption || !OptBisectOption
#ifndef NDEBUG
      || !DebugOption || !DebugOnlyOption
#endif
  )
    return 181;

  struct RawOptionState {
    int Occurrences = 0;
    unsigned Position = 0;
  };
  auto CaptureRawOption = [](const llvm::cl::Option &Option) {
    return RawOptionState{Option.getNumOccurrences(), Option.getPosition()};
  };
  auto SeedRawOption = [&](llvm::cl::Option &Option, unsigned Position,
                           llvm::StringRef Value, int FailureCode) -> bool {
    Option.reset();
    if (!Option.addOccurrence(Position, Option.ArgStr, Value))
      return true;
    (void)recoveryProbeFailure(
        FailureCode,
        (llvm::Twine("could not seed production option -") + Option.ArgStr)
            .str());
    return false;
  };
  if (!SeedRawOption(*AArchAlignOption, 83, "64", 182) ||
      !SeedRawOption(*AArchTailOption, 89,
                     "simple+reductions+noreverse", 183) ||
      !SeedRawOption(*PassRemarksOption, 97,
                     "^neverc_snapshot_passed_[0-9]+$", 184) ||
      !SeedRawOption(*PassRemarksMissedOption, 99,
                     "^neverc_snapshot_missed_[0-9]+$", 208) ||
      !SeedRawOption(*PassRemarksAnalysisOption, 100,
                     "^neverc_snapshot_analysis_[0-9]+$", 209) ||
      !SeedRawOption(*X86AlignOption, 101, "jcc+call", 185))
    return 186;
  const RawOptionState BaselineAArchAlign =
      CaptureRawOption(*AArchAlignOption);
  const unsigned BaselineAArchAlignValue =
      llvm::getAArch64CodeLayoutFunctionAlignmentForTesting();
  const RawOptionState BaselineAArchTail =
      CaptureRawOption(*AArchTailOption);
  const std::uint32_t BaselineAArchTailValue =
      llvm::getAArch64TailFoldingOptionStateForTesting();
  const RawOptionState BaselinePassRemarks =
      CaptureRawOption(*PassRemarksOption);
  const RawOptionState BaselinePassRemarksMissed =
      CaptureRawOption(*PassRemarksMissedOption);
  const RawOptionState BaselinePassRemarksAnalysis =
      CaptureRawOption(*PassRemarksAnalysisOption);
  const RawOptionState BaselineX86Align = CaptureRawOption(*X86AlignOption);
  const std::uint8_t BaselineX86AlignValue =
      llvm::getX86AlignBranchKindStateForTesting();
  llvm::DiagnosticHandler RemarkHandler;
  auto RemarkStateMatches = [&] {
    return RemarkHandler.isPassedOptRemarkEnabled(
               "neverc_snapshot_passed_17") &&
           !RemarkHandler.isPassedOptRemarkEnabled(
               "neverc_snapshot_missed_17") &&
           !RemarkHandler.isPassedOptRemarkEnabled(
               "neverc_snapshot_analysis_17") &&
           RemarkHandler.isMissedOptRemarkEnabled(
               "neverc_snapshot_missed_19") &&
           !RemarkHandler.isMissedOptRemarkEnabled(
               "neverc_snapshot_passed_19") &&
           !RemarkHandler.isMissedOptRemarkEnabled(
               "neverc_snapshot_analysis_19") &&
           RemarkHandler.isAnalysisRemarkEnabled(
               "neverc_snapshot_analysis_23") &&
           !RemarkHandler.isAnalysisRemarkEnabled(
               "neverc_snapshot_passed_23") &&
           !RemarkHandler.isAnalysisRemarkEnabled(
               "neverc_snapshot_missed_23") &&
           !RemarkHandler.isAnyRemarkEnabled("neverc_snapshot_outsider") &&
           RemarkHandler.isAnyRemarkEnabled();
  };
  if (BaselineAArchAlignValue != 64 ||
      BaselineAArchTailValue != 0x00080201u ||
      BaselineX86AlignValue != 0x0au || !RemarkStateMatches())
    return recoveryProbeFailure(187, "production option seed value mismatch");

  LLVMTestOptionState BaselineValidator;
  LLVMTestOptionState BaselineSentinel;
  LLVMFatalValidatorOption.reset();
  LLVMFatalValidatorSentinel.reset();
  if (LLVMFatalValidatorOption.addOccurrence(
          /*pos=*/73, LLVMFatalValidatorOption.ArgStr, "64"))
    return recoveryProbeFailure(124, "could not seed validator option");
  if (LLVMFatalValidatorSentinel.addOccurrence(
          /*pos=*/79, LLVMFatalValidatorSentinel.ArgStr, "101"))
    return recoveryProbeFailure(125, "could not seed sentinel option");
  BaselineValidator = captureLLVMTestOption(LLVMFatalValidatorOption);
  BaselineSentinel = captureLLVMTestOption(LLVMFatalValidatorSentinel);
  if (BaselineValidator.Value != 64 || BaselineValidator.Occurrences != 1 ||
      BaselineValidator.Position != 73)
    return recoveryProbeFailure(126, "validator seed metadata mismatch");
  if (BaselineSentinel.Value != 101 || BaselineSentinel.Occurrences != 1 ||
      BaselineSentinel.Position != 79)
    return recoveryProbeFailure(127, "sentinel seed metadata mismatch");
  if (LLVMFatalValidatorCallbackState.Calls != 1 ||
      LLVMFatalValidatorCallbackState.LastValue != 64)
    return recoveryProbeFailure(151, "validator seed callback mismatch");
  LLVMFatalValidatorCallbackState = {};
  LLVMValidatorParseState = {};

  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-mllvm-fatal-validator", "c", SourcePath))
    return recoveryProbeFailure(128, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return recoveryProbeFailure(129, "could not open source");
    Source << "int neverc_mllvm_fatal_validator(void) { return 37; }\n";
    Source.close();
    if (Source.has_error()) {
      Source.clear_error();
      return recoveryProbeFailure(130, "could not close source");
    }
  }

  llvm::SmallString<128> OutputPath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-mllvm-fatal-validator", "out", OutputPath))
    return recoveryProbeFailure(131, "could not create output sentinel");
  llvm::FileRemover RemoveOutput(OutputPath);
  if (std::error_code Error = llvm::sys::fs::remove(OutputPath))
    return recoveryProbeFailure(132, Error.message());

  llvm::SmallString<128> ResponsePath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-mllvm-fatal-validator", "rsp", ResponsePath))
    return recoveryProbeFailure(133, "could not create response file");
  llvm::FileRemover RemoveResponse(ResponsePath);
  if (std::error_code Error = llvm::sys::fs::make_absolute(ResponsePath))
    return recoveryProbeFailure(134, Error.message());
  std::error_code ResponseError;
  {
    llvm::raw_fd_ostream Response(ResponsePath, ResponseError);
    if (ResponseError)
      return recoveryProbeFailure(135, "could not open response file");
    Response << '-' << LLVMFatalValidatorOption.ArgStr << "=5\n";
    Response.close();
    if (Response.has_error()) {
      Response.clear_error();
      return recoveryProbeFailure(136, "could not close response file");
    }
  }

  std::vector<std::string> ProductionResponsePaths;
  auto RemoveProductionResponses = llvm::make_scope_exit([&] {
    for (const std::string &Path : ProductionResponsePaths)
      (void)llvm::sys::fs::remove(Path);
  });
  auto CreateMarkedResponse = [&](llvm::StringRef Stem,
                                  llvm::StringRef InvalidArgument,
                                  std::string &Path) -> int {
    llvm::SmallString<128> TemporaryPath;
    if (llvm::sys::fs::createTemporaryFile(Stem, "rsp", TemporaryPath))
      return recoveryProbeFailure(174, "could not create production response");
    if (std::error_code Error = llvm::sys::fs::make_absolute(TemporaryPath))
      return recoveryProbeFailure(175, Error.message());
    Path = TemporaryPath.str().str();
    ProductionResponsePaths.push_back(Path);
    std::error_code Error;
    llvm::raw_fd_ostream Response(Path, Error);
    if (Error)
      return recoveryProbeFailure(176, "could not open production response");
    Response << '-' << LLVMFatalValidatorOption.ArgStr << "=7\n"
             << InvalidArgument << '\n';
    Response.close();
    if (Response.has_error()) {
      Response.clear_error();
      return recoveryProbeFailure(177, "could not close production response");
    }
    return 0;
  };
  std::string AArchAlignResponsePath;
  std::string AArchTailResponsePath;
  std::string PassRemarksResponsePath;
  std::string PassRemarksMissedResponsePath;
  std::string PassRemarksAnalysisResponsePath;
  std::string X86AlignResponsePath;
  auto SelectInvalidArgument = [&](LLVMProductionParserCase Candidate,
                                   llvm::StringRef Default) {
    if (ProductionCase == Candidate && !InvalidArgumentOverride.empty())
      return InvalidArgumentOverride.str();
    return Default.str();
  };
  if (ProductionCase == LLVMProductionParserCase::All &&
      !InvalidArgumentOverride.empty())
    return recoveryProbeFailure(
        219, "invalid-argument override requires one production parser case");
  const std::string AArchInvalidArgument = SelectInvalidArgument(
      LLVMProductionParserCase::AArchAlign,
      "-aarch64-code-layout-opt-align-functions=3");
  const std::string AArchTailInvalidArgument = SelectInvalidArgument(
      LLVMProductionParserCase::AArchTail,
      "-sve-tail-folding=simple+neverc-invalid");
  const std::string PassRemarksInvalidArgument = SelectInvalidArgument(
      LLVMProductionParserCase::PassRemarksPassed, "-pass-remarks=[");
  const std::string PassRemarksMissedInvalidArgument = SelectInvalidArgument(
      LLVMProductionParserCase::PassRemarksMissed, "-pass-remarks-missed=[");
  const std::string PassRemarksAnalysisInvalidArgument =
      SelectInvalidArgument(LLVMProductionParserCase::PassRemarksAnalysis,
                            "-pass-remarks-analysis=[");
  const std::string X86AlignInvalidArgument = SelectInvalidArgument(
      LLVMProductionParserCase::X86Align,
      "-x86-align-branch=jcc+neverc-invalid");
  if (int Error = CreateMarkedResponse(
          "neverc-mllvm-aarch-align-invalid",
          AArchInvalidArgument, AArchAlignResponsePath))
    return Error;
  if (int Error = CreateMarkedResponse("neverc-mllvm-aarch-tail-invalid",
                                       AArchTailInvalidArgument,
                                       AArchTailResponsePath))
    return Error;
  if (int Error = CreateMarkedResponse("neverc-mllvm-pass-remarks-invalid",
                                       PassRemarksInvalidArgument,
                                       PassRemarksResponsePath))
    return Error;
  if (int Error = CreateMarkedResponse(
          "neverc-mllvm-pass-remarks-missed-invalid",
          PassRemarksMissedInvalidArgument, PassRemarksMissedResponsePath))
    return Error;
  if (int Error = CreateMarkedResponse(
          "neverc-mllvm-pass-remarks-analysis-invalid",
          PassRemarksAnalysisInvalidArgument,
          PassRemarksAnalysisResponsePath))
    return Error;
  if (int Error = CreateMarkedResponse(
          "neverc-mllvm-x86-align-invalid",
          X86AlignInvalidArgument, X86AlignResponsePath))
    return Error;

  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  std::vector<std::string> StressLLVMArguments;
  StressLLVMArguments.reserve(64);
  for (unsigned I = 0; I != 64; ++I)
    StressLLVMArguments.push_back(
        "-" + LLVMFatalValidatorSentinel.ArgStr.str() + "=" +
        std::to_string(200 + I));
  auto RunWithLLVMArgument = [&](const std::string &LLVMArgument) {
    std::vector<const char *> Args = {
        "-triple", HostTriple.c_str(), "-fsyntax-only", "-o",
        OutputPath.c_str()};
    Args.reserve(Args.size() + StressLLVMArguments.size() * 2 + 3);
    for (const std::string &StressArgument : StressLLVMArguments) {
      Args.push_back("-mllvm");
      Args.push_back(StressArgument.c_str());
    }
    Args.push_back("-mllvm");
    Args.push_back(LLVMArgument.c_str());
    Args.push_back(SourcePath.c_str());
    return neverc::ExecuteFrontendDirect(
        Args, "neverc-test-frontend", frontendMainAddress(), &DirectOpts);
  };
  auto RunWithoutLLVMArgument = [&] {
    const char *Args[] = {"-triple", HostTriple.c_str(), "-fsyntax-only",
                          "-o", OutputPath.c_str(), SourcePath.c_str()};
    return neverc::ExecuteFrontendDirect(
        Args, "neverc-test-frontend", frontendMainAddress(), &DirectOpts);
  };

  auto CheckRestored = [&](llvm::StringRef Stage) -> int {
    auto Fail = [&](int Code, llvm::StringRef Message) {
      return recoveryProbeFailure(
          Code, (llvm::Twine(Stage) + ": " + Message).str());
    };
    if (!backendFatalOptionsMatch(*BaselineOptions))
      return Fail(137, "unrelated LLVM options changed");
    if (!llvmTestOptionMatches(LLVMFatalValidatorOption,
                               BaselineValidator))
      return Fail(138, "validator value or occurrence metadata changed");
    if (!llvmTestOptionMatches(LLVMFatalValidatorSentinel, BaselineSentinel))
      return Fail(139, "sentinel value or occurrence metadata changed");
    auto RawOptionMatches = [](const llvm::cl::Option &Option,
                               const RawOptionState &Expected) {
      return Option.getNumOccurrences() == Expected.Occurrences &&
             Option.getPosition() == Expected.Position;
    };
    if (!RawOptionMatches(*AArchAlignOption, BaselineAArchAlign))
      return Fail(168, "AArch64 alignment option metadata changed");
    if (llvm::getAArch64CodeLayoutFunctionAlignmentForTesting() !=
        BaselineAArchAlignValue)
      return Fail(218, "AArch64 alignment option value changed");
    if (!RawOptionMatches(*AArchTailOption, BaselineAArchTail))
      return Fail(188, "AArch64 tail-folding option metadata changed");
    if (llvm::getAArch64TailFoldingOptionStateForTesting() !=
        BaselineAArchTailValue)
      return Fail(220, "AArch64 tail-folding option value changed");
    if (!RawOptionMatches(*PassRemarksOption, BaselinePassRemarks))
      return Fail(189, "pass-remarks option metadata changed");
    if (!RawOptionMatches(*PassRemarksMissedOption,
                          BaselinePassRemarksMissed))
      return Fail(210, "pass-remarks-missed option metadata changed");
    if (!RawOptionMatches(*PassRemarksAnalysisOption,
                          BaselinePassRemarksAnalysis))
      return Fail(211, "pass-remarks-analysis option metadata changed");
    if (!RawOptionMatches(*X86AlignOption, BaselineX86Align))
      return Fail(190, "X86 alignment option metadata changed");
    if (llvm::getX86AlignBranchKindStateForTesting() !=
        BaselineX86AlignValue)
      return Fail(221, "X86 alignment option value changed");
    if (!RemarkStateMatches())
      return Fail(191, "pass-remarks option values changed");
    if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
      return Fail(140, "timer group registry changed");
    if (llvm::timeTraceProfilerEnabled())
      return Fail(141, "time-trace profiler remained enabled");
    if (::ErrorHandler || ::ErrorHandlerUserData)
      return Fail(142, "LLVM fatal handler remained installed");
    if (neverc::plugin::pluginLLVMOptionGateHeldSharedByCurrentThread() ||
        !neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread())
      return Fail(154, "outer LLVM option snapshot lease changed");
    if (llvm::sys::fs::exists(OutputPath))
      return Fail(143, "syntax-only invocation produced output");
    return 0;
  };

  const std::string DirectInvalidArgument =
      "-" + LLVMFatalValidatorOption.ArgStr.str() + "=3";
  const std::string ResponseInvalidArgument =
      "@" + ResponsePath.str().str();
  const std::string FirstValidArgument =
      "-" + LLVMFatalValidatorOption.ArgStr.str() + "=7";
  const std::string SecondValidArgument =
      "-" + LLVMFatalValidatorOption.ArgStr.str() + "=9";

  auto RunInvalidValidator = [&](const std::string &Argument,
                                 unsigned ExpectedAttempt,
                                 llvm::StringRef Stage) -> int {
    const LLVMFatalValidatorCallbackProbe CallbackBefore =
        LLVMFatalValidatorCallbackState;
    const LLVMFatalValidatorCallbackProbe ParseBefore =
        LLVMValidatorParseState;
    if (RunWithLLVMArgument(Argument) != 1)
      return recoveryProbeFailure(
          144, (llvm::Twine(Stage) + ": validator did not return one").str());
    if (LLVMValidatorParseState.Calls != ParseBefore.Calls + 1 ||
        LLVMValidatorParseState.LastValue != ExpectedAttempt)
      return recoveryProbeFailure(
          171, (llvm::Twine(Stage) + ": parser attempt oracle mismatch").str());
    if (LLVMFatalValidatorCallbackState.Calls != CallbackBefore.Calls ||
        LLVMFatalValidatorCallbackState.LastValue != CallbackBefore.LastValue)
      return recoveryProbeFailure(
          145,
          (llvm::Twine(Stage) + ": invalid value reached callback").str());
    return CheckRestored(Stage);
  };
  auto RunInvalidProduction = [&](const std::string &Argument,
                                  llvm::StringRef Stage) -> int {
    const LLVMFatalValidatorCallbackProbe CallbackBefore =
        LLVMFatalValidatorCallbackState;
    const LLVMFatalValidatorCallbackProbe ParseBefore =
        LLVMValidatorParseState;
    if (RunWithLLVMArgument(Argument) != 1)
      return recoveryProbeFailure(
          172,
          (llvm::Twine(Stage) + ": production parser did not return one")
              .str());
    if (LLVMFatalValidatorCallbackState.Calls != CallbackBefore.Calls ||
        LLVMFatalValidatorCallbackState.LastValue != CallbackBefore.LastValue ||
        LLVMValidatorParseState.Calls != ParseBefore.Calls ||
        LLVMValidatorParseState.LastValue != ParseBefore.LastValue)
      return recoveryProbeFailure(
          173, (llvm::Twine(Stage) + ": unrelated validator was invoked").str());
    return CheckRestored(Stage);
  };
  auto RunInvalidProductionResponse = [&](const std::string &ResponseFile,
                                          llvm::StringRef Stage) -> int {
    const LLVMFatalValidatorCallbackProbe CallbackBefore =
        LLVMFatalValidatorCallbackState;
    const LLVMFatalValidatorCallbackProbe ParseBefore =
        LLVMValidatorParseState;
    const std::string Argument = "@" + ResponseFile;
    if (RunWithLLVMArgument(Argument) != 1)
      return recoveryProbeFailure(
          192,
          (llvm::Twine(Stage) + ": response parser did not return one").str());
    if (LLVMFatalValidatorCallbackState.Calls != CallbackBefore.Calls + 1 ||
        LLVMFatalValidatorCallbackState.LastValue != 7 ||
        LLVMValidatorParseState.Calls != ParseBefore.Calls + 1 ||
        LLVMValidatorParseState.LastValue != 7)
      return recoveryProbeFailure(
          193,
          (llvm::Twine(Stage) + ": response expansion marker mismatch").str());
    return CheckRestored(Stage);
  };
  auto RunValidProduction = [&](const std::string &Argument,
                                llvm::StringRef Stage) -> int {
    const LLVMFatalValidatorCallbackProbe CallbackBefore =
        LLVMFatalValidatorCallbackState;
    const LLVMFatalValidatorCallbackProbe ParseBefore =
        LLVMValidatorParseState;
    if (RunWithLLVMArgument(Argument) != 0)
      return recoveryProbeFailure(
          204,
          (llvm::Twine(Stage) + ": valid production parser failed").str());
    if (LLVMFatalValidatorCallbackState.Calls != CallbackBefore.Calls ||
        LLVMFatalValidatorCallbackState.LastValue != CallbackBefore.LastValue ||
        LLVMValidatorParseState.Calls != ParseBefore.Calls ||
        LLVMValidatorParseState.LastValue != ParseBefore.LastValue)
      return recoveryProbeFailure(
          205, (llvm::Twine(Stage) + ": unrelated validator was invoked").str());
    return CheckRestored(Stage);
  };
  auto RunValid = [&](const std::string &Argument,
                      unsigned ExpectedValue,
                      llvm::StringRef Stage) -> int {
    const unsigned CallsBefore = LLVMFatalValidatorCallbackState.Calls;
    if (RunWithLLVMArgument(Argument) != 0)
      return recoveryProbeFailure(
          146, (llvm::Twine(Stage) + ": valid parser retry failed").str());
    if (LLVMFatalValidatorCallbackState.Calls != CallsBefore + 1 ||
        LLVMFatalValidatorCallbackState.LastValue != ExpectedValue)
      return recoveryProbeFailure(
          147, (llvm::Twine(Stage) + ": callback oracle mismatch").str());
    return CheckRestored(Stage);
  };
  auto Selects = [&](LLVMProductionParserCase Candidate) {
    return ProductionCase == LLVMProductionParserCase::All ||
           ProductionCase == Candidate;
  };
  const bool RunsDirect =
      ProductionInvocation != LLVMProductionInvocation::ResponseOnly;
  const bool RunsResponse =
      ProductionInvocation != LLVMProductionInvocation::DirectOnly;

  if (Order == LLVMValidatorOrder::DirectFirst) {
    if (int Error = RunInvalidValidator(DirectInvalidArgument, 3,
                                        "direct invalid"))
      return Error;
    if (int Error = RunValid(FirstValidArgument, 7, "after direct invalid"))
      return Error;
    if (int Error =
            RunInvalidValidator(ResponseInvalidArgument, 5,
                                "response invalid"))
      return Error;
  } else {
    if (int Error =
            RunInvalidValidator(ResponseInvalidArgument, 5,
                                "response invalid"))
      return Error;
    if (int Error =
            RunValid(FirstValidArgument, 7, "after response invalid"))
      return Error;
    if (int Error = RunInvalidValidator(DirectInvalidArgument, 3,
                                        "direct invalid"))
      return Error;
  }
  if (Selects(LLVMProductionParserCase::AArchAlign)) {
    if (RunsDirect)
      if (int Error = RunInvalidProduction(AArchInvalidArgument,
                                           "AArch64 production invalid"))
        return Error;
    if (RunsResponse)
      if (int Error = RunInvalidProductionResponse(
              AArchAlignResponsePath,
              "AArch64 production response invalid"))
        return Error;
    if (int Error = RunValidProduction(
            "-aarch64-code-layout-opt-align-functions=128",
            "AArch64 alignment valid retry"))
      return Error;
  }
  if (Selects(LLVMProductionParserCase::AArchTail)) {
    if (RunsDirect)
      if (int Error = RunInvalidProduction(AArchTailInvalidArgument,
                                           "AArch64 tail-folding invalid"))
        return Error;
    if (RunsResponse)
      if (int Error = RunInvalidProductionResponse(
              AArchTailResponsePath,
              "AArch64 tail-folding response invalid"))
        return Error;
    if (int Error = RunValidProduction(
            "-sve-tail-folding=reductions+noreverse",
            "AArch64 tail-folding flags-only valid retry"))
      return Error;
  }
  if (Selects(LLVMProductionParserCase::PassRemarksPassed)) {
    if (RunsDirect)
      if (int Error = RunInvalidProduction(PassRemarksInvalidArgument,
                                           "pass-remarks invalid regex"))
        return Error;
    if (RunsResponse)
      if (int Error = RunInvalidProductionResponse(
              PassRemarksResponsePath,
              "pass-remarks response invalid regex"))
        return Error;
    if (int Error = RunValidProduction(
            "-pass-remarks=^neverc_temporary_passed_[0-9]+$",
            "pass-remarks valid retry"))
      return Error;
  }
  if (Selects(LLVMProductionParserCase::PassRemarksMissed)) {
    if (RunsDirect)
      if (int Error = RunInvalidProduction(
              PassRemarksMissedInvalidArgument,
              "pass-remarks-missed invalid regex"))
        return Error;
    if (RunsResponse)
      if (int Error = RunInvalidProductionResponse(
              PassRemarksMissedResponsePath,
              "pass-remarks-missed response invalid regex"))
        return Error;
    if (int Error = RunValidProduction(
            "-pass-remarks-missed=^neverc_temporary_missed_[0-9]+$",
            "pass-remarks-missed valid retry"))
      return Error;
  }
  if (Selects(LLVMProductionParserCase::PassRemarksAnalysis)) {
    if (RunsDirect)
      if (int Error = RunInvalidProduction(
              PassRemarksAnalysisInvalidArgument,
              "pass-remarks-analysis invalid regex"))
        return Error;
    if (RunsResponse)
      if (int Error = RunInvalidProductionResponse(
              PassRemarksAnalysisResponsePath,
              "pass-remarks-analysis response invalid regex"))
        return Error;
    if (int Error = RunValidProduction(
            "-pass-remarks-analysis=^neverc_temporary_analysis_[0-9]+$",
            "pass-remarks-analysis valid retry"))
      return Error;
  }
  if (Selects(LLVMProductionParserCase::X86Align)) {
    if (RunsDirect)
      if (int Error = RunInvalidProduction(X86AlignInvalidArgument,
                                           "X86 alignment invalid"))
        return Error;
    if (RunsResponse)
      if (int Error = RunInvalidProductionResponse(
              X86AlignResponsePath, "X86 alignment response invalid"))
        return Error;
    if (int Error = RunValidProduction("-x86-align-branch=fused+ret",
                                       "X86 alignment valid retry"))
      return Error;
    if (int Error = RunValidProduction("-x86-align-branch=",
                                       "X86 empty alignment valid retry"))
      return Error;
  }

  // OptBisect keeps a limit and a progressed pass counter outside cl::opt.
  // Prime its counter before every nested outcome. The second/third decisions
  // distinguish an exact restore at counter one from either reset-to-zero or
  // accidental advancement during the nested frontend.
  llvm::OptPassGate &PassGate = llvm::getGlobalPassGate();
  auto SeedBisect = [&]() -> int {
    OptBisectOption->reset();
    if (OptBisectOption->addOccurrence(
            /*pos=*/107, OptBisectOption->ArgStr, "2"))
      return recoveryProbeFailure(195, "could not seed opt-bisect state");
    if (OptBisectOption->getValue() != 2 ||
        OptBisectOption->getNumOccurrences() != 1 ||
        OptBisectOption->getPosition() != 107 || !PassGate.isEnabled() ||
        !PassGate.shouldRunPass("neverc-bisect-seed", "test module"))
      return recoveryProbeFailure(196, "opt-bisect seed state mismatch");
    return 0;
  };
  auto VerifyBisectRestored = [&](llvm::StringRef Stage) -> int {
    if (OptBisectOption->getValue() != 2 ||
        OptBisectOption->getNumOccurrences() != 1 ||
        OptBisectOption->getPosition() != 107)
      return recoveryProbeFailure(
          199, (llvm::Twine(Stage) +
                ": opt-bisect option metadata was not restored")
                   .str());
    if (!PassGate.isEnabled() ||
        !PassGate.shouldRunPass("neverc-bisect-second", "test module") ||
        PassGate.shouldRunPass("neverc-bisect-third", "test module"))
      return recoveryProbeFailure(
          200,
          (llvm::Twine(Stage) +
           ": opt-bisect counter state was not restored")
              .str());
    return 0;
  };
  auto UnrelatedValidatorMatches = [&](LLVMFatalValidatorCallbackProbe Callback,
                                       LLVMFatalValidatorCallbackProbe Parse) {
    return LLVMFatalValidatorCallbackState.Calls == Callback.Calls &&
           LLVMFatalValidatorCallbackState.LastValue == Callback.LastValue &&
           LLVMValidatorParseState.Calls == Parse.Calls &&
           LLVMValidatorParseState.LastValue == Parse.LastValue;
  };

  if (int Error = SeedBisect())
    return Error;
  if (int Error =
          RunInvalidValidator(DirectInvalidArgument, 3,
                              "opt-bisect direct-error restore"))
    return Error;
  if (int Error = VerifyBisectRestored("direct ordinary error"))
    return Error;

  if (int Error = SeedBisect())
    return Error;
  if (int Error =
          RunInvalidValidator(ResponseInvalidArgument, 5,
                              "opt-bisect response-error restore"))
    return Error;
  if (int Error = VerifyBisectRestored("response ordinary error"))
    return Error;

  if (int Error = SeedBisect())
    return Error;
  const LLVMFatalValidatorCallbackProbe CallbackBeforeBisectSuccess =
      LLVMFatalValidatorCallbackState;
  const LLVMFatalValidatorCallbackProbe ParseBeforeBisectSuccess =
      LLVMValidatorParseState;
  if (RunWithLLVMArgument("-opt-bisect-limit=1000000") != 0)
    return recoveryProbeFailure(197, "opt-bisect nested frontend failed");
  if (!UnrelatedValidatorMatches(CallbackBeforeBisectSuccess,
                                 ParseBeforeBisectSuccess))
    return recoveryProbeFailure(198,
                                "opt-bisect invoked unrelated validator");
  if (int Error = CheckRestored("opt-bisect successful retry"))
    return Error;
  if (int Error = VerifyBisectRestored("successful retry"))
    return Error;

  if (int Error = SeedBisect())
    return Error;
  const LLVMFatalValidatorCallbackProbe CallbackBeforeBisectNoMllvm =
      LLVMFatalValidatorCallbackState;
  const LLVMFatalValidatorCallbackProbe ParseBeforeBisectNoMllvm =
      LLVMValidatorParseState;
  if (RunWithoutLLVMArgument() != 0)
    return recoveryProbeFailure(214, "opt-bisect no-mllvm retry failed");
  if (!UnrelatedValidatorMatches(CallbackBeforeBisectNoMllvm,
                                 ParseBeforeBisectNoMllvm))
    return recoveryProbeFailure(
        215, "opt-bisect no-mllvm retry invoked unrelated validator");
  if (int Error = CheckRestored("opt-bisect no-mllvm retry"))
    return Error;
  if (int Error = VerifyBisectRestored("no-mllvm retry"))
    return Error;

  OptBisectOption->reset();
  if (PassGate.isEnabled())
    return recoveryProbeFailure(201, "opt-bisect reset did not disable gate");

#ifndef NDEBUG
  // DebugOnlyOpt mutates a separate filter vector. The option snapshot must
  // clear it for the nested parse and restore the exact outer filter on exit.
  DebugOnlyOption->reset();
  if (DebugOnlyOption->addOccurrence(
          /*pos=*/109, DebugOnlyOption->ArgStr,
          "neverc-debug-baseline"))
    return recoveryProbeFailure(212, "could not seed debug-only state");
  // Keep the externally stored filter non-empty while DebugFlag is false.
  // This makes a nested -debug-only parse observably different and proves
  // that the two independently registered options are both restored.
  llvm::DebugFlag = false;
  auto CaptureDebugTypes = [] {
    std::vector<std::string> Types;
    for (const llvm::SmallString<32> &Type :
         *llvm::detail::getCurrentDebugType())
      Types.push_back(Type.str().str());
    return Types;
  };
  const bool BaselineDebugFlag = llvm::DebugFlag;
  const std::vector<std::string> BaselineDebugTypeVector =
      CaptureDebugTypes();
  const RawOptionState BaselineDebug = CaptureRawOption(*DebugOption);
  const RawOptionState BaselineDebugOnly =
      CaptureRawOption(*DebugOnlyOption);
  auto CheckDebugRestored = [&] {
    return llvm::DebugFlag == BaselineDebugFlag &&
           CaptureDebugTypes() == BaselineDebugTypeVector &&
           DebugOption->getNumOccurrences() == BaselineDebug.Occurrences &&
           DebugOption->getPosition() == BaselineDebug.Position &&
           DebugOnlyOption->getNumOccurrences() ==
               BaselineDebugOnly.Occurrences &&
           DebugOnlyOption->getPosition() == BaselineDebugOnly.Position;
  };
  if (RunWithLLVMArgument("-debug-only=neverc-debug-temporary") != 0)
    return recoveryProbeFailure(202, "debug-only nested frontend failed");
  if (!CheckDebugRestored())
    return recoveryProbeFailure(203,
                                "debug-only filter state was not restored");
  if (RunWithLLVMArgument(
          "-aarch64-code-layout-opt-align-functions=128") != 0)
    return recoveryProbeFailure(
        216, "debug-only unrelated-mllvm retry failed");
  if (!CheckDebugRestored())
    return recoveryProbeFailure(
        217, "debug-only state changed without an explicit debug option");
#endif

  if (int Error = RunValid(SecondValidArgument, 9, "after all invalid values"))
    return Error;
  const LLVMFatalValidatorCallbackProbe CallbackBeforeNoMllvm =
      LLVMFatalValidatorCallbackState;
  if (RunWithoutLLVMArgument() != 0)
    return recoveryProbeFailure(148, "fresh no-mllvm frontend retry failed");
  if (LLVMFatalValidatorCallbackState.Calls != CallbackBeforeNoMllvm.Calls ||
      LLVMFatalValidatorCallbackState.LastValue !=
          CallbackBeforeNoMllvm.LastValue)
    return recoveryProbeFailure(149,
                                "no-mllvm retry invoked validator callback");
  if (int Error = CheckRestored("after no-mllvm retry"))
    return Error;
  {
    llvm::Timer PostRecovery("neverc-mllvm-validator-post-recovery",
                             "NeverC mllvm validator post recovery");
  }
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(150,
                                "post-recovery timer registry changed");

  return 0;
}


} // namespace

namespace {
void expectProductionParserRejectsDirectAndResponse(
    LLVMProductionParserCase ProductionCase,
    llvm::StringRef InvalidArgumentOverride = {}) {
  EXPECT_EXIT(
      std::exit(runLLVMValidatorRejectionProbe(
          LLVMValidatorOrder::DirectFirst, ProductionCase,
          LLVMProductionInvocation::DirectOnly, InvalidArgumentOverride)),
      ::testing::ExitedWithCode(0), "");
  EXPECT_EXIT(
      std::exit(runLLVMValidatorRejectionProbe(
          LLVMValidatorOrder::DirectFirst, ProductionCase,
          LLVMProductionInvocation::ResponseOnly, InvalidArgumentOverride)),
      ::testing::ExitedWithCode(0), "");
}
} // namespace

TEST(PluginLLVMOptionOrdinaryErrorTest,
     ParallelSafeUnknownMllvmReturnsWithoutTerminating) {
  EXPECT_EXIT(
      std::exit(runLLVMOptionParseFailureProbe(LLVMOptionFailureKind::Unknown)),
      ::testing::ExitedWithCode(0),
      "frontend LLVM option rejection recovered");
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     GroupedLLVMHelpCannotTerminateFrontend) {
  EXPECT_EXIT(std::exit(runLLVMOptionParseFailureProbe(
                  LLVMOptionFailureKind::GroupedHelp)),
              ::testing::ExitedWithCode(0),
              "frontend LLVM option rejection recovered");
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     LLVMControlOptionInResponseFileCannotTerminateFrontend) {
  EXPECT_EXIT(std::exit(runLLVMOptionParseFailureProbe(
                  LLVMOptionFailureKind::ResponseVersion)),
              ::testing::ExitedWithCode(0),
              "frontend LLVM option rejection recovered");
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     LLVMInvalidValidatorDirectThenResponseRestoresEveryParseSnapshot) {
  EXPECT_EQ(runLLVMValidatorRejectionProbe(LLVMValidatorOrder::DirectFirst),
            0);
  EXPECT_FALSE(
      neverc::plugin::pluginLLVMOptionGateHeldSharedByCurrentThread());
  EXPECT_FALSE(
      neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     LLVMInvalidValidatorResponseThenDirectRestoresEveryParseSnapshot) {
  EXPECT_EQ(runLLVMValidatorRejectionProbe(LLVMValidatorOrder::ResponseFirst),
            0);
  EXPECT_FALSE(
      neverc::plugin::pluginLLVMOptionGateHeldSharedByCurrentThread());
  EXPECT_FALSE(
      neverc::plugin::pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     AArch64TailFoldingInvalidDirectAndResponseReturnNormally) {
  expectProductionParserRejectsDirectAndResponse(
      LLVMProductionParserCase::AArchTail);
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     AArch64TailFoldingMalformedSeparatorsReturnNormally) {
  for (llvm::StringRef InvalidArgument : {
           "-sve-tail-folding=",
           "-sve-tail-folding=+all",
           "-sve-tail-folding=all+",
           "-sve-tail-folding=simple++reverse",
       }) {
    SCOPED_TRACE(InvalidArgument.str());
    expectProductionParserRejectsDirectAndResponse(
        LLVMProductionParserCase::AArchTail, InvalidArgument);
  }
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     PassedRemarkInvalidRegexDirectAndResponseReturnNormally) {
  expectProductionParserRejectsDirectAndResponse(
      LLVMProductionParserCase::PassRemarksPassed);
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     MissedRemarkInvalidRegexDirectAndResponseReturnNormally) {
  expectProductionParserRejectsDirectAndResponse(
      LLVMProductionParserCase::PassRemarksMissed);
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     AnalysisRemarkInvalidRegexDirectAndResponseReturnNormally) {
  expectProductionParserRejectsDirectAndResponse(
      LLVMProductionParserCase::PassRemarksAnalysis);
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     X86BranchAlignmentInvalidDirectAndResponseReturnNormally) {
  expectProductionParserRejectsDirectAndResponse(
      LLVMProductionParserCase::X86Align);
}

TEST(PluginLLVMOptionOrdinaryErrorTest,
     X86BranchAlignmentMalformedSeparatorsReturnNormally) {
  for (llvm::StringRef InvalidArgument : {
           "-x86-align-branch=+",
           "-x86-align-branch=jcc+",
           "-x86-align-branch=jcc++ret",
       }) {
    SCOPED_TRACE(InvalidArgument.str());
    expectProductionParserRejectsDirectAndResponse(
        LLVMProductionParserCase::X86Align, InvalidArgument);
  }
}
