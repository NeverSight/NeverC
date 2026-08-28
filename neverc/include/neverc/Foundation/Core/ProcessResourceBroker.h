#ifndef NEVERC_FOUNDATION_CORE_PROCESSRESOURCEBROKER_H
#define NEVERC_FOUNDATION_CORE_PROCESSRESOURCEBROKER_H

#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

namespace neverc {

/// Resource phases are telemetry and grant-policy labels only. They must never
/// participate in partitioning, cache keys, output identity, or diagnostics.
enum class ResourcePhase : uint8_t {
  FrontendInProcess,
  FrontendSubprocess,
  LinkParseResolve,
  LinkGCICF,
  LinkWrite,
  LTOSerial,
  PCGPrepare,
  PCGCodeGen,
  PCGOptCodeGen,
  PCGMerge,
};

enum class ResourceAdmissionMode : uint8_t {
  Wait,
  DoNotWait,
};

namespace resource_broker_detail {
struct BrokerState;
struct SessionState;
} // namespace resource_broker_detail

class ProcessResourceBroker;
class ProcessResourceBrokerTestAccess;
class ScopedProcessResourceBrokerOverride;

/// A copyable identity for an admitted request. Copies do not extend token
/// ownership; the admission owner and worker grants define token lifetime.
class ResourceSessionView {
public:
  ResourceSessionView() = default;

  uint64_t id() const;
  bool constrained() const { return Constrained; }
  bool bypassesBudget() const { return Bypass; }
  bool refersToSameSession(const ResourceSessionView &Other) const noexcept;
  explicit operator bool() const { return Bypass || Constrained || Session; }

private:
  ResourceSessionView(
      std::shared_ptr<resource_broker_detail::BrokerState> Broker,
      std::shared_ptr<resource_broker_detail::SessionState> Session,
      bool Bypass, bool Constrained)
      : Broker(std::move(Broker)), Session(std::move(Session)), Bypass(Bypass),
        Constrained(Constrained) {}

  std::shared_ptr<resource_broker_detail::BrokerState> Broker;
  std::shared_ptr<resource_broker_detail::SessionState> Session;
  bool Bypass = false;
  bool Constrained = false;

  friend class ProcessResourceBroker;
  friend class ResourceSessionPermit;
  friend class ResourceSessionScope;
  friend class ResourceWorkerGrant;
};

/// Installs a session view on another coordinator/worker thread.
class ResourceSessionScope {
public:
  explicit ResourceSessionScope(ResourceSessionView Session);
  ResourceSessionScope(const ResourceSessionScope &) = delete;
  ResourceSessionScope &operator=(const ResourceSessionScope &) = delete;
  ~ResourceSessionScope();

private:
  ResourceSessionView Installed;
  ResourceSessionView Previous;
  std::thread::id OwnerThread;
};

/// Move-only lifetime for one top-level progress token. Nested acquisitions
/// inherit the current session and therefore do not own another token.
class ResourceSessionPermit {
public:
  ResourceSessionPermit() = default;
  ResourceSessionPermit(const ResourceSessionPermit &) = delete;
  ResourceSessionPermit &operator=(const ResourceSessionPermit &) = delete;
  ResourceSessionPermit(ResourceSessionPermit &&Other) noexcept;
  ResourceSessionPermit &operator=(ResourceSessionPermit &&) = delete;
  ~ResourceSessionPermit();

  bool ownsAdmission() const { return OwnsAdmission; }
  bool constrained() const { return View.constrained(); }
  ResourceSessionView session() const { return View; }

private:
  ResourceSessionPermit(ResourceSessionView View, ResourceSessionView Previous,
                        bool OwnsAdmission, bool RetainsSession = false)
      : View(std::move(View)), Previous(std::move(Previous)),
        OwnerThread(std::this_thread::get_id()), OwnsAdmission(OwnsAdmission),
        RetainsSession(RetainsSession), Active(true) {}

  void reset() noexcept;

  ResourceSessionView View;
  ResourceSessionView Previous;
  std::thread::id OwnerThread;
  bool OwnsAdmission = false;
  bool RetainsSession = false;
  bool Active = false;

  friend class ProcessResourceBroker;
};

/// A phase-wide physical worker grant. Worker zero borrows the session's
/// progress token; ExtraTokens cover the remaining worker threads. Keeping one
/// grant through join avoids per-worker resource waits and hold-and-wait.
class ResourceWorkerGrant {
public:
  ResourceWorkerGrant() = default;
  ResourceWorkerGrant(const ResourceWorkerGrant &) = delete;
  ResourceWorkerGrant &operator=(const ResourceWorkerGrant &) = delete;
  ResourceWorkerGrant(ResourceWorkerGrant &&Other) noexcept;
  ResourceWorkerGrant &operator=(ResourceWorkerGrant &&) = delete;
  ~ResourceWorkerGrant();

  unsigned workerCount() const { return WorkerCount; }
  ResourceSessionView session() const { return View; }

private:
  ResourceWorkerGrant(ResourceSessionView View, ResourcePhase Phase,
                      unsigned WorkerCount, unsigned ExtraTokens,
                      bool TracksSession)
      : View(std::move(View)), Phase(Phase), WorkerCount(WorkerCount),
        ExtraTokens(ExtraTokens), TracksSession(TracksSession) {}

  void reset() noexcept;

  ResourceSessionView View;
  ResourcePhase Phase = ResourcePhase::LinkParseResolve;
  unsigned WorkerCount = 0;
  unsigned ExtraTokens = 0;
  bool TracksSession = false;

  friend class ProcessResourceBroker;
};

class ProcessResourceBroker {
public:
  ProcessResourceBroker(const ProcessResourceBroker &) = delete;
  ProcessResourceBroker &operator=(const ProcessResourceBroker &) = delete;
  ~ProcessResourceBroker() = default;

  static ProcessResourceBroker &global();

  bool enabled() const;
  ResourceSessionPermit
  acquireSession(ResourcePhase Phase,
                 ResourceAdmissionMode Mode = ResourceAdmissionMode::Wait);

  /// Never waits for the broker mutex or for tokens. With the budget disabled,
  /// this returns DesiredWorkers exactly; a constrained/unadmitted enabled
  /// request gets one progress worker.
  ResourceWorkerGrant grantWorkers(ResourcePhase Phase,
                                   unsigned DesiredWorkers) noexcept;
  ResourceWorkerGrant grantWorkers(ResourceSessionView Session,
                                   ResourcePhase Phase,
                                   unsigned DesiredWorkers) noexcept;

private:
  explicit ProcessResourceBroker(
      std::shared_ptr<resource_broker_detail::BrokerState> State)
      : State(std::move(State)) {}

  std::shared_ptr<resource_broker_detail::BrokerState> State;

  friend class ProcessResourceBrokerTestAccess;
  friend class ScopedProcessResourceBrokerOverride;
};

ResourceSessionView currentResourceSession() noexcept;

} // namespace neverc

#endif // NEVERC_FOUNDATION_CORE_PROCESSRESOURCEBROKER_H
