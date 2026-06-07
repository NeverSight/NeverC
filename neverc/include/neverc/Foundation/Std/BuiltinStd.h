#ifndef NEVERC_FOUNDATION_BUILTINSTD_H
#define NEVERC_FOUNDATION_BUILTINSTD_H

#include "llvm/ADT/StringRef.h"
#include <utility>

namespace neverc {
namespace BuiltinStd {

/// Returns true if \p Name is a std runtime function (neverc_* prefix,
/// excluding the string runtime's neverc_string_* namespace).
bool isStdRuntimeFunction(llvm::StringRef Name);

/// Number of embedded std bitcode modules (0 before bootstrap).
unsigned getEmbeddedModuleCount();

/// Return the \p Idx-th embedded module as (name, bitcode data).
/// The name is a sanitized source path like "math_sqrt" or "crypto_sha256_sha256".
std::pair<llvm::StringRef, llvm::StringRef> getEmbeddedModule(unsigned Idx);

} // namespace BuiltinStd
} // namespace neverc

#endif
