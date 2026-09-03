#include "neverc/Compiler/FrontendTool.h"
#include "neverc/Foundation/Core/LLVMTimeTraceRootLease.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Invoke/DirectInvocationOpts.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/IRGenProvider.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/PluginAssemblyPipeline.h"
#include "neverc/Plugin/Host/PluginCodeGenPipeline.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "gtest/gtest.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace linker;

namespace {

constexpr llvm::StringLiteral BusyDiagnostic =
    "neverc: error: cannot start linker time trace: another traced "
    "in-process link is already active\n";
constexpr llvm::StringLiteral LinkerMarker =
    "neverc.test.frontend-linker-interop";

template <typename T>
llvm::cl::opt<T> *findTopLevelOption(llvm::StringRef Name) {
  const auto &Options = llvm::cl::getRegisteredOptions();
  auto It = Options.find(Name);
  return It == Options.end() ? nullptr
                             : static_cast<llvm::cl::opt<T> *>(It->second);
}

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
  auto *DumpDirectory = findTopLevelOption<std::string>("ir-dump-directory");
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

testing::AssertionResult traceHasMarker(llvm::StringRef Path,
                                        llvm::StringRef Marker) {
  auto Buffer = llvm::MemoryBuffer::getFile(Path);
  if (!Buffer)
    return testing::AssertionFailure() << Buffer.getError().message();
  if ((*Buffer)->getBuffer().empty())
    return testing::AssertionFailure() << "empty time trace";
  auto Parsed = llvm::json::parse((*Buffer)->getBuffer());
  if (!Parsed)
    return testing::AssertionFailure()
           << llvm::toString(Parsed.takeError()).str().str();
  const llvm::json::Object *Root = Parsed->getAsObject();
  const llvm::json::Array *Events =
      Root ? Root->getArray("traceEvents") : nullptr;
  if (!Events)
    return testing::AssertionFailure() << "missing traceEvents array";
  unsigned Count = 0;
  for (const llvm::json::Value &Value : *Events) {
    const llvm::json::Object *Event = Value.getAsObject();
    if (Event && Event->getString("ph") == "X" &&
        Event->getString("name") == Marker)
      ++Count;
  }
  if (Count != 1)
    return testing::AssertionFailure() << "marker count=" << Count;
  return testing::AssertionSuccess();
}

bool markerBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                   llvm::raw_ostream &, bool, bool,
                   const LinkerDriverConfig &) {
  llvm::TimeTraceScope MarkerScope(LinkerMarker);
  return true;
}

int runTracedLink(llvm::StringRef Output, std::string &Stdout,
                  std::string &Stderr) {
  LinkerDriverConfig Config;
  Config.outputFile = Output.str();
  Config.timeTraceEnabled = true;
  Config.timeTraceGranularity = 0;
  const DriverDef Drivers[] = {{Flavor::Gnu, markerBackend}};
  const char *Args[] = {"neverc-test-linker"};
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  return dispatchLink(Drivers, Flavor::Gnu, Args, StdoutStream, StderrStream,
                      Config);
}

void *frontendMainAddress() {
  return reinterpret_cast<void *>(
      reinterpret_cast<std::uintptr_t>(&frontendMainAddress));
}

bool initializeNativeCodegen() {
  static const bool Initialized = !llvm::InitializeNativeTarget() &&
                                  !llvm::InitializeNativeTargetAsmPrinter();
  return Initialized;
}

int recoveryProbeFailure(int Code, llvm::StringRef Message) {
  llvm::errs() << "frontend fatal recovery probe " << Code << ": " << Message
               << '\n';
  return Code;
}

struct AmbientFatalHandlerProbe {
  unsigned Calls = 0;
};

void ambientFatalHandler(void *UserData, const char *, bool) {
  auto &Probe = *static_cast<AmbientFatalHandlerProbe *>(UserData);
  ++Probe.Calls;
  if (llvm::CrashRecoveryContext *CRC =
          llvm::CrashRecoveryContext::GetCurrent())
    CRC->HandleExit(191);
  std::_Exit(191);
}

struct RoutedFatalHandlerProbe {
  int RecoveryCode = 0;
  unsigned Calls = 0;
};

void routedFatalHandler(void *UserData, const char *, bool) {
  auto &Probe = *static_cast<RoutedFatalHandlerProbe *>(UserData);
  ++Probe.Calls;
  if (llvm::CrashRecoveryContext *CRC =
          llvm::CrashRecoveryContext::GetCurrent())
    CRC->HandleExit(Probe.RecoveryCode);
  std::_Exit(Probe.RecoveryCode);
}

class CountingCrashCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<CountingCrashCleanup,
                                                   unsigned> {
public:
  CountingCrashCleanup(llvm::CrashRecoveryContext *Context,
                       unsigned *Recoveries)
      : llvm::CrashRecoveryContextCleanupBase<CountingCrashCleanup, unsigned>(
            Context, Recoveries) {}

  void recoverResources() override { ++*this->resource; }
};

using CountingCrashCleanupRegistrar =
    llvm::CrashRecoveryContextCleanupRegistrar<unsigned, CountingCrashCleanup>;

struct ObservedCleanupState {
  unsigned Recoveries = 0;
  unsigned Destructions = 0;
};

class ObservedCrashCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<ObservedCrashCleanup,
                                                   ObservedCleanupState> {
public:
  ObservedCrashCleanup(llvm::CrashRecoveryContext *Context,
                       ObservedCleanupState *State)
      : llvm::CrashRecoveryContextCleanupBase<ObservedCrashCleanup,
                                              ObservedCleanupState>(Context,
                                                                    State) {}

  ~ObservedCrashCleanup() override { ++this->resource->Destructions; }
  void recoverResources() override {
    ++this->resource->Recoveries;
    // Direct unregister is also fail-safe after the node has fired; ownership
    // remains with the CRC's retired list until all callbacks have completed.
    this->getContext()->unregisterCleanup(this);
  }
};

using ObservedCrashCleanupRegistrar =
    llvm::CrashRecoveryContextCleanupRegistrar<ObservedCleanupState,
                                               ObservedCrashCleanup>;

class FiredCleanupRegistrarOwner {
public:
  FiredCleanupRegistrarOwner(ObservedCleanupState &InnerState,
                             bool &DestroyedBeforeOwner,
                             unsigned &Destructions)
      : InnerState(InnerState), DestroyedBeforeOwner(DestroyedBeforeOwner),
        Destructions(Destructions) {}

  ~FiredCleanupRegistrarOwner() {
    DestroyedBeforeOwner = InnerState.Destructions != 0;
    ++Destructions;
  }

  void registerInner() { Inner.emplace(&InnerState); }

private:
  ObservedCleanupState &InnerState;
  bool &DestroyedBeforeOwner;
  unsigned &Destructions;
  std::optional<ObservedCrashCleanupRegistrar> Inner;
};

int runFiredCleanupRegistrarRetirementProbe() {
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  ObservedCleanupState InnerState;
  bool DestroyedBeforeOwner = false;
  unsigned OwnerDestructions = 0;
  bool CompletedNormally = true;
  int RecoveryCode = 0;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      auto *Owner = new FiredCleanupRegistrarOwner(
          InnerState, DestroyedBeforeOwner, OwnerDestructions);
      llvm::CrashRecoveryContextCleanupRegistrar<FiredCleanupRegistrarOwner>
          OwnerCleanup(Owner);
      // Register this after the owner so it fires first. Deleting Owner later
      // destroys a registrar that must still be able to observe this fired
      // cleanup without reading freed storage.
      Owner->registerInner();
      CRC.HandleExit(237);
    });
    RecoveryCode = CRC.RetCode;
  }

  if (CompletedNormally || RecoveryCode != 237)
    return recoveryProbeFailure(237, "cleanup retirement recovery contract");
  if (InnerState.Recoveries != 1 || InnerState.Destructions != 1 ||
      DestroyedBeforeOwner || OwnerDestructions != 1)
    return recoveryProbeFailure(238, "fired cleanup registrar lifetime");
  return 0;
}

class ActiveCleanupRegistrarOwner {
public:
  ActiveCleanupRegistrarOwner(llvm::CrashRecoveryContext &Context,
                              unsigned &VictimRecoveries,
                              unsigned &RegisteredDuringRecovery,
                              unsigned &Destructions)
      : Context(Context), RegisteredDuringRecovery(RegisteredDuringRecovery),
        Destructions(Destructions),
        Victim(std::make_unique<CountingCrashCleanupRegistrar>(
            &VictimRecoveries)) {}

  ~ActiveCleanupRegistrarOwner() {
    ++Destructions;
    // GetCurrent() is intentionally no longer this context after RunSafely
    // transfers control. Register directly through the cleanup's owning CRC;
    // its destructor must re-read head and process this new node too.
    Context.registerCleanup(
        new CountingCrashCleanup(&Context, &RegisteredDuringRecovery));
  }

private:
  llvm::CrashRecoveryContext &Context;
  unsigned &RegisteredDuringRecovery;
  unsigned &Destructions;
  std::unique_ptr<CountingCrashCleanupRegistrar> Victim;
};

int runCleanupHeadMutationProbe() {
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  unsigned VictimRecoveries = 0;
  unsigned RegisteredDuringRecovery = 0;
  unsigned OwnerDestructions = 0;
  bool CompletedNormally = true;
  int RecoveryCode = 0;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      // Victim is registered first. The later owner cleanup deletes its
      // registrar, removing Victim from the active list during recovery, and
      // registers a replacement cleanup that must join the same recovery.
      auto *Owner = new ActiveCleanupRegistrarOwner(
          CRC, VictimRecoveries, RegisteredDuringRecovery, OwnerDestructions);
      llvm::CrashRecoveryContextCleanupRegistrar<ActiveCleanupRegistrarOwner>
          OwnerCleanup(Owner);
      CRC.HandleExit(239);
    });
    RecoveryCode = CRC.RetCode;
  }

  if (CompletedNormally || RecoveryCode != 239)
    return recoveryProbeFailure(239, "cleanup head mutation recovery contract");
  if (OwnerDestructions != 1 || VictimRecoveries != 0 ||
      RegisteredDuringRecovery != 1)
    return recoveryProbeFailure(240, "mutated cleanup list was not honored");
  return 0;
}

int runThreadLocalFatalHandlerNestingProbe() {
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(218, "ambient fatal-handler state");

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  AmbientFatalHandlerProbe Ambient;
  llvm::install_fatal_error_handler(ambientFatalHandler, &Ambient);
  bool AmbientInstalled = true;
  auto RemoveAmbient = llvm::make_scope_exit([&] {
    if (AmbientInstalled)
      llvm::remove_fatal_error_handler();
  });
  if (!llvm::has_fatal_error_handler() ||
      llvm::has_thread_local_fatal_error_handler())
    return recoveryProbeFailure(219, "global fatal-handler visibility");

  auto RouteFatal = [](int ExpectedCode) {
    llvm::CrashRecoveryContext CRC;
    const bool Completed = CRC.RunSafely(
        [] { llvm::report_fatal_error("neverc routed fatal probe"); });
    return !Completed && CRC.RetCode == ExpectedCode;
  };

  RoutedFatalHandlerProbe OuterProbe{220};
  llvm::ScopedThreadLocalFatalErrorHandler Outer(routedFatalHandler,
                                                 &OuterProbe);
  if (!llvm::has_thread_local_fatal_error_handler())
    return recoveryProbeFailure(220, "outer handler was not visible");

  using Handler = llvm::ScopedThreadLocalFatalErrorHandler;
  alignas(Handler) std::byte HandlerStorage[sizeof(Handler)];
  RoutedFatalHandlerProbe FirstInnerProbe{221};
  Handler *Inner =
      new (HandlerStorage) Handler(routedFatalHandler, &FirstInnerProbe);
  const Handler::ResetToken StaleReset = Inner->resetToken();
  if (!RouteFatal(FirstInnerProbe.RecoveryCode))
    return recoveryProbeFailure(221, "inner handler was not selected");
  Inner->~Handler();
  if (!RouteFatal(OuterProbe.RecoveryCode))
    return recoveryProbeFailure(222, "outer handler was not restored");

  RoutedFatalHandlerProbe ReusedInnerProbe{223};
  Inner = new (HandlerStorage) Handler(routedFatalHandler, &ReusedInnerProbe);
  Handler::reset(StaleReset);
  if (!RouteFatal(ReusedInnerProbe.RecoveryCode))
    return recoveryProbeFailure(223, "stale reset removed a new generation");
  Inner->~Handler();
  if (!RouteFatal(OuterProbe.RecoveryCode))
    return recoveryProbeFailure(224, "outer handler changed after reuse");

  Outer.reset();
  if (llvm::has_thread_local_fatal_error_handler() ||
      !RouteFatal(/*ExpectedCode=*/191))
    return recoveryProbeFailure(225, "global fallback was not restored");
  if (Ambient.Calls != 1 || OuterProbe.Calls != 2 ||
      FirstInnerProbe.Calls != 1 || ReusedInnerProbe.Calls != 1)
    return recoveryProbeFailure(226, "fatal-handler routing count changed");

  llvm::remove_fatal_error_handler();
  AmbientInstalled = false;
  if (llvm::has_fatal_error_handler())
    return recoveryProbeFailure(227, "fatal handler remained installed");
  return 0;
}

int runConcurrentThreadLocalFatalHandlersProbe() {
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(228, "ambient fatal-handler state");

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  AmbientFatalHandlerProbe Ambient;
  llvm::install_fatal_error_handler(ambientFatalHandler, &Ambient);
  auto RemoveAmbient =
      llvm::make_scope_exit([] { llvm::remove_fatal_error_handler(); });

  struct WorkerResult {
    int RecoveryCode = 0;
    unsigned Calls = 0;
    bool CompletedNormally = true;
    bool RestoredAmbient = false;
  } Results[2];
  std::mutex ReadyMutex;
  std::condition_variable ReadyCondition;
  unsigned ReadyCount = 0;
  bool Start = false;

  auto RunWorker = [&](unsigned Index, int ExpectedCode) {
    RoutedFatalHandlerProbe Route{ExpectedCode};
    llvm::ScopedThreadLocalFatalErrorHandler Handler(routedFatalHandler,
                                                     &Route);
    {
      std::unique_lock<std::mutex> Lock(ReadyMutex);
      ++ReadyCount;
      ReadyCondition.notify_all();
      ReadyCondition.wait(Lock, [&] { return Start; });
    }

    llvm::CrashRecoveryContext CRC;
    Results[Index].CompletedNormally = CRC.RunSafely(
        [] { llvm::report_fatal_error("neverc concurrent TLS fatal probe"); });
    Results[Index].RecoveryCode = CRC.RetCode;
    Results[Index].Calls = Route.Calls;
    Handler.reset();
    Results[Index].RestoredAmbient =
        !llvm::has_thread_local_fatal_error_handler() &&
        llvm::has_fatal_error_handler();
  };

  std::thread First(RunWorker, 0u, 229);
  std::thread Second(RunWorker, 1u, 230);
  auto JoinWorkers = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(ReadyMutex);
      Start = true;
    }
    ReadyCondition.notify_all();
    if (First.joinable())
      First.join();
    if (Second.joinable())
      Second.join();
  });
  {
    std::unique_lock<std::mutex> Lock(ReadyMutex);
    if (!ReadyCondition.wait_for(Lock, std::chrono::seconds(10),
                                 [&] { return ReadyCount == 2; }))
      return recoveryProbeFailure(229, "TLS fatal workers did not rendezvous");
    Start = true;
  }
  ReadyCondition.notify_all();
  First.join();
  Second.join();
  JoinWorkers.release();

  for (unsigned Index = 0; Index != 2; ++Index) {
    const int ExpectedCode = Index == 0 ? 229 : 230;
    if (Results[Index].CompletedNormally ||
        Results[Index].RecoveryCode != ExpectedCode ||
        Results[Index].Calls != 1 || !Results[Index].RestoredAmbient)
      return recoveryProbeFailure(230 + Index,
                                  "concurrent TLS fatal routing changed");
  }
  if (Ambient.Calls != 0 || llvm::has_thread_local_fatal_error_handler() ||
      !llvm::has_fatal_error_handler())
    return recoveryProbeFailure(232, "ambient fatal handler was disturbed");
  return 0;
}

unsigned countTextOccurrences(llvm::StringRef Text, llvm::StringRef Needle) {
  unsigned Count = 0;
  while (!Text.empty()) {
    const std::size_t Position = Text.find(Needle);
    if (Position == llvm::StringRef::npos)
      break;
    ++Count;
    Text = Text.drop_front(Position + Needle.size());
  }
  return Count;
}

constexpr int PassBodyFatalRecoveryCode = 109;
constexpr llvm::StringLiteral PassBodyFatalTimerName =
    "NevercPassBodyFatalTimerPass";
constexpr llvm::StringLiteral PassBodyFreshTimerName =
    "NevercPassBodyFreshTimerPass";
constexpr int AnalysisFatalRecoveryCode = 110;
constexpr llvm::StringLiteral AnalysisFatalTimerName =
    "NevercAnalysisFatalTimerRecord";
constexpr llvm::StringLiteral AnalysisFreshTimerName =
    "NevercAnalysisFreshTimerRecord";
constexpr int InvalidatedPrintIRRecoveryCode = 111;

class NevercInvalidatedPrintIRPass
    : public llvm::PassInfoMixin<NevercInvalidatedPrintIRPass> {
public:
  static bool isRequired() { return true; }
};

int runInvalidatedPrintIRFatalProbe() {
  auto *PrintBefore = findTopLevelOption<bool>("print-before-all");
  auto *PrintAfter = findTopLevelOption<bool>("print-after-all");
  auto *DumpDirectory = findTopLevelOption<std::string>("ir-dump-directory");
  if (!PrintBefore || !PrintAfter || !DumpDirectory)
    return recoveryProbeFailure(111, "missing PrintIR options");

  llvm::SmallString<128> DumpBlocker;
  if (llvm::sys::fs::createTemporaryFile("neverc-invalidated-ir-dump-blocker",
                                         "file", DumpBlocker))
    return recoveryProbeFailure(112, "could not create dump blocker");
  llvm::FileRemover RemoveDumpBlocker(DumpBlocker);
  const std::string DumpRoot = DumpBlocker.str().str() + "/child";

  // This helper runs only inside EXPECT_EXIT. Keep these process-global option
  // mutations child-local; an in-process caller would need the option gate and
  // an exact value/occurrence snapshot.
  *PrintBefore = false;
  *PrintAfter = true;
  *DumpDirectory = DumpRoot;

  llvm::LLVMContext Context;
  llvm::Module Module("neverc-invalidated-print-ir-module", Context);
  llvm::PrintIRInstrumentation Printer;
  llvm::PassInstrumentationCallbacks Callbacks;
  Printer.registerCallbacks(Callbacks);
  llvm::PassInstrumentation Instrumentation(&Callbacks);
  NevercInvalidatedPrintIRPass ProbePass;

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  RoutedFatalHandlerProbe FatalProbe{InvalidatedPrintIRRecoveryCode};
  llvm::ScopedThreadLocalFatalErrorHandler FatalHandler(routedFatalHandler,
                                                        &FatalProbe);

  bool CompletedNormally = false;
  int RecoveryCode = 0;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      if (!Instrumentation.runBeforePass<llvm::Module>(ProbePass, Module))
        CRC.HandleExit(113);
      Instrumentation.runAfterPassInvalidated<llvm::Module>(
          ProbePass, llvm::PreservedAnalyses::none());
    });
    RecoveryCode = CRC.RetCode;
  }

  if (CompletedNormally || RecoveryCode != InvalidatedPrintIRRecoveryCode ||
      FatalProbe.Calls != 1)
    return recoveryProbeFailure(114, "invalidated PrintIR recovery changed");
  return 0;
}

class NevercPassBodyFatalTimerPass
    : public llvm::PassInfoMixin<NevercPassBodyFatalTimerPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &,
                              llvm::ModuleAnalysisManager &) {
    llvm::CrashRecoveryContext *CRC =
        llvm::CrashRecoveryContext::GetCurrent();
    if (!CRC)
      llvm::report_fatal_error(
          "pass-body timer probe requires an active recovery context");
    CRC->HandleExit(PassBodyFatalRecoveryCode);
  }

  static bool isRequired() { return true; }
};

class NevercPassBodyFreshTimerPass
    : public llvm::PassInfoMixin<NevercPassBodyFreshTimerPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &,
                              llvm::ModuleAnalysisManager &) {
    return llvm::PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};

class NevercAnalysisFatalTimerAnalysis
    : public llvm::AnalysisInfoMixin<NevercAnalysisFatalTimerAnalysis> {
  friend llvm::AnalysisInfoMixin<NevercAnalysisFatalTimerAnalysis>;
  static llvm::AnalysisKey Key;

public:
  struct Result {};

  Result run(llvm::Module &, llvm::ModuleAnalysisManager &) {
    llvm::CrashRecoveryContext *CRC =
        llvm::CrashRecoveryContext::GetCurrent();
    if (!CRC)
      llvm::report_fatal_error(
          "analysis timer probe requires an active recovery context");
    CRC->HandleExit(AnalysisFatalRecoveryCode);
  }

  static llvm::StringRef name() { return AnalysisFatalTimerName; }
};

llvm::AnalysisKey NevercAnalysisFatalTimerAnalysis::Key;

class NevercAnalysisFreshTimerAnalysis
    : public llvm::AnalysisInfoMixin<NevercAnalysisFreshTimerAnalysis> {
  friend llvm::AnalysisInfoMixin<NevercAnalysisFreshTimerAnalysis>;
  static llvm::AnalysisKey Key;

public:
  struct Result {};

  Result run(llvm::Module &, llvm::ModuleAnalysisManager &) { return {}; }

  static llvm::StringRef name() { return AnalysisFreshTimerName; }
};

llvm::AnalysisKey NevercAnalysisFreshTimerAnalysis::Key;

template <typename PassT> class TimedModulePassExecution {
public:
  TimedModulePassExecution(llvm::LLVMContext &Context,
                           llvm::raw_ostream &TimingStream)
      : SI(Context, /*DebugLogging=*/false, /*VerifyEach=*/false) {
    MAM.registerPass(
        [&] { return llvm::PassInstrumentationAnalysis(&PIC); });
    SI.getTimePasses().setOutStream(TimingStream);
    SI.getTimePasses().registerCallbacks(PIC);
    MPM.addPass(PassT());
  }

  void run(llvm::Module &Module) { MPM.run(Module, MAM); }

private:
  // Reverse destruction is deliberate: pass storage and analyses disappear
  // before callbacks, while the timing instrumentation remains alive to close
  // and report any interval abandoned by a recovery transfer.
  llvm::StandardInstrumentations SI;
  llvm::PassInstrumentationCallbacks PIC;
  llvm::ModuleAnalysisManager MAM;
  llvm::ModulePassManager MPM;
};

template <typename AnalysisT> class TimedModuleAnalysisExecution {
public:
  TimedModuleAnalysisExecution(llvm::LLVMContext &Context,
                               llvm::raw_ostream &TimingStream)
      : SI(Context, /*DebugLogging=*/false, /*VerifyEach=*/false) {
    MAM.registerPass(
        [&] { return llvm::PassInstrumentationAnalysis(&PIC); });
    MAM.registerPass([] { return AnalysisT(); });
    SI.getTimePasses().setOutStream(TimingStream);
    SI.getTimePasses().registerCallbacks(PIC);
  }

  void run(llvm::Module &Module) {
    (void)MAM.getResult<AnalysisT>(Module);
  }

private:
  // The instrumentation outlives the manager that can abandon an active
  // analysis callback. Direct getResult keeps this probe independent from the
  // pass-timer stack covered by NevercPassBodyFatalTimerPass above.
  llvm::StandardInstrumentations SI;
  llvm::PassInstrumentationCallbacks PIC;
  llvm::ModuleAnalysisManager MAM;
};

int runPassBodyFatalTimingRecoveryProbe() {
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(156, "ambient LLVM fatal handler");

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery = llvm::make_scope_exit(
      [] { llvm::CrashRecoveryContext::Disable(); });

  const bool SavedTimePasses = llvm::TimePassesIsEnabled;
  const bool SavedTimePassesPerRun = llvm::TimePassesPerRun;
  auto RestoreTimePasses = llvm::make_scope_exit([&] {
    llvm::TimePassesIsEnabled = SavedTimePasses;
    llvm::TimePassesPerRun = SavedTimePassesPerRun;
  });
  llvm::TimePassesIsEnabled = true;
  llvm::TimePassesPerRun = false;

  llvm::SmallString<128> StrayReportPath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-pass-body-fatal-stray", "txt", StrayReportPath))
    return recoveryProbeFailure(157, "could not create stray-report path");
  llvm::FileRemover RemoveStrayReport(StrayReportPath);
  if (std::error_code Error = llvm::sys::fs::remove(StrayReportPath))
    return recoveryProbeFailure(158, Error.message());

  llvm::SmallString<256> SavedInfoOutput(
      llvm::timer_detail::getLibSupportInfoOutputFilename());
  auto RestoreInfoOutput = llvm::make_scope_exit([&] {
    llvm::timer_detail::getLibSupportInfoOutputFilename() = SavedInfoOutput;
  });
  llvm::timer_detail::getLibSupportInfoOutputFilename() = StrayReportPath;

  // TimePassesHandler's first nonempty report compares each private group to
  // the default group, initializing that process-lifetime group lazily. Make
  // it part of the baseline rather than mistaking the lazy initialization for
  // a crash-cleanup leak.
  (void)llvm::timer_detail::getDefaultTimerGroup();
  llvm::TimerGroup *const BaselineTimerGroups =
      llvm::timer_detail::TimerGroupList;
  llvm::LLVMContext FatalContext;
  llvm::Module FatalModule("neverc-pass-body-fatal-timer", FatalContext);
  std::string FatalTimingReport;
  llvm::raw_string_ostream FatalTimingStream(FatalTimingReport);
  bool CompletedNormally = false;
  int RecoveryCode = 0;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      auto Execution =
          std::make_unique<TimedModulePassExecution<
              NevercPassBodyFatalTimerPass>>(FatalContext, FatalTimingStream);
      llvm::CrashRecoveryContextCleanupRegistrar<
          TimedModulePassExecution<NevercPassBodyFatalTimerPass>>
          Cleanup(Execution.get());
      Execution->run(FatalModule);
    });
    RecoveryCode = CRC.RetCode;
  }
  FatalTimingStream.flush();
  if (CompletedNormally || RecoveryCode != PassBodyFatalRecoveryCode)
    return recoveryProbeFailure(159, "pass-body fatal recovery code changed");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(160, "pass timing registry was not restored");
  if (countTextOccurrences(FatalTimingReport, PassBodyFatalTimerName) != 1)
    return recoveryProbeFailure(161,
                                "fatal pass timing record count changed");
  if (llvm::sys::fs::exists(StrayReportPath))
    return recoveryProbeFailure(162,
                                "fatal pass emitted a second timing report");

  llvm::LLVMContext FreshContext;
  llvm::Module FreshModule("neverc-pass-body-fresh-timer", FreshContext);
  std::string FreshTimingReport;
  llvm::raw_string_ostream FreshTimingStream(FreshTimingReport);
  {
    TimedModulePassExecution<NevercPassBodyFreshTimerPass> Execution(
        FreshContext, FreshTimingStream);
    Execution.run(FreshModule);
  }
  FreshTimingStream.flush();
  if (countTextOccurrences(FreshTimingReport, PassBodyFreshTimerName) != 1)
    return recoveryProbeFailure(163, "fresh pass timing record count changed");
  if (llvm::sys::fs::exists(StrayReportPath))
    return recoveryProbeFailure(164, "fresh pass emitted a stray report");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(165,
                                "fresh pass changed the timer group registry");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(166, "fresh pass retained a fatal handler");
  return 0;
}

int runAnalysisFatalTimingRecoveryProbe() {
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(222, "ambient LLVM fatal handler");

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery = llvm::make_scope_exit(
      [] { llvm::CrashRecoveryContext::Disable(); });

  const bool SavedTimePasses = llvm::TimePassesIsEnabled;
  const bool SavedTimePassesPerRun = llvm::TimePassesPerRun;
  auto RestoreTimePasses = llvm::make_scope_exit([&] {
    llvm::TimePassesIsEnabled = SavedTimePasses;
    llvm::TimePassesPerRun = SavedTimePassesPerRun;
  });
  llvm::TimePassesIsEnabled = true;
  llvm::TimePassesPerRun = false;

  llvm::SmallString<128> StrayReportPath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-analysis-fatal-stray", "txt", StrayReportPath))
    return recoveryProbeFailure(223, "could not create stray-report path");
  llvm::FileRemover RemoveStrayReport(StrayReportPath);
  if (std::error_code Error = llvm::sys::fs::remove(StrayReportPath))
    return recoveryProbeFailure(224, Error.message());

  llvm::SmallString<256> SavedInfoOutput(
      llvm::timer_detail::getLibSupportInfoOutputFilename());
  auto RestoreInfoOutput = llvm::make_scope_exit([&] {
    llvm::timer_detail::getLibSupportInfoOutputFilename() = SavedInfoOutput;
  });
  llvm::timer_detail::getLibSupportInfoOutputFilename() = StrayReportPath;

  // See the pass-timing probe above: reporting lazily initializes the
  // process-lifetime default group on first use.
  (void)llvm::timer_detail::getDefaultTimerGroup();
  llvm::TimerGroup *const BaselineTimerGroups =
      llvm::timer_detail::TimerGroupList;
  llvm::LLVMContext FatalContext;
  llvm::Module FatalModule("neverc-analysis-fatal-timer", FatalContext);
  std::string FatalTimingReport;
  llvm::raw_string_ostream FatalTimingStream(FatalTimingReport);
  bool CompletedNormally = false;
  int RecoveryCode = 0;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      auto Execution =
          std::make_unique<TimedModuleAnalysisExecution<
              NevercAnalysisFatalTimerAnalysis>>(FatalContext,
                                                  FatalTimingStream);
      llvm::CrashRecoveryContextCleanupRegistrar<
          TimedModuleAnalysisExecution<NevercAnalysisFatalTimerAnalysis>>
          Cleanup(Execution.get());
      Execution->run(FatalModule);
    });
    RecoveryCode = CRC.RetCode;
  }
  FatalTimingStream.flush();
  if (CompletedNormally || RecoveryCode != AnalysisFatalRecoveryCode)
    return recoveryProbeFailure(225, "analysis fatal recovery code changed");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(226,
                                "analysis timing registry was not restored");
  if (countTextOccurrences(FatalTimingReport, AnalysisFatalTimerName) != 1)
    return recoveryProbeFailure(227,
                                "fatal analysis timing record count changed");
  if (llvm::sys::fs::exists(StrayReportPath))
    return recoveryProbeFailure(228,
                                "fatal analysis emitted a second report");

  llvm::LLVMContext FreshContext;
  llvm::Module FreshModule("neverc-analysis-fresh-timer", FreshContext);
  std::string FreshTimingReport;
  llvm::raw_string_ostream FreshTimingStream(FreshTimingReport);
  {
    TimedModuleAnalysisExecution<NevercAnalysisFreshTimerAnalysis> Execution(
        FreshContext, FreshTimingStream);
    Execution.run(FreshModule);
  }
  FreshTimingStream.flush();
  if (countTextOccurrences(FreshTimingReport, AnalysisFreshTimerName) != 1)
    return recoveryProbeFailure(229,
                                "fresh analysis timing record count changed");
  if (llvm::sys::fs::exists(StrayReportPath))
    return recoveryProbeFailure(230, "fresh analysis emitted a stray report");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(
        231, "fresh analysis changed the timer group registry");
  if (::ErrorHandler || ::ErrorHandlerUserData)
    return recoveryProbeFailure(232,
                                "fresh analysis retained a fatal handler");
  return 0;
}

int runChangeReporterHalfStackRecoveryProbe() {
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery = llvm::make_scope_exit(
      [] { llvm::CrashRecoveryContext::Disable(); });
  llvm::LLVMContext Context;
  llvm::Module Module("neverc-change-reporter-fatal", Context);

  bool CompletedNormally = false;
  int RecoveryCode = 0;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      auto *Reporter = new llvm::IRChangedPrinter(/*VerboseMode=*/false);
      llvm::CrashRecoveryContextCleanupRegistrar<llvm::IRChangedPrinter>
          ReporterCleanup(Reporter);
      Reporter->saveIRBeforePass(
          llvm::Any(static_cast<const llvm::Module *>(&Module)),
          "NeverCTestFatalPass", "NeverCTestFatalPass");
      llvm::CrashRecoveryContext::GetCurrent()->HandleExit(3);
    });
    RecoveryCode = CRC.RetCode;
  }

  if (CompletedNormally)
    return recoveryProbeFailure(32, "half-stack fatal returned normally");
  if (RecoveryCode != 3)
    return recoveryProbeFailure(33, "half-stack recovery code changed");
  return 0;
}

int runBackendFatalRecoveryProbe(bool EnableAfterPrinting,
                                 bool ParallelSafe = false,
                                 bool UseInvalidBackendOption = false,
                                 bool UsePluginSession = false,
                                 bool VerifyOutputRecovery = false,
                                 bool VerifyAmbientHandlerIsolation = false,
                                 bool VerifyAmbientThreadLocalHandler = false,
                                 bool EnableBeforePrinting = true) {
  if (!initializeNativeCodegen())
    return recoveryProbeFailure(237, "could not initialize native target");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(10, "ambient time-trace profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(11, "ambient LLVM fatal handler");
  AmbientFatalHandlerProbe AmbientHandler;
  bool AmbientHandlerInstalled = false;
  auto RemoveAmbientHandler = llvm::make_scope_exit([&] {
    if (AmbientHandlerInstalled)
      llvm::remove_fatal_error_handler();
  });
  if (VerifyAmbientHandlerIsolation) {
    llvm::install_fatal_error_handler(ambientFatalHandler, &AmbientHandler);
    AmbientHandlerInstalled = true;
  }
  if (VerifyAmbientHandlerIsolation && VerifyAmbientThreadLocalHandler)
    return recoveryProbeFailure(233, "ambiguous ambient handler probe");
  RoutedFatalHandlerProbe AmbientThreadLocalProbe{234};
  std::optional<llvm::ScopedThreadLocalFatalErrorHandler>
      AmbientThreadLocalHandler;
  if (VerifyAmbientThreadLocalHandler)
    AmbientThreadLocalHandler.emplace(routedFatalHandler,
                                      &AmbientThreadLocalProbe);
  const void *const BaselinePrettyStack = llvm::SavePrettyStackState();
  std::optional<BackendFatalOptionState> BaselineOptions =
      captureBackendFatalOptions();
  if (!BaselineOptions)
    return recoveryProbeFailure(29, "could not capture LLVM option baseline");
  neverc::OutputCoordinator OutputCoordinator;

  std::unique_ptr<neverc::plugin::PluginProcessServices> PluginServices;
  std::shared_ptr<neverc::plugin::PluginSession> PluginSession;
  if (UsePluginSession) {
    // This contract injects a backend fatal only after the invocation and
    // translation-unit tasks have begun, and before normal task teardown.
    // Fatal task creation, an active callback, and TaskEnd itself require a
    // separate crash-abandon state machine and are intentionally not covered.
    PluginServices = std::make_unique<neverc::plugin::PluginProcessServices>(
        "neverc-frontend-fatal-plugin-task-test", LLVM_VERSION_MAJOR);
    if (llvm::Error Error =
            neverc::plugin::registerPluginIOInterface(*PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(238,
                                  "could not register plugin IO interface");
    }
    if (llvm::Error Error =
            neverc::plugin::registerPluginFrontendInterface(*PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(
          239, "could not register plugin frontend interface");
    }
    if (llvm::Error Error =
            neverc::plugin::registerPluginIRInterface(*PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(240,
                                  "could not register plugin IR interface");
    }
    if (llvm::Error Error =
            neverc::plugin::registerPluginTargetInterfaces(*PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(
          241, "could not register plugin target interfaces");
    }
    if (llvm::Error Error =
            neverc::plugin::registerPluginCodeGenProviderInterfaces(
                *PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(
          242, "could not register codegen provider interfaces");
    }
    if (llvm::Error Error = neverc::plugin::registerPluginObjectPhaseInterface(
            *PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(243,
                                  "could not register plugin object interface");
    }
    if (llvm::Error Error =
            neverc::plugin::registerPluginAssemblyProviderInterface(
                *PluginServices)) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(
          244, "could not register assembly provider interface");
    }
    if (llvm::Error Error = PluginServices->interfaces().freeze()) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(171, "could not freeze plugin interfaces");
    }
    const std::vector<llvm::StringRef> NoPlugins;
    auto Plan = neverc::plugin::makePluginActivationPlan(
        PluginServices->registry(), NoPlugins);
    if (!Plan) {
      llvm::errs() << llvm::toString(Plan.takeError()) << '\n';
      return recoveryProbeFailure(172, "could not create empty plugin plan");
    }
    auto CreatedSession =
        neverc::plugin::PluginSession::create(*PluginServices, *Plan);
    if (!CreatedSession) {
      llvm::errs() << llvm::toString(CreatedSession.takeError()) << '\n';
      return recoveryProbeFailure(173, "could not create empty plugin session");
    }
    PluginSession = std::shared_ptr<neverc::plugin::PluginSession>(
        std::move(*CreatedSession));
  }

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  // Keep one untriggered timer in the managed default group for the complete
  // probe. Removing the fatal codegen timer then queues its final record for
  // deterministic inspection instead of immediately printing it to stderr.
  // The fatal cleanup must remove only its own timer nodes and restore the
  // exact group-list head.
  llvm::Timer RegistrySentinel("neverc-frontend-fatal-recovery-sentinel",
                               "NeverC frontend fatal recovery sentinel");
  llvm::TimerGroup *const BaselineTimerGroups =
      llvm::timer_detail::TimerGroupList;
  if (!BaselineTimerGroups)
    return recoveryProbeFailure(12, "default timer group was not initialized");
  llvm::TimerGroup *const DefaultTimerGroup =
      llvm::timer_detail::getDefaultTimerGroup();

  // Death-test children may inherit queued timer records from fixture setup.
  // Drain those records, then print once more: any ambient running timer would
  // have been restarted by the reset and would make the second report nonempty.
  std::string DiscardedTimingReport;
  {
    llvm::raw_string_ostream TimingStream(DiscardedTimingReport);
    DefaultTimerGroup->print(TimingStream, /*ResetAfterPrint=*/true);
    TimingStream.flush();
  }
  std::string AmbientTimingReport;
  {
    llvm::raw_string_ostream TimingStream(AmbientTimingReport);
    DefaultTimerGroup->print(TimingStream, /*ResetAfterPrint=*/false);
    TimingStream.flush();
  }
  if (!AmbientTimingReport.empty())
    return recoveryProbeFailure(155, "ambient default timer was active");

  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile("neverc-frontend-fatal", "c",
                                         SourcePath))
    return recoveryProbeFailure(13, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return recoveryProbeFailure(14, "could not open source");
    Source << "int neverc_frontend_fatal_probe(void) { return 17; }\n";
  }

  llvm::SmallString<128> DumpBlocker;
  if (llvm::sys::fs::createTemporaryFile("neverc-ir-dump-blocker", "file",
                                         DumpBlocker))
    return recoveryProbeFailure(15, "could not create dump blocker");
  llvm::FileRemover RemoveDumpBlocker(DumpBlocker);
  const std::string DumpRoot = DumpBlocker.str().str() + "/child";
  const std::string DumpArgument = "-ir-dump-directory=" + DumpRoot;

  TraceFiles FailedFiles;
  if (std::error_code Error =
          createTraceFiles("neverc-frontend-fatal-output", FailedFiles))
    return recoveryProbeFailure(16, Error.message());
  llvm::FileRemover RemoveFailedOutput(FailedFiles.Output);
  llvm::FileRemover RemoveFailedTrace(FailedFiles.Trace);

  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const char *const DebugPassValue =
      UseInvalidBackendOption ? "neverc-invalid-debug-pass" : "Structure";
  std::vector<const char *> FatalArgs = {
      "-triple",      HostTriple.c_str(),        "-emit-obj",
      "-O1",          "-ftime-report",           "-mdebug-pass",
      DebugPassValue, "-mlimit-float-precision", "12",
      "-o",           FailedFiles.Output.c_str()};
  if (EnableBeforePrinting) {
    FatalArgs.push_back("-mllvm");
    FatalArgs.push_back("-print-before-all");
  }
  if (EnableAfterPrinting) {
    FatalArgs.push_back("-mllvm");
    FatalArgs.push_back("-print-after-all");
  }
  FatalArgs.push_back("-mllvm");
  FatalArgs.push_back(DumpArgument.c_str());
  FatalArgs.push_back(SourcePath.c_str());

  bool CompletedNormally = false;
  int RecoveryCode = 0;
  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = ParallelSafe;
  DirectOpts.PluginSession = PluginSession;
  DirectOpts.Outputs = VerifyOutputRecovery ? &OutputCoordinator : nullptr;
  {
    llvm::CrashRecoveryContext CRC;
    CompletedNormally = CRC.RunSafely([&] {
      (void)neverc::ExecuteFrontendDirect(
          FatalArgs, "neverc-test-frontend", frontendMainAddress(),
          (ParallelSafe || PluginSession || VerifyOutputRecovery) ? &DirectOpts
                                                                  : nullptr);
    });
    RecoveryCode = CRC.RetCode;
  }
  if (CompletedNormally)
    return recoveryProbeFailure(17, "fatal frontend returned normally");
  if (RecoveryCode == 0)
    return recoveryProbeFailure(18, "fatal frontend had no recovery code");
  if (VerifyOutputRecovery) {
    // A backend fatal abandons CompilerInstance's stack owner. The recovery
    // cleanup must abort its output transaction and release the path lease.
    // The cancellation check makes the old leak fail in one coordinator
    // polling interval instead of hanging this death-test child.
    unsigned CancellationChecks = 0;
    auto Reacquired = OutputCoordinator.acquire(
        FailedFiles.Output, [&] { return ++CancellationChecks > 1; },
        neverc::OutputLeaseOwner{UINT64_C(313), UINT64_C(911)});
    if (!Reacquired) {
      llvm::consumeError(Reacquired.takeError());
      return recoveryProbeFailure(
          177, "fatal frontend retained its output path lease");
    }
    Reacquired->release();
  }
  if (PluginSession) {
    if (PluginSession->activeTaskCount() != 0) {
      llvm::errs() << "active frontend plugin tasks after recovery: "
                   << PluginSession->activeTaskCount() << '\n';
      const int Code =
          recoveryProbeFailure(174, "frontend plugin tasks were not ended");
      // The expected RED state cannot be destroyed normally because the
      // session intentionally fails closed while tasks remain active.
      llvm::errs().flush();
      std::_Exit(Code);
    }
    if (llvm::Error Error = PluginSession->end()) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      const int Code = recoveryProbeFailure(175, "plugin session end failed");
      llvm::errs().flush();
      std::_Exit(Code);
    }
    DirectOpts.PluginSession.reset();
    PluginSession.reset();
    if (llvm::Error Error = PluginServices->shutdown()) {
      llvm::errs() << llvm::toString(std::move(Error)) << '\n';
      return recoveryProbeFailure(176,
                                  "plugin process services shutdown failed");
    }
  }
  if (llvm::SavePrettyStackState() != BaselinePrettyStack)
    return recoveryProbeFailure(169, "pretty-stack state was not restored");

  if (!backendFatalOptionsMatch(*BaselineOptions))
    return recoveryProbeFailure(30, "LLVM options were not restored");

  if (VerifyAmbientHandlerIsolation) {
    if (::ErrorHandler != ambientFatalHandler ||
        ::ErrorHandlerUserData != &AmbientHandler ||
        AmbientHandler.Calls != 0 ||
        llvm::has_thread_local_fatal_error_handler())
      return recoveryProbeFailure(
          178, "frontend fatal handler changed its ambient predecessor");
    llvm::remove_fatal_error_handler();
    AmbientHandlerInstalled = false;
  }
  if (VerifyAmbientThreadLocalHandler) {
    if (::ErrorHandler || ::ErrorHandlerUserData ||
        !llvm::has_thread_local_fatal_error_handler())
      return recoveryProbeFailure(
          234, "frontend fatal handler did not restore its TLS predecessor");
    llvm::CrashRecoveryContext AmbientCRC;
    const bool AmbientCompleted = AmbientCRC.RunSafely([] {
      llvm::report_fatal_error("neverc ambient TLS predecessor probe");
    });
    if (AmbientCompleted || AmbientCRC.RetCode != 234 ||
        AmbientThreadLocalProbe.Calls != 1)
      return recoveryProbeFailure(235,
                                  "ambient TLS predecessor was not routable");
    AmbientThreadLocalHandler.reset();
    if (llvm::has_thread_local_fatal_error_handler() ||
        llvm::has_fatal_error_handler())
      return recoveryProbeFailure(236,
                                  "ambient TLS predecessor was not removable");
  }

  // Cleanup registrars run only in the CRC destructor above. Inspecting these
  // invariants inside that scope would be a false positive.
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(19, "timer group registry was not restored");

  // -ftime-report makes CodeGenerationTime active before the injected fatal.
  // Its crash-owned helper must stop the running timer before destruction and
  // detach it exactly once from the default group's intrusive list.
  constexpr llvm::StringLiteral CodeGenerationTimer = "Code Generation Time";
  std::string FirstTimingReport;
  {
    llvm::raw_string_ostream TimingStream(FirstTimingReport);
    DefaultTimerGroup->print(TimingStream, /*ResetAfterPrint=*/false);
    TimingStream.flush();
  }
  llvm::StringRef FirstTiming(FirstTimingReport);
  const std::size_t CodeGenerationPosition =
      FirstTiming.find(CodeGenerationTimer);
  if (CodeGenerationPosition == llvm::StringRef::npos ||
      FirstTiming.find(CodeGenerationTimer,
                       CodeGenerationPosition + CodeGenerationTimer.size()) !=
          llvm::StringRef::npos)
    return recoveryProbeFailure(34, "codegen timing record count changed");
#if !defined(_WIN32)
  // LLVM's Windows timer backend can render a stopped timer as dashes when
  // both measured durations round to zero.  The exact record count above, the
  // registry-drain check below, and the fresh retry remain format-independent
  // cleanup checks there.
  const std::size_t PreviousNewline =
      FirstTiming.take_front(CodeGenerationPosition).rfind('\n');
  const std::size_t LineBegin =
      PreviousNewline == llvm::StringRef::npos ? 0 : PreviousNewline + 1;
  const std::size_t NextNewline =
      FirstTiming.find('\n', CodeGenerationPosition);
  const llvm::StringRef CodeGenerationLine = FirstTiming.slice(
      LineBegin,
      NextNewline == llvm::StringRef::npos ? FirstTiming.size() : NextNewline);
  if (CodeGenerationLine.contains("-----"))
    return recoveryProbeFailure(35, "running codegen timer was not stopped");
#endif

  std::string SecondTimingReport;
  {
    llvm::raw_string_ostream TimingStream(SecondTimingReport);
    DefaultTimerGroup->print(TimingStream, /*ResetAfterPrint=*/false);
    TimingStream.flush();
  }
  if (llvm::StringRef(SecondTimingReport).contains(CodeGenerationTimer))
    return recoveryProbeFailure(36, "codegen timer remained in the registry");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(20, "time-trace profiler remained enabled");
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(21, "LLVM fatal handler was not removed");
  // GenAssemblyHelper uses the persistent default group rather than adding a
  // group-list node. Creating a fresh timer forces add/remove to touch the
  // default group's intrusive list, exposing a stale codegen timer to ASan.
  {
    llvm::Timer PostRecoveryTimer("neverc-frontend-fatal-recovery-post-check",
                                  "NeverC frontend fatal recovery post-check");
  }
  llvm::TimerGroup::clearAll();

  TraceFiles RetryFiles;
  if (std::error_code Error =
          createTraceFiles("neverc-frontend-fatal-retry", RetryFiles))
    return recoveryProbeFailure(22, Error.message());
  llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
  llvm::FileRemover RemoveRetryTrace(RetryFiles.Trace);
  const std::string RetryTraceArgument = "-ftime-trace=" + RetryFiles.Trace;
  const char *RetryOutput = VerifyOutputRecovery ? FailedFiles.Output.c_str()
                                                 : RetryFiles.Output.c_str();
  const char *RetryArgs[] = {
      "-triple",
      HostTriple.c_str(),
      "-emit-obj",
      "-O0",
      "-o",
      RetryOutput,
      RetryTraceArgument.c_str(),
      "-ftime-trace-granularity=0",
      SourcePath.c_str(),
  };
  neverc::driver::DirectInvocationOpts RetryDirectOpts;
  RetryDirectOpts.Outputs = &OutputCoordinator;
  if (neverc::ExecuteFrontendDirect(
          RetryArgs, "neverc-test-frontend", frontendMainAddress(),
          VerifyOutputRecovery ? &RetryDirectOpts : nullptr) != 0)
    return recoveryProbeFailure(23, "fresh frontend retry failed");
  if (!llvm::sys::fs::exists(RetryOutput))
    return recoveryProbeFailure(24, "fresh frontend emitted no output");
  if (!traceHasMarker(RetryFiles.Trace, "ExecuteCompiler"))
    return recoveryProbeFailure(25, "fresh frontend emitted an invalid trace");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(
        26, "fresh frontend changed the timer group registry");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(27, "fresh frontend retained its profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(28, "fresh frontend retained its fatal handler");
  if (!backendFatalOptionsMatch(*BaselineOptions))
    return recoveryProbeFailure(31, "fresh frontend changed LLVM options");
  if (llvm::SavePrettyStackState() != BaselinePrettyStack)
    return recoveryProbeFailure(170,
                                "fresh frontend changed pretty-stack state");
  return 0;
}

int runAmbientNoTraceFrontendFatalProbe() {
  if (!initializeNativeCodegen())
    return recoveryProbeFailure(98, "could not initialize native target");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(70, "ambient time-trace profiler");
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(71, "ambient LLVM fatal handler");
  std::optional<BackendFatalOptionState> BaselineOptions =
      captureBackendFatalOptions();
  if (!BaselineOptions)
    return recoveryProbeFailure(72, "could not capture LLVM option baseline");

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery = llvm::make_scope_exit(
      [] { llvm::CrashRecoveryContext::Disable(); });

  {
    llvm::Timer Warmup("neverc-ambient-frontend-fatal-warmup",
                       "NeverC ambient frontend fatal warmup");
  }
  llvm::TimerGroup *const BaselineTimerGroups =
      llvm::timer_detail::TimerGroupList;
  if (!BaselineTimerGroups)
    return recoveryProbeFailure(73, "default timer group was not initialized");

  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile("neverc-ambient-frontend-fatal", "c",
                                         SourcePath))
    return recoveryProbeFailure(74, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return recoveryProbeFailure(75, "could not open source");
    Source << "int neverc_ambient_fatal_probe(void) { return 41; }\n";
  }

  llvm::SmallString<128> DumpBlocker;
  if (llvm::sys::fs::createTemporaryFile("neverc-ambient-dump-blocker",
                                         "file", DumpBlocker))
    return recoveryProbeFailure(76, "could not create dump blocker");
  llvm::FileRemover RemoveDumpBlocker(DumpBlocker);
  const std::string DumpArgument =
      "-ir-dump-directory=" + DumpBlocker.str().str() + "/child";

  TraceFiles FailedFiles;
  if (std::error_code Error =
          createTraceFiles("neverc-ambient-frontend-failed", FailedFiles))
    return recoveryProbeFailure(77, Error.message());
  llvm::FileRemover RemoveFailedOutput(FailedFiles.Output);
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const char *FatalArgs[] = {
      "-triple", HostTriple.c_str(), "-emit-obj", "-O1", "-o",
      FailedFiles.Output.c_str(), "-mllvm", "-print-before-all", "-mllvm",
      DumpArgument.c_str(), SourcePath.c_str()};
  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;

  {
    neverc::LLVMTimeTraceProfilerOwner Outer(
        /*Granularity=*/0, "neverc-test-ambient-frontend-root");
    if (!Outer.ownsProfiler())
      return recoveryProbeFailure(78, "could not acquire outer trace root");
    if (neverc::time_trace_detail::shouldGuardManagedRootAgainstCrash())
      return recoveryProbeFailure(79, "crash guard active without a context");

    bool GuardWasVisible = false;
    bool CompletedNormally = false;
    int FrontendResult = -1;
    int RecoveryCode = 0;
    {
      llvm::CrashRecoveryContext CRC;
      CompletedNormally = CRC.RunSafely([&] {
        GuardWasVisible = neverc::time_trace_detail::
            shouldGuardManagedRootAgainstCrash();
        FrontendResult = neverc::ExecuteFrontendDirect(
            FatalArgs, "neverc-test-frontend", frontendMainAddress(),
            &DirectOpts);
      });
      RecoveryCode = CRC.RetCode;
    }
    if (CompletedNormally || RecoveryCode == 0 || FrontendResult != -1 ||
        !GuardWasVisible)
      return recoveryProbeFailure(80, "fatal frontend recovery contract");
    if (llvm::timeTraceProfilerEnabled())
      return recoveryProbeFailure(81, "outer profiler was not invalidated");
    if (llvm::sys::fs::exists(FailedFiles.Output))
      return recoveryProbeFailure(82, "fatal frontend retained output");
    if (!backendFatalOptionsMatch(*BaselineOptions))
      return recoveryProbeFailure(83, "LLVM options were not restored");
    if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
      return recoveryProbeFailure(84, "timer registry was not restored");
    if (::ErrorHandler || ::ErrorHandlerUserData ||
        llvm::has_thread_local_fatal_error_handler() ||
        llvm::has_fatal_error_handler())
      return recoveryProbeFailure(85, "fatal handler was not removed");

    llvm::SmallString<128> InvalidatedTrace;
    if (llvm::sys::fs::createTemporaryFile(
            "neverc-ambient-frontend-invalidated", "json", InvalidatedTrace))
      return recoveryProbeFailure(86, "could not create trace output");
    llvm::FileRemover RemoveInvalidatedTrace(InvalidatedTrace);
    std::error_code TraceError;
    llvm::raw_fd_ostream TraceOutput(InvalidatedTrace, TraceError);
    if (TraceError)
      return recoveryProbeFailure(87, "could not open trace output");
    llvm::Error WriteError = Outer.write(TraceOutput);
    if (!WriteError)
      return recoveryProbeFailure(88, "invalidated outer trace was writable");
    const std::string WriteMessage =
        llvm::toString(std::move(WriteError)).str().str();
    if (!llvm::StringRef(WriteMessage)
             .contains("invalidated during crash recovery"))
      return recoveryProbeFailure(89, "outer trace diagnostic changed");
  }

  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(90, "outer root remained after release");
  TraceFiles RetryFiles;
  if (std::error_code Error =
          createTraceFiles("neverc-ambient-frontend-retry", RetryFiles))
    return recoveryProbeFailure(91, Error.message());
  llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
  const char *RetryArgs[] = {"-triple", HostTriple.c_str(), "-emit-obj", "-o",
                             RetryFiles.Output.c_str(), SourcePath.c_str()};
  if (neverc::ExecuteFrontendDirect(RetryArgs, "neverc-test-frontend",
                                    frontendMainAddress(), &DirectOpts) != 0)
    return recoveryProbeFailure(92, "fresh frontend retry failed");
  if (!llvm::sys::fs::exists(RetryFiles.Output))
    return recoveryProbeFailure(93, "fresh frontend emitted no output");
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(94, "fresh retry retained a profiler");
  if (!backendFatalOptionsMatch(*BaselineOptions))
    return recoveryProbeFailure(95, "fresh retry changed LLVM options");
  if (llvm::timer_detail::TimerGroupList != BaselineTimerGroups)
    return recoveryProbeFailure(96, "fresh retry changed timer registry");
  if (::ErrorHandler || ::ErrorHandlerUserData ||
      llvm::has_thread_local_fatal_error_handler() ||
      llvm::has_fatal_error_handler())
    return recoveryProbeFailure(97, "fresh retry retained fatal handler");
  return 0;
}

int runUnmanagedAmbientNoTraceFrontendProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return recoveryProbeFailure(180, "ambient time-trace profiler");
  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-test-unmanaged-frontend");
  llvm::TimeTraceProfiler *const AmbientProfiler =
      llvm::getTimeTraceProfilerInstance();
  constexpr llvm::StringLiteral AmbientMarker =
      "neverc.test.unmanaged.frontend.ambient";
  {
    llvm::TimeTraceScope MarkerScope(AmbientMarker);
  }
  auto CleanupProfiler = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  llvm::SmallString<128> SourcePath;
  if (llvm::sys::fs::createTemporaryFile(
          "neverc-unmanaged-frontend", "c", SourcePath))
    return recoveryProbeFailure(181, "could not create source");
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    if (SourceError)
      return recoveryProbeFailure(182, "could not open source");
    Source << "int neverc_unmanaged_frontend(void) { return 47; }\n";
  }

  TraceFiles Files;
  if (std::error_code Error =
          createTraceFiles("neverc-unmanaged-frontend", Files))
    return recoveryProbeFailure(183, Error.message());
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const char *Args[] = {"-triple", HostTriple.c_str(), "-emit-obj", "-o",
                        Files.Output.c_str(), SourcePath.c_str()};
  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery = llvm::make_scope_exit(
      [] { llvm::CrashRecoveryContext::Disable(); });
  int FrontendResult = -1;
  llvm::CrashRecoveryContext CRC;
  if (!CRC.RunSafely([&] {
        FrontendResult = neverc::ExecuteFrontendDirect(
            Args, "neverc-test-frontend", frontendMainAddress(), &DirectOpts);
      })) {
    // The RED implementation has already abandoned ExecuteCompiler in the
    // raw ambient profiler. Avoid a second assertion during test cleanup.
    llvm::errs().flush();
    std::_Exit(184);
  }
  if (FrontendResult != 1)
    return recoveryProbeFailure(185, "unmanaged frontend was not rejected");
  if (llvm::sys::fs::exists(Files.Output))
    return recoveryProbeFailure(186, "rejected frontend produced output");
  if (!llvm::timeTraceProfilerEnabled() ||
      llvm::getTimeTraceProfilerInstance() != AmbientProfiler)
    return recoveryProbeFailure(187, "ambient profiler was replaced");

  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(Files.Trace, std::string())) {
    llvm::consumeError(std::move(Error));
    return recoveryProbeFailure(188, "ambient trace was not writable");
  }
  if (!traceHasMarker(Files.Trace, AmbientMarker))
    return recoveryProbeFailure(189, "ambient marker changed");
  if (traceHasMarker(Files.Trace, "ExecuteCompiler"))
    return recoveryProbeFailure(190, "frontend scope entered ambient trace");
  return 0;
}

} // namespace

TEST(PluginFrontendTimeTraceInteropTest,
     DirectInvocationBooleanFlagsAreNeverDropped) {
  neverc::driver::DirectInvocationOpts DirectOpts;
  EXPECT_FALSE(neverc::driver::hasAnyDirectOpts(DirectOpts));
  DirectOpts.InMemoryLTOOutput = true;
  EXPECT_TRUE(neverc::driver::hasAnyDirectOpts(DirectOpts));
  DirectOpts = {};
  DirectOpts.ParallelSafe = true;
  EXPECT_TRUE(neverc::driver::hasAnyDirectOpts(DirectOpts));
}

TEST(PluginFrontendTimeTraceInteropTest,
     ChangeReporterHalfStackIsDiscardedDuringCrashCleanup) {
  EXPECT_EXIT(std::exit(runChangeReporterHalfStackRecoveryProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     PassBodyFatalClosesPassTimingDuringCrashCleanup) {
  EXPECT_EXIT(std::exit(runPassBodyFatalTimingRecoveryProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     AnalysisFatalClosesPassTimingDuringCrashCleanup) {
  EXPECT_EXIT(std::exit(runAnalysisFatalTimingRecoveryProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     FiredCleanupRemainsObservableUntilRegistrarOwnerIsDestroyed) {
  EXPECT_EXIT(std::exit(runFiredCleanupRegistrarRetirementProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     RecoveryRereadsHeadAfterCleanupUnregistersItsSuccessor) {
  EXPECT_EXIT(std::exit(runCleanupHeadMutationProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     ThreadLocalFatalHandlerNestsAndRejectsStaleReset) {
  EXPECT_EXIT(std::exit(runThreadLocalFatalHandlerNestingProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     ConcurrentThreadLocalFatalHandlersRouteIndependently) {
  EXPECT_EXIT(std::exit(runConcurrentThreadLocalFatalHandlersProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     BackendFatalBeforeOnlyRestoresTimersAndAllowsFreshRetry) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/false)),
              ::testing::ExitedWithCode(0), "Failed to create directory");
}

TEST(PluginFrontendTimeTraceInteropTest,
     BackendFatalAbortsOutputLeaseAndReusesTheSamePath) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/false,
                  /*ParallelSafe=*/false,
                  /*UseInvalidBackendOption=*/false,
                  /*UsePluginSession=*/false,
                  /*VerifyOutputRecovery=*/true)),
              ::testing::ExitedWithCode(0), "Failed to create directory");
}

TEST(PluginFrontendTimeTraceInteropTest,
     BackendFatalWithAfterPrintingClearsPendingInstrumentation) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/true)),
              ::testing::ExitedWithCode(0), "Failed to create directory");
}

TEST(PluginFrontendTimeTraceInteropTest,
     BackendFatalAfterOnlyRestoresPassStateWithoutLeaks) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/true,
                  /*ParallelSafe=*/false,
                  /*UseInvalidBackendOption=*/false,
                  /*UsePluginSession=*/false,
                  /*VerifyOutputRecovery=*/false,
                  /*VerifyAmbientHandlerIsolation=*/false,
                  /*VerifyAmbientThreadLocalHandler=*/false,
                  /*EnableBeforePrinting=*/false)),
              ::testing::ExitedWithCode(0), "Failed to create directory");
}

TEST(PluginFrontendTimeTraceInteropTest,
     InvalidatedPassFatalRestoresDumpStateWithoutLeaks) {
  EXPECT_EXIT(std::exit(runInvalidatedPrintIRFatalProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginFrontendTimeTraceInteropTest,
     ParallelSafeOptionMutationUsesRecoverableFatalHandler) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/true,
                  /*ParallelSafe=*/true,
                  /*UseInvalidBackendOption=*/true)),
              ::testing::ExitedWithCode(0),
              "error in backend: invalid value for backend LLVM option "
              "-debug-pass");
}

TEST(PluginFrontendTimeTraceInteropTest,
     ParallelSafeFatalShadowsAndRestoresAmbientGlobalHandler) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/true,
                  /*ParallelSafe=*/true,
                  /*UseInvalidBackendOption=*/true,
                  /*UsePluginSession=*/false,
                  /*VerifyOutputRecovery=*/false,
                  /*VerifyAmbientHandlerIsolation=*/true)),
              ::testing::ExitedWithCode(0),
              "error in backend: invalid value for backend LLVM option "
              "-debug-pass");
}

TEST(PluginFrontendTimeTraceInteropTest,
     ParallelSafeFatalShadowsAndRestoresAmbientThreadLocalHandler) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/true,
                  /*ParallelSafe=*/true,
                  /*UseInvalidBackendOption=*/true,
                  /*UsePluginSession=*/false,
                  /*VerifyOutputRecovery=*/false,
                  /*VerifyAmbientHandlerIsolation=*/false,
                  /*VerifyAmbientThreadLocalHandler=*/true)),
              ::testing::ExitedWithCode(0),
              "error in backend: invalid value for backend LLVM option "
              "-debug-pass");
}

TEST(PluginFrontendTimeTraceInteropTest,
     BackendFatalEndsFrontendPluginTasksBeforeSessionTeardown) {
  EXPECT_EXIT(std::exit(runBackendFatalRecoveryProbe(
                  /*EnableAfterPrinting=*/false,
                  /*ParallelSafe=*/false,
                  /*UseInvalidBackendOption=*/false,
                  /*UsePluginSession=*/true)),
              ::testing::ExitedWithCode(0), "Failed to create directory");
}

TEST(PluginFrontendTimeTraceInteropTest,
     ParallelSafeFrontendWritesRequestedTimeTrace) {
  ASSERT_TRUE(initializeNativeCodegen());
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  ASSERT_EQ(::ErrorHandler, nullptr);
  ASSERT_EQ(::ErrorHandlerUserData, nullptr);
  ASSERT_FALSE(llvm::has_thread_local_fatal_error_handler());
  ASSERT_FALSE(llvm::has_fatal_error_handler());

  TraceFiles Files;
  ASSERT_FALSE(createTraceFiles("neverc-parallel-safe-frontend", Files));
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  llvm::SmallString<128> SourcePath;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile(
      "neverc-parallel-safe-frontend", "c", SourcePath));
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    ASSERT_FALSE(SourceError);
    Source << "int neverc_parallel_safe_trace(void) { return 31; }\n";
  }

  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const std::string TraceArgument = "-ftime-trace=" + Files.Trace;
  const char *Args[] = {
      "-triple", HostTriple.c_str(), "-emit-obj", "-o", Files.Output.c_str(),
      TraceArgument.c_str(), "-ftime-trace-granularity=0", SourcePath.c_str()};
  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;
  EXPECT_EQ(neverc::ExecuteFrontendDirect(Args, "neverc-test-frontend",
                                          frontendMainAddress(), &DirectOpts),
            0);
  EXPECT_TRUE(llvm::sys::fs::is_regular_file(Files.Output));
  EXPECT_TRUE(traceHasMarker(Files.Trace, "ExecuteCompiler"));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(::ErrorHandler, nullptr);
  EXPECT_EQ(::ErrorHandlerUserData, nullptr);
  EXPECT_FALSE(llvm::has_thread_local_fatal_error_handler());
  EXPECT_FALSE(llvm::has_fatal_error_handler());
}

TEST(PluginFrontendTimeTraceInteropTest,
     NoTraceFrontendFatalInvalidatesManagedAmbientRoot) {
  EXPECT_EXIT(std::exit(runAmbientNoTraceFrontendFatalProbe()),
              ::testing::ExitedWithCode(0), "Failed to create directory");
}

TEST(PluginFrontendTimeTraceInteropTest,
     UnmanagedAmbientNoTraceCRCRejectsBeforeFrontendScope) {
  EXPECT_EXIT(std::exit(runUnmanagedAmbientNoTraceFrontendProbe()),
              ::testing::ExitedWithCode(0),
              "unmanaged LLVM time-trace profiler");
}

TEST(PluginFrontendTimeTraceInteropTest,
     FrontendTraceWriteFailureReturnsWithoutTerminatingAndAllowsRetry) {
  if (!llvm::sys::fs::exists("/dev/full"))
    GTEST_SKIP() << "host has no deterministic failing output sink";
  ASSERT_TRUE(initializeNativeCodegen());
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  ASSERT_EQ(::ErrorHandler, nullptr);
  ASSERT_EQ(::ErrorHandlerUserData, nullptr);
  ASSERT_FALSE(llvm::has_thread_local_fatal_error_handler());
  ASSERT_FALSE(llvm::has_fatal_error_handler());

  TraceFiles Files;
  ASSERT_FALSE(createTraceFiles("neverc-frontend-trace-write-failure", Files));
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::SmallString<128> SourcePath;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile(
      "neverc-frontend-trace-write-failure", "c", SourcePath));
  llvm::FileRemover RemoveSource(SourcePath);
  std::error_code SourceError;
  {
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    ASSERT_FALSE(SourceError);
    Source << "int neverc_trace_write_failure(void) { return 43; }\n";
  }

  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const char *Args[] = {"-triple", HostTriple.c_str(), "-emit-obj", "-o",
                        Files.Output.c_str(), "-ftime-trace=/dev/full",
                        "-ftime-trace-granularity=0", SourcePath.c_str()};
  neverc::driver::DirectInvocationOpts DirectOpts;
  DirectOpts.ParallelSafe = true;
  EXPECT_EQ(neverc::ExecuteFrontendDirect(Args, "neverc-test-frontend",
                                          frontendMainAddress(), &DirectOpts),
            1);
  // The object transaction commits when the frontend action completes. A
  // later trace-sink failure is still a failed invocation, but must not delete
  // or corrupt that already-committed primary output.
  EXPECT_TRUE(llvm::sys::fs::is_regular_file(Files.Output));
  EXPECT_TRUE(llvm::sys::fs::exists("/dev/full"));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(::ErrorHandler, nullptr);
  EXPECT_EQ(::ErrorHandlerUserData, nullptr);
  EXPECT_FALSE(llvm::has_thread_local_fatal_error_handler());
  EXPECT_FALSE(llvm::has_fatal_error_handler());

  TraceFiles RetryFiles;
  ASSERT_FALSE(createTraceFiles("neverc-frontend-trace-write-retry",
                                RetryFiles));
  llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
  llvm::FileRemover RemoveRetryTrace(RetryFiles.Trace);
  const std::string RetryTraceArgument = "-ftime-trace=" + RetryFiles.Trace;
  const char *RetryArgs[] = {
      "-triple", HostTriple.c_str(), "-emit-obj", "-o",
      RetryFiles.Output.c_str(), RetryTraceArgument.c_str(),
      "-ftime-trace-granularity=0", SourcePath.c_str()};
  EXPECT_EQ(neverc::ExecuteFrontendDirect(
                RetryArgs, "neverc-test-frontend", frontendMainAddress(),
                &DirectOpts),
            0);
  EXPECT_TRUE(llvm::sys::fs::is_regular_file(RetryFiles.Output));
  EXPECT_TRUE(traceHasMarker(RetryFiles.Trace, "ExecuteCompiler"));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(::ErrorHandler, nullptr);
  EXPECT_EQ(::ErrorHandlerUserData, nullptr);
  EXPECT_FALSE(llvm::has_thread_local_fatal_error_handler());
  EXPECT_FALSE(llvm::has_fatal_error_handler());
}

TEST(PluginFrontendTimeTraceInteropTest,
     PrintSupportedCPUsFailureDoesNotPoisonFreshLinkerTrace) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  TraceFiles FrontendFiles;
  TraceFiles LinkerFiles;
  ASSERT_FALSE(createTraceFiles("neverc-frontend-early-trace", FrontendFiles));
  ASSERT_FALSE(createTraceFiles("neverc-linker-after-frontend", LinkerFiles));
  llvm::FileRemover RemoveFrontendOutput(FrontendFiles.Output);
  llvm::FileRemover RemoveFrontendTrace(FrontendFiles.Trace);
  llvm::FileRemover RemoveLinkerOutput(LinkerFiles.Output);
  llvm::FileRemover RemoveLinkerTrace(LinkerFiles.Trace);
  auto CleanupAmbient = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  const std::string TraceArg = "-ftime-trace=" + FrontendFiles.Trace;
  const char *FrontendArgs[] = {"-triple", "neverc-unsupported-target",
                                "-print-supported-cpus", TraceArg.c_str()};
  EXPECT_EQ(neverc::ExecuteFrontendDirect(FrontendArgs, "neverc-test-frontend",
                                          frontendMainAddress()),
            1);
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());

  std::string Stdout;
  std::string Stderr;
  EXPECT_EQ(runTracedLink(LinkerFiles.Output, Stdout, Stderr), 0) << Stderr;
  EXPECT_TRUE(Stdout.empty());
  EXPECT_TRUE(Stderr.empty());
  EXPECT_TRUE(traceHasMarker(LinkerFiles.Trace, LinkerMarker));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}

TEST(PluginFrontendTimeTraceInteropTest,
     InvalidArgumentsReleaseProfilerAndFatalHandler) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  ASSERT_EQ(::ErrorHandler, nullptr);
  ASSERT_EQ(::ErrorHandlerUserData, nullptr);
  ASSERT_FALSE(llvm::has_thread_local_fatal_error_handler());
  ASSERT_FALSE(llvm::has_fatal_error_handler());
  auto CleanupGlobals = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
    if (::ErrorHandler)
      llvm::remove_fatal_error_handler();
  });

  TraceFiles FrontendFiles;
  ASSERT_FALSE(
      createTraceFiles("neverc-frontend-invalid-trace", FrontendFiles));
  llvm::FileRemover RemoveFrontendOutput(FrontendFiles.Output);
  llvm::FileRemover RemoveFrontendTrace(FrontendFiles.Trace);
  const std::string TraceArg = "-ftime-trace=" + FrontendFiles.Trace;
  const std::string TripleArg = llvm::sys::getDefaultTargetTriple();
  const char *FrontendArgs[] = {"-triple", TripleArg.c_str(), TraceArg.c_str(),
                                "-neverc-invalid-frontend-option"};
  EXPECT_EQ(neverc::ExecuteFrontendDirect(FrontendArgs, "neverc-test-frontend",
                                          frontendMainAddress()),
            1);
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(::ErrorHandler, nullptr);
  EXPECT_EQ(::ErrorHandlerUserData, nullptr);
  EXPECT_FALSE(llvm::has_thread_local_fatal_error_handler());
  EXPECT_FALSE(llvm::has_fatal_error_handler());
}

TEST(PluginFrontendTimeTraceInteropTest,
     FoundationRootLeaseBlocksFrontendAndLinkerUntilRelease) {
  ASSERT_TRUE(initializeNativeCodegen());
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  TraceFiles RejectedFiles;
  TraceFiles RetryFiles;
  ASSERT_FALSE(
      createTraceFiles("neverc-linker-cross-root-rejected", RejectedFiles));
  ASSERT_FALSE(createTraceFiles("neverc-linker-cross-root-retry", RetryFiles));
  llvm::FileRemover RemoveRejectedOutput(RejectedFiles.Output);
  llvm::FileRemover RemoveRejectedTrace(RejectedFiles.Trace);
  llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
  llvm::FileRemover RemoveRetryTrace(RetryFiles.Trace);

  std::mutex Mutex;
  std::condition_variable Condition;
  bool Ready = false;
  bool Release = false;
  bool LeaseOwned = false;
  std::thread FrontendRoot([&] {
    neverc::LLVMTimeTraceProfilerOwner Owner(
        /*Granularity=*/0, "neverc-test-frontend-root");
    LeaseOwned = Owner.ownsProfiler();
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Ready = true;
    }
    Condition.notify_all();
    {
      std::unique_lock<std::mutex> Lock(Mutex);
      Condition.wait(Lock, [&] { return Release; });
    }
  });
  auto ReleaseAndJoin = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Release = true;
    }
    Condition.notify_all();
    if (FrontendRoot.joinable())
      FrontendRoot.join();
  });
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    ASSERT_TRUE(Condition.wait_for(Lock, std::chrono::seconds(10),
                                   [&] { return Ready; }));
  }
  ASSERT_TRUE(LeaseOwned);

  std::string RejectedStdout;
  std::string RejectedStderr;
  EXPECT_EQ(runTracedLink(RejectedFiles.Output, RejectedStdout, RejectedStderr),
            1);
  EXPECT_TRUE(RejectedStdout.empty());
  EXPECT_EQ(RejectedStderr, BusyDiagnostic);
  EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Output));
  EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Trace));

  TraceFiles RejectedFrontendFiles;
  ASSERT_FALSE(createTraceFiles("neverc-frontend-cross-root-rejected",
                                RejectedFrontendFiles));
  llvm::FileRemover RemoveRejectedFrontendOutput(
      RejectedFrontendFiles.Output);
  llvm::FileRemover RemoveRejectedFrontendTrace(RejectedFrontendFiles.Trace);
  llvm::SmallString<128> SourcePath;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile(
      "neverc-frontend-cross-root", "c", SourcePath));
  llvm::FileRemover RemoveSource(SourcePath);
  {
    std::error_code SourceError;
    llvm::raw_fd_ostream Source(SourcePath, SourceError);
    ASSERT_FALSE(SourceError);
    Source << "int neverc_frontend_root_probe(void) { return 7; }\n";
  }
  const std::string FrontendTraceArg =
      "-ftime-trace=" + RejectedFrontendFiles.Trace;
  const std::string HostTriple = llvm::sys::getDefaultTargetTriple();
  const char *RejectedFrontendArgs[] = {
      "-triple", HostTriple.c_str(), "-emit-obj", "-o",
      RejectedFrontendFiles.Output.c_str(), FrontendTraceArg.c_str(),
      SourcePath.c_str()};
  neverc::driver::DirectInvocationOpts ParallelSafeTraceOpts;
  ParallelSafeTraceOpts.ParallelSafe = true;
  EXPECT_EQ(neverc::ExecuteFrontendDirect(
                RejectedFrontendArgs, "neverc-test-frontend",
                frontendMainAddress(), &ParallelSafeTraceOpts),
            1);
  EXPECT_FALSE(llvm::sys::fs::exists(RejectedFrontendFiles.Output));
  EXPECT_FALSE(llvm::sys::fs::exists(RejectedFrontendFiles.Trace));

  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Release = true;
  }
  Condition.notify_all();
  FrontendRoot.join();
  ReleaseAndJoin.release();

  std::string RetryStdout;
  std::string RetryStderr;
  EXPECT_EQ(runTracedLink(RetryFiles.Output, RetryStdout, RetryStderr), 0)
      << RetryStderr;
  EXPECT_TRUE(RetryStdout.empty());
  EXPECT_TRUE(RetryStderr.empty());
  EXPECT_TRUE(traceHasMarker(RetryFiles.Trace, LinkerMarker));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}
