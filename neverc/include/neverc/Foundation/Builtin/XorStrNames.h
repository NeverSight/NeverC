#ifndef NEVERC_FOUNDATION_XORSTRNAMES_H
#define NEVERC_FOUNDATION_XORSTRNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace XorStrNames {

inline constexpr llvm::StringLiteral DecryptFunctionName =
    "__neverc_xorstr_decrypt";
inline constexpr llvm::StringLiteral DecryptABIAnchorName =
    "__neverc_xorstr_decrypt_abi_anchor";
inline constexpr llvm::StringLiteral RouteStateName =
    "__neverc_xorstr_route_state";

} // namespace XorStrNames
} // namespace neverc

#endif
