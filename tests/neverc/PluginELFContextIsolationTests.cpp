#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/ELF/ELFLinkerContext.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

using namespace linker;
using namespace linker::elf;

TEST(PluginELFContextIsolationTest, ConcurrentStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Failed{false};

  auto Run = [&](bool InGroup, uint32_t GroupID, unsigned Vernaux) {
    {
      LinkerExecutionContext Execution;
      ELFLinkerContext &Context =
          Execution.createBackend<ELFLinkerContext>();
      elfInputFileIsInGroup() = InGroup;
      elfNextGroupId() = GroupID;
      elfVernauxNum() = Vernaux;

      Ready.fetch_add(1, std::memory_order_release);
      while (Ready.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();

      if (currentLinkerContext() != &Context ||
          elfInputFileIsInGroup() != InGroup ||
          elfNextGroupId() != GroupID || elfVernauxNum() != Vernaux)
        Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != nullptr)
      Failed.store(true, std::memory_order_relaxed);
  };

  std::thread First([&] { Run(false, 7, 11); });
  std::thread Second([&] { Run(true, 41, 97); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
}
