#ifndef NEVERC_FOUNDATION_BUILTINNVKKERNEL_H
#define NEVERC_FOUNDATION_BUILTINNVKKERNEL_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace BuiltinNvkKernel {

/// Return the pre-merged NVK kernel runtime bitcode (empty before bootstrap).
llvm::StringRef getEmbeddedBitcode();

/// Return true when \p Name is defined by the embedded runtime module.
bool hasEmbeddedSymbol(llvm::StringRef Name);

} // namespace BuiltinNvkKernel
} // namespace neverc

#endif
