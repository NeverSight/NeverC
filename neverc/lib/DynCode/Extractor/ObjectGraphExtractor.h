#ifndef NEVERC_LIB_DYNCODE_EXTRACTOR_OBJECTGRAPHEXTRACTOR_H
#define NEVERC_LIB_DYNCODE_EXTRACTOR_OBJECTGRAPHEXTRACTOR_H

// Volume 6 task 11: the format-agnostic dyncode code extractor.
//
// The extractor consumes a verified plugin::PluginObjectGraph (the volume 4
// in-memory object) and produces a typed DynCodeExtractionPlan plus a candidate
// DynCodeImage (the selected code bytes laid out entry-first) and a
// DynCodeReport.  It replaces the old per-format ELF/COFF/Mach-O readers that
// each re-parsed a file on disk: here nothing is read from a path and no format
// switch is hard-coded, so a plugin object format that can be read into an
// ObjectGraph (and has a matching relocation/extractor provider) flows through
// the same path.
//
// This task is the *planner*: it selects/orders/lays-out the code fragments,
// builds the symbol output map, records the relocation worklist (disposition
// left Pending) and the surviving external references, and assembles the
// candidate bytes.  It does not apply relocations (task 12) or run byte-level
// rewrites / the final verifier (tasks 13-14).

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

/// The bundle produced by a successful extraction: the metadata plan, the
/// candidate image bytes and the audit report.
struct ObjectGraphExtractionResult {
  DynCodeExtractionPlan Plan;
  DynCodeImage Image;
  DynCodeReport Report;
};

/// One planned output fragment: a contiguous byte range copied from a source
/// code section into the image.  Fragments are function-granular so the entry
/// function can be placed first (dyncode requires the entry at output offset 0).
struct PlannedFragment {
  uint64_t SectionID = 0;
  std::string SectionName;
  uint64_t StartInSection = 0; ///< byte offset of this fragment inside the source section
  uint64_t Size = 0;
  uint64_t Alignment = 1;
  uint64_t OutputOffset = 0;
  bool IsEntry = false;
  llvm::ArrayRef<uint8_t> Bytes; ///< view into the source section Data (may be shorter than Size for tail zero-fill)
};

/// Returns true if the section holds code that dyncode extracts.
bool isDynCodeCodeSection(const plugin::PluginObjectSection &Section,
                          const TargetDesc &Target);

/// Verifies a candidate plan/image pair against the request.  This is the plan
/// verifier the builtin path runs and that any plugin replacement of
/// object.graph -> extraction.plan must also pass: a single in-range entry, no
/// symbol mapping past the image end, and the entry offset consistent with the
/// entry symbol mapping.
llvm::Error verifyDynCodeExtractionPlan(const DynCodeExtractionPlan &Plan,
                                        const DynCodeImage &Image,
                                        const DynCodeOptions &Opts);

/// Format-agnostic extractor: validate the graph, plan sections/symbols/
/// relocations, resolve the entry, assemble the candidate image and verify.
class ObjectGraphExtractor {
public:
  ObjectGraphExtractor(const plugin::PluginObjectGraph &Graph,
                       const DynCodeOptions &Opts)
      : Graph(Graph), Opts(Opts) {}

  llvm::Expected<ObjectGraphExtractionResult> run();

private:
  llvm::Error validateGraph();
  llvm::Error resolveEntry();
  llvm::Error planSections();
  llvm::Error planSymbols();
  llvm::Error planRelocations();
  llvm::Error assembleImage(DynCodeImage &Image);

  /// Returns the planned output fragment that covers ``OffsetInSection`` of the
  /// source section ``SectionID``, or null if the offset falls outside every
  /// selected fragment (e.g. it lands in a discarded section).
  const PlannedFragment *fragmentContaining(uint64_t SectionID,
                                            uint64_t OffsetInSection) const;

  const plugin::PluginObjectGraph &Graph;
  const DynCodeOptions &Opts;

  DynCodeExtractionPlan Plan;
  DynCodeReport Report;
  std::vector<PlannedFragment> Fragments; ///< in output order (entry first)

  const plugin::PluginObjectSymbol *EntrySymbol = nullptr;
  uint64_t EntrySectionID = 0;
  uint64_t EntryValueInSection = 0;
  uint64_t EntryOutputOffset = 0;

  /// Output offset of every defined symbol that landed in the image, keyed by
  /// the source symbol ID; used to resolve intra-image relocation targets.
  llvm::DenseMap<uint64_t, uint64_t> SymOffsetByID;

  uint64_t SelectedCount = 0;
  uint64_t RejectedCount = 0;
  uint64_t RuntimeContractCount = 0;
  uint64_t RemainingExternalCount = 0;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_EXTRACTOR_OBJECTGRAPHEXTRACTOR_H
