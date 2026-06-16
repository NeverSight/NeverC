#ifndef NEVERC_FOUNDATION_BUILTINNVKKERNEL_H
#define NEVERC_FOUNDATION_BUILTINNVKKERNEL_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace BuiltinNvkKernel {

/// Return the pre-merged NVK kernel runtime bitcode (empty before bootstrap).
llvm::StringRef getEmbeddedBitcode();

} // namespace BuiltinNvkKernel
} // namespace neverc

#endif
