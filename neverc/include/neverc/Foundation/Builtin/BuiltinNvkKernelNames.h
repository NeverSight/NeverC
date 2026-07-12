#ifndef NEVERC_FOUNDATION_BUILTINNVKKERNELNAMES_H
#define NEVERC_FOUNDATION_BUILTINNVKKERNELNAMES_H

#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"

#include "llvm/IR/Module.h"

namespace neverc {
namespace BuiltinNvkKernelNames {

/// Returns true if \p M references any NVK kernel runtime symbol.
inline bool hasNvkKernelRuntimeSymbols(const llvm::Module &M) {
  for (const llvm::GlobalValue &GV : M.global_values())
    if (GV.isDeclaration() && !GV.use_empty() &&
        BuiltinNvkKernel::hasEmbeddedSymbol(GV.getName()))
      return true;

  return false;
}

} // namespace BuiltinNvkKernelNames
} // namespace neverc

#endif
