#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

using namespace linker;
using namespace linker::coff;

TEST(PluginCOFFContextIsolationTest, ConcurrentStateDoesNotCrossContexts) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Failed{false};

  auto Run = [&](llvm::COFF::MachineTypes Machine, llvm::StringRef Name) {
    {
      LinkerExecutionContext Execution;
      COFFLinkerContext &Context =
          Execution.createBackend<COFFLinkerContext>();
      Context.config.machine = Machine;
      Context.config.outputFile = Name.str();
      Context.overrideSymbols.try_emplace(Name, nullptr);

      Ready.fetch_add(1, std::memory_order_release);
      while (Ready.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();

      if (currentLinkerContext() != &Context ||
          Context.config.machine != Machine ||
          Context.config.outputFile != Name ||
          Context.overrideSymbols.size() != 1 ||
          Context.overrideSymbols.count(Name) != 1)
        Failed.store(true, std::memory_order_relaxed);
    }
    if (currentLinkerContext() != nullptr)
      Failed.store(true, std::memory_order_relaxed);
  };

  std::thread First(
      [&] { Run(llvm::COFF::IMAGE_FILE_MACHINE_AMD64, "first.exe"); });
  std::thread Second(
      [&] { Run(llvm::COFF::IMAGE_FILE_MACHINE_ARM64, "second.exe"); });
  First.join();
  Second.join();

  EXPECT_FALSE(Failed.load(std::memory_order_relaxed));
}
