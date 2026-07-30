//===- DwarfRebase.h - Re-point DWARF offsets after an object merge -------===//
//
// Parallel codegen splits a module into partitions, emits one object per
// partition and concatenates them.  Every partition emits a full set of DWARF
// sections, and the offsets those sections hold for each other are plain
// integers written into the data -- not relocations.  Concatenation therefore
// leaves each partition after the first pointing at the first partition's
// tables: compile units read the wrong abbreviation table (a debugger then
// rejects the unit outright) and names resolve to whatever string happens to
// sit at that offset in the first partition's .debug_str.
//
// This rewrites those offsets so they address the merged sections.
//
// Mach-O needs it.  Ordinary ELF and COFF objects do not: they express the
// same offsets as relocations, which the merger's relocation remapping
// already re-points.  Standalone Split-DWARF objects are the exception: their
// `.dwo` sections deliberately contain no relocations, so ELF and COFF DWO
// contributions must be rewritten after concatenation too.
//
// Apple accelerator tables (__apple_names and friends, emitted for DWARF 4
// and earlier) put one unchainable hash-table header at the start of their
// section.  They cannot be repaired by offset rebasing, so the Mach-O merger
// omits those derived indexes and lets LLDB/dsymutil rebuild them from the
// complete .debug_info.  DWARF 5's .debug_names does chain, and is rebased.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_MERGE_COMMON_DWARFREBASE_H
#define NEVERC_LIB_MERGE_COMMON_DWARFREBASE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <array>
#include <cstdint>

namespace neverc::merge {

/// The DWARF sections a merge has to reason about: either something addresses
/// them by offset, or they hold such an offset themselves.  Anything neither
/// pointed at nor pointing is absent.
enum class DwarfSection : uint8_t {
  Info,
  Types,
  Abbrev,
  Aranges,
  PubNames,
  PubTypes,
  Str,
  LineStr,
  Line,
  Frame,
  Ranges,
  RngLists,
  Loc,
  LocLists,
  StrOffsets,
  Addr,
  MacInfo,
  Macro,
  Names,
  Count,
};

constexpr size_t dwarfSectionIndex(DwarfSection S) {
  return static_cast<size_t>(S);
}
constexpr size_t DwarfSectionCount = dwarfSectionIndex(DwarfSection::Count);

/// Classify a section name, or return Count when it is not a DWARF section
/// this cares about.  Accepts both spellings: Mach-O writes "__debug_info"
/// and truncates names to 16 characters, ELF writes ".debug_info".
DwarfSection classifyDwarfSection(llvm::StringRef SectionName);

/// True when rebasing rewrites bytes stored in \p S itself.  Keep this policy
/// next to the rebaser so merger verification cannot drift from the set of
/// sections rebasePartitionDwarf() intentionally changes.
constexpr bool dwarfSectionContentsAreRebased(DwarfSection S) {
  switch (S) {
  case DwarfSection::Info:
  case DwarfSection::Types:
  case DwarfSection::Aranges:
  case DwarfSection::PubNames:
  case DwarfSection::PubTypes:
  case DwarfSection::Line:
  case DwarfSection::Frame:
  case DwarfSection::StrOffsets:
  case DwarfSection::Macro:
  case DwarfSection::Names:
    return true;
  default:
    return false;
  }
}

/// Where one partition's DWARF sections landed inside the merged output.
class PartitionDwarf {
public:
  static constexpr unsigned NoSection = ~0u;

  /// Note where one input section landed.  Ignores non-DWARF sections, so
  /// callers can hand it every section they merge.
  void record(llvm::StringRef SectionName, unsigned MergedSectionIndex,
              uint64_t Offset, uint64_t Size);
  void record(DwarfSection Section, unsigned MergedSectionIndex,
              uint64_t Offset, uint64_t Size);

  /// Start of this partition's contribution to \p S within the merged
  /// section.  Zero also means "did not contribute", which needs no
  /// adjustment either way.
  uint64_t start(DwarfSection S) const { return At[dwarfSectionIndex(S)].Start; }
  uint64_t size(DwarfSection S) const { return At[dwarfSectionIndex(S)].Size; }
  unsigned sectionIndex(DwarfSection S) const {
    return At[dwarfSectionIndex(S)].MergedIndex;
  }

  /// False when this partition is the first contributor to every section and
  /// so already sits at the offsets it was emitted with.
  bool needsRebase() const;

private:
  struct Contribution {
    unsigned MergedIndex = NoSection;
    uint64_t Start = 0;
    uint64_t Size = 0;
  };
  std::array<Contribution, DwarfSectionCount> At{};
};

/// One partition's slice of each merged DWARF section, indexed by
/// DwarfSection.  Empty where the partition contributed nothing.
using DwarfSlices =
    std::array<llvm::MutableArrayRef<char>, DwarfSectionCount>;

/// Rewrite, in place, every offset in one partition's DWARF that addresses
/// another DWARF section, so it addresses the merged section instead.
///
/// Offsets are read at their original, pre-merge values, so each slice must
/// start exactly where that partition's contribution does.
///
/// Returns false if the DWARF could not be parsed, in which case the slices
/// may have been partially rewritten and the caller must treat the merge as
/// failed rather than emit the object.
bool rebasePartitionDwarf(const DwarfSlices &Slices, const PartitionDwarf &Part,
                          bool IsLittleEndian);

/// The package indexes needed when several standalone DWO contributions are
/// placed in one file. Split units intentionally omit section-base attributes;
/// without these indexes every unit would read partition zero's string,
/// range, location, and abbreviation contributions.
struct DwarfPackageIndexes {
  llvm::SmallVector<char, 0> CompileUnits;
  llvm::SmallVector<char, 0> TypeUnits;
};

/// Finalize a merged DWARF 5 package.
///
/// String-offset entries are made absolute within the merged `.debug_str.dwo`
/// section. All other contribution-relative offsets remain untouched and are
/// resolved through the returned `.debug_cu_index` / `.debug_tu_index`.
///
/// Returns false for malformed units, duplicate signatures, out-of-range
/// contributions, or package-index values that exceed DWARF 5's 32-bit fields.
bool finalizeDwarfPackage(
    llvm::ArrayRef<PartitionDwarf> Partitions,
    llvm::function_ref<llvm::MutableArrayRef<char>(unsigned)> SectionData,
    bool IsLittleEndian, DwarfPackageIndexes &Indexes);

/// Rebase every partition of one merged object.
///
/// \p SectionData maps a merged-section index to that section's bytes.  The
/// index comes from what each partition recorded, so a format whose sections
/// do not merge one-to-one by name still gets the section its offsets are
/// relative to.
template <typename SectionDataFn>
bool rebaseMergedDwarf(llvm::ArrayRef<PartitionDwarf> Partitions,
                       SectionDataFn SectionData, bool IsLittleEndian) {
  for (const PartitionDwarf &Part : Partitions) {
    if (!Part.needsRebase())
      continue;

    DwarfSlices Slices;
    for (size_t I = 0; I != DwarfSectionCount; ++I) {
      const DwarfSection S = static_cast<DwarfSection>(I);
      const unsigned Idx = Part.sectionIndex(S);
      if (Idx == PartitionDwarf::NoSection)
        continue;
      llvm::MutableArrayRef<char> All = SectionData(Idx);
      const uint64_t Start = Part.start(S);
      const uint64_t Size = Part.size(S);
      // A contribution that does not fit the section it was recorded against
      // means the bookkeeping and the merged bytes disagree; rewriting from
      // here would corrupt a neighbouring partition.
      if (Start > All.size() || Size > All.size() - Start)
        return false;
      Slices[I] = All.slice(Start, Size);
    }

    if (!rebasePartitionDwarf(Slices, Part, IsLittleEndian))
      return false;
  }
  return true;
}

} // namespace neverc::merge

#endif // NEVERC_LIB_MERGE_COMMON_DWARFREBASE_H
