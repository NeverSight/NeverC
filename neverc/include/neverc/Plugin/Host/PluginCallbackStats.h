#ifndef NEVERC_PLUGIN_HOST_PLUGINCALLBACKSTATS_H
#define NEVERC_PLUGIN_HOST_PLUGINCALLBACKSTATS_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {

// Low-overhead aggregation of plugin callback activity, keyed by
// (plugin ID, callback/phase name).  Volume 6 task 24: the single callback gate
// in PluginSession feeds this so `-ftime-trace` side summaries and the
// machine-readable overhead report can attribute call counts, wall time,
// errors and cache hits per plugin/phase without maintaining a second
// bookkeeping path.
//
// The object never allocates until the first callback is recorded, so a
// compilation that selects no plugin -- which never enters the gate -- pays
// nothing.  It is owned per PluginSession and its methods are thread-safe
// because THREAD_SAFE plugins may run callbacks concurrently.
class PluginCallbackStats {
public:
  struct Entry {
    std::string PluginID;
    std::string Callback;
    uint64_t Calls = 0;
    uint64_t TotalNanos = 0;
    uint64_t Errors = 0;
    uint64_t CacheHits = 0;
  };

  // Records one callback invocation.  An empty plugin ID is ignored so the
  // host cannot accidentally attribute work to no plugin.
  void record(llvm::StringRef PluginID, llvm::StringRef Callback,
              uint64_t Nanos, bool Error);

  // Records a cache hit that let the host skip a callback's real work.
  void recordCacheHit(llvm::StringRef PluginID, llvm::StringRef Callback);

  bool empty() const;
  uint64_t totalCalls() const;
  uint64_t totalNanos() const;

  // Snapshot of all entries, stably sorted by (PluginID, Callback) so the JSON
  // summary and any diff are deterministic.
  std::vector<Entry> snapshot() const;

  // Machine-readable summary with a fixed key order and deterministic entry
  // order.  Empty stats still produce a valid, empty document.
  std::string toJSON() const;

private:
  // Caller must hold Mutex.
  Entry &lookup(llvm::StringRef PluginID, llvm::StringRef Callback);

  mutable std::mutex Mutex;
  // The number of distinct (plugin, phase) pairs is small (a few plugins times
  // a few dozen phases), so a linear scan under the gate mutex is cheap next to
  // the plugin callback it wraps.
  std::vector<Entry> Entries;
};

} // namespace neverc::plugin

#endif
