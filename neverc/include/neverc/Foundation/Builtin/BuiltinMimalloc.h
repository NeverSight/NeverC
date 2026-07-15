#ifndef NEVERC_FOUNDATION_BUILTINMIMALLOC_H
#define NEVERC_FOUNDATION_BUILTINMIMALLOC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

namespace neverc {
namespace BuiltinMimalloc {

/// Whether mimalloc bitcode embedding is supported for the given triple.
/// Selection is per-OS and per-arch: mimalloc IR embeds arch-specific inline
/// asm (e.g. x86 `{di}` constraints), so a single OS blob cannot be retargeted
/// by stripping host attributes alone.
bool isSupported(const llvm::Triple &TT);

/// Return the embedded mimalloc bitcode blob for \p TT.
/// Returns an empty StringRef when unsupported or the placeholder is active.
llvm::StringRef getEmbeddedBitcode(const llvm::Triple &TT);

} // namespace BuiltinMimalloc
} // namespace neverc

#endif
