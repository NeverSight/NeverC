#ifndef NEVERC_FOUNDATION_XORSTRNAMES_H
#define NEVERC_FOUNDATION_XORSTRNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace XorStrNames {

inline constexpr llvm::StringLiteral SupportFunctionPrefix = "__neverc_xorstr_";
inline constexpr llvm::StringLiteral DecryptFunctionName =
    "__neverc_xorstr_decrypt";
inline constexpr llvm::StringLiteral DecryptABIAnchorName =
    "__neverc_xorstr_decrypt_abi_anchor";
inline constexpr llvm::StringLiteral RouteStateName =
    "__neverc_xorstr_route_state";

inline bool hasScopedNameMarker(llvm::StringRef Name, llvm::StringRef Marker) {
  const size_t Position = Name.find(Marker);
  if (Position == llvm::StringRef::npos)
    return false;
  const size_t End = Position + Marker.size();
  const bool HasNameBoundaryBefore = Position == 0 || Name[Position - 1] == '.';
  const bool HasNameBoundaryAfter = End == Name.size() || Name[End] == '.';
  return HasNameBoundaryBefore && HasNameBoundaryAfter;
}

inline bool isDecryptFunctionName(llvm::StringRef Name) {
  return hasScopedNameMarker(Name, DecryptFunctionName);
}

inline bool isSupportFunctionName(llvm::StringRef Name) {
  const size_t Position = Name.find(SupportFunctionPrefix);
  return Position != llvm::StringRef::npos &&
         (Position == 0 || Name[Position - 1] == '.');
}

} // namespace XorStrNames
} // namespace neverc

#endif
