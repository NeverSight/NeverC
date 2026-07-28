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
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_MERGE_COMMON_DWARFREBASE_H
#define NEVERC_LIB_MERGE_COMMON_DWARFREBASE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <array>
#include <cstdint>

namespace neverc::merge {

/// The DWARF sections whose contents are addressed by offsets held in other
/// DWARF sections.  Anything a merge does not have to re-point is absent.
enum class DwarfSection : uint8_t {
  Info,
  Abbrev,
  Str,
  LineStr,
  Line,
  Ranges,
  RngLists,
  Loc,
  LocLists,
  StrOffsets,
  Addr,
  MacInfo,
  Macro,
  Count,
};

/// Classify a section name, or return Count when it is not a DWARF section
/// this cares about.  Accepts every spelling the three object formats use:
/// Mach-O writes "__debug_info" and truncates names to 16 characters, ELF and
/// COFF write ".debug_info", and COFF may add a "$" grouping suffix.
DwarfSection classifyDwarfSection(llvm::StringRef SectionName);

/// Where one partition's DWARF sections begin inside the merged output, plus
/// the extra bookkeeping needed to find and bound its contribution again once
/// every partition has been appended.
class PartitionDwarf {
public:
  /// Note where one input section landed.  Ignores non-DWARF sections, so
  /// callers can hand it every section they merge.
  void record(llvm::StringRef SectionName, unsigned MergedSectionIndex,
              uint64_t Offset, uint64_t Size);

  /// Start of this partition's contribution to \p S within the merged
  /// section.  Zero also means "did not contribute", which needs no
  /// adjustment either way.
  uint64_t start(DwarfSection S) const {
    return Starts[static_cast<size_t>(S)];
  }

  /// False when this partition has no compile units, or when it is the first
  /// contributor to every section and so already sits at the offsets its
  /// units were emitted with.
  bool needsRebase() const;

  unsigned infoSectionIndex() const { return InfoSectionIndex; }
  unsigned abbrevSectionIndex() const { return AbbrevSectionIndex; }
  uint64_t infoSize() const { return InfoSize; }
  uint64_t abbrevSize() const { return AbbrevSize; }

  static constexpr unsigned NoSection = ~0u;

private:
  std::array<uint64_t, static_cast<size_t>(DwarfSection::Count)> Starts{};
  unsigned InfoSectionIndex = NoSection;
  unsigned AbbrevSectionIndex = NoSection;
  uint64_t InfoSize = 0;
  uint64_t AbbrevSize = 0;
};

/// Rewrite, in place, the inter-section offsets inside one partition's
/// .debug_info contribution so they address the merged sections.
///
/// \p Info is that partition's slice of the merged .debug_info.
/// \p Abbrev is the same partition's slice of the merged .debug_abbrev; unit
/// headers are read with their original, pre-merge offsets, so the slice must
/// start exactly where the partition's abbreviations do.
///
/// Returns false if the DWARF could not be parsed, in which case \p Info may
/// have been partially rewritten and the caller must treat the merge as
/// failed rather than emit the object.
bool rebaseDebugInfo(llvm::MutableArrayRef<char> Info,
                     llvm::ArrayRef<char> Abbrev, const PartitionDwarf &Part,
                     bool IsLittleEndian);

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

    llvm::MutableArrayRef<char> Info = SectionData(Part.infoSectionIndex());
    llvm::MutableArrayRef<char> Abbrev =
        SectionData(Part.abbrevSectionIndex());
    // A unit cannot be read without its abbreviations, so a partition that
    // contributed one without the other is malformed.
    const uint64_t InfoStart = Part.start(DwarfSection::Info);
    const uint64_t AbbrevStart = Part.start(DwarfSection::Abbrev);
    if (InfoStart + Part.infoSize() > Info.size() ||
        AbbrevStart + Part.abbrevSize() > Abbrev.size())
      return false;

    if (!rebaseDebugInfo(Info.slice(InfoStart, Part.infoSize()),
                         Abbrev.slice(AbbrevStart, Part.abbrevSize()), Part,
                         IsLittleEndian))
      return false;
  }
  return true;
}

} // namespace neverc::merge

#endif // NEVERC_LIB_MERGE_COMMON_DWARFREBASE_H
