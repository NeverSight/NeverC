#ifndef NEVERC_FOUNDATION_ELFDEBUGSECTIONPOLICY_H
#define NEVERC_FOUNDATION_ELFDEBUGSECTIONPOLICY_H

#include "llvm/ADT/StringRef.h"

namespace neverc::ELFDebugSectionPolicy {

/// Matches the ELF debug-section spellings recognized by LLVM's object reader.
inline bool isDebugSectionName(llvm::StringRef Name) {
  return Name.starts_with(".debug") || Name.starts_with(".zdebug") ||
         Name == ".gdb_index";
}

} // namespace neverc::ELFDebugSectionPolicy

#endif // NEVERC_FOUNDATION_ELFDEBUGSECTIONPOLICY_H
