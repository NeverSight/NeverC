#include "Driver/ELFInputWorkload.h"
#include "Emit/PEChecksum.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/ContentHashWorkers.h"
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
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
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

std::unique_ptr<neverc::ProcessResourceBroker> makeResourceBroker(
    unsigned Tokens,
    neverc::ProcessResourceBrokerTestAccess::Observer Observer = {}) {
  neverc::ProcessResourceBrokerConfig Config;
  Config.Enabled = true;
  Config.CpuTokens = Tokens;
  return neverc::ProcessResourceBrokerTestAccess::create(Config,
                                                         std::move(Observer));
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
  // The legacy ELF-only wrapper predates the cross-backend automatic cap and
  // must continue to honor the caller-supplied capacity exactly.
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/64,
                                  /*InputBytes=*/UINT64_MAX,
                                  /*InputFiles=*/1),
            64U);
}

TEST(PluginParallelLinkTest,
     AutomaticLinkThreadsHandleSaturatedOneByteDivisor) {
  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 1;
  Policy.BytesPerAdditionalThread = 1;
  Policy.MinAverageFileBytes = 0;
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0,
                /*AvailableThreads=*/16,
                /*InputBytes=*/UINT64_MAX,
                /*InputFiles=*/1, Policy),
            16U);
}

TEST(PluginParallelLinkTest, AutomaticLinkThreadsHonorPolicyCapDirectly) {
  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 1;
  Policy.BytesPerAdditionalThread = 0;
  Policy.MinAverageFileBytes = 0;
  Policy.MaxAutoThreads = 16;
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0,
                /*AvailableThreads=*/64,
                /*InputBytes=*/UINT64_MAX,
                /*InputFiles=*/1, Policy),
            16U);

  Policy.MaxAutoThreads = 0;
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0,
                /*AvailableThreads=*/64,
                /*InputBytes=*/UINT64_MAX,
                /*InputFiles=*/1, Policy),
            64U);

  Policy.MaxAutoThreads = 16;
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/48,
                /*AvailableThreads=*/64,
                /*InputBytes=*/0,
                /*InputFiles=*/0, Policy),
            48U);
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

TEST(PluginParallelLinkTest, RepeatedParallelConfigurationIsIdempotent) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "parallel configuration needs two available CPUs";

  CommonLinkerContext Context;
  Context.configureParallel(/*RequestedThreads=*/2);
  ASSERT_EQ(Context.parallelThreadCount(), 2U);

  Context.configureParallel(/*RequestedThreads=*/7);
  EXPECT_EQ(Context.parallelThreadCount(), 2U);
  EXPECT_EQ(Context.configureParallelForInputWorkload(
                /*RequestedThreads=*/0, /*InputBytes=*/UINT64_MAX,
                /*InputFiles=*/1),
            2U);
  EXPECT_EQ(Context.parallelThreadCount(), 2U);
}

TEST(PluginParallelLinkTest,
     ProvisionalSerialDecisionCanGrowAfterMaterialization) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "automatic worker selection needs two available CPUs";

  CommonLinkerContext Context;
  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 8ULL * 1024ULL * 1024ULL;
  Policy.BytesPerAdditionalThread = 0;
  Policy.MinAverageFileBytes = 0;

  EXPECT_EQ(Context.configureParallelForInputWorkload(
                /*RequestedThreads=*/0, /*InputBytes=*/1024,
                /*InputFiles=*/1, Policy, /*FinalizeSerial=*/false),
            1U);
  EXPECT_FALSE(Context.parallelConfigured());

  EXPECT_EQ(Context.configureParallelForInputWorkload(
                /*RequestedThreads=*/0,
                /*InputBytes=*/9ULL * 1024ULL * 1024ULL,
                /*InputFiles=*/1, Policy, /*FinalizeSerial=*/true),
            std::min(16U, llvm::thread::hardware_concurrency()));
  EXPECT_TRUE(Context.parallelConfigured());
}

TEST(PluginParallelLinkTest,
     ELFPostLTOWorkloadReplacesBitcodeWithNativeObjects) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  const auto PreLTO = linker::elf::detail::mergeMaterializedInputWorkload(
      /*NativeBytes=*/0, /*NativeFiles=*/0,
      /*BitcodeBytes=*/MiB, /*BitcodeFiles=*/1,
      /*BinaryBytes=*/0, /*BinaryFiles=*/0,
      /*IncludeBitcode=*/true);
  const auto PostLTO = linker::elf::detail::mergeMaterializedInputWorkload(
      /*NativeBytes=*/64 * MiB, /*NativeFiles=*/2,
      /*BitcodeBytes=*/MiB, /*BitcodeFiles=*/1,
      /*BinaryBytes=*/0, /*BinaryFiles=*/0,
      /*IncludeBitcode=*/false);

  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/16, PreLTO.first,
                                  PreLTO.second),
            1U);
  EXPECT_EQ(PostLTO.first, 64 * MiB);
  EXPECT_EQ(PostLTO.second, 2U);
  EXPECT_EQ(selectLinkThreadCount(/*RequestedThreads=*/0,
                                  /*AvailableThreads=*/16, PostLTO.first,
                                  PostLTO.second),
            3U);
}

TEST(PluginParallelLinkTest,
     ELFDefersAutomaticPoolUntilBitcodeHasMaterialized) {
  EXPECT_FALSE(linker::elf::detail::shouldConfigureProvisionalLinkPool(
      /*RequestedThreads=*/0, /*BitcodeFiles=*/1,
      /*NativeCandidateThreads=*/3, /*MaximumAutoThreads=*/16));
  EXPECT_TRUE(linker::elf::detail::shouldConfigureProvisionalLinkPool(
      /*RequestedThreads=*/4, /*BitcodeFiles=*/1,
      /*NativeCandidateThreads=*/1, /*MaximumAutoThreads=*/16));
  EXPECT_TRUE(linker::elf::detail::shouldConfigureProvisionalLinkPool(
      /*RequestedThreads=*/0, /*BitcodeFiles=*/0,
      /*NativeCandidateThreads=*/3, /*MaximumAutoThreads=*/16));
  EXPECT_TRUE(linker::elf::detail::shouldConfigureProvisionalLinkPool(
      /*RequestedThreads=*/0, /*BitcodeFiles=*/1,
      /*NativeCandidateThreads=*/16, /*MaximumAutoThreads=*/16));
}

TEST(PluginParallelLinkTest,
     ELFMaximumNativeCandidateIsNotDilutedByTinySourceBitcode) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  LinkThreadPolicy Policy;
  const auto Native = linker::elf::detail::mergeMaterializedInputWorkload(
      /*NativeBytes=*/512 * MiB, /*NativeFiles=*/1,
      /*BitcodeBytes=*/0, /*BitcodeFiles=*/0,
      /*BinaryBytes=*/0, /*BinaryFiles=*/0,
      /*IncludeBitcode=*/false);
  const auto WithSourceBitcode =
      linker::elf::detail::mergeMaterializedInputWorkload(
          /*NativeBytes=*/512 * MiB, /*NativeFiles=*/1,
          /*BitcodeBytes=*/MiB, /*BitcodeFiles=*/1'000'000,
          /*BinaryBytes=*/0, /*BinaryFiles=*/0,
          /*IncludeBitcode=*/true);
  const unsigned NativeCandidate = selectAdaptiveLinkThreadCount(
      /*RequestedThreads=*/0, /*AvailableThreads=*/16, Native.first,
      Native.second, Policy);
  const unsigned DilutedCandidate = selectAdaptiveLinkThreadCount(
      /*RequestedThreads=*/0, /*AvailableThreads=*/16,
      WithSourceBitcode.first, WithSourceBitcode.second, Policy);

  ASSERT_EQ(NativeCandidate, 16u);
  ASSERT_EQ(DilutedCandidate, 1u)
      << "the fixture must reproduce source-bitcode average dilution";
  EXPECT_EQ(linker::elf::detail::selectProvisionalLinkPoolThreads(
                /*RequestedThreads=*/0, /*BitcodeFiles=*/1'000'000,
                NativeCandidate, /*MaximumAutoThreads=*/16),
            std::optional<unsigned>(16u));
}

TEST(PluginParallelLinkTest,
     DenseOutputThreadPolicyUsesFullBudgetAtItsThreshold) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 8 * MiB;
  Policy.BytesPerAdditionalThread = 0;
  Policy.MinAverageFileBytes = 0;
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0,
                /*AvailableThreads=*/16,
                /*InputBytes=*/8 * MiB,
                /*InputFiles=*/1, Policy),
            16U);

  // Dense output work is driven by total bytes. A large payload plus many
  // tiny contributors must not accidentally fall back to one thread.
  EXPECT_EQ(linker::selectAdaptiveLinkThreadCount(
                /*RequestedThreads=*/0,
                /*AvailableThreads=*/16,
                /*InputBytes=*/8 * MiB,
                /*InputFiles=*/4097, Policy),
            16U);
}

TEST(PluginParallelLinkTest, BuildIdHashKeepsSmallOutputsSerial) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  EXPECT_EQ(linker::detail::selectContentHashWorkerCount(
                /*OutputBytes=*/MiB,
                /*ChunkCount=*/2,
                /*AvailableWorkers=*/16),
            1U);
}

TEST(PluginParallelLinkTest, BuildIdHashUsesBoundedGrantedWorkers) {
  constexpr uint64_t MiB = 1024ULL * 1024ULL;
  EXPECT_EQ(linker::detail::selectContentHashWorkerCount(
                /*OutputBytes=*/2 * MiB,
                /*ChunkCount=*/2,
                /*AvailableWorkers=*/16),
            2U);
  EXPECT_EQ(linker::detail::selectContentHashWorkerCount(
                /*OutputBytes=*/8 * MiB,
                /*ChunkCount=*/8,
                /*AvailableWorkers=*/16),
            4U);
  EXPECT_EQ(linker::detail::selectContentHashWorkerCount(
                /*OutputBytes=*/8 * MiB,
                /*ChunkCount=*/8,
                /*AvailableWorkers=*/2),
            2U);
  EXPECT_EQ(linker::detail::selectContentHashWorkerCount(
                /*OutputBytes=*/8 * MiB,
                /*ChunkCount=*/8,
                /*AvailableWorkers=*/1),
            1U);
  EXPECT_EQ(linker::detail::selectContentHashWorkerCount(
                /*OutputBytes=*/8 * MiB,
                /*ChunkCount=*/8,
                /*AvailableWorkers=*/0),
            1U);
}

TEST(PluginParallelLinkTest,
     BuildIdHashChunkStripesSurviveEveryWorkerStartFailure) {
#if LLVM_ENABLE_THREADS && !LLVM_ON_UNIX && !defined(_WIN32)
  GTEST_SKIP() << "this std::thread fallback deliberately declines optional "
                  "worker starts";
#endif
  constexpr size_t ChunkCount = 37;
  constexpr unsigned WorkerCount = 4;

  // Zero means all starts are attempted.  One through three inject failure at
  // each optional worker position, including after earlier helpers are live.
  for (unsigned FailureAt : {0U, 1U, 2U, 3U}) {
    std::array<std::atomic<unsigned>, ChunkCount> Visits{};
    unsigned StartAttempts = 0;
    linker::detail::runContentHashChunkStripes(
        ChunkCount, WorkerCount,
        [&](size_t I) { Visits[I].fetch_add(1, std::memory_order_relaxed); },
        [&](llvm::thread &Worker, auto HashWorkerStripe) {
          ++StartAttempts;
          if (FailureAt != 0 && StartAttempts == FailureAt)
            return false;
          return Worker.try_create(/*StackSizeInBytes=*/0,
                                   std::move(HashWorkerStripe));
        });

    const unsigned ExpectedAttempts =
        FailureAt == 0 ? WorkerCount - 1 : FailureAt;
    EXPECT_EQ(StartAttempts, ExpectedAttempts)
        << "did not reach the requested injected failure, or continued after "
           "it";
    for (size_t I = 0; I < ChunkCount; ++I)
      EXPECT_EQ(Visits[I].load(std::memory_order_relaxed), 1U)
          << "chunk " << I << " was lost or repeated when start " << FailureAt
          << " failed";
  }
}

namespace {

uint32_t referencePEChecksum(llvm::ArrayRef<uint8_t> Image,
                             size_t ChecksumOffset) {
  uint64_t Sum = 0;
  for (size_t I = 0; I + 1 < Image.size(); I += 2) {
    if (I >= ChecksumOffset && I < ChecksumOffset + 4)
      continue;
    Sum += llvm::support::endian::read16le(Image.data() + I);
    Sum = (Sum & 0xffff) + (Sum >> 16);
  }
  if ((Image.size() & 1) != 0) {
    Sum += Image.back();
    Sum = (Sum & 0xffff) + (Sum >> 16);
  }
  Sum = (Sum & 0xffff) + (Sum >> 16);
  return static_cast<uint32_t>(Sum) + static_cast<uint32_t>(Image.size());
}

} // namespace

TEST(PluginParallelLinkTest,
     PEChecksumMatchesReferenceAcrossChunkAndOddByteBoundaries) {
  constexpr size_t MiB = 1024 * 1024;
  for (size_t Size : {size_t(64), size_t(65), MiB - 1, MiB, MiB + 1, MiB + 2,
                      2 * MiB + 257}) {
    SCOPED_TRACE(Size);
    std::vector<uint8_t> Image(Size);
    for (size_t I = 0; I < Image.size(); ++I)
      Image[I] = static_cast<uint8_t>((I * 131 + I / 17 + 29) & 0xff);

    const size_t ChecksumOffset = Size >= MiB + 2 ? MiB - 2 : 24;
    ASSERT_LE(ChecksumOffset + 4, Image.size());
    Image[ChecksumOffset + 0] = 0xde;
    Image[ChecksumOffset + 1] = 0xad;
    Image[ChecksumOffset + 2] = 0xbe;
    Image[ChecksumOffset + 3] = 0xef;

    EXPECT_EQ(linker::coff::detail::computePEChecksum(
                  Image, ChecksumOffset, /*ExplicitlySerial=*/true),
              referencePEChecksum(Image, ChecksumOffset));
  }
}

TEST(PluginParallelLinkTest,
     PEChecksumChunkEndDoesNotOverflowAtAddressSpaceLimit) {
  constexpr size_t MaxSize = std::numeric_limits<size_t>::max();
  constexpr size_t ChunkBytes = 1024 * 1024;
  EXPECT_EQ(linker::coff::detail::peChecksumChunkEnd(MaxSize, MaxSize - 7,
                                                     ChunkBytes),
            MaxSize);
  EXPECT_EQ(linker::coff::detail::peChecksumChunkEnd(
                MaxSize, MaxSize - ChunkBytes, ChunkBytes),
            MaxSize);
}

TEST(PluginParallelLinkTest,
     ContentHashPoolWorkUsesLinkWriteBudgetAndReturnsEveryToken) {
  if (llvm::thread::hardware_concurrency() < 2)
    GTEST_SKIP() << "persistent hash work needs two available CPUs";

  std::atomic<unsigned> LinkWriteGrants{0};
  std::atomic<unsigned> LinkWriteReleases{0};
  auto Broker = makeResourceBroker(
      /*Tokens=*/2, [&](const neverc::ProcessResourceBrokerEvent &Event) {
        if (Event.Phase != neverc::ResourcePhase::LinkWrite)
          return;
        if (Event.Kind ==
            neverc::ProcessResourceBrokerEventKind::WorkersGranted)
          LinkWriteGrants.fetch_add(1, std::memory_order_relaxed);
        else if (Event.Kind ==
                 neverc::ProcessResourceBrokerEventKind::WorkersReleased)
          LinkWriteReleases.fetch_add(1, std::memory_order_relaxed);
      });
  neverc::ScopedProcessResourceBrokerOverride Override(*Broker);

  constexpr size_t ChunkCount = 8;
  std::array<std::atomic<unsigned>, ChunkCount> Visits{};
  {
    neverc::ResourceSessionPermit Permit =
        Broker->acquireSession(neverc::ResourcePhase::LinkParseResolve);
    {
      CommonLinkerContext Context;
      Context.configureParallel(/*RequestedThreads=*/2);
      linker::detail::runContentHashChunks(
          /*OutputBytes=*/8ULL * 1024ULL * 1024ULL, ChunkCount,
          /*ExplicitlySerial=*/false,
          [&](size_t I) { Visits[I].fetch_add(1, std::memory_order_relaxed); });

      // Returning from the phase must also mean its grants are gone; waiting
      // for the pool destructor here would hide a late-release race.
      EXPECT_GE(LinkWriteGrants.load(std::memory_order_relaxed), 1U);
      EXPECT_EQ(LinkWriteReleases.load(std::memory_order_relaxed),
                LinkWriteGrants.load(std::memory_order_relaxed));
    }
  }

  for (size_t I = 0; I < ChunkCount; ++I)
    EXPECT_EQ(Visits[I].load(std::memory_order_relaxed), 1U);

  const neverc::ProcessResourceBrokerSnapshot Snapshot =
      neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.ActiveSessions, 0U);
}

TEST(PluginParallelLinkTest,
     BuildIdHashUsesLinkWriteGrantAndHonorsExplicitSerialMode) {
  const unsigned availableWorkers =
      std::min(4U, llvm::thread::hardware_concurrency());
  if (availableWorkers < 2)
    GTEST_SKIP() << "transient hash workers need two available CPUs";

  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute("x86_64-unknown-linux-gnu");
  ASSERT_NE(Route, nullptr);
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  ASSERT_TRUE(static_cast<bool>(Target))
      << llvm::toString(Target.takeError()).str().str();

  const std::string Assembly = R"(
.text
.globl build_id_entry
.type build_id_entry,@function
build_id_entry:
  ret
.section .rodata.large,"a",@progbits
.zero 4194561
)";
  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream ObjectStream(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      llvm::Triple(llvm::Triple::normalize(Route->CanonicalTriple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "build-id-hash.s");
  Request.Output = &ObjectStream;
  if (llvm::Error Error = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    FAIL() << llvm::toString(std::move(Error)).str().str();
  ASSERT_LT(Object.size(), 16U * 1024U * 1024U);

  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  constexpr llvm::StringLiteral ObjectPath = "/virtual/build-id-hash.o";
  llvm::SmallString<0> &ObjectBytes = Store.create(ObjectPath, Object.size());
  ObjectBytes.append(Object.begin(), Object.end());
  Store.freeze();

  llvm::SmallString<128> ParallelOutput;
  llvm::SmallString<128> SerialOutput;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(
      "neverc-build-id-parallel", "elf", ParallelOutput);
  ASSERT_FALSE(EC) << EC.message();
  EC = llvm::sys::fs::createTemporaryFile("neverc-build-id-serial", "elf",
                                          SerialOutput);
  ASSERT_FALSE(EC) << EC.message();
  llvm::FileRemover RemoveParallelOutput(ParallelOutput);
  llvm::FileRemover RemoveSerialOutput(SerialOutput);

  std::atomic<unsigned> LinkWriteGrants{0};
  std::atomic<unsigned> LinkWriteReleases{0};
  std::atomic<unsigned> RequestedWorkers{0};
  std::atomic<unsigned> GrantedWorkers{0};
  auto Broker = makeResourceBroker(
      availableWorkers, [&](const neverc::ProcessResourceBrokerEvent &Event) {
        if (Event.Phase != neverc::ResourcePhase::LinkWrite)
          return;
        if (Event.Kind ==
            neverc::ProcessResourceBrokerEventKind::WorkersGranted) {
          LinkWriteGrants.fetch_add(1, std::memory_order_relaxed);
          RequestedWorkers.store(Event.RequestedWorkers,
                                 std::memory_order_relaxed);
          GrantedWorkers.store(Event.GrantedWorkers, std::memory_order_relaxed);
        } else if (Event.Kind ==
                   neverc::ProcessResourceBrokerEventKind::WorkersReleased) {
          LinkWriteReleases.fetch_add(1, std::memory_order_relaxed);
        }
      });
  neverc::ScopedProcessResourceBrokerOverride Override(*Broker);

  auto link = [&](llvm::StringRef OutputPath, unsigned ThreadCount) {
    LinkerExecutionContext Execution;
    LinkerDriverConfig Config;
    Config.executionContext = &Execution;
    Config.outputFile = OutputPath.str();
    Config.emulation = "elf_x86_64";
    Config.endianness = 1;
    Config.staticLink = true;
    Config.noDynamicLinker = true;
    Config.ehFrameHdr = false;
    Config.buildId = "sha1";
    Config.threadCount = ThreadCount;
    const char *Args[] = {"neverc-test-linker", "-e", "build_id_entry",
                          ObjectPath.data()};
    std::string Stdout;
    std::string Stderr;
    llvm::raw_string_ostream StdoutStream(Stdout);
    llvm::raw_string_ostream StderrStream(Stderr);
    EXPECT_TRUE(linker::elf::link(Args, StdoutStream, StderrStream,
                                  /*exitEarly=*/false,
                                  /*disableOutput=*/false, Config))
        << Stderr;
  };

  link(ParallelOutput, /*ThreadCount=*/0);
  EXPECT_EQ(LinkWriteGrants.load(std::memory_order_relaxed), 1U);
  EXPECT_EQ(LinkWriteReleases.load(std::memory_order_relaxed), 1U);
  EXPECT_EQ(RequestedWorkers.load(std::memory_order_relaxed), availableWorkers);
  EXPECT_EQ(GrantedWorkers.load(std::memory_order_relaxed), availableWorkers);

  link(SerialOutput, /*ThreadCount=*/1);
  EXPECT_EQ(LinkWriteGrants.load(std::memory_order_relaxed), 1U)
      << "an explicit one-thread link must not request transient workers";
  EXPECT_EQ(LinkWriteReleases.load(std::memory_order_relaxed), 1U);

  auto ParallelBytes = llvm::MemoryBuffer::getFile(ParallelOutput);
  ASSERT_TRUE(static_cast<bool>(ParallelBytes))
      << ParallelBytes.getError().message();
  auto SerialBytes = llvm::MemoryBuffer::getFile(SerialOutput);
  ASSERT_TRUE(static_cast<bool>(SerialBytes))
      << SerialBytes.getError().message();
  ASSERT_GT((*ParallelBytes)->getBufferSize(), 4U * 1024U * 1024U);
  EXPECT_TRUE((*ParallelBytes)->getBuffer() == (*SerialBytes)->getBuffer())
      << "transient hash workers changed the emitted ELF bytes";

  const neverc::ProcessResourceBrokerSnapshot Snapshot =
      neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.ActiveSessions, 0U);
  EXPECT_EQ(Snapshot.HighWaterTokens, availableWorkers);
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
