#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
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
    LinkerExecutionContext InnerExecution;
    CommonLinkerContext &Inner =
        InnerExecution.createBackend<CommonLinkerContext>();
    ASSERT_EQ(currentLinkerContext(), &Inner);
  }

  EXPECT_EQ(currentLinkerContext(), &Outer);
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
