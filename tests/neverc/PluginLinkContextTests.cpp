#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "ProcessResourceBrokerInternal.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

using namespace linker;

namespace {

struct TrackedValue {
  explicit TrackedValue(std::atomic<unsigned> &DestroyedValue)
      : Destroyed(DestroyedValue) {}
  ~TrackedValue() { Destroyed.fetch_add(1, std::memory_order_relaxed); }

  std::atomic<unsigned> &Destroyed;
};

} // namespace

TEST(PluginLinkContextTest, CleanupAndArenaObjectsFinishExactlyOnce) {
  std::atomic<unsigned> CleanupCount{0};
  std::atomic<unsigned> DestroyedCount{0};
  {
    LinkerExecutionContext Execution;
    CommonLinkerContext &Context =
        Execution.createBackend<CommonLinkerContext>();
    ASSERT_EQ(currentLinkerContext(), &Context);

    Context.e.cleanupCallback = [&] {
      EXPECT_EQ(currentLinkerContext(), &Context);
      CleanupCount.fetch_add(1, std::memory_order_relaxed);
    };
    (void)make<TrackedValue>(DestroyedCount);
  }

  EXPECT_EQ(CleanupCount.load(std::memory_order_relaxed), 1U);
  EXPECT_EQ(DestroyedCount.load(std::memory_order_relaxed), 1U);
  EXPECT_FALSE(hasContext());
}

TEST(PluginLinkContextTest, NestedExecutionRestoresOuterContext) {
  LinkerExecutionContext OuterExecution;
  CommonLinkerContext &Outer =
      OuterExecution.createBackend<CommonLinkerContext>();
  ASSERT_EQ(currentLinkerContext(), &Outer);

  {
    LinkerExecutionContext InnerExecution(OuterExecution.resourceSession());
    CommonLinkerContext &Inner =
        InnerExecution.createBackend<CommonLinkerContext>();
    ASSERT_EQ(currentLinkerContext(), &Inner);
    EXPECT_TRUE(Inner.resourceSession().refersToSameSession(
        Outer.resourceSession()));
  }

  EXPECT_EQ(currentLinkerContext(), &Outer);
}

TEST(PluginLinkContextTest, NestedExecutionRestoresOuterWorkerSlot) {
  neverc::ProcessResourceBrokerConfig Config;
  Config.Enabled = true;
  Config.CpuTokens = 2;
  auto Broker = neverc::ProcessResourceBrokerTestAccess::create(Config);
  neverc::ScopedProcessResourceBrokerOverride Override(*Broker);

  LinkerExecutionContext OuterExecution;
  CommonLinkerContext &Outer =
      OuterExecution.createBackend<CommonLinkerContext>();
  Outer.configureParallel(/*RequestedThreads=*/2);

  unsigned BeforeNested = 0;
  unsigned BeforeNestedActiveTokens = 0;
  unsigned InsideNested = 1;
  unsigned AfterNested = 0;
  std::thread::id OuterWorker;
  std::thread::id NestedTaskWorker;
  unsigned NestedTaskSlot = 0;
  unsigned NestedActiveTokens = 0;
  bool NestedSharesResourceSession = false;
  LinkerTaskGroup Group;
  Group.spawn([&] {
    OuterWorker = std::this_thread::get_id();
    BeforeNested = currentLinkerWorkerSlot();
    BeforeNestedActiveTokens =
        neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker)
            .ActiveTokens;
    {
      LinkerExecutionContext InnerExecution(OuterExecution.resourceSession());
      CommonLinkerContext &Inner =
          InnerExecution.createBackend<CommonLinkerContext>();
      InsideNested = currentLinkerWorkerSlot();
      NestedActiveTokens =
          neverc::ProcessResourceBrokerTestAccess::snapshot(*Broker)
              .ActiveTokens;
      NestedSharesResourceSession = Inner.resourceSession().refersToSameSession(
          Outer.resourceSession());
    }
    AfterNested = currentLinkerWorkerSlot();
    LinkerTaskGroup NestedGroup;
    NestedGroup.spawn([&] {
      NestedTaskWorker = std::this_thread::get_id();
      NestedTaskSlot = currentLinkerWorkerSlot();
    });
    NestedGroup.sync();
  });
  Group.sync();

  EXPECT_GT(BeforeNested, 0U);
  EXPECT_EQ(InsideNested, 0U);
  // The outer session's baseline token and this running worker's grant remain
  // active. Entering the nested execution must not acquire a third token.
  EXPECT_EQ(BeforeNestedActiveTokens, 2U);
  EXPECT_EQ(NestedActiveTokens, BeforeNestedActiveTokens);
  EXPECT_TRUE(NestedSharesResourceSession);
  EXPECT_EQ(AfterNested, BeforeNested);
  EXPECT_EQ(NestedTaskWorker, OuterWorker);
  EXPECT_EQ(NestedTaskSlot, BeforeNested);
}

TEST(PluginLinkContextTest, WorkerBindingRestoresReusedThreadTLS) {
  std::atomic<unsigned> DestroyedCount{0};
  CommonLinkerContext *Inside = nullptr;
  CommonLinkerContext *After = reinterpret_cast<CommonLinkerContext *>(1);
  unsigned WorkerSlot = 0;

  {
    LinkerExecutionContext Execution;
    CommonLinkerContext &Context =
        Execution.createBackend<CommonLinkerContext>();
    auto Bound = bindLinkerContext([&] {
      Inside = currentLinkerContext();
      WorkerSlot = currentLinkerWorkerSlot();
      (void)makeThreadLocal<TrackedValue>(DestroyedCount);
    });

    std::thread Worker([&] {
      Bound();
      After = currentLinkerContext();
    });
    Worker.join();

    EXPECT_EQ(Inside, &Context);
    EXPECT_GT(WorkerSlot, 0U);
    EXPECT_EQ(After, nullptr);
    EXPECT_EQ(DestroyedCount.load(std::memory_order_relaxed), 0U);
  }

  EXPECT_EQ(DestroyedCount.load(std::memory_order_relaxed), 1U);
}
