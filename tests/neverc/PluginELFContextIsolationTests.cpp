#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/ELF/ELFLinkerContext.h"
#include "Linker/ELF/Relocations.h"
#include "gtest/gtest.h"
#include <array>
#include <atomic>
#include <mutex>
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

TEST(PluginELFContextIsolationTest,
     ConcurrentRelocationStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<unsigned> Checked{0};
  std::atomic<bool> Failed{false};
  std::array<detail::ELFRelocationState *, 2> States{};
  std::array<std::mutex *, 2> Mutexes{};

  auto Run = [&](unsigned Index, uint64_t Offset) {
    LinkerExecutionContext Execution;
    ELFLinkerContext &Context = Execution.createBackend<ELFLinkerContext>();
    detail::ELFRelocationState &State = detail::elfRelocationState();
    States[Index] = &State;
    Mutexes[Index] = &State.mutex;

    {
      std::lock_guard<std::mutex> Lock(State.mutex);
      State.undefs.push_back({nullptr, {{nullptr, Offset}}, false});
    }

    Ready.fetch_add(1, std::memory_order_release);
    while (Ready.load(std::memory_order_acquire) != 2)
      std::this_thread::yield();

    if (States[0] == States[1] || Mutexes[0] == Mutexes[1])
      Failed.store(true, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> Lock(State.mutex);
      if (State.undefs.size() != 1 || State.undefs[0].locs.size() != 1 ||
          State.undefs[0].locs[0].offset != Offset)
        Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != &Context)
      Failed.store(true, std::memory_order_relaxed);

    Checked.fetch_add(1, std::memory_order_release);
    while (Checked.load(std::memory_order_acquire) != 2)
      std::this_thread::yield();
  };

  std::thread First([&] { Run(0, 17); });
  std::thread Second([&] { Run(1, 29); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
}

TEST(PluginELFContextIsolationTest,
     RelocationStateIsDiscardedWithoutReporting) {
  LinkerExecutionContext Execution;
  Execution.createBackend<ELFLinkerContext>();
  {
    detail::ELFRelocationState &State = detail::elfRelocationState();
    std::lock_guard<std::mutex> Lock(State.mutex);
    State.undefs.push_back({nullptr, {{nullptr, 41}}, false});
  }

  Execution.destroyBackend();
  ASSERT_EQ(currentLinkerContext(), nullptr);

  Execution.createBackend<ELFLinkerContext>();
  detail::ELFRelocationState &FreshState = detail::elfRelocationState();
  std::lock_guard<std::mutex> Lock(FreshState.mutex);
  EXPECT_TRUE(FreshState.undefs.empty());
}

TEST(PluginELFContextIsolationTest, BoundWorkersShareOwningRelocationState) {
  LinkerExecutionContext Execution;
  ELFLinkerContext &Context = Execution.createBackend<ELFLinkerContext>();
  detail::ELFRelocationState &OwnerState = detail::elfRelocationState();
  std::atomic<bool> Failed{false};

  auto Record = bindLinkerContext([&](uint64_t BaseOffset) {
    detail::ELFRelocationState &WorkerState = detail::elfRelocationState();
    if (currentLinkerContext() != &Context || &WorkerState != &OwnerState ||
        currentLinkerWorkerSlot() == 0)
      Failed.store(true, std::memory_order_relaxed);

    for (uint64_t I = 0; I != 64; ++I) {
      std::lock_guard<std::mutex> Lock(WorkerState.mutex);
      WorkerState.undefs.push_back(
          {nullptr, {{nullptr, BaseOffset + I}}, false});
    }
  });

  std::thread First([&] { Record(1000); });
  std::thread Second([&] { Record(2000); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
  std::lock_guard<std::mutex> Lock(OwnerState.mutex);
  EXPECT_EQ(OwnerState.undefs.size(), 128U);
}
