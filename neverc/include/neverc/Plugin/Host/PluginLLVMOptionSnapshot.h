#ifndef NEVERC_PLUGIN_HOST_PLUGINLLVMOPTIONSNAPSHOT_H
#define NEVERC_PLUGIN_HOST_PLUGINLLVMOPTIONSNAPSHOT_H

#include <functional>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace neverc::plugin {

std::shared_mutex &pluginLLVMOptionGate();
bool pluginLLVMOptionGateHeldExclusivelyByCurrentThread();
bool pluginLLVMOptionGateHeldSharedByCurrentThread();

class PluginLLVMOptionExclusiveLease {
public:
  explicit PluginLLVMOptionExclusiveLease(std::shared_mutex &Gate);
  ~PluginLLVMOptionExclusiveLease();

  PluginLLVMOptionExclusiveLease(const PluginLLVMOptionExclusiveLease &) =
      delete;
  PluginLLVMOptionExclusiveLease &
  operator=(const PluginLLVMOptionExclusiveLease &) = delete;

private:
  std::shared_mutex *Gate = nullptr;
  std::unique_lock<std::shared_mutex> Lock;
};

class PluginLLVMOptionSharedLease {
public:
  explicit PluginLLVMOptionSharedLease(std::shared_mutex &Gate);
  ~PluginLLVMOptionSharedLease();

  PluginLLVMOptionSharedLease(const PluginLLVMOptionSharedLease &) = delete;
  PluginLLVMOptionSharedLease &
  operator=(const PluginLLVMOptionSharedLease &) = delete;

private:
  std::shared_mutex *Gate = nullptr;
  std::shared_lock<std::shared_mutex> Lock;
  bool CoveredByExclusive = false;
};

/// Serializes access to LLVM's process-global command-line registry and
/// restores every registered option to its exact entry state on destruction.
class PluginLLVMOptionSnapshot {
public:
  explicit PluginLLVMOptionSnapshot(std::shared_mutex &Gate);
  ~PluginLLVMOptionSnapshot();

  PluginLLVMOptionSnapshot(const PluginLLVMOptionSnapshot &) = delete;
  PluginLLVMOptionSnapshot &
  operator=(const PluginLLVMOptionSnapshot &) = delete;

private:
  PluginLLVMOptionExclusiveLease Lock;
  std::vector<std::function<void()>> Restorers;
};

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_HOST_PLUGINLLVMOPTIONSNAPSHOT_H
