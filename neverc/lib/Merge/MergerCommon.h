//===- MergerCommon.h - Internal helpers for object mergers ----===//
//
// Helpers shared across the per-format mergers.  Kept private to the
// `neverc/lib/Merge/` translation unit so the public API in
// `Merge/Merger.h` stays minimal.
//
//===-----------------------------------------------------------===//

#ifndef NEVERC_LIB_MERGE_MERGER_COMMON_H
#define NEVERC_LIB_MERGE_MERGER_COMMON_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace neverc::merge::detail {

/// Canonical output section name for the ELF `mergeSections` mode: per-symbol
/// sections (`.text.foo`, `.bss.bar`, ...) collapse to their umbrella section
/// so symbol names stop leaking through section names (Android kernel modules).
///
/// This is the *single* source of truth for that renaming.  The merger applies
/// it while laying out sections (MergerELF.cpp) and the independent verifier
/// applies it to predict where a symbol should land (MergerVerify.cpp); keeping
/// them as two hand-synced copies risked one drifting from the other and either
/// false-rejecting a good merge or masking a real offset bug.  When
/// \p MergeSections is false, or the name is empty, or it is in \p Preserved,
/// the name is returned unchanged.
inline llvm::StringRef
canonicalELFSectionName(llvm::StringRef Name, bool MergeSections,
                        llvm::ArrayRef<llvm::StringRef> Preserved) {
  if (!MergeSections || Name.empty())
    return Name;
  for (llvm::StringRef P : Preserved)
    if (Name == P)
      return Name;
  if (Name.starts_with(".text."))
    return ".text";
  if (Name.starts_with(".bss."))
    return ".bss";
  if (Name.starts_with(".data."))
    return ".data";
  if (Name.starts_with(".rodata."))
    return ".rodata";
  return Name;
}

/// Deduplicating string table: each unique payload is appended once and
/// callers receive a 4-byte offset that they can splat into ELF/MachO/COFF
/// `*_strx` fields.  Index 0 is reserved as the empty string per the ELF
/// strtab convention.
struct DedupStrTab {
  llvm::SmallVector<char, 0> Data;
  llvm::StringMap<uint32_t> Index;

  DedupStrTab() : Data{'\0'} {}

  uint32_t add(llvm::StringRef S) {
    if (S.empty())
      return 0;
    auto [It, Inserted] = Index.try_emplace(S, 0);
    if (!Inserted)
      return It->second;
    uint32_t Off = Data.size();
    Data.append(S.begin(), S.end());
    Data.push_back('\0');
    It->second = Off;
    return Off;
  }
};

} // namespace neverc::merge::detail

#endif // NEVERC_LIB_MERGE_MERGER_COMMON_H
