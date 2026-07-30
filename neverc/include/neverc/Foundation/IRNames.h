#ifndef NEVERC_FOUNDATION_IRNAMES_H
#define NEVERC_FOUNDATION_IRNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc::IRNames {

/// Marks the function selected by the frontend as the process entry point.
///
/// Backend passes must use this semantic marker instead of guessing from
/// spellings such as WinMain, which can be ordinary user functions on targets
/// where those spellings have no entry-point meaning.
inline constexpr llvm::StringLiteral ProgramEntryAttribute =
    "neverc.program-entry";

} // namespace neverc::IRNames

#endif
