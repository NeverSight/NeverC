#include "Link/LinkRequest.h"
#include "Link/PluginLinkRegistry.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "neverc/Linker/Core/Runtime/Allocator.h"
#include "neverc/Linker/Core/Runtime/LinkerExecutionContext.h"
#include "neverc/Linker/Core/Runtime/LinkerParallel.h"
#include "neverc/Linker/ELF/ELFLinkerContext.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TargetA{UINT64_C(0x8100), UINT64_C(1)};
constexpr NevercTargetID TargetB{UINT64_C(0x8100), UINT64_C(2)};
constexpr NevercObjectFormatID ELFFormat{UINT64_C(0x8200), UINT64_C(1)};
constexpr NevercObjectFormatID COFFFormat{UINT64_C(0x8200), UINT64_C(2)};

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

PluginLinkSnapshot::LinkerProviderRecord
linker(StringRef ID, NevercTargetID Target = {},
       NevercObjectFormatID Input = {},
       NevercObjectFormatID Output = {},
       NevercLinkOutputKind Kind = 0,
       StringRef Compatibility = {}) {
  PluginLinkSnapshot::LinkerProviderRecord Record;
  Record.PluginID = "org.neverc.test.link-route";
  Record.ProviderID = ID.str();
  Record.TargetID = Target;
  Record.InputFormat = Input;
  Record.OutputFormat = Output;
  Record.OutputKind = Kind;
  Record.CompatibilityKey = Compatibility.str();
  Record.ProductID = {UINT64_C(0x8300), UINT64_C(1)};
  return Record;
}

PluginLinkSnapshot::ObjectMergeProviderRecord
merger(StringRef ID, NevercTargetID Target = {},
       NevercObjectFormatID Format = {}) {
  PluginLinkSnapshot::ObjectMergeProviderRecord Record;
  Record.PluginID = "org.neverc.test.link-route";
  Record.ProviderID = ID.str();
  Record.TargetID = Target;
  Record.FormatID = Format;
  Record.ProductID = {UINT64_C(0x8300), UINT64_C(2)};
  return Record;
}

LinkRouteRequest request() {
  LinkRouteRequest Request;
  Request.TargetID = TargetA;
  Request.InputFormat = ELFFormat;
  Request.OutputFormat = ELFFormat;
  Request.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  Request.CompatibilityKey = "target-key-a";
  return Request;
}

TEST(PluginLinkRouteTest, PublishesIndependentLinkAndLTOInterfaces) {
  PluginProcessServices Services{"neverc-plugin-link-route-tests", 1};
  ASSERT_FALSE(registerPluginLinkInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());

  auto Link = Services.interfaces().query(
      linkInterfaceID(), NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR);
  auto LinkRegistrar = Services.interfaces().query(
      linkRegistrarInterfaceID(), NEVERC_LINK_REGISTRAR_API_MAJOR,
      NEVERC_LINK_REGISTRAR_API_MINOR);
  auto LTO = Services.interfaces().query(
      ltoInterfaceID(), NEVERC_LTO_API_MAJOR, NEVERC_LTO_API_MINOR);
  auto LTORegistrar = Services.interfaces().query(
      ltoRegistrarInterfaceID(), NEVERC_LTO_REGISTRAR_API_MAJOR,
      NEVERC_LTO_REGISTRAR_API_MINOR);

  ASSERT_TRUE(static_cast<bool>(Link)) << errorText(Link.takeError());
  ASSERT_TRUE(static_cast<bool>(LinkRegistrar))
      << errorText(LinkRegistrar.takeError());
  ASSERT_TRUE(static_cast<bool>(LTO)) << errorText(LTO.takeError());
  ASSERT_TRUE(static_cast<bool>(LTORegistrar))
      << errorText(LTORegistrar.takeError());
  EXPECT_NE(Link->Table, nullptr);
}

TEST(PluginLinkRouteTest, ExactMatchBeatsHostDefinedFallback) {
  std::array<PluginLinkSnapshot::LinkerProviderRecord, 2> Providers = {
      linker("org.neverc.test.fallback"),
      linker("org.neverc.test.exact", TargetA, ELFFormat, ELFFormat,
             NEVERC_LINK_OUTPUT_EXECUTABLE, "target-key-a")};
  auto Plan = LinkRoutePlanner::plan(Providers, {}, request());
  ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());
  ASSERT_NE(Plan->linkerProvider(), nullptr);
  EXPECT_EQ(Plan->linkerProvider()->ProviderID,
            "org.neverc.test.exact");
  EXPECT_GT(Plan->specificity(), 0U);
}

TEST(PluginLinkRouteTest, RejectsEqualSpecificityConflict) {
  std::array<PluginLinkSnapshot::LinkerProviderRecord, 2> Providers = {
      linker("org.neverc.test.first", TargetA, ELFFormat, ELFFormat,
             NEVERC_LINK_OUTPUT_EXECUTABLE, "target-key-a"),
      linker("org.neverc.test.second", TargetA, ELFFormat, ELFFormat,
             NEVERC_LINK_OUTPUT_EXECUTABLE, "target-key-a")};
  auto Plan = LinkRoutePlanner::plan(Providers, {}, request());
  ASSERT_FALSE(static_cast<bool>(Plan));
  EXPECT_NE(errorText(Plan.takeError()).find("ambiguous link.full route"),
            std::string::npos);
}

TEST(PluginLinkRouteTest, ReportsMissingTargetFormatAndForcedProvider) {
  std::array<PluginLinkSnapshot::LinkerProviderRecord, 1> Providers = {
      linker("org.neverc.test.coff", TargetB, COFFFormat, COFFFormat,
             NEVERC_LINK_OUTPUT_EXECUTABLE)};
  LinkRouteRequest Request = request();
  Request.ForcedProvider = "org.neverc.test.missing";
  auto Plan = LinkRoutePlanner::plan(Providers, {}, Request);
  ASSERT_FALSE(static_cast<bool>(Plan));
  const std::string Message = errorText(Plan.takeError());
  EXPECT_NE(Message.find("target/format/output kind"), std::string::npos);
  EXPECT_NE(Message.find("forced provider"), std::string::npos);
}

TEST(PluginLinkRouteTest, RelocatableOutputUsesObjectMergeRoute) {
  std::array<PluginLinkSnapshot::ObjectMergeProviderRecord, 2> Mergers = {
      merger("org.neverc.test.merge-fallback"),
      merger("org.neverc.test.merge-exact", TargetA, ELFFormat)};
  LinkRouteRequest Request = request();
  Request.OutputKind = NEVERC_LINK_OUTPUT_RELOCATABLE;
  auto Plan = LinkRoutePlanner::plan({}, Mergers, Request);
  ASSERT_TRUE(static_cast<bool>(Plan)) << errorText(Plan.takeError());
  EXPECT_EQ(Plan->kind(), PlannedLinkRoute::Kind::ObjectMerge);
  ASSERT_NE(Plan->objectMergeProvider(), nullptr);
  EXPECT_EQ(Plan->objectMergeProvider()->ProviderID,
            "org.neverc.test.merge-exact");
}

TEST(PluginLinkRouteTest, RepeatedPlansAreStableWithinSnapshot) {
  std::array<PluginLinkSnapshot::LinkerProviderRecord, 2> Providers = {
      linker("org.neverc.test.fallback"),
      linker("org.neverc.test.exact", TargetA, ELFFormat, ELFFormat,
             NEVERC_LINK_OUTPUT_EXECUTABLE, "target-key-a")};
  auto First = LinkRoutePlanner::plan(Providers, {}, request());
  auto Second = LinkRoutePlanner::plan(Providers, {}, request());
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  EXPECT_EQ(First->linkerProvider(), Second->linkerProvider());
  EXPECT_EQ(First->specificity(), Second->specificity());
}

TEST(PluginLinkRouteTest, LinkRequestOwnsFrozenInputAndTargetData) {
  auto Target = TargetKeyBuilder()
                    .setTargetID(TargetA)
                    .setTriple("x86_64-unknown-linux-gnu", "x86_64",
                               "unknown", "linux", "gnu")
                    .setCPU("generic", "generic")
                    .setFeatures({})
                    .setABI({UINT64_C(0x8400), UINT64_C(1)})
                    .setCallingConvention(
                        {UINT64_C(0x8400), UINT64_C(2)})
                    .setObjectFormat(ELFFormat)
                    .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                                       NEVERC_TARGET_CODE_MODEL_SMALL)
                    .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                                  NEVERC_TARGET_ENDIAN_LITTLE)
                    .setSchemaDigest(
                        "0123456789abcdef0123456789abcdef"
                        "0123456789abcdef0123456789abcdef")
                    .build();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  LinkRequestData Data;
  Data.Target = std::move(*Target);
  Data.InputFormat = ELFFormat;
  Data.OutputFormat = ELFFormat;
  Data.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  Data.OutputURI = "out.bin";
  Data.Inputs.push_back(
      {NEVERC_LINK_INPUT_OBJECT, NEVERC_LINK_INPUT_FLAG_NONE, 0,
       "vfs:///input.o", {}, {}, {}});

  auto Frozen = LinkRequest::create(std::move(Data));
  ASSERT_TRUE(static_cast<bool>(Frozen)) << errorText(Frozen.takeError());
  EXPECT_EQ((*Frozen)->inputs().front().LogicalURI, "vfs:///input.o");
  EXPECT_EQ((*Frozen)->cView().RawInputs.Inputs.Count, 1U);
  EXPECT_EQ((*Frozen)->target().TargetID.High, TargetA.High);
}

class CompletingHooks final : public linker::LinkExecutionHooks {
public:
  Expected<linker::LinkHookResult>
  execute(const linker::LinkExecutionRequest &,
          const linker::LinkerDriverConfig &, raw_ostream &,
          raw_ostream &) override {
    ++ExecuteCalls;
    return linker::LinkHookResult{
        linker::LinkHookDisposition::Completed, 0};
  }

  void complete(bool Success) noexcept override {
    ++CompleteCalls;
    CompletedSuccessfully = Success;
  }

  unsigned ExecuteCalls = 0;
  unsigned CompleteCalls = 0;
  bool CompletedSuccessfully = false;
};

unsigned BackendCalls = 0;

bool testBackend(ArrayRef<const char *>, raw_ostream &, raw_ostream &,
                 bool, bool, const linker::LinkerDriverConfig &) {
  ++BackendCalls;
  return true;
}

TEST(PluginLinkRouteTest, CompleteProviderRunsBeforeBackendSelection) {
  auto Hooks = std::make_shared<CompletingHooks>();
  linker::LinkerDriverConfig Config;
  Config.executionHooks = Hooks;
  Config.executionRequest =
      std::make_shared<linker::LinkExecutionRequest>();
  const linker::DriverDef Drivers[] = {
      {linker::Flavor::Gnu, testBackend}};
  std::string Output;
  raw_string_ostream Stream(Output);
  BackendCalls = 0;

  EXPECT_EQ(linker::dispatchLink(
                Drivers, linker::Flavor::Gnu, {}, Stream, Stream, Config),
            0);
  EXPECT_EQ(BackendCalls, 0U);
  EXPECT_EQ(Hooks->ExecuteCalls, 1U);
  EXPECT_EQ(Hooks->CompleteCalls, 1U);
  EXPECT_TRUE(Hooks->CompletedSuccessfully);
}

struct TrackedArenaValue {
  explicit TrackedArenaValue(unsigned &Destructions)
      : Destructions(Destructions) {}
  ~TrackedArenaValue() { ++Destructions; }
  unsigned &Destructions;
};

TEST(PluginLinkRouteTest, ExecutionContextOwnsArenaDestructors) {
  unsigned Destructions = 0;
  linker::LinkerExecutionContext Execution;
  Execution.createBackend<linker::CommonLinkerContext>();
  linker::make<TrackedArenaValue>(Destructions);

  EXPECT_TRUE(linker::hasContext());
  Execution.destroyBackend();
  EXPECT_EQ(Destructions, 1U);
  EXPECT_FALSE(linker::hasContext());
}

class TwoPartyBarrier {
public:
  void arriveAndWait() {
    std::unique_lock<std::mutex> Lock(Mutex);
    if (++Arrivals == 2) {
      Condition.notify_all();
      return;
    }
    Condition.wait(Lock, [&] { return Arrivals == 2; });
  }

private:
  std::mutex Mutex;
  std::condition_variable Condition;
  unsigned Arrivals = 0;
};

TEST(PluginLinkRouteTest, ELFBackendMutableStateIsTaskLocal) {
  TwoPartyBarrier Barrier;
  std::atomic<bool> Isolated{true};
  auto Run = [&](uint32_t Group, bool InGroup) {
    linker::LinkerExecutionContext Execution;
    Execution.createBackend<linker::elf::ELFLinkerContext>();
    linker::elf::elfNextGroupId() = Group;
    linker::elf::elfInputFileIsInGroup() = InGroup;
    Barrier.arriveAndWait();
    if (linker::elf::elfNextGroupId() != Group ||
        linker::elf::elfInputFileIsInGroup() != InGroup)
      Isolated.store(false, std::memory_order_release);
  };

  std::thread First(Run, 7, true);
  std::thread Second(Run, 19, false);
  First.join();
  Second.join();
  EXPECT_TRUE(Isolated.load(std::memory_order_acquire));
}

TEST(PluginLinkRouteTest, ParallelRuntimeIsScopedToExecutionContext) {
  TwoPartyBarrier Barrier;
  std::array<unsigned, 2> Counts{};
  std::array<bool, 2> ContextBound{};
  auto Run = [&](unsigned Index, unsigned Budget) {
    linker::LinkerExecutionContext Execution;
    auto &Context = Execution.createBackend<linker::CommonLinkerContext>();
    Context.configureParallel(Budget);
    Barrier.arriveAndWait();
    Counts[Index] = Context.parallelThreadCount();
    std::atomic<unsigned> Visits{0};
    std::atomic<bool> Bound{true};
    linker::parallelForWithContext(0, 256, [&](size_t) {
      if (linker::currentLinkerContext() != &Context)
        Bound.store(false, std::memory_order_release);
      Visits.fetch_add(1, std::memory_order_relaxed);
    });
    ContextBound[Index] =
        Bound.load(std::memory_order_acquire) &&
        Visits.load(std::memory_order_relaxed) == 256;
  };

  std::thread Serial(Run, 0, 1);
  std::thread Parallel(Run, 1, 2);
  Serial.join();
  Parallel.join();
  EXPECT_EQ(Counts[0], 1U);
  EXPECT_GE(Counts[1], 1U);
  EXPECT_LE(Counts[1], 2U);
  EXPECT_TRUE(ContextBound[0]);
  EXPECT_TRUE(ContextBound[1]);
}

} // namespace
