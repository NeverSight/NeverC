#ifndef NEVERC_FOUNDATION_XORSTRNAMES_H
#define NEVERC_FOUNDATION_XORSTRNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace XorStrNames {

inline constexpr llvm::StringLiteral DecryptFunctionName =
    "__neverc_xorstr_decrypt";

} // namespace XorStrNames
} // namespace neverc

#endif
