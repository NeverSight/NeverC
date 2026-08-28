#ifndef NEVERC_LIB_FOUNDATION_CORE_PROCESSRESOURCEBROKERINTERNAL_H
#define NEVERC_LIB_FOUNDATION_CORE_PROCESSRESOURCEBROKERINTERNAL_H

#include "neverc/Foundation/Core/ProcessResourceBroker.h"

#include <functional>
#include <memory>

namespace neverc {

struct ProcessResourceBrokerConfig {
  bool Enabled = false;
  unsigned CpuTokens = 0;
};

struct ProcessResourceBrokerSnapshot {
  unsigned Capacity = 0;
  unsigned AvailableTokens = 0;
  unsigned ActiveTokens = 0;
  unsigned ActiveSessions = 0;
  unsigned WaitingSessions = 0;
  unsigned HighWaterTokens = 0;
  uint64_t WaitEpoch = 0;
};

enum class ProcessResourceBrokerEventKind : uint8_t {
  SessionWaiting,
  SessionGranted,
  SessionReleased,
  WorkersGranted,
  WorkersReleased,
};

struct ProcessResourceBrokerEvent {
  ProcessResourceBrokerEventKind Kind;
  ResourcePhase Phase;
  uint64_t Sequence = 0;
  uint64_t SessionID = 0;
  unsigned RequestedWorkers = 0;
  unsigned GrantedWorkers = 0;
};

class ProcessResourceBrokerTestAccess {
public:
  using Observer = std::function<void(const ProcessResourceBrokerEvent &)>;

  static std::unique_ptr<ProcessResourceBroker>
  create(ProcessResourceBrokerConfig Config, Observer Observe = {});
  static ProcessResourceBrokerSnapshot
  snapshot(const ProcessResourceBroker &Broker);

  /// Calls Callback while the broker mutex is held. Tests use this only to
  /// prove that worker grants take the try-lock path and return immediately.
  static void withMutexHeld(ProcessResourceBroker &Broker,
                            const std::function<void()> &Callback);
};

class ScopedProcessResourceBrokerOverride {
public:
  explicit ScopedProcessResourceBrokerOverride(ProcessResourceBroker &Broker);
  ScopedProcessResourceBrokerOverride(
      const ScopedProcessResourceBrokerOverride &) = delete;
  ScopedProcessResourceBrokerOverride &
  operator=(const ScopedProcessResourceBrokerOverride &) = delete;
  ~ScopedProcessResourceBrokerOverride();

private:
  std::shared_ptr<resource_broker_detail::BrokerState> PreviousState;
};

} // namespace neverc

#endif // NEVERC_LIB_FOUNDATION_CORE_PROCESSRESOURCEBROKERINTERNAL_H
