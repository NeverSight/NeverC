#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "gtest/gtest.h"
#include "Linker/Core/Runtime/CrashRecovery.h"
#include <atomic>
#include <thread>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <new>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace linker;

// Inspect only the non-owning binding. The actual memo and allocator pointers
// stay private to Session.cpp and are owned by the context's worker records.
namespace linker::session_detail {
extern LLVM_THREAD_LOCAL WorkerAllocCache *ActiveWorkerCache;
}

namespace {
static_assert(std::is_trivially_destructible_v<
              decltype(session_detail::ActiveWorkerCache)>);

template <size_t Tag> struct CacheTrackedValue {
  CacheTrackedValue(std::atomic<unsigned> &Count, unsigned Generation)
      : Count(Count), Generation(Generation) {}
  ~CacheTrackedValue() { Count.fetch_add(1, std::memory_order_relaxed); }
  std::atomic<unsigned> &Count;
  unsigned Generation;
};

template <size_t... Tags>
void allocateManyTags(std::atomic<unsigned> &Destroyed,
                      std::array<SpecificAllocBase *, sizeof...(Tags)> &Arenas,
                      std::index_sequence<Tags...>) {
  auto Check = [&](auto Index) {
    constexpr size_t I = decltype(Index)::value;
    auto *Arena = SpecificAllocBase::getOrCreateWorker(
        &SpecificAlloc<CacheTrackedValue<I>>::tag,
        sizeof(SpecificAlloc<CacheTrackedValue<I>>),
        alignof(SpecificAlloc<CacheTrackedValue<I>>),
        SpecificAlloc<CacheTrackedValue<I>>::create);
    if (Arenas[I])
      EXPECT_EQ(Arenas[I], Arena);
    Arenas[I] = Arena;
    EXPECT_EQ(makeThreadLocal<CacheTrackedValue<I>>(Destroyed, I)->Generation, I);
  };
  (Check(std::integral_constant<size_t, Tags>{}), ...);
}
} // namespace

TEST(SessionCacheOwnershipTest, GuardBorrowClearsOnReusedForeignThread) {
  CommonLinkerContext Context(neverc::ResourceSessionView{});
  auto *MainCache = session_detail::ActiveWorkerCache;
  ASSERT_NE(MainCache, nullptr);
  std::atomic<unsigned> Destroyed{0};
  std::thread Worker([&] {
    auto Bound = bindLinkerContext([&] {
      EXPECT_EQ(currentLinkerContext(), &Context);
      EXPECT_GT(currentLinkerWorkerSlot(), 0U);
      EXPECT_NE(session_detail::ActiveWorkerCache, nullptr);
      EXPECT_NE(session_detail::ActiveWorkerCache, MainCache);
      (void)makeThreadLocal<CacheTrackedValue<0>>(Destroyed, 0);
    });
    for (unsigned I = 0; I != 32; ++I) {
      EXPECT_FALSE(hasContext());
      EXPECT_EQ(session_detail::ActiveWorkerCache, nullptr);
      Bound();
      EXPECT_FALSE(hasContext());
      EXPECT_EQ(currentLinkerWorkerSlot(), 0U);
      EXPECT_EQ(session_detail::ActiveWorkerCache, nullptr);
    }
  });
  Worker.join();
  EXPECT_EQ(session_detail::ActiveWorkerCache, MainCache);
  Context.finalizeOwnedState();
  EXPECT_EQ(Destroyed.load(), 32U);
}

TEST(SessionCacheOwnershipTest, NestedContextsRestoreCachedArenaAcrossUnwind) {
  CommonLinkerContext Outer(neverc::ResourceSessionView{});
  std::atomic<unsigned> OuterDestroyed{0}, InnerDestroyed{0};
  auto *OuterCache = session_detail::ActiveWorkerCache;
  auto *Arena = &getTaskWorkerSpecificAlloc<CacheTrackedValue<0>>();
  (void)makeThreadLocal<CacheTrackedValue<0>>(OuterDestroyed, 9);
  {
    LinkerContextGuard OuterGuard(Outer, 17);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
#endif
    {
      CommonLinkerContext Inner(neverc::ResourceSessionView{});
      EXPECT_NE(session_detail::ActiveWorkerCache, OuterCache);
      EXPECT_EQ(currentLinkerWorkerSlot(), 0U);
      (void)makeThreadLocal<CacheTrackedValue<0>>(InnerDestroyed, 10);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
      throw std::runtime_error("session binding unwind");
#endif
    }
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    } catch (const std::runtime_error &) {
    }
#endif
    EXPECT_EQ(currentLinkerContext(), &Outer);
    EXPECT_EQ(currentLinkerWorkerSlot(), 17U);
    EXPECT_EQ(session_detail::ActiveWorkerCache, OuterCache);
    EXPECT_EQ(&getTaskWorkerSpecificAlloc<CacheTrackedValue<0>>(), Arena);
  }
  EXPECT_EQ(currentLinkerWorkerSlot(), 0U);
  EXPECT_EQ(InnerDestroyed.load(), 1U);
  EXPECT_EQ(OuterDestroyed.load(), 0U);
  Outer.finalizeOwnedState();
  EXPECT_EQ(OuterDestroyed.load(), 1U);
}

TEST(SessionCacheOwnershipTest, ReusedContextAddressDoesNotRetainArenas) {
  alignas(CommonLinkerContext) unsigned char Storage[sizeof(CommonLinkerContext)];
  std::mutex Mutex;
  std::condition_variable Condition;
  CommonLinkerContext *Selected = nullptr;
  bool Ready = false, Done = false, Stop = false;
  unsigned Generation = 0;
  std::atomic<unsigned> Destroyed{0};
  std::thread Worker([&] {
    for (;;) {
      std::unique_lock<std::mutex> Lock(Mutex);
      Condition.wait(Lock, [&] { return Ready || Stop; });
      if (Stop)
        return;
      CommonLinkerContext *Context = Selected;
      unsigned Round = Generation;
      Lock.unlock();
      {
        LinkerContextGuard Guard(*Context, Context->workerSlotForCurrentThread());
        EXPECT_EQ(currentLinkerContext(), Context);
        EXPECT_NE(session_detail::ActiveWorkerCache, nullptr);
        EXPECT_EQ(makeThreadLocal<CacheTrackedValue<0>>(Destroyed, Round)->Generation,
                  Round);
      }
      EXPECT_FALSE(hasContext());
      EXPECT_EQ(session_detail::ActiveWorkerCache, nullptr);
      Lock.lock();
      Ready = false;
      Done = true;
      Condition.notify_all();
    }
  });
  for (unsigned Round = 0; Round != 16; ++Round) {
    auto *Context = new (Storage) CommonLinkerContext(neverc::ResourceSessionView{});
    {
      std::unique_lock<std::mutex> Lock(Mutex);
      Selected = Context;
      Generation = Round;
      Done = false;
      Ready = true;
      Condition.notify_all();
      Condition.wait(Lock, [&] { return Done; });
    }
    Context->~CommonLinkerContext();
    EXPECT_EQ(Destroyed.load(), Round + 1);
    EXPECT_FALSE(hasContext());
    EXPECT_EQ(session_detail::ActiveWorkerCache, nullptr);
  }
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Stop = true;
    Condition.notify_all();
  }
  Worker.join();
}

TEST(SessionCacheOwnershipTest, MoreThan64TagsUseOwnedFallback) {
  std::atomic<unsigned> Destroyed{0};
  std::array<SpecificAllocBase *, 80> Arenas{};
  {
    CommonLinkerContext Context(neverc::ResourceSessionView{});
    allocateManyTags(Destroyed, Arenas, std::make_index_sequence<80>{});
    allocateManyTags(Destroyed, Arenas, std::make_index_sequence<80>{});
    std::set<SpecificAllocBase *> Unique(Arenas.begin(), Arenas.end());
    EXPECT_EQ(Unique.size(), 80U);
    EXPECT_EQ(Destroyed.load(), 0U);
  }
  EXPECT_EQ(Destroyed.load(), 160U);
  EXPECT_EQ(session_detail::ActiveWorkerCache, nullptr);
}

TEST(SessionCacheOwnershipTest, FinalizationJoinsWorkersBeforeCleanupAndRelease) {
  CommonLinkerContext Context(neverc::ResourceSessionView{});
  Context.configureParallel(2);
  ASSERT_NE(Context.parallelPool(), nullptr);
  std::atomic<unsigned> Completed{0}, Destroyed{0}, Cleanup{0};
  for (unsigned I = 0; I != 128; ++I)
    Context.parallelPool()->async(bindLinkerContext([&, I] {
      EXPECT_EQ(makeThreadLocal<CacheTrackedValue<0>>(Destroyed, I)->Generation, I);
      Completed.fetch_add(1);
    }));
  Context.e.cleanupCallback = [&] {
    EXPECT_EQ(Completed.load(), 128U);
    EXPECT_EQ(Destroyed.load(), 0U);
    EXPECT_EQ(currentLinkerContext(), &Context);
    (void)makeThreadLocal<CacheTrackedValue<1>>(Destroyed, 129);
    Cleanup.fetch_add(1);
  };
  Context.finalizeOwnedState();
  Context.finalizeOwnedState();
  EXPECT_EQ(Cleanup.load(), 1U);
  EXPECT_EQ(Destroyed.load(), 129U);
}

TEST(SessionCacheOwnershipTest, ConcurrentContextsOwnDifferentWorkerArenas) {
  std::array<std::thread, 4> Threads;
  std::array<session_detail::WorkerAllocCache *, 4> Records{};
  std::atomic<unsigned> Ready{0}, Destroyed{0};
  for (unsigned I = 0; I != Threads.size(); ++I)
    Threads[I] = std::thread([&, I] {
      for (unsigned Round = 0; Round != 8; ++Round) {
        {
          CommonLinkerContext Context(neverc::ResourceSessionView{});
          if (Round == 0) {
            Records[I] = session_detail::ActiveWorkerCache;
            Ready.fetch_add(1, std::memory_order_acq_rel);
            while (Ready.load(std::memory_order_acquire) != Threads.size())
              std::this_thread::yield();
          }
          for (unsigned J = 0; J != 64; ++J)
            EXPECT_EQ(makeThreadLocal<CacheTrackedValue<0>>(Destroyed, I)->Generation, I);
        }
        EXPECT_FALSE(hasContext());
        EXPECT_EQ(session_detail::ActiveWorkerCache, nullptr);
      }
    });
  for (auto &Thread : Threads)
    Thread.join();
  std::set<session_detail::WorkerAllocCache *> Unique(Records.begin(), Records.end());
  EXPECT_EQ(Unique.size(), Threads.size());
  EXPECT_EQ(Destroyed.load(), 4U * 8U * 64U);
}

TEST(SessionCacheOwnershipTest, CrashBackendCleanupRestoresBorrowAndSlot) {
  CommonLinkerContext Outer(neverc::ResourceSessionView{});
  LinkerContextGuard OuterSlot(Outer, 23);
  auto *Saved = session_detail::ActiveWorkerCache;
  std::atomic<unsigned> Destroyed{0};
  LinkerExecutionContext Execution;
  llvm::CrashRecoveryContext::Enable();
  {
    llvm::CrashRecoveryContext Recovery;
    EXPECT_FALSE(Recovery.RunSafely([&] {
      auto &Inner = Execution.createBackend<CommonLinkerContext>();
      llvm::CrashRecoveryContextCleanupRegistrar<
          LinkerExecutionContext,
          crash_recovery_detail::CrashRecoveryDestroyBackendCleanup<
              LinkerExecutionContext>> Cleanup(&Execution);
      (void)makeThreadLocal<CacheTrackedValue<0>>(Destroyed, 0);
      EXPECT_EQ(currentLinkerContext(), &Inner);
      Recovery.HandleExit(17);
    }));
    EXPECT_EQ(Recovery.RetCode, 17);
  }
  llvm::CrashRecoveryContext::Disable();
  EXPECT_FALSE(Execution.hasBackend());
  EXPECT_EQ(currentLinkerContext(), &Outer);
  EXPECT_EQ(currentLinkerWorkerSlot(), 23U);
  EXPECT_EQ(session_detail::ActiveWorkerCache, Saved);
  EXPECT_EQ(Destroyed.load(), 1U);
}

TEST(SessionCacheOwnershipTest, AbandonRestoresPreviousBorrowBeforeProcessExit) {
  EXPECT_EXIT({
    CommonLinkerContext Outer(neverc::ResourceSessionView{});
    LinkerContextGuard OuterSlot(Outer, 29);
    auto *Saved = session_detail::ActiveWorkerCache;
    auto *Inner = new CommonLinkerContext(neverc::ResourceSessionView{});
    Inner->configureParallel(2);
    Inner->abandon();
    const bool Restored = currentLinkerContext() == &Outer &&
        currentLinkerWorkerSlot() == 29 && session_detail::ActiveWorkerCache == Saved;
    std::_Exit(Restored ? 0 : 1);
  }, ::testing::ExitedWithCode(0), "");
}
