#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/MachO/MachOLinkerContext.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

using namespace linker;
using namespace linker::macho;

TEST(PluginMachOContextIsolationTest, ConcurrentStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Failed{false};

  auto Run = [&](uint32_t DylibCount, llvm::StringRef Warning) {
    {
      LinkerExecutionContext Execution;
      MachOLinkerContext &Context =
          Execution.createBackend<MachOLinkerContext>();
      machoLCDylibCount() = DylibCount;
      machoMissingAutolinkWarnings().push_back(Warning);

      Ready.fetch_add(1, std::memory_order_release);
      while (Ready.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();

      if (currentLinkerContext() != &Context ||
          machoLCDylibCount() != DylibCount ||
          machoMissingAutolinkWarnings().size() != 1 ||
          machoMissingAutolinkWarnings().front() != Warning)
        Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != nullptr)
      Failed.store(true, std::memory_order_relaxed);
  };

  std::thread First([&] { Run(3, "first"); });
  std::thread Second([&] { Run(19, "second"); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
}
