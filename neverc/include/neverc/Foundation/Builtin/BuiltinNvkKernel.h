#ifndef NEVERC_FOUNDATION_BUILTINNVKKERNEL_H
#define NEVERC_FOUNDATION_BUILTINNVKKERNEL_H

#include "llvm/ADT/StringRef.h"
#include <utility>

namespace neverc {
namespace BuiltinNvkKernel {

/// Number of embedded NVK kernel runtime bitcode modules.
unsigned getEmbeddedModuleCount();

/// Return the \p Idx-th embedded module as (name, bitcode data).
std::pair<llvm::StringRef, llvm::StringRef> getEmbeddedModule(unsigned Idx);

} // namespace BuiltinNvkKernel
} // namespace neverc

#endif
