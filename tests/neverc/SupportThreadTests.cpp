//===- SupportThreadTests.cpp - Thread wrapper regressions ----------------===//
//
// llvm::thread promises the std::thread callable interface while crossing the
// CSupport thread-creation boundary.  Keep move-only callables and arguments
// working so that boundary cannot accidentally copy the stored invocation or
// erase a platform calling convention.
//
//===----------------------------------------------------------------------===//

#include "csupport/lpath.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/thread.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

#if LLVM_ENABLE_THREADS
constexpr size_t ConcurrentCallers = 16;

template <typename Callable>
void runConcurrently(Callable &&Call) {
  std::atomic<size_t> Ready{0};
  std::atomic<bool> Start{false};
  std::vector<thread> Threads;
  Threads.reserve(ConcurrentCallers);

  for (size_t I = 0; I < ConcurrentCallers; ++I) {
    Threads.emplace_back([&, I] {
      Ready.fetch_add(1, std::memory_order_release);
      while (!Start.load(std::memory_order_acquire))
        std::this_thread::yield();
      Call(I);
    });
  }

  while (Ready.load(std::memory_order_acquire) != ConcurrentCallers)
    std::this_thread::yield();
  Start.store(true, std::memory_order_release);
  for (thread &Thread : Threads)
    Thread.join();
}
#endif

} // namespace

TEST(SupportThreadTest, AcceptsMoveOnlyCallableAndArguments) {
  std::atomic<int> Result{0};

  auto CallablePayload = std::make_unique<int>(17);
  thread MoveOnlyCallable([Payload = std::move(CallablePayload), &Result] {
    Result.fetch_add(*Payload, std::memory_order_release);
  });
  EXPECT_EQ(CallablePayload, nullptr);
#if LLVM_ENABLE_THREADS
  EXPECT_TRUE(MoveOnlyCallable.joinable());
  MoveOnlyCallable.join();
  EXPECT_FALSE(MoveOnlyCallable.joinable());
#endif

  auto ArgumentPayload = std::make_unique<int>(25);
  thread MoveOnlyArgument(
      [](std::unique_ptr<int> Payload, std::atomic<int> *Output) {
        Output->fetch_add(*Payload, std::memory_order_release);
      },
      std::move(ArgumentPayload), &Result);
  EXPECT_EQ(ArgumentPayload, nullptr);
#if LLVM_ENABLE_THREADS
  EXPECT_TRUE(MoveOnlyArgument.joinable());
  MoveOnlyArgument.join();
  EXPECT_FALSE(MoveOnlyArgument.joinable());
#endif

  struct Receiver {
    std::atomic<int> *Output;
    void add(std::unique_ptr<int> Payload) {
      Output->fetch_add(*Payload, std::memory_order_release);
    }
  } Target{&Result};

  thread MemberFunction(&Receiver::add, &Target, std::make_unique<int>(8));
#if LLVM_ENABLE_THREADS
  EXPECT_TRUE(MemberFunction.joinable());
  MemberFunction.join();
  EXPECT_FALSE(MemberFunction.joinable());
#endif

  EXPECT_EQ(Result.load(std::memory_order_acquire), 50);
}

#if LLVM_ENABLE_THREADS
TEST(SupportThreadTest, ConcurrentLazyInitializationIsRaceFree) {
  std::array<int, ConcurrentCallers> PhysicalCores{};
  std::array<int, ConcurrentCallers> HasProcSelfFD{};
  std::array<unsigned, ConcurrentCallers> RandomChecksums{};

  runConcurrently([&](size_t I) {
    PhysicalCores[I] = llvm::get_physical_cores();
    HasProcSelfFD[I] = csupport_has_proc_self_fd();
    for (unsigned J = 0; J != 128; ++J)
      RandomChecksums[I] ^= sys::Process::GetRandomNumber();
  });

  for (size_t I = 1; I < ConcurrentCallers; ++I) {
    EXPECT_EQ(PhysicalCores[I], PhysicalCores[0]);
    EXPECT_EQ(HasProcSelfFD[I], HasProcSelfFD[0]);
  }

  // Keep every random-number call observable without imposing a probabilistic
  // uniqueness requirement on the process-wide generator.
  (void)RandomChecksums;
}

TEST(SupportThreadTest, ThreadIdsAreStableAndUniqueWhileThreadsAreAlive) {
  std::array<uint64_t, ConcurrentCallers> FirstIds{};
  std::array<uint64_t, ConcurrentCallers> SecondIds{};
  std::atomic<size_t> Captured{0};

  runConcurrently([&](size_t I) {
    FirstIds[I] = llvm::get_threadid();
    SecondIds[I] = llvm::get_threadid();

    // Keep every thread alive until all IDs have been sampled.  Thread IDs may
    // be reused after a thread exits, but live threads must not alias.
    Captured.fetch_add(1, std::memory_order_release);
    while (Captured.load(std::memory_order_acquire) != ConcurrentCallers)
      std::this_thread::yield();
  });

  for (size_t I = 0; I != ConcurrentCallers; ++I) {
    EXPECT_NE(FirstIds[I], 0u);
    EXPECT_EQ(FirstIds[I], SecondIds[I]);
  }

  std::sort(FirstIds.begin(), FirstIds.end());
  EXPECT_EQ(std::adjacent_find(FirstIds.begin(), FirstIds.end()),
            FirstIds.end());
}
#endif
