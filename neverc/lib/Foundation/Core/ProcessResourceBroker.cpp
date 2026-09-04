#include "neverc/Foundation/Core/ProcessResourceBroker.h"
#include "ProcessResourceBrokerInternal.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace neverc::resource_broker_detail {

struct BrokerState {
  bool Enabled = false;
  unsigned Capacity = 0;
  unsigned AvailableTokens = 0;
  unsigned ActiveTokens = 0;
  unsigned ActiveSessions = 0;
  unsigned HighWaterTokens = 0;
  uint64_t WaitEpoch = 0;
  uint64_t NextTicket = 1;
  uint64_t NextSessionID = 1;
  uint64_t NextEventSequence = 1;
  std::mutex Mutex;
  std::condition_variable Available;
  std::deque<uint64_t> Waiters;
  ProcessResourceBrokerTestAccess::Observer Observe;
};

struct SessionState {
  std::shared_ptr<BrokerState> Broker;
  uint64_t ID = 0;
  ResourcePhase AdmissionPhase = ResourcePhase::LinkParseResolve;
  std::atomic<unsigned> ActiveUsers{1};
  unsigned WorkerTokens = 0;
  bool OwnerReleased = false;
  bool BaselineReleased = false;
};

} // namespace neverc::resource_broker_detail

using neverc::resource_broker_detail::BrokerState;
using neverc::resource_broker_detail::SessionState;

namespace neverc {
namespace {

thread_local unsigned ObserverDepth = 0;
std::atomic<bool> GlobalOverrideActive{false};

/// Crash recovery skips stack destructors, so register the stable TLS depth
/// address itself before incrementing it. The cleanup captures the exact
/// prior nesting value and thread; normal exceptions use the scope-exit below.
/// RunSafelyOnThread joins its worker before the caller destroys the recovery
/// context, so that cross-thread cleanup must not dereference the exited
/// worker's TLS address.
class ObserverDepthCrashCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<ObserverDepthCrashCleanup,
                                                   unsigned> {
public:
  ObserverDepthCrashCleanup(llvm::CrashRecoveryContext *Context,
                            unsigned *Depth)
      : llvm::CrashRecoveryContextCleanupBase<ObserverDepthCrashCleanup,
                                              unsigned>(Context, Depth),
        PreviousDepth(*Depth), OwnerThread(std::this_thread::get_id()) {}

  void recoverResources() override {
    if (OwnerThread != std::this_thread::get_id())
      return;
    *this->resource = PreviousDepth;
  }

private:
  unsigned PreviousDepth;
  std::thread::id OwnerThread;
};

void emitEvent(const std::shared_ptr<BrokerState> &State,
               const ProcessResourceBrokerEvent &Event) {
  if (!State->Observe || ObserverDepth != 0)
    return;
  llvm::CrashRecoveryContextCleanupRegistrar<unsigned,
                                             ObserverDepthCrashCleanup>
      CrashCleanup(&ObserverDepth);
  const unsigned PreviousDepth = ObserverDepth;
  auto RestoreDepth = llvm::make_scope_exit(
      [&] { ObserverDepth = PreviousDepth; });
  ++ObserverDepth;
  State->Observe(Event);
}

bool retainSessionUser(const std::shared_ptr<SessionState> &Session) {
  unsigned Users = Session->ActiveUsers.load(std::memory_order_acquire);
  while (Users != 0) {
    if (Session->ActiveUsers.compare_exchange_weak(Users, Users + 1,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire))
      return true;
  }
  return false;
}

bool releaseSessionUserLocked(BrokerState &State, SessionState &Session) {
  const unsigned PreviousUsers =
      Session.ActiveUsers.fetch_sub(1, std::memory_order_acq_rel);
  if (PreviousUsers == 0)
    llvm::report_fatal_error("process resource session released twice");
  if (PreviousUsers != 1)
    return false;
  if (!Session.OwnerReleased || Session.BaselineReleased ||
      State.ActiveTokens == 0)
    llvm::report_fatal_error("invalid process resource session lifetime");
  Session.BaselineReleased = true;
  ++State.AvailableTokens;
  --State.ActiveTokens;
  return true;
}

ProcessResourceBrokerEvent
makeEventLocked(BrokerState &State, ProcessResourceBrokerEventKind Kind,
                ResourcePhase Phase, uint64_t SessionID = 0,
                unsigned RequestedWorkers = 0, unsigned GrantedWorkers = 0) {
  return {Kind,
          Phase,
          State.NextEventSequence++,
          SessionID,
          RequestedWorkers,
          GrantedWorkers};
}

void warnInvalidEnvironment(llvm::StringRef Name, llvm::StringRef Value) {
  llvm::errs() << "neverc: ignoring invalid " << Name << "='" << Value
               << "'; process resource budget is disabled\n";
}

ProcessResourceBrokerConfig configFromEnvironment() {
  ProcessResourceBrokerConfig Config;
  const char *Enabled = std::getenv("NEVERC_RESOURCE_BUDGET");
  if (!Enabled || llvm::StringRef(Enabled) == "0")
    return Config;
  if (llvm::StringRef(Enabled) != "1") {
    warnInvalidEnvironment("NEVERC_RESOURCE_BUDGET", Enabled);
    return Config;
  }

  Config.Enabled = true;
  const char *CpuTokens = std::getenv("NEVERC_RESOURCE_CPU_TOKENS");
  if (!CpuTokens) {
    const unsigned Hardware =
        std::max(1U, llvm::thread::hardware_concurrency());
    Config.CpuTokens = std::min(Hardware, 32U);
    return Config;
  }

  unsigned Parsed = 0;
  if (llvm::StringRef(CpuTokens).getAsInteger(10, Parsed) || Parsed == 0 ||
      Parsed > 4096) {
    warnInvalidEnvironment("NEVERC_RESOURCE_CPU_TOKENS", CpuTokens);
    Config.Enabled = false;
    Config.CpuTokens = 0;
    return Config;
  }
  Config.CpuTokens = Parsed;
  return Config;
}

std::shared_ptr<BrokerState>
makeState(ProcessResourceBrokerConfig Config,
          ProcessResourceBrokerTestAccess::Observer Observe = {}) {
  auto State = std::make_shared<BrokerState>();
  State->Enabled = Config.Enabled && Config.CpuTokens != 0;
  State->Capacity = State->Enabled ? Config.CpuTokens : 0;
  State->AvailableTokens = State->Capacity;
  State->Observe = std::move(Observe);
  return State;
}

} // namespace

uint64_t ResourceSessionView::id() const { return Session ? Session->ID : 0; }

bool ResourceSessionView::refersToSameSession(
    const ResourceSessionView &Other) const noexcept {
  return Broker == Other.Broker && Session == Other.Session &&
         Bypass == Other.Bypass && Constrained == Other.Constrained;
}

ResourceSessionPermit::ResourceSessionPermit(
    ResourceSessionPermit &&Other) noexcept
    : View(std::move(Other.View)),
      OwnsAdmission(std::exchange(Other.OwnsAdmission, false)),
      RetainsSession(std::exchange(Other.RetainsSession, false)),
      Active(std::exchange(Other.Active, false)) {}

ResourceSessionPermit::~ResourceSessionPermit() { reset(); }

void ResourceSessionPermit::reset() noexcept {
  if (!Active)
    return;
  Active = false;
  if (RetainsSession && View.Session) {
    const std::shared_ptr<SessionState> Session = View.Session;
    const std::shared_ptr<BrokerState> State = Session->Broker;
    bool ReleasedBaseline = false;
    {
      std::lock_guard<std::mutex> Lock(State->Mutex);
      ReleasedBaseline = releaseSessionUserLocked(*State, *Session);
    }
    RetainsSession = false;
    if (ReleasedBaseline)
      State->Available.notify_all();
  }
  if (!OwnsAdmission || !View.Session)
    return;

  const std::shared_ptr<SessionState> Session = View.Session;
  const std::shared_ptr<BrokerState> State = Session->Broker;
  ProcessResourceBrokerEvent Event;
  {
    std::lock_guard<std::mutex> Lock(State->Mutex);
    assert(!Session->OwnerReleased && State->ActiveSessions != 0 &&
           "resource session released twice");
    Session->OwnerReleased = true;
    --State->ActiveSessions;
    releaseSessionUserLocked(*State, *Session);
    Event =
        makeEventLocked(*State, ProcessResourceBrokerEventKind::SessionReleased,
                        Session->AdmissionPhase, Session->ID);
  }
  OwnsAdmission = false;
  State->Available.notify_all();
  emitEvent(State, Event);
}

ResourceWorkerGrant::ResourceWorkerGrant(ResourceWorkerGrant &&Other) noexcept
    : View(std::move(Other.View)), Phase(Other.Phase),
      WorkerCount(std::exchange(Other.WorkerCount, 0)),
      ExtraTokens(std::exchange(Other.ExtraTokens, 0)),
      TracksSession(std::exchange(Other.TracksSession, false)) {}

ResourceWorkerGrant::~ResourceWorkerGrant() { reset(); }

void ResourceWorkerGrant::reset() noexcept {
  if (!TracksSession) {
    WorkerCount = 0;
    return;
  }
  TracksSession = false;
  const unsigned Released = std::exchange(ExtraTokens, 0);
  WorkerCount = 0;
  const std::shared_ptr<SessionState> Session = View.Session;
  const std::shared_ptr<BrokerState> State = View.Broker;
  assert(Session && State && "tracked workers require an admitted session");
  ProcessResourceBrokerEvent Event;
  bool HasEvent = false;
  {
    std::lock_guard<std::mutex> Lock(State->Mutex);
    assert(Session->WorkerTokens >= Released &&
           State->ActiveTokens >= Released &&
           "resource worker tokens released twice");
    Session->WorkerTokens -= Released;
    State->AvailableTokens += Released;
    State->ActiveTokens -= Released;
    releaseSessionUserLocked(*State, *Session);
    if (Released != 0) {
      Event = makeEventLocked(*State,
                              ProcessResourceBrokerEventKind::WorkersReleased,
                              Phase, Session->ID, Released + 1, 1);
      HasEvent = true;
    }
  }
  State->Available.notify_all();
  if (HasEvent)
    emitEvent(State, Event);
}

ProcessResourceBroker &ProcessResourceBroker::global() {
  static ProcessResourceBroker Default(makeState(configFromEnvironment()));
  return Default;
}

bool ProcessResourceBroker::enabled() const {
  return std::atomic_load_explicit(&State, std::memory_order_acquire)->Enabled;
}

ResourceSessionPermit
ProcessResourceBroker::acquireSession(ResourcePhase Phase,
                                      ResourceAdmissionMode Mode) {
  return acquireSession({}, Phase, Mode);
}

ResourceSessionPermit ProcessResourceBroker::acquireSession(
    ResourceSessionView Parent, ResourcePhase Phase,
    ResourceAdmissionMode Mode) {
  const std::shared_ptr<BrokerState> State =
      std::atomic_load_explicit(&this->State, std::memory_order_acquire);
  if (Parent.Broker == State) {
    const bool Retained =
        Parent.Session && retainSessionUser(Parent.Session);
    if (!Parent.Session || Retained) {
      return ResourceSessionPermit(std::move(Parent),
                                   /*OwnsAdmission=*/false,
                                   /*RetainsSession=*/Retained);
    }
  }

  if (!State->Enabled) {
    ResourceSessionView View(State, nullptr, /*Bypass=*/true,
                             /*Constrained=*/false);
    return ResourceSessionPermit(std::move(View), /*OwnsAdmission=*/false);
  }

  auto Constrained = [&] {
    ResourceSessionView View(State, nullptr, /*Bypass=*/false,
                             /*Constrained=*/true);
    return ResourceSessionPermit(std::move(View), /*OwnsAdmission=*/false);
  };

  if (ObserverDepth != 0)
    return Constrained();

  std::unique_lock<std::mutex> Lock(State->Mutex, std::defer_lock);
  if (Mode == ResourceAdmissionMode::DoNotWait) {
    if (!Lock.try_lock() || !State->Waiters.empty() ||
        State->AvailableTokens == 0)
      return Constrained();
  } else {
    Lock.lock();
  }

  if (Mode == ResourceAdmissionMode::Wait &&
      (!State->Waiters.empty() || State->AvailableTokens == 0)) {
    const uint64_t Ticket = State->NextTicket++;
    State->Waiters.push_back(Ticket);
    ++State->WaitEpoch;
    ProcessResourceBrokerEvent Waiting = makeEventLocked(
        *State, ProcessResourceBrokerEventKind::SessionWaiting, Phase);
    Lock.unlock();
    emitEvent(State, Waiting);
    Lock.lock();
    State->Available.wait(Lock, [&] {
      return !State->Waiters.empty() && State->Waiters.front() == Ticket &&
             State->AvailableTokens != 0;
    });
    assert(State->Waiters.front() == Ticket);
    State->Waiters.pop_front();
  }

  assert(State->AvailableTokens != 0);
  auto Session = std::make_shared<SessionState>();
  Session->Broker = State;
  Session->ID = State->NextSessionID++;
  Session->AdmissionPhase = Phase;
  --State->AvailableTokens;
  ++State->ActiveTokens;
  ++State->ActiveSessions;
  State->HighWaterTokens =
      std::max(State->HighWaterTokens, State->ActiveTokens);
  ProcessResourceBrokerEvent Granted =
      makeEventLocked(*State, ProcessResourceBrokerEventKind::SessionGranted,
                      Phase, Session->ID);
  Lock.unlock();
  State->Available.notify_all();
  emitEvent(State, Granted);

  ResourceSessionView View(State, std::move(Session), /*Bypass=*/false,
                           /*Constrained=*/false);
  return ResourceSessionPermit(std::move(View), /*OwnsAdmission=*/true);
}

ResourceWorkerGrant
ProcessResourceBroker::grantWorkers(ResourceSessionView Session,
                                    ResourcePhase Phase,
                                    unsigned DesiredWorkers) noexcept {
  const std::shared_ptr<BrokerState> State =
      std::atomic_load_explicit(&this->State, std::memory_order_acquire);
  if (DesiredWorkers == 0)
    return ResourceWorkerGrant(std::move(Session), Phase, 0, 0,
                               /*TracksSession=*/false);
  if (!State->Enabled)
    return ResourceWorkerGrant(std::move(Session), Phase, DesiredWorkers, 0,
                               /*TracksSession=*/false);
  if (Session.Broker != State || Session.Constrained || !Session.Session)
    return ResourceWorkerGrant(std::move(Session), Phase, 1, 0,
                               /*TracksSession=*/false);

  if (!retainSessionUser(Session.Session))
    return ResourceWorkerGrant(std::move(Session), Phase, 1, 0,
                               /*TracksSession=*/false);

  std::unique_lock<std::mutex> Lock(State->Mutex, std::try_to_lock);
  if (!Lock.owns_lock() || !State->Waiters.empty())
    return ResourceWorkerGrant(std::move(Session), Phase, 1, 0,
                               /*TracksSession=*/true);

  const unsigned RequestedExtras = DesiredWorkers - 1;
  const unsigned GrantedExtras =
      std::min(RequestedExtras, State->AvailableTokens);
  if (GrantedExtras == 0)
    return ResourceWorkerGrant(std::move(Session), Phase, 1, 0,
                               /*TracksSession=*/true);

  State->AvailableTokens -= GrantedExtras;
  State->ActiveTokens += GrantedExtras;
  Session.Session->WorkerTokens += GrantedExtras;
  State->HighWaterTokens =
      std::max(State->HighWaterTokens, State->ActiveTokens);
  ProcessResourceBrokerEvent Event = makeEventLocked(
      *State, ProcessResourceBrokerEventKind::WorkersGranted, Phase,
      Session.Session->ID, DesiredWorkers, GrantedExtras + 1);
  Lock.unlock();
  emitEvent(State, Event);
  return ResourceWorkerGrant(std::move(Session), Phase, GrantedExtras + 1,
                             GrantedExtras, /*TracksSession=*/true);
}

std::unique_ptr<ProcessResourceBroker>
ProcessResourceBrokerTestAccess::create(ProcessResourceBrokerConfig Config,
                                        Observer Observe) {
  return std::unique_ptr<ProcessResourceBroker>(
      new ProcessResourceBroker(makeState(Config, std::move(Observe))));
}

ProcessResourceBrokerSnapshot
ProcessResourceBrokerTestAccess::snapshot(const ProcessResourceBroker &Broker) {
  const std::shared_ptr<BrokerState> State =
      std::atomic_load_explicit(&Broker.State, std::memory_order_acquire);
  std::lock_guard<std::mutex> Lock(State->Mutex);
  return {State->Capacity,
          State->AvailableTokens,
          State->ActiveTokens,
          State->ActiveSessions,
          static_cast<unsigned>(State->Waiters.size()),
          State->HighWaterTokens,
          State->WaitEpoch};
}

void ProcessResourceBrokerTestAccess::withMutexHeld(
    ProcessResourceBroker &Broker, const std::function<void()> &Callback) {
  const std::shared_ptr<BrokerState> State =
      std::atomic_load_explicit(&Broker.State, std::memory_order_acquire);
  std::lock_guard<std::mutex> Lock(State->Mutex);
  Callback();
}

void ProcessResourceBrokerTestAccess::emitSyntheticEvent(
    ProcessResourceBroker &Broker, const ProcessResourceBrokerEvent &Event) {
  // A synthetic observer may deliberately leave through crash recovery. Keep
  // the loaded strong reference in CRC-owned storage so that non-local exit
  // cannot skip its destructor and retain the broker state indefinitely.
  auto StateOwner = std::make_unique<std::shared_ptr<BrokerState>>(
      std::atomic_load_explicit(&Broker.State, std::memory_order_acquire));
  llvm::CrashRecoveryContextCleanupRegistrar<std::shared_ptr<BrokerState>>
      StateCleanup(StateOwner.get());
  emitEvent(*StateOwner, Event);
}

ScopedProcessResourceBrokerOverride::ScopedProcessResourceBrokerOverride(
    ProcessResourceBroker &Broker) {
  bool Expected = false;
  if (!GlobalOverrideActive.compare_exchange_strong(
          Expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
    llvm::report_fatal_error("nested process resource broker test override");
  ProcessResourceBroker &Global = ProcessResourceBroker::global();
  const std::shared_ptr<BrokerState> InstalledState =
      std::atomic_load_explicit(&Broker.State, std::memory_order_acquire);
  PreviousState = std::atomic_exchange_explicit(&Global.State, InstalledState,
                                                std::memory_order_acq_rel);
}

ScopedProcessResourceBrokerOverride::~ScopedProcessResourceBrokerOverride() {
  ProcessResourceBroker &Global = ProcessResourceBroker::global();
  std::atomic_store_explicit(&Global.State, std::move(PreviousState),
                             std::memory_order_release);
  bool Expected = true;
  if (!GlobalOverrideActive.compare_exchange_strong(Expected, false,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
    llvm::report_fatal_error(
        "unbalanced process resource broker test override");
}

} // namespace neverc
