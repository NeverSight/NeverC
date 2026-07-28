#ifndef NEVERC_FOUNDATION_BUILTINSTD_H
#define NEVERC_FOUNDATION_BUILTINSTD_H

#include "llvm/ADT/StringRef.h"
#include <utility>

namespace llvm {
class Triple;
} // namespace llvm

namespace neverc {
namespace BuiltinStd {

/// Returns true if \p Name is a std runtime function (neverc_* prefix,
/// excluding the string runtime's neverc_string_* namespace).
bool isStdRuntimeFunction(llvm::StringRef Name);

// The bitcode is bootstrapped once per supported OS/arch pair rather than
// retargeted from a single host blob.  Aggregate layout, va_list lowering and
// calling conventions are all baked into the IR when it is emitted, so a blob
// built for one target describes the wrong ABI for any other: a struct that
// arm64 returns in a register pair is returned indirectly on Windows x64, and
// the consumer's call site would disagree with the definition it links
// against.  The mimalloc and string runtimes are split the same way.

/// Number of embedded std bitcode modules built for \p TT.  Zero when that
/// target has not been bootstrapped.
unsigned getEmbeddedModuleCount(const llvm::Triple &TT);

/// The \p Idx-th embedded module for \p TT as (name, bitcode data).
/// The name is a sanitized source path like "math_sqrt" or "crypto_sha256_sha256".
std::pair<llvm::StringRef, llvm::StringRef>
getEmbeddedModule(const llvm::Triple &TT, unsigned Idx);

} // namespace BuiltinStd
} // namespace neverc

#endif
