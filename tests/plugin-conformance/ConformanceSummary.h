//===- ConformanceSummary.h - per-capability conformance summary --------===//
//
// Records a pass/skip/fail result and reason for each plugin capability the
// conformance suite exercises, and emits a machine-readable JSON summary at
// the end of the run. The summary lets a downstream (or CI) see, capability by
// capability, what a compatible host proved and what it skipped and why. The
// output path comes from the NEVERC_CONFORMANCE_SUMMARY environment variable;
// when unset no file is written but the run is otherwise unchanged.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_PLUGIN_CONFORMANCE_SUMMARY_H
#define NEVERC_PLUGIN_CONFORMANCE_SUMMARY_H

#include <string>

namespace neverc::conformance {

enum class CapStatus { Pass, Skip, Fail };

/// Record the outcome for one capability (e.g. "neverc.object.write/replace").
/// Thread-safe; the last non-empty reason for a capability is kept.
void recordCapability(const std::string &Capability, CapStatus Status,
                      const std::string &Reason = std::string());

/// Write the accumulated summary as JSON to the path in
/// NEVERC_CONFORMANCE_SUMMARY, if that variable is set. Safe to call once at
/// the end of the process. Returns true if a file was written.
bool writeConformanceSummary();

} // namespace neverc::conformance

#endif
