#ifndef NEVERC_FOUNDATION_NATIVETARGETABI_H
#define NEVERC_FOUNDATION_NATIVETARGETABI_H

#include "llvm/TargetParser/Triple.h"

namespace neverc {

// Some triples name a 64-bit ISA with a 32-bit pointer ABI. Both the
// compiler and the independently built Clang translator consume this check.
static inline bool hasNative64BitABI(const llvm::Triple &T) {
  return (T.getArch() == llvm::Triple::x86_64 ||
          T.getArch() == llvm::Triple::aarch64) &&
         !T.isX32() && !T.getEnvironmentName().contains_insensitive("ilp32");
}

} // namespace neverc

#endif
