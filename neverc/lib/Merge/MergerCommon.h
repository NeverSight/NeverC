//===- MergerCommon.h - Internal helpers for object mergers ----===//
//
// Helpers shared across the per-format mergers.  Kept private to the
// `neverc/lib/Merge/` translation unit so the public API in
// `Merge/Merger.h` stays minimal.
//
//===-----------------------------------------------------------===//

#ifndef NEVERC_LIB_MERGE_MERGER_COMMON_H
#define NEVERC_LIB_MERGE_MERGER_COMMON_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace neverc::merge::detail {

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
