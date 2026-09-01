#include "ProcessResourceBrokerInternal.h"
#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#if defined(__cpp_exceptions)
#include <stdexcept>
#endif
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace neverc;

namespace {

ProcessResourceBrokerConfig enabledBroker(unsigned Tokens) {
  ProcessResourceBrokerConfig Config;
  Config.Enabled = true;
  Config.CpuTokens = Tokens;
  return Config;
}

std::unique_ptr<ProcessResourceBroker>
makeBroker(unsigned Tokens,
           ProcessResourceBrokerTestAccess::Observer Observer = {}) {
  return ProcessResourceBrokerTestAccess::create(enabledBroker(Tokens),
                                                 std::move(Observer));
}

ProcessResourceBrokerSnapshot snapshot(const ProcessResourceBroker &Broker) {
  return ProcessResourceBrokerTestAccess::snapshot(Broker);
}

void expectSnapshotEq(const ProcessResourceBrokerSnapshot &Left,
                      const ProcessResourceBrokerSnapshot &Right) {
  EXPECT_EQ(Left.Capacity, Right.Capacity);
  EXPECT_EQ(Left.AvailableTokens, Right.AvailableTokens);
  EXPECT_EQ(Left.ActiveTokens, Right.ActiveTokens);
  EXPECT_EQ(Left.ActiveSessions, Right.ActiveSessions);
  EXPECT_EQ(Left.WaitingSessions, Right.WaitingSessions);
  EXPECT_EQ(Left.HighWaterTokens, Right.HighWaterTokens);
  EXPECT_EQ(Left.WaitEpoch, Right.WaitEpoch);
}

static_assert(!std::is_copy_constructible_v<ResourceSessionPermit>);
static_assert(!std::is_copy_constructible_v<ResourceWorkerGrant>);
static_assert(std::is_nothrow_move_constructible_v<ResourceSessionPermit>);
static_assert(std::is_nothrow_move_constructible_v<ResourceWorkerGrant>);

} // namespace

TEST(ProcessResourceBrokerTest, DisabledBrokerPreservesDesiredWorkers) {
  auto Broker = ProcessResourceBrokerTestAccess::create({});
  auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  auto Grant = Broker->grantWorkers(Session.session(),
                                    ResourcePhase::LinkParseResolve, 7);

  EXPECT_FALSE(Session.ownsAdmission());
  EXPECT_FALSE(Session.constrained());
  EXPECT_TRUE(Session.session().bypassesBudget());
  EXPECT_EQ(Grant.workerCount(), 7U);
  const ProcessResourceBrokerSnapshot Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.Capacity, 0U);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.HighWaterTokens, 0U);
}

TEST(ProcessResourceBrokerTest, NestedSessionInheritsOneProgressToken) {
  auto Broker = makeBroker(3);
  auto Outer = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  const uint64_t SessionID = Outer.session().id();
  EXPECT_TRUE(Outer.ownsAdmission());

  {
    auto Inner = Broker->acquireSession(
        Outer.session(), ResourcePhase::PCGOptCodeGen);
    EXPECT_FALSE(Inner.ownsAdmission());
    EXPECT_EQ(Inner.session().id(), SessionID);
    EXPECT_EQ(snapshot(*Broker).ActiveTokens, 1U);
  }

  EXPECT_EQ(Outer.session().id(), SessionID);
}

TEST(ProcessResourceBrokerTest, ExpiredSessionViewCannotBypassAdmission) {
  auto Broker = makeBroker(1);
  ResourceSessionView Expired;
  {
    auto Original = Broker->acquireSession(ResourcePhase::LinkParseResolve);
    Expired = Original.session();
  }
  ASSERT_EQ(snapshot(*Broker).ActiveTokens, 0U);

  auto Occupied = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  ASSERT_TRUE(Occupied.ownsAdmission());
  {
    auto Reacquired = Broker->acquireSession(
        Expired, ResourcePhase::PCGOptCodeGen,
        ResourceAdmissionMode::DoNotWait);
    EXPECT_FALSE(Reacquired.ownsAdmission());
    EXPECT_TRUE(Reacquired.constrained());
    EXPECT_NE(Reacquired.session().id(), Expired.id());
    EXPECT_EQ(snapshot(*Broker).ActiveTokens, 1U);
  }
}

TEST(ProcessResourceBrokerTest, WorkerGrantMoveReleasesEveryTokenExactlyOnce) {
  auto Broker = makeBroker(4);
  auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  {
    auto Original = Broker->grantWorkers(Session.session(),
                                         ResourcePhase::PCGPrepare, 4);
    ASSERT_EQ(Original.workerCount(), 4U);
    EXPECT_EQ(snapshot(*Broker).ActiveTokens, 4U);
    auto Moved = std::move(Original);
    EXPECT_EQ(Original.workerCount(), 0U);
    EXPECT_EQ(Moved.workerCount(), 4U);
    EXPECT_EQ(snapshot(*Broker).ActiveTokens, 4U);
  }
  const ProcessResourceBrokerSnapshot Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveTokens, 1U);
  EXPECT_EQ(Snapshot.AvailableTokens, 3U);
  EXPECT_EQ(Snapshot.HighWaterTokens, 4U);
}

TEST(ProcessResourceBrokerTest, SessionAdmissionIsFIFOWithoutSleeps) {
  std::mutex Mutex;
  std::condition_variable Condition;
  unsigned Waiting = 0;
  std::vector<unsigned> Acquired;
  auto Broker = makeBroker(1, [&](const ProcessResourceBrokerEvent &Event) {
    if (Event.Kind != ProcessResourceBrokerEventKind::SessionWaiting)
      return;
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      ++Waiting;
    }
    Condition.notify_all();
  });

  std::optional<ResourceSessionPermit> First;
  First.emplace(Broker->acquireSession(ResourcePhase::LinkParseResolve));
  auto WaitForWaiting = [&](unsigned Count) {
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [&] { return Waiting >= Count; });
  };
  auto RunWaiter = [&](unsigned Marker) {
    auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Acquired.push_back(Marker);
    }
    Condition.notify_all();
  };

  std::thread Second([&] { RunWaiter(2); });
  WaitForWaiting(1);
  std::thread Third([&] { RunWaiter(3); });
  WaitForWaiting(2);
  First.reset();
  Second.join();
  Third.join();

  EXPECT_EQ(Acquired, (std::vector<unsigned>{2, 3}));
  const ProcessResourceBrokerSnapshot Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.WaitingSessions, 0U);
  EXPECT_EQ(Snapshot.HighWaterTokens, 1U);
}

TEST(ProcessResourceBrokerTest,
     WaitingSessionPreventsOldSessionReacquiringExtras) {
  std::mutex Mutex;
  std::condition_variable Condition;
  bool Waiting = false;
  bool Acquired = false;
  bool Release = false;
  auto Broker = makeBroker(2, [&](const ProcessResourceBrokerEvent &Event) {
    if (Event.Kind != ProcessResourceBrokerEventKind::SessionWaiting)
      return;
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Waiting = true;
    }
    Condition.notify_all();
  });

  auto First = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  std::optional<ResourceWorkerGrant> Extra;
  Extra.emplace(Broker->grantWorkers(
      First.session(), ResourcePhase::LinkParseResolve, 2));
  ASSERT_EQ(Extra->workerCount(), 2U);

  std::thread Second([&] {
    auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
    std::unique_lock<std::mutex> Lock(Mutex);
    Acquired = true;
    Condition.notify_all();
    Condition.wait(Lock, [&] { return Release; });
  });
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [&] { return Waiting; });
  }

  Extra.reset();
  auto Regrant = Broker->grantWorkers(
      First.session(), ResourcePhase::LinkParseResolve, 2);
  EXPECT_EQ(Regrant.workerCount(), 1U);
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [&] { return Acquired; });
    Release = true;
  }
  Condition.notify_all();
  Second.join();
}

TEST(ProcessResourceBrokerTest, NonBlockingAdmissionFallsBackToSerialProgress) {
  auto Broker = makeBroker(1);
  auto First = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  bool Constrained = false;
  unsigned Workers = 0;
  std::thread Second([&] {
    auto Session = Broker->acquireSession(ResourcePhase::PCGOptCodeGen,
                                          ResourceAdmissionMode::DoNotWait);
    Constrained = Session.constrained();
    Workers = Broker
                  ->grantWorkers(Session.session(),
                                 ResourcePhase::PCGOptCodeGen, 8)
                  .workerCount();
  });
  Second.join();

  EXPECT_TRUE(Constrained);
  EXPECT_EQ(Workers, 1U);
  EXPECT_EQ(snapshot(*Broker).WaitingSessions, 0U);
}

TEST(ProcessResourceBrokerTest, WorkerGrantDoesNotWaitForBrokerMutex) {
  using namespace std::chrono_literals;

  auto Broker = makeBroker(2);
  auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  const ResourceSessionView View = Session.session();
  std::promise<unsigned> Result;
  std::future<unsigned> Future = Result.get_future();
  std::thread Worker;
  bool ReadyWhileLocked = false;
  unsigned Workers = 0;

  ProcessResourceBrokerTestAccess::withMutexHeld(*Broker, [&] {
    Worker = std::thread([&] {
      auto Grant = Broker->grantWorkers(View, ResourcePhase::PCGOptCodeGen, 2);
      Result.set_value(Grant.workerCount());
    });
    ReadyWhileLocked = Future.wait_for(2s) == std::future_status::ready;
    if (ReadyWhileLocked)
      Workers = Future.get();
  });
  Worker.join();
  if (!ReadyWhileLocked)
    Workers = Future.get();

  EXPECT_TRUE(ReadyWhileLocked);
  EXPECT_EQ(Workers, 1U);
}

TEST(ProcessResourceBrokerTest,
     SessionOwnerKeepsBaselineUntilOutstandingWorkersRelease) {
  auto Broker = makeBroker(3);
  std::optional<ResourceSessionPermit> Session;
  Session.emplace(Broker->acquireSession(ResourcePhase::LinkParseResolve));
  std::optional<ResourceWorkerGrant> Workers;
  Workers.emplace(Broker->grantWorkers(
      Session->session(), ResourcePhase::PCGOptCodeGen, 3));
  ASSERT_EQ(snapshot(*Broker).ActiveTokens, 3U);

  Session.reset();
  ProcessResourceBrokerSnapshot Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveSessions, 0U);
  EXPECT_EQ(Snapshot.ActiveTokens, 3U);
  EXPECT_EQ(Snapshot.AvailableTokens, 0U);

  Workers.reset();
  Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.AvailableTokens, 3U);
}

TEST(ProcessResourceBrokerTest,
     SessionOwnerKeepsBaselineWhileProgressWorkerIsStillRunning) {
  auto Broker = makeBroker(1);
  std::optional<ResourceSessionPermit> Session;
  Session.emplace(Broker->acquireSession(ResourcePhase::LinkParseResolve));
  std::optional<ResourceWorkerGrant> Worker;
  Worker.emplace(Broker->grantWorkers(
      Session->session(), ResourcePhase::PCGOptCodeGen, 1));
  ASSERT_EQ(Worker->workerCount(), 1U);

  Session.reset();
  ProcessResourceBrokerSnapshot Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveSessions, 0U);
  EXPECT_EQ(Snapshot.ActiveTokens, 1U);
  EXPECT_EQ(Snapshot.AvailableTokens, 0U);

  Worker.reset();
  Snapshot = snapshot(*Broker);
  EXPECT_EQ(Snapshot.ActiveTokens, 0U);
  EXPECT_EQ(Snapshot.AvailableTokens, 1U);
}

TEST(ProcessResourceBrokerTest, ObserverMayReenterSnapshotWithoutDeadlock) {
  ProcessResourceBroker *ObservedBroker = nullptr;
  unsigned WorkerGrantEvents = 0;
  auto Broker = makeBroker(2, [&](const ProcessResourceBrokerEvent &Event) {
    if (Event.Kind != ProcessResourceBrokerEventKind::WorkersGranted)
      return;
    ++WorkerGrantEvents;
    EXPECT_EQ(snapshot(*ObservedBroker).ActiveTokens, 2U);
  });
  ObservedBroker = Broker.get();

  auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  auto Workers = Broker->grantWorkers(Session.session(),
                                      ResourcePhase::PCGPrepare, 2);
  EXPECT_EQ(Workers.workerCount(), 2U);
  EXPECT_EQ(WorkerGrantEvents, 1U);
}

#if defined(__cpp_exceptions)
TEST(ProcessResourceBrokerTest,
     ObserverDepthRestoresAfterNestedOrdinaryException) {
  const ProcessResourceBrokerEvent Event{
      ProcessResourceBrokerEventKind::WorkersGranted,
      ResourcePhase::PCGPrepare,
      /*Sequence=*/1,
      /*SessionID=*/0,
      /*RequestedWorkers=*/2,
      /*GrantedWorkers=*/2};
  ProcessResourceBroker *ObservedBroker = nullptr;
  unsigned Calls = 0;
  auto Broker = makeBroker(2, [&](const ProcessResourceBrokerEvent &) {
    ++Calls;
    if (Calls != 1)
      return;
    ProcessResourceBrokerTestAccess::emitSyntheticEvent(*ObservedBroker,
                                                        Event);
    throw std::runtime_error("synthetic observer failure");
  });
  ObservedBroker = Broker.get();
  const ProcessResourceBrokerSnapshot Before = snapshot(*Broker);

  EXPECT_THROW(ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker,
                                                                   Event),
               std::runtime_error);
  EXPECT_NO_THROW(
      ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker, Event));

  EXPECT_EQ(Calls, 2U);
  expectSnapshotEq(snapshot(*Broker), Before);
}
#endif

TEST(ProcessResourceBrokerTest,
     ObserverDepthRestoresAfterNestedCrashRecoveryExit) {
  const ProcessResourceBrokerEvent Event{
      ProcessResourceBrokerEventKind::WorkersGranted,
      ResourcePhase::PCGPrepare,
      /*Sequence=*/1,
      /*SessionID=*/0,
      /*RequestedWorkers=*/2,
      /*GrantedWorkers=*/2};
  ProcessResourceBroker *ObservedBroker = nullptr;
  unsigned Calls = 0;
  auto Broker = makeBroker(2, [&](const ProcessResourceBrokerEvent &) {
    ++Calls;
    if (Calls != 1)
      return;
    ProcessResourceBrokerTestAccess::emitSyntheticEvent(*ObservedBroker,
                                                        Event);
    llvm::CrashRecoveryContext::GetCurrent()->HandleExit(37);
  });
  ObservedBroker = Broker.get();
  const ProcessResourceBrokerSnapshot Before = snapshot(*Broker);

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  {
    llvm::CrashRecoveryContext CRC;
    EXPECT_FALSE(CRC.RunSafely([&] {
      ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker, Event);
    }));
    EXPECT_EQ(CRC.RetCode, 37);
  }
  ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker, Event);

  EXPECT_EQ(Calls, 2U);
  expectSnapshotEq(snapshot(*Broker), Before);
}

TEST(ProcessResourceBrokerTest,
     ObserverDepthCleanupIgnoresExitedRunSafelyWorkerTLS) {
  const ProcessResourceBrokerEvent Event{
      ProcessResourceBrokerEventKind::WorkersGranted,
      ResourcePhase::PCGPrepare,
      /*Sequence=*/1,
      /*SessionID=*/0,
      /*RequestedWorkers=*/2,
      /*GrantedWorkers=*/2};
  ProcessResourceBroker *ObservedBroker = nullptr;
  std::atomic<unsigned> Calls{0};
  auto Broker = makeBroker(2, [&](const ProcessResourceBrokerEvent &) {
    const unsigned Call = Calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (Call != 1)
      return;
    ProcessResourceBrokerTestAccess::emitSyntheticEvent(*ObservedBroker, Event);
    llvm::CrashRecoveryContext::GetCurrent()->HandleExit(37);
  });
  ObservedBroker = Broker.get();
  const ProcessResourceBrokerSnapshot Before = snapshot(*Broker);

  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  {
    llvm::CrashRecoveryContext CRC;
    EXPECT_FALSE(CRC.RunSafelyOnThread([&] {
      ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker, Event);
    }));
    EXPECT_EQ(CRC.RetCode, 37);
  }

  ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker, Event);
  std::thread NewWorker([&] {
    ProcessResourceBrokerTestAccess::emitSyntheticEvent(*Broker, Event);
  });
  NewWorker.join();

  EXPECT_EQ(Calls.load(std::memory_order_relaxed), 3U);
  expectSnapshotEq(snapshot(*Broker), Before);
}

TEST(ProcessResourceBrokerTest,
     ObserverReentrantWaitingAdmissionIsForcedNonBlocking) {
  ProcessResourceBroker *ObservedBroker = nullptr;
  bool Reentered = false;
  bool ReentryWasConstrained = false;
  auto Broker = makeBroker(1, [&](const ProcessResourceBrokerEvent &Event) {
    if (Event.Kind != ProcessResourceBrokerEventKind::SessionGranted ||
        Reentered)
      return;
    Reentered = true;
    auto Nested = ObservedBroker->acquireSession(ResourcePhase::PCGOptCodeGen,
                                                 ResourceAdmissionMode::Wait);
    ReentryWasConstrained = Nested.constrained();
  });
  ObservedBroker = Broker.get();

  auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  EXPECT_TRUE(Reentered);
  EXPECT_TRUE(ReentryWasConstrained);
}

TEST(ProcessResourceBrokerTest, EarlyReturnReleasesAdmissionForNextSession) {
  auto Broker = makeBroker(1);
  auto ReturnEarly = [&] {
    auto Session = Broker->acquireSession(ResourcePhase::LinkParseResolve);
    EXPECT_EQ(snapshot(*Broker).ActiveTokens, 1U);
  };
  ReturnEarly();
  EXPECT_EQ(snapshot(*Broker).ActiveTokens, 0U);

  auto Next = Broker->acquireSession(ResourcePhase::LinkParseResolve);
  EXPECT_TRUE(Next.ownsAdmission());
  EXPECT_EQ(snapshot(*Broker).ActiveTokens, 1U);
}

TEST(ProcessResourceBrokerTest, ScopedGlobalOverrideUsesTheSelectedBroker) {
  auto Broker = makeBroker(2);
  ProcessResourceBroker *StableGlobal = &ProcessResourceBroker::global();
  {
    ScopedProcessResourceBrokerOverride Override(*Broker);
    EXPECT_EQ(&ProcessResourceBroker::global(), StableGlobal);
    auto Session = ProcessResourceBroker::global().acquireSession(
        ResourcePhase::LinkParseResolve);
    EXPECT_EQ(snapshot(*Broker).ActiveTokens, 1U);
  }
  EXPECT_EQ(&ProcessResourceBroker::global(), StableGlobal);
}
