#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "gtest/gtest.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using namespace linker;

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

} // namespace

TEST(PluginParallelLinkTest, ConcurrentExecutionsKeepBudgetsAndWorkersIsolated) {
  TwoPartyBarrier Barrier;
  ParallelRunResult First;
  ParallelRunResult Second;

  auto Run = [&](ParallelRunResult &Result, unsigned Budget,
                 unsigned Marker) {
    {
      LinkerExecutionContext Execution;
      CommonLinkerContext &Context =
          Execution.createBackend<CommonLinkerContext>();
      Context.configureParallel(Budget);
      Result.ContextAddress = reinterpret_cast<uintptr_t>(&Context);
      Result.ThreadBudget = Context.parallelThreadCount();

      Barrier.arriveAndWait();
      parallelForWithContext(
          0, Result.Values.size(), [&](size_t Index) {
            if (currentLinkerContext() != &Context ||
                currentLinkerWorkerSlot() == 0 ||
                currentLinkerWorkerSlot() >= Context.parallelShardCount())
              Result.Failed.store(true, std::memory_order_relaxed);
            Result.Values[Index] = Marker;
          });

      if (currentLinkerContext() != &Context ||
          currentLinkerWorkerSlot() != 0)
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
