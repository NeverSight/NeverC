#ifndef NEVERC_FOUNDATION_BUILTINMIMALLOC_H
#define NEVERC_FOUNDATION_BUILTINMIMALLOC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

namespace neverc {
namespace BuiltinMimalloc {

/// Whether mimalloc bitcode embedding is supported for the given OS.
bool isSupported(llvm::Triple::OSType OS);

/// Return the embedded mimalloc bitcode blob for \p OS.
/// Returns an empty StringRef when unsupported or the placeholder is active.
llvm::StringRef getEmbeddedBitcode(llvm::Triple::OSType OS);

} // namespace BuiltinMimalloc
} // namespace neverc

#endif
