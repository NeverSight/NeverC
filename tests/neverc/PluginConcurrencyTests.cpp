#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::strlen(Text))};
}

void loadPlugin(PluginProcessServices &Services, StringRef Path) {
  auto Loaded = Services.registry().load(Path);
  if (!Loaded)
    ADD_FAILURE() << takeErrorMessage(Loaded.takeError());
}

std::unique_ptr<PluginSession>
createSession(PluginProcessServices &Services, StringRef PluginID) {
  const std::array<StringRef, 1> Selected = {PluginID};
  auto Plan = makePluginActivationPlan(Services.registry(), Selected);
  if (!Plan) {
    ADD_FAILURE() << takeErrorMessage(Plan.takeError());
    return nullptr;
  }
  auto Session = PluginSession::create(Services, *Plan);
  if (!Session) {
    ADD_FAILURE() << takeErrorMessage(Session.takeError());
    return nullptr;
  }
  return std::move(*Session);
}

void updateMaximum(std::atomic<int> &Maximum, int Value) {
  int Current = Maximum.load(std::memory_order_relaxed);
  while (Current < Value &&
         !Maximum.compare_exchange_weak(Current, Value,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
  }
}

int runParallelCallbacks(PluginSession &Session, StringRef PluginID,
                         unsigned ThreadCount) {
  std::atomic<unsigned> Ready{0};
  std::atomic<bool> Start{false};
  std::atomic<int> Active{0};
  std::atomic<int> Maximum{0};
  std::atomic<int> Failures{0};
  std::vector<std::thread> Threads;
  Threads.reserve(ThreadCount);
  for (unsigned I = 0; I != ThreadCount; ++I) {
    Threads.emplace_back([&] {
      Ready.fetch_add(1, std::memory_order_release);
      while (!Start.load(std::memory_order_acquire))
        std::this_thread::yield();
      auto Result = Session.invokeCallback(
          PluginID, "concurrency-test",
          [&] {
            int Current =
                Active.fetch_add(1, std::memory_order_acq_rel) + 1;
            updateMaximum(Maximum, Current);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Active.fetch_sub(1, std::memory_order_acq_rel);
            return neverc_status_ok();
          });
      if (!Result) {
        consumeError(Result.takeError());
        Failures.fetch_add(1, std::memory_order_relaxed);
      } else if (Result->Code != NEVERC_STATUS_OK) {
        Failures.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  while (Ready.load(std::memory_order_acquire) != ThreadCount)
    std::this_thread::yield();
  Start.store(true, std::memory_order_release);
  for (std::thread &Thread : Threads)
    Thread.join();
  EXPECT_EQ(Failures.load(), 0);
  return Maximum.load();
}

TEST(PluginConcurrencyTest, HonorsThreadSafeAndSessionSerialModels) {
  PluginProcessServices SerialServices("neverc-plugin-concurrency-tests",
                                       LLVM_VERSION_MAJOR);
  ASSERT_FALSE(SerialServices.interfaces().freeze());
  loadPlugin(SerialServices, NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  auto Serial =
      createSession(SerialServices, "org.neverc.test.scope.session");
  ASSERT_NE(Serial, nullptr);
  EXPECT_EQ(runParallelCallbacks(*Serial,
                                 "org.neverc.test.scope.session", 8),
            1);
  EXPECT_FALSE(Serial->end());
  EXPECT_FALSE(SerialServices.shutdown());

  PluginProcessServices ThreadSafeServices(
      "neverc-plugin-concurrency-tests", LLVM_VERSION_MAJOR);
  ASSERT_FALSE(ThreadSafeServices.interfaces().freeze());
  loadPlugin(ThreadSafeServices, NEVERC_TEST_SCOPE_THREAD_SAFE_PLUGIN);
  auto ThreadSafe = createSession(
      ThreadSafeServices, "org.neverc.test.scope.thread-safe");
  ASSERT_NE(ThreadSafe, nullptr);
  EXPECT_GT(runParallelCallbacks(*ThreadSafe,
                                 "org.neverc.test.scope.thread-safe", 8),
            1);
  EXPECT_FALSE(ThreadSafe->end());
  EXPECT_FALSE(ThreadSafeServices.shutdown());
}

TEST(PluginConcurrencyTest, SessionSerialIsIndependentAcrossSessions) {
  PluginProcessServices Services("neverc-plugin-concurrency-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  loadPlugin(Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  auto First =
      createSession(Services, "org.neverc.test.scope.session");
  auto Second =
      createSession(Services, "org.neverc.test.scope.session");
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);

  std::atomic<int> Ready{0};
  std::atomic<bool> Start{false};
  std::atomic<int> Active{0};
  std::atomic<int> Maximum{0};
  auto Run = [&](PluginSession &Session) {
    Ready.fetch_add(1, std::memory_order_release);
    while (!Start.load(std::memory_order_acquire))
      std::this_thread::yield();
    auto Result = Session.invokeCallback(
        "org.neverc.test.scope.session", "cross-session",
        [&] {
          int Current = Active.fetch_add(1) + 1;
          updateMaximum(Maximum, Current);
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
          Active.fetch_sub(1);
          return neverc_status_ok();
        });
    EXPECT_TRUE(static_cast<bool>(Result));
    if (Result)
      EXPECT_EQ(Result->Code, NEVERC_STATUS_OK);
  };
  std::thread A(Run, std::ref(*First));
  std::thread B(Run, std::ref(*Second));
  while (Ready.load(std::memory_order_acquire) != 2)
    std::this_thread::yield();
  Start.store(true, std::memory_order_release);
  A.join();
  B.join();
  EXPECT_EQ(Maximum.load(), 2);

  EXPECT_FALSE(Second->end());
  EXPECT_FALSE(First->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginConcurrencyTest, ProcessSerialIsSharedAcrossSessions) {
  PluginProcessServices Services("neverc-plugin-concurrency-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  loadPlugin(Services, NEVERC_TEST_SCOPE_PROCESS_SERIAL_PLUGIN);
  auto First = createSession(
      Services, "org.neverc.test.scope.process-serial");
  auto Second = createSession(
      Services, "org.neverc.test.scope.process-serial");
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);

  std::atomic<int> Active{0};
  std::atomic<int> Maximum{0};
  auto Run = [&](PluginSession &Session) {
    auto Result = Session.invokeCallback(
        "org.neverc.test.scope.process-serial", "process-serial",
        [&] {
          int Current = Active.fetch_add(1) + 1;
          updateMaximum(Maximum, Current);
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          Active.fetch_sub(1);
          return neverc_status_ok();
        });
    EXPECT_TRUE(static_cast<bool>(Result));
    if (Result)
      EXPECT_EQ(Result->Code, NEVERC_STATUS_OK);
  };
  std::thread A(Run, std::ref(*First));
  std::thread B(Run, std::ref(*Second));
  A.join();
  B.join();
  EXPECT_EQ(Maximum.load(), 1);

  EXPECT_FALSE(Second->end());
  EXPECT_FALSE(First->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginConcurrencyTest, EnforcesDeclaredReentrancyAndCancellation) {
  PluginProcessServices NonReentrantServices(
      "neverc-plugin-concurrency-tests", LLVM_VERSION_MAJOR);
  ASSERT_FALSE(NonReentrantServices.interfaces().freeze());
  loadPlugin(NonReentrantServices, NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  auto NonReentrant = createSession(
      NonReentrantServices, "org.neverc.test.scope.session");
  ASSERT_NE(NonReentrant, nullptr);

  NevercStatusCode NestedCode = NEVERC_STATUS_OK;
  auto Outer = NonReentrant->invokeCallback(
      "org.neverc.test.scope.session", "outer",
      [&] {
        auto Nested = NonReentrant->invokeCallback(
            "org.neverc.test.scope.session", "inner",
            [] { return neverc_status_ok(); });
        if (!Nested) {
          consumeError(Nested.takeError());
          NestedCode = NEVERC_STATUS_PLUGIN_FAILURE;
        } else {
          NestedCode = Nested->Code;
        }
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(Outer));
  EXPECT_EQ(Outer->Code, NEVERC_STATUS_OK);
  EXPECT_EQ(NestedCode, NEVERC_STATUS_REENTRANCY_DENIED);

  NevercStatusCode ForeignThreadLookup = NEVERC_STATUS_OK;
  auto ThreadProbe = NonReentrant->invokeCallback(
      "org.neverc.test.scope.session", "thread-probe",
      [&] {
        std::thread Foreign([&] {
          void *State = nullptr;
          ForeignThreadLookup =
              NonReentrantServices.coreAPI()
                  .GetSessionState(
                      NonReentrantServices.coreAPI().Context,
                      NonReentrant->handle(),
                      view("org.neverc.test.scope.session"), &State)
                  .Code;
        });
        Foreign.join();
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(ThreadProbe));
  EXPECT_EQ(ThreadProbe->Code, NEVERC_STATUS_OK);
  EXPECT_EQ(ForeignThreadLookup, NEVERC_STATUS_INVALID_STATE);

  NonReentrant->cancel();
  auto Cancelled = NonReentrant->invokeCallback(
      "org.neverc.test.scope.session", "cancelled",
      [] { return neverc_status_ok(); });
  ASSERT_TRUE(static_cast<bool>(Cancelled));
  EXPECT_EQ(Cancelled->Code, NEVERC_STATUS_CANCELLED);
  EXPECT_FALSE(NonReentrant->end());
  EXPECT_FALSE(NonReentrantServices.shutdown());

  PluginProcessServices ReentrantServices(
      "neverc-plugin-concurrency-tests", LLVM_VERSION_MAJOR);
  ASSERT_FALSE(ReentrantServices.interfaces().freeze());
  loadPlugin(ReentrantServices, NEVERC_TEST_SCOPE_REENTRANT_PLUGIN);
  auto Reentrant = createSession(
      ReentrantServices, "org.neverc.test.scope.reentrant");
  ASSERT_NE(Reentrant, nullptr);
  NevercStatusCode AllowedNestedCode = NEVERC_STATUS_PLUGIN_FAILURE;
  auto AllowedOuter = Reentrant->invokeCallback(
      "org.neverc.test.scope.reentrant", "outer",
      [&] {
        auto Nested = Reentrant->invokeCallback(
            "org.neverc.test.scope.reentrant", "inner",
            [] { return neverc_status_ok(); });
        if (Nested)
          AllowedNestedCode = Nested->Code;
        else
          consumeError(Nested.takeError());
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(AllowedOuter));
  EXPECT_EQ(AllowedOuter->Code, NEVERC_STATUS_OK);
  EXPECT_EQ(AllowedNestedCode, NEVERC_STATUS_OK);
  EXPECT_FALSE(Reentrant->end());
  EXPECT_FALSE(ReentrantServices.shutdown());
}

} // namespace
