#include "Linker/COFF/Driver.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/ELF/Driver.h"
#include "Linker/MachO/Driver.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Foundation/Core/LLVMTimeTraceRootLease.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

using namespace linker;

LINKER_HAS_DRIVER(elf)
LINKER_HAS_DRIVER(coff)
LINKER_HAS_DRIVER(macho)

namespace {

void initializeMachOAssemblyTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
  });
}

constexpr llvm::StringLiteral BusyDiagnostic =
    "neverc: error: cannot start linker time trace: another traced "
    "in-process link is already active\n";
constexpr llvm::StringLiteral UnmanagedDiagnostic =
    "neverc: error: cannot start linker time trace: the current thread has "
    "an unmanaged LLVM time-trace profiler\n";

using DirectLink = bool (*)(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                            llvm::raw_ostream &, bool, bool,
                            const LinkerDriverConfig &);

struct TraceFiles {
  std::string Output;
  std::string Trace;
};

class CleanupReentrantTraceStream final : public llvm::raw_pwrite_stream {
public:
  CleanupReentrantTraceStream()
      : llvm::raw_pwrite_stream(/*Unbuffered=*/true) {}

  llvm::StringRef bytes() const { return {Bytes.data(), Bytes.size()}; }
  bool cleanedProfiler() const { return CleanedProfiler; }

private:
  uint64_t current_pos() const override { return Bytes.size(); }

  void write_impl(const char *Ptr, size_t Size) override {
    if (!CleanedProfiler) {
      CleanedProfiler = true;
      llvm::timeTraceProfilerCleanup();
    }
    Bytes.append(Ptr, Ptr + Size);
  }

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override {
    assert(Offset + Size <= Bytes.size() &&
           "time-trace stream pwrite exceeds rendered bytes");
    for (size_t I = 0; I != Size; ++I)
      Bytes[Offset + I] = Ptr[I];
  }

  llvm::SmallVector<char, 0> Bytes;
  bool CleanedProfiler = false;
};

std::error_code createTraceFiles(llvm::StringRef Stem, TraceFiles &Files) {
  llvm::SmallString<128> Output;
  if (std::error_code EC =
          llvm::sys::fs::createTemporaryFile(Stem, "image", Output))
    return EC;
  Files.Output = Output.str().str();
  Files.Trace = Files.Output + ".time-trace";
  return llvm::sys::fs::remove(Output);
}

testing::AssertionResult traceContainsOnlyMarker(llvm::StringRef Path,
                                                 llvm::StringRef Required,
                                                 llvm::StringRef Forbidden) {
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

  unsigned RequiredCount = 0;
  unsigned ForbiddenCount = 0;
  for (const llvm::json::Value &Value : *Events) {
    const llvm::json::Object *Event = Value.getAsObject();
    if (!Event || Event->getString("ph") != "X")
      continue;
    const llvm::StringRef Name = Event->getString("name");
    RequiredCount += Name == Required;
    ForbiddenCount += !Forbidden.empty() && Name == Forbidden;
  }
  if (RequiredCount != 1 || ForbiddenCount != 0)
    return testing::AssertionFailure()
           << "required marker count=" << RequiredCount
           << ", forbidden marker count=" << ForbiddenCount;
  return testing::AssertionSuccess();
}

struct TraceBackendPlan {
  std::mutex Mutex;
  std::condition_variable Condition;
  std::atomic<unsigned> Calls{0};
  std::string Marker;
  bool Hold = false;
  bool Ready = false;
  bool Release = false;

  bool waitUntilReady() {
    std::unique_lock<std::mutex> Lock(Mutex);
    return Condition.wait_for(Lock, std::chrono::seconds(10),
                              [&] { return Ready; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Release = true;
    }
    Condition.notify_all();
  }
};

thread_local TraceBackendPlan *ActiveTraceBackendPlan = nullptr;

class ScopedTraceBackendPlan {
public:
  explicit ScopedTraceBackendPlan(TraceBackendPlan &Plan)
      : Previous(ActiveTraceBackendPlan) {
    ActiveTraceBackendPlan = &Plan;
  }

  ~ScopedTraceBackendPlan() { ActiveTraceBackendPlan = Previous; }

  ScopedTraceBackendPlan(const ScopedTraceBackendPlan &) = delete;
  ScopedTraceBackendPlan &operator=(const ScopedTraceBackendPlan &) = delete;

private:
  TraceBackendPlan *Previous;
};

class ObservingHooks final : public LinkExecutionHooks {
public:
  llvm::Expected<LinkHookResult> execute(const LinkExecutionRequest &,
                                         const LinkerDriverConfig &,
                                         llvm::raw_ostream &,
                                         llvm::raw_ostream &) override {
    ++ExecuteCalls;
    return LinkHookResult{LinkHookDisposition::ContinueBuiltin, 0};
  }

  void complete(bool Success) noexcept override {
    ++CompleteCalls;
    CompletedSuccessfully = Success;
  }

  unsigned ExecuteCalls = 0;
  unsigned CompleteCalls = 0;
  bool CompletedSuccessfully = true;
};

bool plannedTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                         llvm::raw_ostream &, bool, bool,
                         const LinkerDriverConfig &) {
  TraceBackendPlan *Plan = ActiveTraceBackendPlan;
  if (!Plan)
    return false;
  Plan->Calls.fetch_add(1, std::memory_order_relaxed);

  const llvm::TimeTraceProfilerSession Session =
      llvm::timeTraceProfilerCurrentSession();
  std::thread Worker([Marker = Plan->Marker, Session] {
    if (llvm::Error Error = llvm::timeTraceProfilerInitializeThread(
            /*TimeTraceGranularity=*/0, "neverc-link-trace-worker", Session)) {
      llvm::consumeError(std::move(Error));
      return;
    }
    {
      llvm::TimeTraceScope MarkerScope(Marker);
    }
    llvm::timeTraceProfilerFinishThread();
  });
  Worker.join();

  if (Plan->Hold) {
    std::unique_lock<std::mutex> Lock(Plan->Mutex);
    Plan->Ready = true;
    Plan->Condition.notify_all();
    Plan->Condition.wait(Lock, [&] { return Plan->Release; });
  }
  return true;
}

bool successfulNoTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                              llvm::raw_ostream &, bool, bool,
                              const LinkerDriverConfig &) {
  llvm::TimeTraceScope Scope("neverc.test.untraced.success");
  return true;
}

bool fatalNoTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                         llvm::raw_ostream &, bool, bool,
                         const LinkerDriverConfig &) {
  llvm::TimeTraceScope AbandonedScope("neverc.test.untraced.dispatcher.fatal");
  llvm::CrashRecoveryContext::GetCurrent()->HandleExit(7);
}

int runPlannedDispatcher(Flavor LinkFlavor, const LinkerDriverConfig &Config,
                         TraceBackendPlan &Plan, std::string &Stdout,
                         std::string &Stderr) {
  ScopedTraceBackendPlan Scope(Plan);
  const DriverDef Drivers[] = {{LinkFlavor, plannedTraceBackend}};
  const char *Args[] = {"neverc-test-linker"};
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  return dispatchLink(Drivers, LinkFlavor, Args, StdoutStream, StderrStream,
                      Config);
}

int runLegacyLateWorkerIsolationProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return 231;

  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-legacy-old-root");
  bool RootActive = true;
  auto CleanupRoot = llvm::make_scope_exit([&] {
    if (RootActive && llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  std::mutex WorkerMutex;
  std::condition_variable WorkerCondition;
  bool WorkerReady = false;
  bool FinishWorker = false;
  constexpr llvm::StringLiteral OldMarker =
      "neverc.test.legacy.old.late.worker";
  std::thread LateWorker([&] {
    llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                      "neverc-legacy-old-worker");
    {
      llvm::TimeTraceScope Marker(OldMarker);
    }
    {
      std::unique_lock<std::mutex> Lock(WorkerMutex);
      WorkerReady = true;
      WorkerCondition.notify_all();
      WorkerCondition.wait(Lock, [&] { return FinishWorker; });
    }
    llvm::timeTraceProfilerFinishThread();
  });
  auto FinishAndJoinWorker = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(WorkerMutex);
      FinishWorker = true;
    }
    WorkerCondition.notify_all();
    if (LateWorker.joinable())
      LateWorker.join();
  });
  {
    std::unique_lock<std::mutex> Lock(WorkerMutex);
    if (!WorkerCondition.wait_for(Lock, std::chrono::seconds(10),
                                  [&] { return WorkerReady; }))
      return 232;
  }

  llvm::timeTraceProfilerCleanup();
  RootActive = false;

  TraceFiles ManagedFiles;
  if (createTraceFiles("neverc-legacy-late-worker-managed", ManagedFiles))
    return 233;
  llvm::FileRemover RemoveManagedOutput(ManagedFiles.Output);
  llvm::FileRemover RemoveManagedTrace(ManagedFiles.Trace);
  constexpr llvm::StringLiteral ManagedMarker =
      "neverc.test.managed.root.after.closing.legacy";
  {
    neverc::LLVMTimeTraceProfilerOwner ManagedOwner(
        /*Granularity=*/0, "neverc-managed-after-closing-legacy");
    if (!ManagedOwner.ownsProfiler())
      return 234;
    {
      llvm::TimeTraceScope Marker(ManagedMarker);
    }
    if (llvm::Error Error =
            ManagedOwner.write(ManagedFiles.Trace, std::string())) {
      llvm::consumeError(std::move(Error));
      return 235;
    }
    if (!traceContainsOnlyMarker(ManagedFiles.Trace, ManagedMarker, OldMarker))
      return 236;
  }

  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-legacy-fresh-root");
  RootActive = true;
  constexpr llvm::StringLiteral FreshMarker = "neverc.test.legacy.fresh.root";
  {
    llvm::TimeTraceScope Marker(FreshMarker);
  }

  {
    std::lock_guard<std::mutex> Lock(WorkerMutex);
    FinishWorker = true;
  }
  WorkerCondition.notify_all();
  LateWorker.join();
  FinishAndJoinWorker.release();

  TraceFiles Files;
  if (createTraceFiles("neverc-legacy-late-worker", Files))
    return 237;
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(Files.Trace, std::string())) {
    llvm::consumeError(std::move(Error));
    return 238;
  }
  if (!traceContainsOnlyMarker(Files.Trace, FreshMarker, OldMarker))
    return 239;

  llvm::timeTraceProfilerCleanup();
  RootActive = false;
  return 0;
}

int runProfilerOutputStreamReentryProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return 240;

  std::mutex WatchdogMutex;
  std::condition_variable WatchdogCondition;
  bool Completed = false;
  std::thread Watchdog([&] {
    std::unique_lock<std::mutex> Lock(WatchdogMutex);
    if (!WatchdogCondition.wait_for(Lock, std::chrono::seconds(10),
                                    [&] { return Completed; }))
      std::_Exit(241);
  });
  auto FinishWatchdog = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(WatchdogMutex);
      Completed = true;
    }
    WatchdogCondition.notify_all();
    Watchdog.join();
  });

  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-reentrant-trace-stream");
  auto CleanupProfiler = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });
  constexpr llvm::StringLiteral Marker =
      "neverc.test.reentrant.trace.stream";
  {
    llvm::TimeTraceScope MarkerScope(Marker);
  }

  CleanupReentrantTraceStream Output;
  llvm::timeTraceProfilerWrite(Output);
  if (!Output.cleanedProfiler() || llvm::timeTraceProfilerEnabled())
    return 242;
  auto Parsed = llvm::json::parse(Output.bytes());
  if (!Parsed) {
    llvm::consumeError(Parsed.takeError());
    return 243;
  }
  if (!Output.bytes().contains(Marker))
    return 244;
  return 0;
}

struct NestedTracePlan {
  Flavor LinkFlavor = Flavor::Invalid;
  std::string NestedOutput;
  int NestedResult = -1;
  std::atomic<unsigned> NestedCalls{0};
};

thread_local NestedTracePlan *ActiveNestedTracePlan = nullptr;

bool nestedTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                        llvm::raw_ostream &, bool, bool,
                        const LinkerDriverConfig &) {
  if (!ActiveNestedTracePlan)
    return false;
  ActiveNestedTracePlan->NestedCalls.fetch_add(1, std::memory_order_relaxed);
  llvm::TimeTraceScope Scope("neverc.test.nested.trace");
  return true;
}

bool outerTraceBackend(llvm::ArrayRef<const char *>, llvm::raw_ostream &Stdout,
                       llvm::raw_ostream &Stderr, bool, bool,
                       const LinkerDriverConfig &Config) {
  NestedTracePlan *Plan = ActiveNestedTracePlan;
  if (!Plan)
    return false;
  llvm::TimeTraceScope Scope("neverc.test.outer.trace");
  LinkerDriverConfig NestedConfig = Config;
  NestedConfig.outputFile = Plan->NestedOutput;
  const DriverDef Drivers[] = {{Plan->LinkFlavor, nestedTraceBackend}};
  const char *Args[] = {"neverc-test-linker"};
  Plan->NestedResult = dispatchLink(Drivers, Plan->LinkFlavor, Args, Stdout,
                                    Stderr, NestedConfig);
  return Plan->NestedResult == 0;
}

int runRawLeaseFatalBalanceProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return 10;
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  bool OwnedBeforeFatal = false;
  std::uint64_t FirstToken = 0;
  {
    llvm::CrashRecoveryContext CRC;
    if (CRC.RunSafely([&] {
          neverc::LLVMTimeTraceRootLease Lease;
          OwnedBeforeFatal = Lease.ownsRoot();
          FirstToken = neverc::time_trace_detail::
                           captureCurrentRootBindingForTesting()
                               .Token;
          llvm::CrashRecoveryContext::GetCurrent()->HandleExit(1);
        }))
      return 11;
    if (CRC.RetCode != 1)
      return 12;
  }
  if (!OwnedBeforeFatal)
    return 13;

  bool OwnedBeforeProvisionalFatal = false;
  {
    llvm::CrashRecoveryContext CRC;
    if (CRC.RunSafely([&] {
          neverc::LLVMTimeTraceRootLease Lease;
          OwnedBeforeProvisionalFatal = Lease.ownsRoot();
          llvm::timeTraceProfilerInitialize(
              /*TimeTraceGranularity=*/0,
              "neverc-test-provisional-profiler");
          llvm::CrashRecoveryContext::GetCurrent()->HandleExit(3);
        }))
      return 21;
    if (CRC.RetCode != 3)
      return 22;
  }
  if (!OwnedBeforeProvisionalFatal)
    return 23;
  if (llvm::timeTraceProfilerEnabled())
    return 24;

  std::uint64_t LastOuterToken = 0;
  for (unsigned Attempt = 0; Attempt != 2; ++Attempt) {
    neverc::LLVMTimeTraceProfilerOwner Outer(
        /*Granularity=*/0, "neverc-test-nested-crc-root");
    if (!Outer.ownsProfiler())
      return 14;
    const std::uint64_t OuterToken =
        neverc::time_trace_detail::captureCurrentRootBindingForTesting().Token;
    const std::uint64_t ExpectedToken =
        Attempt == 0 ? FirstToken + 4 : LastOuterToken + 2;
    if (FirstToken == 0 || OuterToken != ExpectedToken)
      return 19;
    LastOuterToken = OuterToken;

    bool BorrowedBeforeFatal = false;
    int NestedRecoveryCode = 0;
    {
      llvm::CrashRecoveryContext NestedCRC;
      if (NestedCRC.RunSafely([&] {
            neverc::LLVMTimeTraceRootLease Lease;
            BorrowedBeforeFatal = Lease.borrowsRoot();
            llvm::TimeTraceScope AbandonedScope(
                "neverc.test.raw.lease.abandoned.scope");
            llvm::CrashRecoveryContext::GetCurrent()->HandleExit(2);
          }))
        return 15;
      NestedRecoveryCode = NestedCRC.RetCode;
    }
    if (NestedRecoveryCode != 2 || !BorrowedBeforeFatal)
      return 16;
    if (Outer.ownsProfiler() || llvm::timeTraceProfilerEnabled())
      return 25;
    llvm::SmallVector<char, 0> InvalidatedTrace;
    llvm::raw_svector_ostream InvalidatedOutput(InvalidatedTrace);
    llvm::Error WriteError = Outer.write(InvalidatedOutput);
    if (!WriteError)
      return 26;
    const std::string WriteMessage =
        llvm::toString(std::move(WriteError)).str().str();
    if (!llvm::StringRef(WriteMessage)
             .contains("invalidated during crash recovery"))
      return 27;
  }

  if (llvm::timeTraceProfilerEnabled())
    return 17;
  neverc::LLVMTimeTraceProfilerOwner Retry(
      /*Granularity=*/0, "neverc-test-raw-lease-retry");
  if (!Retry.ownsProfiler())
    return 18;
  const std::uint64_t RetryToken =
      neverc::time_trace_detail::captureCurrentRootBindingForTesting().Token;
  return RetryToken == LastOuterToken + 2 ? 0 : 20;
}

int validateInvalidatedOuterTrace(
    neverc::LLVMTimeTraceProfilerOwner &Outer,
    llvm::StringRef Stem) {
  if (llvm::timeTraceProfilerEnabled())
    return 1;

  llvm::SmallString<128> TracePath;
  if (llvm::sys::fs::createTemporaryFile(Stem, "json", TracePath))
    return 2;
  llvm::FileRemover RemoveTrace(TracePath);
  std::error_code OpenError;
  llvm::raw_fd_ostream TraceOutput(TracePath, OpenError);
  if (OpenError)
    return 3;
  llvm::Error WriteError = Outer.write(TraceOutput);
  if (!WriteError)
    return 4;
  const std::string WriteMessage =
      llvm::toString(std::move(WriteError)).str().str();
  if (!llvm::StringRef(WriteMessage)
           .contains("invalidated during crash recovery"))
    return 5;
  return 0;
}

int runAmbientNoTraceDispatcherFatalProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return 10;
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  {
    neverc::LLVMTimeTraceProfilerOwner Outer(
        /*Granularity=*/0, "neverc-test-untraced-dispatcher-outer");
    if (!Outer.ownsProfiler())
      return 11;
    if (neverc::time_trace_detail::shouldGuardManagedRootAgainstCrash())
      return 12;

    LinkerDriverConfig Config;
    Config.timeTraceEnabled = false;
    const DriverDef Drivers[] = {{Flavor::Gnu, fatalNoTraceBackend}};
    const char *Args[] = {"neverc-test-linker"};
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    int Result = -1;
    bool GuardWasVisible = false;
    bool CompletedNormally = false;
    int RecoveryCode = 0;
    {
      llvm::CrashRecoveryContext CRC;
      CompletedNormally = CRC.RunSafely([&] {
        GuardWasVisible =
            neverc::time_trace_detail::shouldGuardManagedRootAgainstCrash();
        Result = dispatchLink(Drivers, Flavor::Gnu, Args, StdoutStream,
                              StderrStream, Config);
      });
      RecoveryCode = CRC.RetCode;
    }
    if (CompletedNormally)
      return 13;
    if (RecoveryCode != 7 || Result != -1 || !GuardWasVisible)
      return 14;
    if (!Stdout.empty() || !Stderr.empty())
      return 15;
    if (const int Error = validateInvalidatedOuterTrace(
            Outer, "neverc-untraced-dispatcher-invalidated"))
      return 20 + Error;
  }

  if (llvm::timeTraceProfilerEnabled())
    return 30;
  neverc::LLVMTimeTraceProfilerOwner Retry(
      /*Granularity=*/0, "neverc-test-untraced-dispatcher-retry");
  return Retry.ownsProfiler() ? 0 : 31;
}

int runAmbientNoTraceDirectCOFFFatalProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return 40;
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  TraceFiles FailedFiles;
  if (createTraceFiles("neverc-untraced-coff-failed", FailedFiles))
    return 41;
  llvm::FileRemover RemoveFailedOutput(FailedFiles.Output);

  {
    neverc::LLVMTimeTraceProfilerOwner Outer(
        /*Granularity=*/0, "neverc-test-untraced-coff-outer");
    if (!Outer.ownsProfiler())
      return 42;

    LinkerDriverConfig Config;
    Config.outputFile = FailedFiles.Output;
    Config.timeTraceEnabled = false;
    const char *Args[] = {"neverc-test-linker", "--machine=x64"};
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    bool Result = true;
    bool CompletedNormally = false;
    int RecoveryCode = 0;
    {
      llvm::CrashRecoveryContext CRC;
      CompletedNormally = CRC.RunSafely([&] {
        Result = linker::coff::link(
            Args, StdoutStream, StderrStream,
            /*exitEarly=*/false, /*disableOutput=*/false, Config);
      });
      RecoveryCode = CRC.RetCode;
    }
    if (CompletedNormally)
      return 43;
    if (RecoveryCode != 1 || !Result)
      return 44;
    if (llvm::sys::fs::exists(FailedFiles.Output))
      return 45;
    if (const int Error = validateInvalidatedOuterTrace(
            Outer, "neverc-untraced-coff-invalidated"))
      return 50 + Error;
  }

  if (llvm::timeTraceProfilerEnabled())
    return 60;
  neverc::LLVMTimeTraceProfilerOwner Retry(
      /*Granularity=*/0, "neverc-test-untraced-coff-retry");
  return Retry.ownsProfiler() ? 0 : 61;
}

int runUnmanagedAmbientNoTraceDispatcherProbe() {
  if (llvm::timeTraceProfilerEnabled())
    return 70;
  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-test-unmanaged-no-trace");
  llvm::TimeTraceProfiler *const AmbientProfiler =
      llvm::getTimeTraceProfilerInstance();
  {
    llvm::TimeTraceScope AmbientMarker(
        "neverc.test.unmanaged.no-trace.ambient");
  }
  auto CleanupProfiler = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  LinkerDriverConfig Config;
  Config.timeTraceEnabled = false;
  const DriverDef Drivers[] = {{Flavor::Gnu, fatalNoTraceBackend}};
  const char *Args[] = {"neverc-test-linker"};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  int Result = -1;
  llvm::CrashRecoveryContext CRC;
  if (!CRC.RunSafely([&] {
        Result = dispatchLink(Drivers, Flavor::Gnu, Args, StdoutStream,
                              StderrStream, Config);
      })) {
    // The RED implementation has already abandoned TimeTraceScope state in
    // the raw ambient profiler. Avoid a second assertion during test cleanup.
    llvm::errs().flush();
    std::_Exit(71);
  }
  if (Result != 1 || !Stdout.empty() || Stderr != UnmanagedDiagnostic)
    return 72;
  if (!llvm::timeTraceProfilerEnabled() ||
      llvm::getTimeTraceProfilerInstance() != AmbientProfiler)
    return 73;

  TraceFiles Files;
  if (createTraceFiles("neverc-unmanaged-no-trace", Files))
    return 74;
  llvm::FileRemover RemoveTrace(Files.Trace);
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(Files.Trace, std::string())) {
    llvm::consumeError(std::move(Error));
    return 75;
  }
  if (!traceContainsOnlyMarker(Files.Trace,
                               "neverc.test.unmanaged.no-trace.ambient",
                               "neverc.link.dispatch"))
    return 76;
  if (!traceContainsOnlyMarker(Files.Trace,
                               "neverc.test.unmanaged.no-trace.ambient",
                               "neverc.link.backend"))
    return 77;
  return 0;
}

int runUnmanagedAmbientNoTraceDirectProbe(DirectLink Direct) {
  if (llvm::timeTraceProfilerEnabled())
    return 80;
  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-test-unmanaged-direct");
  llvm::TimeTraceProfiler *const AmbientProfiler =
      llvm::getTimeTraceProfilerInstance();
  {
    llvm::TimeTraceScope AmbientMarker(
        "neverc.test.unmanaged.direct.ambient");
  }
  auto CleanupProfiler = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  TraceFiles Files;
  if (createTraceFiles("neverc-unmanaged-direct", Files))
    return 81;
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  LinkerExecutionContext ExternalExecution;
  LinkerDriverConfig Config;
  Config.executionContext = &ExternalExecution;
  Config.outputFile = Files.Output;
  Config.timeTraceEnabled = false;
  const char *Args[] = {"neverc-test-linker"};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  bool Result = true;
  llvm::CrashRecoveryContext CRC;
  if (!CRC.RunSafely([&] {
        Result = Direct(Args, StdoutStream, StderrStream,
                        /*exitEarly=*/false, /*disableOutput=*/false, Config);
      })) {
    llvm::errs().flush();
    std::_Exit(82);
  }
  if (Result || !Stdout.empty() || Stderr != UnmanagedDiagnostic)
    return 83;
  if (ExternalExecution.common() || llvm::sys::fs::exists(Files.Output))
    return 84;
  if (!llvm::timeTraceProfilerEnabled() ||
      llvm::getTimeTraceProfilerInstance() != AmbientProfiler)
    return 85;
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(Files.Trace, std::string())) {
    llvm::consumeError(std::move(Error));
    return 86;
  }
  return traceContainsOnlyMarker(Files.Trace,
                                 "neverc.test.unmanaged.direct.ambient", {})
             ? 0
             : 87;
}

} // namespace

TEST(PluginLinkTimeTraceConcurrencyTest,
     ConcurrentDispatcherRootsFailClosedAcrossFormats) {
  const std::array<std::pair<Flavor, llvm::StringLiteral>, 3> Formats = {{
      {Flavor::Gnu, "elf"},
      {Flavor::WinLink, "coff"},
      {Flavor::Darwin, "macho"},
  }};

  for (const auto &Format : Formats) {
    const Flavor LinkFlavor = Format.first;
    const llvm::StringLiteral Name = Format.second;
    SCOPED_TRACE(Name.str());
    TraceFiles FirstFiles;
    TraceFiles RejectedFiles;
    TraceFiles RetryFiles;
    ASSERT_FALSE(createTraceFiles("neverc-trace-root-first", FirstFiles));
    ASSERT_FALSE(createTraceFiles("neverc-trace-root-rejected", RejectedFiles));
    ASSERT_FALSE(createTraceFiles("neverc-trace-root-retry", RetryFiles));
    llvm::FileRemover RemoveFirstOutput(FirstFiles.Output);
    llvm::FileRemover RemoveFirstTrace(FirstFiles.Trace);
    llvm::FileRemover RemoveRejectedOutput(RejectedFiles.Output);
    llvm::FileRemover RemoveRejectedTrace(RejectedFiles.Trace);
    llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
    llvm::FileRemover RemoveRetryTrace(RetryFiles.Trace);

    TraceBackendPlan FirstPlan;
    FirstPlan.Marker = std::string("neverc.test.first.") + Name.str();
    FirstPlan.Hold = true;
    LinkerDriverConfig FirstConfig;
    FirstConfig.outputFile = FirstFiles.Output;
    FirstConfig.timeTraceEnabled = true;
    FirstConfig.timeTraceGranularity = 0;
    std::string FirstStdout;
    std::string FirstStderr;
    int FirstResult = -1;
    std::thread FirstThread([&] {
      FirstResult = runPlannedDispatcher(LinkFlavor, FirstConfig, FirstPlan,
                                         FirstStdout, FirstStderr);
    });
    auto ReleaseAndJoinFirst = llvm::make_scope_exit([&] {
      FirstPlan.release();
      if (FirstThread.joinable())
        FirstThread.join();
    });
    ASSERT_TRUE(FirstPlan.waitUntilReady());

    TraceBackendPlan RejectedPlan;
    RejectedPlan.Marker = std::string("neverc.test.rejected.") + Name.str();
    LinkerDriverConfig RejectedConfig;
    RejectedConfig.outputFile = RejectedFiles.Output;
    RejectedConfig.timeTraceEnabled = true;
    RejectedConfig.timeTraceGranularity = 0;
    auto RejectedHooks = std::make_shared<ObservingHooks>();
    RejectedConfig.executionHooks = RejectedHooks;
    RejectedConfig.executionRequest = std::make_shared<LinkExecutionRequest>();
    std::string RejectedStdout;
    std::string RejectedStderr;
    const int RejectedResult =
        runPlannedDispatcher(LinkFlavor, RejectedConfig, RejectedPlan,
                             RejectedStdout, RejectedStderr);

    FirstPlan.release();
    FirstThread.join();
    ReleaseAndJoinFirst.release();

    EXPECT_EQ(RejectedResult, 1);
    EXPECT_EQ(RejectedPlan.Calls.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(RejectedHooks->ExecuteCalls, 0U);
    EXPECT_EQ(RejectedHooks->CompleteCalls, 1U);
    EXPECT_FALSE(RejectedHooks->CompletedSuccessfully);
    EXPECT_TRUE(RejectedStdout.empty());
    EXPECT_EQ(RejectedStderr, BusyDiagnostic);
    EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Output));
    EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Trace));
    EXPECT_EQ(FirstResult, 0) << FirstStderr;
    EXPECT_TRUE(FirstStdout.empty());
    EXPECT_TRUE(FirstStderr.empty());
    EXPECT_TRUE(traceContainsOnlyMarker(FirstFiles.Trace, FirstPlan.Marker,
                                        RejectedPlan.Marker));
    EXPECT_FALSE(llvm::timeTraceProfilerEnabled());

    TraceBackendPlan RetryPlan;
    RetryPlan.Marker = std::string("neverc.test.retry.") + Name.str();
    LinkerDriverConfig RetryConfig = RejectedConfig;
    RetryConfig.outputFile = RetryFiles.Output;
    RetryConfig.executionHooks.reset();
    RetryConfig.executionRequest.reset();
    std::string RetryStdout;
    std::string RetryStderr;
    EXPECT_EQ(runPlannedDispatcher(LinkFlavor, RetryConfig, RetryPlan,
                                   RetryStdout, RetryStderr),
              0)
        << RetryStderr;
    EXPECT_EQ(RetryPlan.Calls.load(std::memory_order_relaxed), 1U);
    EXPECT_TRUE(RetryStdout.empty());
    EXPECT_TRUE(RetryStderr.empty());
    EXPECT_TRUE(traceContainsOnlyMarker(RetryFiles.Trace, RetryPlan.Marker,
                                        FirstPlan.Marker));
    EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  }
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     DirectBackendsFailClosedWhileDispatcherTraceIsActive) {
  const std::array<std::pair<llvm::StringLiteral, DirectLink>, 3> Backends = {{
      {"ELF", linker::elf::link},
      {"COFF", linker::coff::link},
      {"Mach-O", linker::macho::link},
  }};

  TraceFiles FirstFiles;
  ASSERT_FALSE(createTraceFiles("neverc-direct-trace-owner", FirstFiles));
  llvm::FileRemover RemoveFirstOutput(FirstFiles.Output);
  llvm::FileRemover RemoveFirstTrace(FirstFiles.Trace);
  TraceBackendPlan FirstPlan;
  FirstPlan.Marker = "neverc.test.direct.owner";
  FirstPlan.Hold = true;
  LinkerDriverConfig FirstConfig;
  FirstConfig.outputFile = FirstFiles.Output;
  FirstConfig.timeTraceEnabled = true;
  FirstConfig.timeTraceGranularity = 0;
  std::string FirstStdout;
  std::string FirstStderr;
  int FirstResult = -1;
  std::thread FirstThread([&] {
    FirstResult = runPlannedDispatcher(Flavor::Gnu, FirstConfig, FirstPlan,
                                       FirstStdout, FirstStderr);
  });
  auto ReleaseAndJoinFirst = llvm::make_scope_exit([&] {
    FirstPlan.release();
    if (FirstThread.joinable())
      FirstThread.join();
  });
  ASSERT_TRUE(FirstPlan.waitUntilReady());

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  for (const auto &Backend : Backends) {
    const llvm::StringLiteral Name = Backend.first;
    const DirectLink Link = Backend.second;
    SCOPED_TRACE(Name.str());
    TraceFiles RejectedFiles;
    ASSERT_FALSE(
        createTraceFiles("neverc-direct-trace-rejected", RejectedFiles));
    llvm::FileRemover RemoveRejectedOutput(RejectedFiles.Output);
    llvm::FileRemover RemoveRejectedTrace(RejectedFiles.Trace);
    LinkerExecutionContext ExternalExecution;
    LinkerDriverConfig Config;
    Config.executionContext = &ExternalExecution;
    Config.outputFile = RejectedFiles.Output;
    Config.timeTraceEnabled = true;
    Config.timeTraceGranularity = 0;
    const char *Args[] = {"neverc-test-linker"};
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    bool LinkResult = true;
    llvm::CrashRecoveryContext CRC;
    EXPECT_TRUE(CRC.RunSafely([&] {
      LinkResult = Link(Args, StdoutStream, StderrStream,
                        /*exitEarly=*/false, /*disableOutput=*/false, Config);
    }));
    EXPECT_FALSE(LinkResult);
    EXPECT_TRUE(Stdout.empty());
    EXPECT_EQ(Stderr, BusyDiagnostic);
    EXPECT_EQ(ExternalExecution.common(), nullptr);
    EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Output));
    EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Trace));
  }

  FirstPlan.release();
  FirstThread.join();
  ReleaseAndJoinFirst.release();
  EXPECT_EQ(FirstResult, 0) << FirstStderr;
  EXPECT_TRUE(traceContainsOnlyMarker(FirstFiles.Trace, FirstPlan.Marker, {}));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     UnmanagedAmbientProfilerIsRejectedWithoutMutation) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-unmanaged-profiler");
  llvm::TimeTraceProfiler *AmbientProfiler =
      llvm::getTimeTraceProfilerInstance();
  {
    llvm::TimeTraceScope AmbientMarker("neverc.test.unmanaged.ambient");
  }
  auto CleanupProfiler = llvm::make_scope_exit([] {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerCleanup();
  });

  TraceFiles Files;
  ASSERT_FALSE(createTraceFiles("neverc-unmanaged-trace", Files));
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  TraceBackendPlan Plan;
  Plan.Marker = "neverc.test.unmanaged";
  LinkerDriverConfig Config;
  Config.outputFile = Files.Output;
  Config.timeTraceEnabled = true;
  Config.timeTraceGranularity = 0;
  std::string Stdout;
  std::string Stderr;
  EXPECT_EQ(runPlannedDispatcher(Flavor::Gnu, Config, Plan, Stdout, Stderr), 1);
  EXPECT_EQ(Plan.Calls.load(std::memory_order_relaxed), 0U);
  EXPECT_TRUE(Stdout.empty());
  EXPECT_EQ(Stderr, UnmanagedDiagnostic);
  EXPECT_FALSE(llvm::sys::fs::exists(Files.Trace));
  EXPECT_TRUE(llvm::timeTraceProfilerEnabled());
  EXPECT_EQ(llvm::getTimeTraceProfilerInstance(), AmbientProfiler);

  const std::string AmbientTrace = Files.Trace + ".ambient";
  llvm::FileRemover RemoveAmbientTrace(AmbientTrace);
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(AmbientTrace, std::string()))
    ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  else
    EXPECT_TRUE(traceContainsOnlyMarker(
        AmbientTrace, "neverc.test.unmanaged.ambient", Plan.Marker));
  EXPECT_TRUE(traceContainsOnlyMarker(
      AmbientTrace, "neverc.test.unmanaged.ambient", "neverc.link.dispatch"));
  EXPECT_TRUE(traceContainsOnlyMarker(
      AmbientTrace, "neverc.test.unmanaged.ambient", "neverc.link.backend"));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     ForeignFinishedProfilerIsNeitherMergedNorDeleted) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  constexpr llvm::StringLiteral ForeignMarker =
      "neverc.test.foreign.finished.profiler";
  constexpr llvm::StringLiteral ManagedMarker =
      "neverc.test.managed.finished.profiler";
  std::thread ForeignWorker([ForeignMarker] {
    llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                      "neverc-foreign-finished-worker");
    {
      llvm::TimeTraceScope Marker(ForeignMarker);
    }
    llvm::timeTraceProfilerFinishThread();
  });
  ForeignWorker.join();
  auto CleanupForeignRegistry =
      llvm::make_scope_exit([] { llvm::timeTraceProfilerCleanup(); });

  TraceFiles ManagedFiles;
  ASSERT_FALSE(createTraceFiles("neverc-managed-session-filter", ManagedFiles));
  llvm::FileRemover RemoveManagedOutput(ManagedFiles.Output);
  llvm::FileRemover RemoveManagedTrace(ManagedFiles.Trace);
  {
    neverc::LLVMTimeTraceProfilerOwner Owner(
        /*Granularity=*/0, "neverc-managed-session-filter");
    ASSERT_TRUE(Owner.ownsProfiler()) << Owner.acquisitionError().str();
    {
      llvm::TimeTraceScope Marker(ManagedMarker);
    }
    if (llvm::Error Error = Owner.write(ManagedFiles.Trace, "unused"))
      ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  }
  EXPECT_TRUE(traceContainsOnlyMarker(ManagedFiles.Trace, ManagedMarker,
                                      ForeignMarker));

  // The managed owner must not destroy a foreign finished profiler while
  // filtering it from its own trace. A fresh legacy root still owns and emits
  // the foreign worker entry.
  const std::string ForeignTrace = ManagedFiles.Trace + ".foreign";
  llvm::FileRemover RemoveForeignTrace(ForeignTrace);
  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-foreign-finished-root");
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(ForeignTrace, std::string()))
    ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  else
    EXPECT_TRUE(
        traceContainsOnlyMarker(ForeignTrace, ForeignMarker, ManagedMarker));
  llvm::timeTraceProfilerCleanup();
  CleanupForeignRegistry.release();
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     ForeignActiveProfilerPreventsManagedRootClaim) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  std::mutex WorkerMutex;
  std::condition_variable WorkerCondition;
  bool WorkerReady = false;
  bool ReleaseWorker = false;
  std::thread ForeignWorker([&] {
    llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                      "neverc-foreign-active-worker");
    {
      std::unique_lock<std::mutex> Lock(WorkerMutex);
      WorkerReady = true;
      WorkerCondition.notify_all();
      WorkerCondition.wait(Lock, [&] { return ReleaseWorker; });
    }
    llvm::timeTraceProfilerCleanup();
  });
  auto ReleaseAndJoinWorker = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(WorkerMutex);
      ReleaseWorker = true;
    }
    WorkerCondition.notify_all();
    if (ForeignWorker.joinable())
      ForeignWorker.join();
  });
  {
    std::unique_lock<std::mutex> Lock(WorkerMutex);
    ASSERT_TRUE(WorkerCondition.wait_for(Lock, std::chrono::seconds(10),
                                         [&] { return WorkerReady; }));
  }

  neverc::LLVMTimeTraceProfilerOwner Attempt(
      /*Granularity=*/0, "neverc-managed-root-blocked-by-foreign");
  EXPECT_FALSE(Attempt.ownsProfiler());
  EXPECT_FALSE(Attempt.acquisitionError().empty());
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     LegacyRawRootStillCollectsLegacyWorker) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                    "neverc-legacy-raw-root");
  auto CleanupLegacy =
      llvm::make_scope_exit([] { llvm::timeTraceProfilerCleanup(); });
  constexpr llvm::StringLiteral WorkerMarker = "neverc.test.legacy.raw.worker";
  std::thread Worker([WorkerMarker] {
    llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                      "neverc-legacy-raw-worker");
    {
      llvm::TimeTraceScope Marker(WorkerMarker);
    }
    llvm::timeTraceProfilerFinishThread();
  });
  Worker.join();

  TraceFiles Files;
  ASSERT_FALSE(createTraceFiles("neverc-legacy-raw-session", Files));
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(Files.Trace, std::string()))
    ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  else
    EXPECT_TRUE(traceContainsOnlyMarker(Files.Trace, WorkerMarker, {}));

  constexpr llvm::StringLiteral LateMarker =
      "neverc.test.legacy.raw.late.worker";
  std::thread LateWorker([LateMarker] {
    llvm::timeTraceProfilerInitialize(/*TimeTraceGranularity=*/0,
                                      "neverc-legacy-late-worker");
    {
      llvm::TimeTraceScope Marker(LateMarker);
    }
    llvm::timeTraceProfilerCleanup();
  });
  LateWorker.join();

  const std::string RewrittenTrace = Files.Trace + ".rewritten";
  llvm::FileRemover RemoveRewrittenTrace(RewrittenTrace);
  if (llvm::Error Error =
          llvm::timeTraceProfilerWrite(RewrittenTrace, std::string()))
    ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  else
    EXPECT_TRUE(
        traceContainsOnlyMarker(RewrittenTrace, WorkerMarker, LateMarker));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     LegacyLateWorkerCannotPoisonFreshRootGeneration) {
  EXPECT_EXIT(std::exit(runLegacyLateWorkerIsolationProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     OutputStreamReentryDoesNotHoldTheProfilerRegistryLock) {
  EXPECT_EXIT(std::exit(runProfilerOutputStreamReentryProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     RootWriteRejectsUnfinishedManagedWorker) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  neverc::LLVMTimeTraceProfilerOwner Owner(
      /*Granularity=*/0, "neverc-managed-unfinished-worker");
  ASSERT_TRUE(Owner.ownsProfiler()) << Owner.acquisitionError().str();
  const llvm::TimeTraceProfilerSession Session =
      llvm::timeTraceProfilerCurrentSession();

  std::mutex WorkerMutex;
  std::condition_variable WorkerCondition;
  bool WorkerReady = false;
  bool WorkerInitialized = false;
  bool ReleaseWorker = false;
  constexpr llvm::StringLiteral WorkerMarker =
      "neverc.test.managed.unfinished.worker";
  std::thread Worker([&] {
    llvm::Error InitializeError = llvm::timeTraceProfilerInitializeThread(
        /*TimeTraceGranularity=*/0, "neverc-managed-unfinished-worker",
        Session);
    WorkerInitialized = !InitializeError;
    if (InitializeError)
      llvm::consumeError(std::move(InitializeError));
    if (WorkerInitialized) {
      llvm::TimeTraceScope Marker(WorkerMarker);
    }
    {
      std::unique_lock<std::mutex> Lock(WorkerMutex);
      WorkerReady = true;
      WorkerCondition.notify_all();
      WorkerCondition.wait(Lock, [&] { return ReleaseWorker; });
    }
    if (WorkerInitialized)
      llvm::timeTraceProfilerFinishThread();
  });
  auto ReleaseAndJoinWorker = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(WorkerMutex);
      ReleaseWorker = true;
    }
    WorkerCondition.notify_all();
    if (Worker.joinable())
      Worker.join();
  });
  {
    std::unique_lock<std::mutex> Lock(WorkerMutex);
    ASSERT_TRUE(WorkerCondition.wait_for(
        Lock, std::chrono::seconds(10), [&] { return WorkerReady; }));
  }
  ASSERT_TRUE(WorkerInitialized);

  llvm::SmallVector<char, 0> RejectedBytes;
  llvm::raw_svector_ostream RejectedOutput(RejectedBytes);
  llvm::Error RejectedWrite = Owner.write(RejectedOutput);
  ASSERT_TRUE(static_cast<bool>(RejectedWrite));
  EXPECT_TRUE(llvm::StringRef(llvm::toString(std::move(RejectedWrite)))
                  .contains("active worker profilers"));
  EXPECT_TRUE(RejectedBytes.empty());

  TraceFiles RejectedFiles;
  ASSERT_FALSE(
      createTraceFiles("neverc-managed-unfinished-worker", RejectedFiles));
  llvm::FileRemover RemoveRejectedOutput(RejectedFiles.Output);
  llvm::FileRemover RemoveRejectedTrace(RejectedFiles.Trace);
  llvm::Error RejectedFileWrite =
      Owner.write(RejectedFiles.Trace, "unused");
  ASSERT_TRUE(static_cast<bool>(RejectedFileWrite));
  EXPECT_TRUE(llvm::StringRef(llvm::toString(std::move(RejectedFileWrite)))
                  .contains("active worker profilers"));
  EXPECT_FALSE(llvm::sys::fs::exists(RejectedFiles.Trace));

  {
    std::lock_guard<std::mutex> Lock(WorkerMutex);
    ReleaseWorker = true;
  }
  WorkerCondition.notify_all();
  Worker.join();
  ReleaseAndJoinWorker.release();

  llvm::SmallVector<char, 0> TraceBytes;
  llvm::raw_svector_ostream TraceOutput(TraceBytes);
  if (llvm::Error Error = Owner.write(TraceOutput))
    ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  auto Parsed = llvm::json::parse(
      llvm::StringRef(TraceBytes.data(), TraceBytes.size()));
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << llvm::toString(Parsed.takeError()).str().str();
  EXPECT_TRUE(llvm::StringRef(TraceBytes.data(), TraceBytes.size())
                  .contains(WorkerMarker));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     ClosedManagedSessionDiscardsLateWorkerBeforeFreshGeneration) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  std::mutex WorkerMutex;
  std::condition_variable WorkerCondition;
  bool WorkerReady = false;
  bool WorkerInitialized = false;
  bool FinishWorker = false;
  constexpr llvm::StringLiteral OldMarker =
      "neverc.test.closed.session.late.worker";
  std::thread LateWorker;
  auto FinishAndJoinWorker = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(WorkerMutex);
      FinishWorker = true;
    }
    WorkerCondition.notify_all();
    if (LateWorker.joinable())
      LateWorker.join();
  });
  {
    neverc::LLVMTimeTraceProfilerOwner OldOwner(
        /*Granularity=*/0, "neverc-old-managed-session");
    ASSERT_TRUE(OldOwner.ownsProfiler()) << OldOwner.acquisitionError().str();
    const llvm::TimeTraceProfilerSession OldSession =
        llvm::timeTraceProfilerCurrentSession();
    LateWorker = std::thread([&, OldSession] {
      llvm::Error InitializeError = llvm::timeTraceProfilerInitializeThread(
          /*TimeTraceGranularity=*/0, "neverc-old-managed-worker", OldSession);
      WorkerInitialized = !InitializeError;
      if (InitializeError)
        llvm::consumeError(std::move(InitializeError));
      if (WorkerInitialized) {
        llvm::TimeTraceScope Marker(OldMarker);
      }
      {
        std::unique_lock<std::mutex> Lock(WorkerMutex);
        WorkerReady = true;
        WorkerCondition.notify_all();
        WorkerCondition.wait(Lock, [&] { return FinishWorker; });
      }
      if (WorkerInitialized)
        llvm::timeTraceProfilerFinishThread();
    });
    {
      std::unique_lock<std::mutex> Lock(WorkerMutex);
      ASSERT_TRUE(WorkerCondition.wait_for(
          Lock, std::chrono::seconds(10), [&] { return WorkerReady; }));
    }
    ASSERT_TRUE(WorkerInitialized);
  }
  TraceFiles FreshFiles;
  ASSERT_FALSE(createTraceFiles("neverc-fresh-managed-session", FreshFiles));
  llvm::FileRemover RemoveFreshOutput(FreshFiles.Output);
  llvm::FileRemover RemoveFreshTrace(FreshFiles.Trace);
  {
    neverc::LLVMTimeTraceProfilerOwner FreshOwner(
        /*Granularity=*/0, "neverc-fresh-managed-session");
    ASSERT_TRUE(FreshOwner.ownsProfiler())
        << FreshOwner.acquisitionError().str();
    constexpr llvm::StringLiteral FreshMarker =
        "neverc.test.fresh.session.root";
    {
      llvm::TimeTraceScope Marker(FreshMarker);
    }
    if (llvm::Error Error = FreshOwner.write(FreshFiles.Trace, "unused"))
      ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
    EXPECT_TRUE(traceContainsOnlyMarker(FreshFiles.Trace, FreshMarker,
                                        OldMarker));
  }

  {
    std::lock_guard<std::mutex> Lock(WorkerMutex);
    FinishWorker = true;
  }
  WorkerCondition.notify_all();
  LateWorker.join();
  FinishAndJoinWorker.release();

  neverc::LLVMTimeTraceProfilerOwner FinalOwner(
      /*Granularity=*/0, "neverc-final-managed-session");
  ASSERT_TRUE(FinalOwner.ownsProfiler())
      << FinalOwner.acquisitionError().str();
  llvm::SmallVector<char, 0> FinalBytes;
  llvm::raw_svector_ostream FinalOutput(FinalBytes);
  if (llvm::Error Error = FinalOwner.write(FinalOutput))
    ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  EXPECT_FALSE(llvm::StringRef(FinalBytes.data(), FinalBytes.size())
                   .contains(OldMarker));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     UnmanagedAmbientNoTraceCRCRejectsBeforeDispatcherScope) {
  EXPECT_EXIT(std::exit(runUnmanagedAmbientNoTraceDispatcherProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     UnmanagedAmbientNoTraceCRCRejectsEveryDirectBackend) {
  const std::array<DirectLink, 3> DirectLinks = {
      linker::elf::link, linker::coff::link, linker::macho::link};
  for (DirectLink Direct : DirectLinks)
    EXPECT_EXIT(std::exit(runUnmanagedAmbientNoTraceDirectProbe(Direct)),
                ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     DirectMachORelocatableLinkWritesCompleteTrace) {
  initializeMachOAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-apple-macosx13.0");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr llvm::StringLiteral Assembly = R"(
.text
.globl _neverc_relocatable_trace
_neverc_relocatable_trace:
  retq
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input =
      llvm::MemoryBufferRef(Assembly, "macho-relocatable-trace.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath =
      "/virtual/macho-relocatable-trace.o";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  Store.freeze();

  TraceFiles Files;
  ASSERT_FALSE(createTraceFiles("neverc-macho-relocatable-trace", Files));
  llvm::FileRemover RemoveOutput(Files.Output);
  llvm::FileRemover RemoveTrace(Files.Trace);
  LinkerExecutionContext Execution;
  LinkerDriverConfig Config;
  Config.executionContext = &Execution;
  Config.outputFile = Files.Output;
  Config.relocatable = true;
  Config.nostdlib = true;
  Config.archName = "x86_64";
  Config.platformName = "macos";
  Config.platformMinVersion = "13.0";
  Config.platformSdkVersion = "13.0";
  Config.threadCount = 1;
  Config.timeTraceEnabled = true;
  Config.timeTraceGranularity = 0;
  const char *Args[] = {"neverc-test-linker", ObjectPath.data()};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  ASSERT_TRUE(linker::macho::link(Args, StdoutStream, StderrStream,
                                  /*exitEarly=*/false,
                                  /*disableOutput=*/false, Config))
      << Stderr;
  auto Output = llvm::MemoryBuffer::getFile(Files.Output);
  ASSERT_TRUE(static_cast<bool>(Output)) << Output.getError().message();
  EXPECT_FALSE((*Output)->getBuffer().empty());
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  EXPECT_TRUE(traceContainsOnlyMarker(Files.Trace, "Relocatable merge", {}));
  EXPECT_TRUE(traceContainsOnlyMarker(Files.Trace, "ExecuteLinker", {}));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     OwnerReleaseWaitsForLastBorrowerAndBlocksNewGeneration) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  auto Owner = std::make_unique<neverc::LLVMTimeTraceProfilerOwner>(
      /*Granularity=*/0, "neverc-test-root-owner");
  ASSERT_TRUE(Owner->ownsProfiler());
  auto Borrower = std::make_unique<neverc::LLVMTimeTraceProfilerOwner>(
      /*Granularity=*/0, "neverc-test-root-borrower");
  ASSERT_EQ(Borrower->state(),
            neverc::LLVMTimeTraceRootLeaseState::Borrowed);
  const std::uint64_t InitialToken =
      neverc::time_trace_detail::captureCurrentRootBindingForTesting().Token;
  ASSERT_NE(InitialToken, 0U);

  Owner.reset();
  EXPECT_TRUE(llvm::timeTraceProfilerEnabled());

  neverc::LLVMTimeTraceProfilerOwner SameThreadAttempt(
      /*Granularity=*/0, "neverc-test-closing-root");
  EXPECT_EQ(SameThreadAttempt.state(),
            neverc::LLVMTimeTraceRootLeaseState::Inconsistent);
  EXPECT_EQ(neverc::time_trace_detail::captureCurrentRootBindingForTesting()
                .Token,
            InitialToken);

  neverc::LLVMTimeTraceRootLeaseState ConcurrentState =
      neverc::LLVMTimeTraceRootLeaseState::Released;
  std::thread ConcurrentRoot([&] {
    neverc::LLVMTimeTraceProfilerOwner Attempt(
        /*Granularity=*/0, "neverc-test-concurrent-root");
    ConcurrentState = Attempt.state();
  });
  ConcurrentRoot.join();
  EXPECT_EQ(ConcurrentState, neverc::LLVMTimeTraceRootLeaseState::Busy);

  Borrower.reset();
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  neverc::LLVMTimeTraceProfilerOwner Retry(
      /*Granularity=*/0, "neverc-test-root-retry");
  EXPECT_TRUE(Retry.ownsProfiler());
  EXPECT_EQ(neverc::time_trace_detail::captureCurrentRootBindingForTesting()
                .Token,
            InitialToken + 2);
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     OwnerReleaseThenBorrowerFatalReleasesGeneration) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  bool OwnerAcquired = false;
  bool BorrowerAcquired = false;
  bool ProfilerSurvivedOwnerRelease = false;
  std::uint64_t InitialToken = 0;
  {
    llvm::CrashRecoveryContext CRC;
    EXPECT_FALSE(CRC.RunSafely([&] {
      auto Owner = std::make_unique<neverc::LLVMTimeTraceProfilerOwner>(
          /*Granularity=*/0, "neverc-test-owner-before-borrower-fatal");
      OwnerAcquired = Owner->ownsProfiler();
      if (!OwnerAcquired)
        return;

      neverc::LLVMTimeTraceProfilerOwner Borrower(
          /*Granularity=*/0, "neverc-test-last-borrower-fatal");
      BorrowerAcquired =
          Borrower.state() == neverc::LLVMTimeTraceRootLeaseState::Borrowed;
      InitialToken = neverc::time_trace_detail::
                         captureCurrentRootBindingForTesting()
                             .Token;

      Owner.reset();
      ProfilerSurvivedOwnerRelease = llvm::timeTraceProfilerEnabled();
      llvm::CrashRecoveryContext::GetCurrent()->HandleExit(1);
    }));
    EXPECT_EQ(CRC.RetCode, 1);
  }
  EXPECT_TRUE(OwnerAcquired);
  EXPECT_TRUE(BorrowerAcquired);
  EXPECT_TRUE(ProfilerSurvivedOwnerRelease);
  EXPECT_NE(InitialToken, 0U);
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
  const neverc::time_trace_detail::RootGenerationBinding ReleasedBinding =
      neverc::time_trace_detail::captureCurrentRootBindingForTesting();
  EXPECT_EQ(ReleasedBinding.Token, 0U);
  EXPECT_EQ(ReleasedBinding.Profiler, nullptr);

  neverc::LLVMTimeTraceProfilerOwner Retry(
      /*Granularity=*/0, "neverc-test-last-borrower-fatal-retry");
  ASSERT_TRUE(Retry.ownsProfiler());
  EXPECT_EQ(neverc::time_trace_detail::captureCurrentRootBindingForTesting()
                .Token,
            InitialToken + 2);
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     StaleCrashBindingCannotInvalidateNewGeneration) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  neverc::time_trace_detail::RootGenerationBinding Stale;
  {
    neverc::LLVMTimeTraceProfilerOwner First(
        /*Granularity=*/0, "neverc-test-stale-root");
    ASSERT_TRUE(First.ownsProfiler());
    Stale = neverc::time_trace_detail::
        captureCurrentRootBindingForTesting();
    ASSERT_NE(Stale.Token, 0U);
    ASSERT_NE(Stale.Profiler, nullptr);
  }

  neverc::LLVMTimeTraceProfilerOwner Current(
      /*Granularity=*/0, "neverc-test-current-root");
  ASSERT_TRUE(Current.ownsProfiler());
  const neverc::time_trace_detail::RootGenerationBinding CurrentBinding =
      neverc::time_trace_detail::captureCurrentRootBindingForTesting();
  ASSERT_NE(CurrentBinding.Token, Stale.Token);
  ASSERT_NE(CurrentBinding.Profiler, nullptr);

  // Simulate allocator address reuse while preserving the abandoned cleanup's
  // old generation token. Token validation must reject the stale cleanup.
  Stale.Profiler = CurrentBinding.Profiler;
  neverc::time_trace_detail::invalidateProfilerAfterCrash(Stale);
  EXPECT_TRUE(Current.ownsProfiler());
  EXPECT_TRUE(llvm::timeTraceProfilerEnabled());
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     RootWriteWaitsForNestedParticipantToLeave) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  TraceFiles BorrowerBlockedFiles;
  TraceFiles ScopeBlockedFiles;
  TraceFiles CompletedFiles;
  ASSERT_FALSE(createTraceFiles("neverc-active-borrower-blocked",
                                BorrowerBlockedFiles));
  ASSERT_FALSE(
      createTraceFiles("neverc-active-scope-blocked", ScopeBlockedFiles));
  ASSERT_FALSE(
      createTraceFiles("neverc-active-borrower-completed", CompletedFiles));
  llvm::FileRemover RemoveBorrowerBlockedOutput(BorrowerBlockedFiles.Output);
  llvm::FileRemover RemoveBorrowerBlockedTrace(BorrowerBlockedFiles.Trace);
  llvm::FileRemover RemoveScopeBlockedOutput(ScopeBlockedFiles.Output);
  llvm::FileRemover RemoveScopeBlockedTrace(ScopeBlockedFiles.Trace);
  llvm::FileRemover RemoveCompletedOutput(CompletedFiles.Output);
  llvm::FileRemover RemoveCompletedTrace(CompletedFiles.Trace);

  neverc::LLVMTimeTraceProfilerOwner Owner(
      /*Granularity=*/0, "neverc-test-active-borrower-owner");
  ASSERT_TRUE(Owner.ownsProfiler());
  auto Borrower = std::make_unique<neverc::LLVMTimeTraceProfilerOwner>(
      /*Granularity=*/0, "neverc-test-active-borrower");
  ASSERT_EQ(Borrower->state(),
            neverc::LLVMTimeTraceRootLeaseState::Borrowed);
  llvm::Error BorrowerWriteError =
      Owner.write(BorrowerBlockedFiles.Trace, "unused");
  ASSERT_TRUE(static_cast<bool>(BorrowerWriteError));
  const std::string BorrowerWriteMessage =
      llvm::toString(std::move(BorrowerWriteError)).str().str();
  EXPECT_TRUE(llvm::StringRef(BorrowerWriteMessage)
                  .contains("active nested participants"));
  EXPECT_FALSE(llvm::sys::fs::exists(BorrowerBlockedFiles.Trace));

  Borrower.reset();
  constexpr llvm::StringLiteral ScopeMarker =
      "neverc.test.active.owner.scope";
  {
    llvm::TimeTraceScope ActiveScope(ScopeMarker);
    llvm::Error ScopeWriteError =
        Owner.write(ScopeBlockedFiles.Trace, "unused");
    ASSERT_TRUE(static_cast<bool>(ScopeWriteError));
    const std::string ScopeWriteMessage =
        llvm::toString(std::move(ScopeWriteError)).str().str();
    EXPECT_TRUE(
        llvm::StringRef(ScopeWriteMessage).contains("active scopes"));
  }
  EXPECT_FALSE(llvm::sys::fs::exists(ScopeBlockedFiles.Trace));

  if (llvm::Error Error = Owner.write(CompletedFiles.Trace, "unused"))
    FAIL() << llvm::toString(std::move(Error)).str().str();
  EXPECT_TRUE(
      traceContainsOnlyMarker(CompletedFiles.Trace, ScopeMarker, {}));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     BorrowedWriteRejectsInvalidatedGeneration) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  neverc::LLVMTimeTraceProfilerOwner Owner(
      /*Granularity=*/0, "neverc-test-invalidated-owner");
  ASSERT_TRUE(Owner.ownsProfiler());
  neverc::LLVMTimeTraceProfilerOwner Borrower(
      /*Granularity=*/0, "neverc-test-invalidated-borrower");
  ASSERT_EQ(Borrower.state(),
            neverc::LLVMTimeTraceRootLeaseState::Borrowed);

  const neverc::time_trace_detail::RootGenerationBinding Binding =
      neverc::time_trace_detail::captureCurrentRootBindingForTesting();
  neverc::time_trace_detail::invalidateProfilerAfterCrash(Binding);
  llvm::Error WriteError =
      Borrower.write("neverc-invalidated-borrower.json",
                     "neverc-invalidated-borrower-fallback.json");
  ASSERT_TRUE(static_cast<bool>(WriteError));
  const std::string WriteMessage =
      llvm::toString(std::move(WriteError)).str().str();
  EXPECT_TRUE(llvm::StringRef(WriteMessage)
                  .contains("invalidated during crash recovery"));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     RawLeaseFatalAndNestedRecoveryBalanceGenerationExactlyOnce) {
  EXPECT_EXIT(std::exit(runRawLeaseFatalBalanceProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     NoTraceDispatcherFatalInvalidatesManagedAmbientRoot) {
  EXPECT_EXIT(std::exit(runAmbientNoTraceDispatcherFatalProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     NoTraceDirectCOFFFatalInvalidatesManagedAmbientRoot) {
  EXPECT_EXIT(std::exit(runAmbientNoTraceDirectCOFFFatalProbe()),
              ::testing::ExitedWithCode(0), "");
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     NoTraceDispatcherDoesNotContendWithOtherThreadRoot) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  std::mutex Mutex;
  std::condition_variable Condition;
  bool Ready = false;
  bool Release = false;
  bool Owned = false;
  std::thread RootThread([&] {
    neverc::LLVMTimeTraceProfilerOwner Owner(
        /*Granularity=*/0, "neverc-test-other-thread-root");
    Owned = Owner.ownsProfiler();
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Ready = true;
    }
    Condition.notify_all();
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [&] { return Release; });
  });
  auto ReleaseAndJoin = llvm::make_scope_exit([&] {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Release = true;
    }
    Condition.notify_all();
    if (RootThread.joinable())
      RootThread.join();
  });
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    ASSERT_TRUE(Condition.wait_for(Lock, std::chrono::seconds(10),
                                   [&] { return Ready; }));
  }
  ASSERT_TRUE(Owned);

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  EXPECT_FALSE(
      neverc::time_trace_detail::shouldGuardManagedRootAgainstCrash());
  LinkerDriverConfig Config;
  Config.timeTraceEnabled = false;
  const DriverDef Drivers[] = {{Flavor::Darwin, successfulNoTraceBackend}};
  const char *Args[] = {"neverc-test-linker"};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  int Result = -1;
  llvm::CrashRecoveryContext CRC;
  EXPECT_TRUE(CRC.RunSafely([&] {
    Result = dispatchLink(Drivers, Flavor::Darwin, Args, StdoutStream,
                          StderrStream, Config);
  }));
  EXPECT_EQ(Result, 0) << Stderr;
  EXPECT_TRUE(Stdout.empty());
  EXPECT_TRUE(Stderr.empty());
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     LinkerTraceWriteFailureReturnsErrorWithoutTerminating) {
  if (!llvm::sys::fs::exists("/dev/full"))
    GTEST_SKIP() << "host has no deterministic failing output sink";
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  {
    neverc::LLVMTimeTraceProfilerOwner Owner(
        /*Granularity=*/0, "neverc-test-linker-trace-write-failure");
    ASSERT_TRUE(Owner.ownsProfiler());
    {
      llvm::TimeTraceScope Marker("neverc.test.trace.write.failure");
    }
    llvm::Error WriteError = Owner.write("/dev/full", "unused");
    ASSERT_TRUE(static_cast<bool>(WriteError));
    const std::string WriteMessage =
        llvm::toString(std::move(WriteError)).str().str();
    EXPECT_TRUE(llvm::StringRef(WriteMessage)
                    .contains("Could not write LLVM time trace"));
    EXPECT_TRUE(Owner.ownsProfiler());
  }
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());

  TraceFiles RetryFiles;
  ASSERT_FALSE(createTraceFiles("neverc-linker-trace-write-retry",
                                RetryFiles));
  llvm::FileRemover RemoveRetryTrace(RetryFiles.Trace);
  constexpr llvm::StringLiteral RetryMarker =
      "neverc.test.trace.write.retry";
  neverc::LLVMTimeTraceProfilerOwner Retry(
      /*Granularity=*/0, "neverc-test-linker-trace-write-retry");
  ASSERT_TRUE(Retry.ownsProfiler());
  {
    llvm::TimeTraceScope Marker(RetryMarker);
  }
  if (llvm::Error RetryError = Retry.write(RetryFiles.Trace, "unused"))
    ADD_FAILURE() << llvm::toString(std::move(RetryError)).str().str();
  EXPECT_TRUE(traceContainsOnlyMarker(RetryFiles.Trace, RetryMarker, {}));
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     NestedBorrowerFatalInvalidatesOuterRootAndAllowsFreshRetry) {
  ASSERT_FALSE(llvm::timeTraceProfilerEnabled());
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });

  bool Borrowed = false;
  constexpr llvm::StringLiteral LateWorkerMarker =
      "neverc.test.nested.fatal.late.worker";
  {
    neverc::LLVMTimeTraceProfilerOwner Outer(
        /*Granularity=*/0, "neverc-test-outer-trace");
    ASSERT_TRUE(Outer.ownsProfiler());

    std::mutex WorkerMutex;
    std::condition_variable WorkerCondition;
    bool WorkerReady = false;
    bool WorkerInitialized = false;
    bool FinishWorker = false;
    const llvm::TimeTraceProfilerSession Session =
        llvm::timeTraceProfilerCurrentSession();
    std::thread LateWorker([&, Session] {
      llvm::Error InitializeError = llvm::timeTraceProfilerInitializeThread(
          /*TimeTraceGranularity=*/0, "neverc-test-late-worker", Session);
      WorkerInitialized = !InitializeError;
      if (InitializeError)
        llvm::consumeError(std::move(InitializeError));
      {
        std::lock_guard<std::mutex> Lock(WorkerMutex);
        WorkerReady = true;
      }
      WorkerCondition.notify_all();
      if (!WorkerInitialized)
        return;
      {
        llvm::TimeTraceScope MarkerScope(LateWorkerMarker);
      }
      {
        std::unique_lock<std::mutex> Lock(WorkerMutex);
        WorkerCondition.wait(Lock, [&] { return FinishWorker; });
      }
      llvm::timeTraceProfilerFinishThread();
    });
    auto ReleaseAndJoinWorker = llvm::make_scope_exit([&] {
      {
        std::lock_guard<std::mutex> Lock(WorkerMutex);
        FinishWorker = true;
      }
      WorkerCondition.notify_all();
      if (LateWorker.joinable())
        LateWorker.join();
    });
    {
      std::unique_lock<std::mutex> Lock(WorkerMutex);
      ASSERT_TRUE(WorkerCondition.wait_for(
          Lock, std::chrono::seconds(10), [&] { return WorkerReady; }));
    }
    ASSERT_TRUE(WorkerInitialized);

    {
      llvm::CrashRecoveryContext NestedCRC;
      EXPECT_FALSE(NestedCRC.RunSafely([&] {
        neverc::LLVMTimeTraceProfilerOwner Inner(
            /*Granularity=*/0, "neverc-test-inner-trace");
        Borrowed =
            Inner.state() == neverc::LLVMTimeTraceRootLeaseState::Borrowed;
        llvm::TimeTraceScope AbandonedScope(
            "neverc.test.abandoned.nested.trace");
        llvm::CrashRecoveryContext::GetCurrent()->HandleExit(1);
      }));
      EXPECT_EQ(NestedCRC.RetCode, 1);
      EXPECT_TRUE(Borrowed);
    }
    EXPECT_FALSE(llvm::timeTraceProfilerEnabled());

    {
      std::lock_guard<std::mutex> Lock(WorkerMutex);
      FinishWorker = true;
    }
    WorkerCondition.notify_all();
    LateWorker.join();
    ReleaseAndJoinWorker.release();

    llvm::SmallString<128> PoisonedTrace;
    ASSERT_FALSE(llvm::sys::fs::createTemporaryFile(
        "neverc-poisoned-outer-trace", "json", PoisonedTrace));
    llvm::FileRemover RemovePoisonedTrace(PoisonedTrace);
    std::error_code OpenError;
    llvm::raw_fd_ostream PoisonedOutput(PoisonedTrace, OpenError);
    ASSERT_FALSE(OpenError);
    llvm::Error WriteError = Outer.write(PoisonedOutput);
    ASSERT_TRUE(static_cast<bool>(WriteError));
    const std::string WriteMessage =
        llvm::toString(std::move(WriteError)).str().str();
    EXPECT_TRUE(
        llvm::StringRef(WriteMessage)
            .contains("invalidated during crash recovery"));
  }

  TraceFiles RetryFiles;
  ASSERT_FALSE(createTraceFiles("neverc-nested-fatal-retry", RetryFiles));
  llvm::FileRemover RemoveRetryOutput(RetryFiles.Output);
  llvm::FileRemover RemoveRetryTrace(RetryFiles.Trace);
  TraceBackendPlan RetryPlan;
  RetryPlan.Marker = "neverc.test.nested.fatal.retry";
  LinkerDriverConfig RetryConfig;
  RetryConfig.outputFile = RetryFiles.Output;
  RetryConfig.timeTraceEnabled = true;
  RetryConfig.timeTraceGranularity = 0;
  std::string RetryStdout;
  std::string RetryStderr;
  EXPECT_EQ(runPlannedDispatcher(Flavor::Gnu, RetryConfig, RetryPlan,
                                 RetryStdout, RetryStderr),
            0)
      << RetryStderr;
  EXPECT_TRUE(RetryStdout.empty());
  EXPECT_TRUE(RetryStderr.empty());
  EXPECT_TRUE(traceContainsOnlyMarker(RetryFiles.Trace, RetryPlan.Marker,
                                      LateWorkerMarker));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}

TEST(PluginLinkTimeTraceConcurrencyTest,
     SameThreadNestedDispatcherBorrowsSingleManagedRoot) {
  TraceFiles OuterFiles;
  TraceFiles NestedFiles;
  ASSERT_FALSE(createTraceFiles("neverc-nested-trace-outer", OuterFiles));
  ASSERT_FALSE(createTraceFiles("neverc-nested-trace-inner", NestedFiles));
  llvm::FileRemover RemoveOuterOutput(OuterFiles.Output);
  llvm::FileRemover RemoveOuterTrace(OuterFiles.Trace);
  llvm::FileRemover RemoveNestedOutput(NestedFiles.Output);
  llvm::FileRemover RemoveNestedTrace(NestedFiles.Trace);

  NestedTracePlan Plan;
  Plan.LinkFlavor = Flavor::Darwin;
  Plan.NestedOutput = NestedFiles.Output;
  NestedTracePlan *PreviousPlan = ActiveNestedTracePlan;
  ActiveNestedTracePlan = &Plan;
  auto RestorePlan = llvm::make_scope_exit(
      [PreviousPlan] { ActiveNestedTracePlan = PreviousPlan; });
  LinkerDriverConfig Config;
  Config.outputFile = OuterFiles.Output;
  Config.timeTraceEnabled = true;
  Config.timeTraceGranularity = 0;
  const DriverDef Drivers[] = {{Plan.LinkFlavor, outerTraceBackend}};
  const char *Args[] = {"neverc-test-linker"};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  EXPECT_EQ(dispatchLink(Drivers, Plan.LinkFlavor, Args, StdoutStream,
                         StderrStream, Config),
            0)
      << Stderr;
  EXPECT_EQ(Plan.NestedResult, 0);
  EXPECT_EQ(Plan.NestedCalls.load(std::memory_order_relaxed), 1U);
  EXPECT_FALSE(llvm::sys::fs::exists(NestedFiles.Trace));
  EXPECT_TRUE(traceContainsOnlyMarker(OuterFiles.Trace,
                                      "neverc.test.nested.trace", {}));
  EXPECT_TRUE(
      traceContainsOnlyMarker(OuterFiles.Trace, "neverc.test.outer.trace", {}));
  EXPECT_FALSE(llvm::timeTraceProfilerEnabled());
}
