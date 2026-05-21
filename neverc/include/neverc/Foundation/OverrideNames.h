#ifndef NEVERC_FOUNDATION_OVERRIDENAMES_H
#define NEVERC_FOUNDATION_OVERRIDENAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace OverrideNames {

inline constexpr llvm::StringLiteral SymbolPrefix = "__neverc_ovr.";

inline constexpr llvm::StringLiteral ELFSectionName = ".neverc.overrides";
inline constexpr llvm::StringLiteral ELFMarkerSectionName =
    ".neverc.overrides.marker";
inline constexpr llvm::StringLiteral MachOSectionSpec = "__DATA,__neverc_ovr";
inline constexpr llvm::StringLiteral MachOSectionName = "__neverc_ovr";

} // namespace OverrideNames
} // namespace neverc

#endif
