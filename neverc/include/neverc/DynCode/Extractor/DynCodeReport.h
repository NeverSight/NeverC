#ifndef NEVERC_DYNCODE_EXTRACTOR_DYNCODEREPORT_H
#define NEVERC_DYNCODE_EXTRACTOR_DYNCODEREPORT_H

// The dyncode extraction report.
//
// The report carries the summary counts and digests mirrored by the C ABI
// NevercDynCodeReportInfo, plus a journal of per-phase/provider records.  On
// ``freeze`` the journal is sorted into a stable (phase-order, provider, key)
// ordering and the report becomes read-only; ``toCanonicalJSON`` then renders a
// deterministic document with lexicographically ordered keys so two runs of a
// deterministic pipeline produce byte-identical reports.

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

struct DynCodeReportSummary {
  std::array<uint8_t, 32> RequestDigest{};
  std::array<uint8_t, 32> RouteDigest{};
  std::array<uint8_t, 32> InputDigest{};
  std::array<uint8_t, 32> OutputDigest{};
  uint64_t SelectedSectionCount = 0;
  uint64_t RejectedSectionCount = 0;
  uint64_t PatchedRelocationCount = 0;
  uint64_t RuntimeContractCount = 0;
  uint64_t RemainingExternalCount = 0;
  uint64_t ImageSize = 0;
  uint64_t Alignment = 1;
  uint64_t PaddingSize = 0;
  uint64_t BadByteHitCount = 0;
  uint64_t EntryOffset = 0;
  std::string EntrySymbol;
};

struct DynCodeReportRecord {
  uint64_t PhaseOrder = 0;
  std::string Provider;
  std::string Key;
  std::string Value;
};

class DynCodeReport {
public:
  DynCodeReport() = default;

  bool frozen() const { return Frozen; }
  const DynCodeReportSummary &summary() const { return Summary; }

  llvm::Error setSummary(const DynCodeReportSummary &Value);
  llvm::Error addRecord(DynCodeReportRecord Record);

  /// Sorts the journal into stable (phase-order, provider, key) order and marks
  /// the report read-only.  Idempotent.
  llvm::Error freeze();

  llvm::ArrayRef<DynCodeReportRecord> records() const { return Records; }

  /// Deterministic canonical JSON.  Requires the report to be frozen so the
  /// journal ordering is stable.
  llvm::Expected<std::string> toCanonicalJSON() const;

private:
  bool Frozen = false;
  DynCodeReportSummary Summary;
  std::vector<DynCodeReportRecord> Records;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_EXTRACTOR_DYNCODEREPORT_H
