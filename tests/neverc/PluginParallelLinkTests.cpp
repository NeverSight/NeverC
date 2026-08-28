#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/ELF/Driver.h"
#include "ProcessResourceBrokerInternal.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#if LLVM_ENABLE_ZSTD
#include <zstd.h>
#endif

using namespace linker;
using namespace linker::elf;

LINKER_HAS_DRIVER(elf)

namespace {

class TwoPartyBarrier {
public:
  void arriveAndWait() {
    std::unique_lock<std::mutex> Lock(Mutex);
    if (++Arrived == 2) {
      Condition.notify_all();
      return;
    }
    Condition.wait(Lock, [&] { return Arrived == 2; });
  }

private:
  std::mutex Mutex;
  std::condition_variable Condition;
  unsigned Arrived = 0;
};

struct ParallelRunResult {
  std::atomic<bool> Failed{false};
  std::vector<unsigned> Values = std::vector<unsigned>(64);
  uintptr_t ContextAddress = 0;
  unsigned ThreadBudget = 0;
};

std::unique_ptr<neverc::ProcessResourceBroker>
makeResourceBroker(unsigned Tokens) {
  neverc::ProcessResourceBrokerConfig Config;
  Config.Enabled = true;
  Config.CpuTokens = Tokens;
  return neverc::ProcessResourceBrokerTestAccess::create(Config);
}

void initializeAssemblyTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
  });
}

#if LLVM_ENABLE_ZSTD
std::optional<std::vector<uint8_t>>
compressStreamingWithZstdWorkers(const std::vector<uint8_t> &Input,
                                 unsigned Workers) {
  ZSTD_CCtx *Context = ZSTD_createCCtx();
  if (!Context)
    return std::nullopt;
  if (ZSTD_isError(
          ZSTD_CCtx_setParameter(Context, ZSTD_c_nbWorkers, Workers))) {
    ZSTD_freeCCtx(Context);
    return std::nullopt;
  }

  std::vector<uint8_t> Output(std::max<size_t>(Input.size() / 2, 32));
  ZSTD_outBuffer OutputBuffer = {Output.data(), Output.size(), 0};
  const size_t BlockSize = ZSTD_CStreamInSize();
  size_t Position = 0;
  ZSTD_EndDirective Directive = ZSTD_e_continue;
  do {
    const size_t Bytes = std::min(Input.size() - Position, BlockSize);
    if (Bytes == Input.size() - Position)
      Directive = ZSTD_e_end;
    ZSTD_inBuffer InputBuffer = {Input.data() + Position, Bytes, 0};
    size_t BytesRemaining = 0;
    do {
      if (OutputBuffer.pos == OutputBuffer.size) {
        Output.resize(Output.size() * 3 / 2);
        OutputBuffer.dst = Output.data();
        OutputBuffer.size = Output.size();
      }
      BytesRemaining =
          ZSTD_compressStream2(Context, &OutputBuffer, &InputBuffer, Directive);
      if (ZSTD_isError(BytesRemaining)) {
        ZSTD_freeCCtx(Context);
        return std::nullopt;
      }
    } while (InputBuffer.pos != InputBuffer.size ||
             (Directive == ZSTD_e_end && BytesRemaining != 0));
    Position += Bytes;
  } while (Directive != ZSTD_e_end);

  Output.resize(OutputBuffer.pos);
  ZSTD_freeCCtx(Context);
  return Output;
}
#endif

} // namespace

TEST(PluginParallelLinkTest, AutomaticLinkThreadsKeepSmallWorkloadsSerial) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/16,
                                  /*InputBytes=*/8 * MiB,
                                  /*InputFiles=*/2),
            1U);
}

TEST(PluginParallelLinkTest, AutomaticLinkThreadsScaleDenseWorkloadsGradually) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/16,
                                  /*InputBytes=*/34 * MiB,
                                  /*InputFiles=*/1024),
            3U);
}

TEST(PluginParallelLinkTest, AutomaticLinkThreadsRespectAvailableCapacity) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/4,
                                  /*InputBytes=*/141 * MiB,
                                  /*InputFiles=*/4096),
            4U);
}

TEST(PluginParallelLinkTest, AutomaticLinkThreadsHandleSaturatedInputSize) {
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/16,
                                  /*InputBytes=*/UINT64_MAX,
                                  /*InputFiles=*/1),
            16U);
}

TEST(PluginParallelLinkTest, AutomaticLinkThreadsAvoidFineGrainedFanout) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/16,
                                  /*InputBytes=*/37 * MiB,
                                  /*InputFiles=*/32768),
            1U);
}

TEST(PluginParallelLinkTest, ExplicitLinkThreadCountOverridesAutomaticPolicy) {
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/7,
                                  /*AvailableThreads=*/16,
                                  /*InputBytes=*/0,
                                  /*InputFiles=*/0),
            7U);
}

TEST(PluginParallelLinkTest,
     AutomaticLinkThreadsIncludeEntryExtractedArchiveMembers) {
  llvm::ThreadPoolStrategy Strategy = llvm::hardware_concurrency();
  if (Strategy.compute_thread_count() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available threads";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-unknown-linux-gnu");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  const std::string Assembly = ".text\n"
                               ".globl late_archive_entry\n"
                               ".type late_archive_entry,@function\n"
                               "late_archive_entry:\n"
                               "  ret\n"
                               ".section .rodata.large,\"a\",@progbits\n"
                               ".zero 17825792\n";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "late-archive-member.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();
  ASSERT_GT(Object.size(), 16 * MiB);

  llvm::SmallVector<llvm::NewArchiveMember, 1> Members;
  Members.emplace_back(llvm::MemoryBufferRef(
      llvm::StringRef(Object.data(), Object.size()), "late-member.o"));
  auto Archive = llvm::writeArchiveToBuffer(
      Members, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_GNU, /*Deterministic=*/true, /*Thin=*/false);
  ASSERT_TRUE(static_cast<bool>(Archive))
      << llvm::toString(Archive.takeError()).str().str();

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ArchivePath = "/virtual/late-extract.a";
  llvm::SmallString<0> &ArchiveBytes =
      Store.create(ArchivePath, (*Archive)->getBufferSize());
  ArchiveBytes.append((*Archive)->getBuffer().begin(),
                      (*Archive)->getBuffer().end());
  Store.freeze();

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile("neverc-late-extract",
                                                          "elf", OutputPath);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveOutput(OutputPath);

  LinkerExecutionContext Execution;
  LinkerDriverConfig Config;
  Config.executionContext = &Execution;
  Config.outputFile = OutputPath.str().str();
  Config.emulation = "elf_x86_64";
  Config.endianness = 1;
  Config.staticLink = true;
  Config.noDynamicLinker = true;
  Config.ehFrameHdr = false;
  const char *Args[] = {"neverc-test-linker", "-e", "late_archive_entry",
                        ArchivePath.data()};
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  ASSERT_TRUE(linker::elf::link(Args, StdoutStream, StderrStream,
                                /*exitEarly=*/false,
                                /*disableOutput=*/false, Config))
      << Stderr;
  ASSERT_NE(Execution.common(), nullptr);
  EXPECT_EQ(Execution.common()->parallelThreadCount(), 2U);
}

TEST(PluginParallelLinkTest,
     ConcurrentExecutionsKeepBudgetsAndWorkersIsolated) {
  TwoPartyBarrier Barrier;
  ParallelRunResult First;
  ParallelRunResult Second;

  auto Run = [&](ParallelRunResult &Result, unsigned Budget, unsigned Marker) {
    {
      LinkerExecutionContext Execution;
      CommonLinkerContext &Context =
          Execution.createBackend<CommonLinkerContext>();
      Context.configureParallel(Budget);
      Result.ContextAddress = reinterpret_cast<uintptr_t>(&Context);
      Result.ThreadBudget = Context.parallelThreadCount();

      Barrier.arriveAndWait();
      parallelForWithContext(0, Result.Values.size(), [&](size_t Index) {
        if (currentLinkerContext() != &Context ||
            currentLinkerWorkerSlot() == 0 ||
            currentLinkerWorkerSlot() >= Context.parallelShardCount())
          Result.Failed.store(true, std::memory_order_relaxed);
        Result.Values[Index] = Marker;
      });

      if (currentLinkerContext() != &Context || currentLinkerWorkerSlot() != 0)
        Result.Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != nullptr)
      Result.Failed.store(true, std::memory_order_relaxed);
  };

  std::thread FirstThread([&] { Run(First, 2, 0x11U); });
  std::thread SecondThread([&] { Run(Second, 3, 0x22U); });
  FirstThread.join();
  SecondThread.join();

  EXPECT_FALSE(First.Failed.load(std::memory_order_relaxed));
  EXPECT_FALSE(Second.Failed.load(std::memory_order_relaxed));
  EXPECT_EQ(First.ThreadBudget, 2U);
  EXPECT_EQ(Second.ThreadBudget, 3U);
  EXPECT_NE(First.ContextAddress, Second.ContextAddress);
  for (unsigned Value : First.Values)
    EXPECT_EQ(Value, 0x11U);
  for (unsigned Value : Second.Values)
    EXPECT_EQ(Value, 0x22U);
}

TEST(PluginParallelLinkTest,
     ProcessBudgetCapsTwoSessionsWithoutChangingLogicalBudgets) {
  auto Broker = makeResourceBroker(2);
  neverc::ScopedProcessResourceBrokerOverride Override(*Broker);
  TwoPartyBarrier Barrier;
  ParallelRunResult First;
  ParallelRunResult Second;

  auto Run = [&](ParallelRunResult &Result, unsigned Budget, unsigned Marker) {
    LinkerExecutionContext Execution;
    CommonLinkerContext &Context =
        Execution.createBackend<CommonLinkerContext>();
    Context.configureParallel(Budget);
    Result.ThreadBudget = Context.parallelThreadCount();
    const neverc::ResourceSessionView ExpectedSession =
        Context.resourceSession();
    Barrier.arriveAndWait();
    parallelForWithContext(0, Result.Values.size(), [&](size_t Index) {
      if (currentLinkerContext() != &Context ||
          currentLinkerWorkerSlot() >= Context.parallelShardCount() ||
          !neverc::currentResourceSession().refersToSameSession(
              ExpectedSession))
        Result.Failed.store(true, std::memory_order_relaxed);
      Result.Values[Index] = Marker;
    });
  };

  std::thread FirstThread([&] { Run(First, 2, 0x31U); });
  std::thread SecondThread([&] { Run(Second, 3, 0x42U); });
  FirstThread.join();
  SecondThread.join();

  EXPECT_FALSE(First.Failed.load(std::memory_order_relaxed));
  EXPECT_FALSE(Second.Failed.load(std::memory_order_relaxed));
  EXPECT_EQ(First.ThreadBudget, 2U);
  EXPECT_EQ(Second.ThreadBudget, 3U);
  const neverc::ProcessResourceBrokerSnapshot Snapshot =
      neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
  EXPECT_EQ(Snapshot.HighWaterTokens, 2U);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.ActiveSessions, 0U);
}

TEST(PluginParallelLinkTest, BudgetedNestedTaskGroupRunsInlineOnOuterWorker) {
  auto Broker = makeResourceBroker(2);
  neverc::ScopedProcessResourceBrokerOverride Override(*Broker);
  LinkerExecutionContext Execution;
  CommonLinkerContext &Context = Execution.createBackend<CommonLinkerContext>();
  Context.configureParallel(2);

  std::thread::id OuterThread;
  std::thread::id NestedThread;
  unsigned OuterSlot = 0;
  unsigned NestedSlot = 0;
  LinkerTaskGroup Outer;
  Outer.spawn([&] {
    OuterThread = std::this_thread::get_id();
    OuterSlot = currentLinkerWorkerSlot();
    LinkerTaskGroup Nested;
    Nested.spawn([&] {
      NestedThread = std::this_thread::get_id();
      NestedSlot = currentLinkerWorkerSlot();
    });
  });
  Outer.sync();

  EXPECT_NE(OuterSlot, 0U);
  EXPECT_EQ(NestedThread, OuterThread);
  EXPECT_EQ(NestedSlot, OuterSlot);
}

TEST(PluginParallelLinkTest,
     BudgetedNestedCompressionKeepsOneAsynchronousWorker) {
  {
    neverc::ProcessResourceBrokerConfig Config;
    Config.Enabled = false;
    auto Broker = neverc::ProcessResourceBrokerTestAccess::create(Config);
    neverc::ScopedProcessResourceBrokerOverride Override(*Broker);
    EXPECT_EQ(nestedCompressionWorkerCount(/*DesiredWorkers=*/7), 7U);
  }

  {
    auto Broker = makeResourceBroker(/*Tokens=*/4);
    neverc::ScopedProcessResourceBrokerOverride Override(*Broker);
    EXPECT_EQ(nestedCompressionWorkerCount(/*DesiredWorkers=*/7), 1U);
  }
}

#if LLVM_ENABLE_ZSTD
TEST(PluginParallelLinkTest,
     ZstdStreamingBytesStayStableAcrossAsynchronousWorkerCounts) {
  std::vector<uint8_t> Input(16U * 1024U * 1024U);
  for (size_t Index = 0; Index != Input.size(); ++Index) {
    const uint8_t Structured = static_cast<uint8_t>((Index / 13U) & 0xffU);
    const uint8_t Perturbation =
        static_cast<uint8_t>((Index * 131U + Index / 257U) & 0xffU);
    Input[Index] = Index % 4096U < 3072U ? Structured : Perturbation;
  }

  auto OneWorker = compressStreamingWithZstdWorkers(Input, 1);
  if (!OneWorker)
    GTEST_SKIP() << "linked Zstd library has no asynchronous worker support";
  auto SevenWorkers = compressStreamingWithZstdWorkers(Input, 7);
  ASSERT_TRUE(SevenWorkers);
  ASSERT_EQ(OneWorker->size(), SevenWorkers->size());
  EXPECT_TRUE(
      std::equal(OneWorker->begin(), OneWorker->end(), SevenWorkers->begin()));
}
#endif
