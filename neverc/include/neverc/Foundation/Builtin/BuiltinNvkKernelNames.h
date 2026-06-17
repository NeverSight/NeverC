#ifndef NEVERC_FOUNDATION_BUILTINNVKKERNELNAMES_H
#define NEVERC_FOUNDATION_BUILTINNVKKERNELNAMES_H

#include "llvm/IR/Module.h"

namespace neverc {
namespace BuiltinNvkKernelNames {

/// Returns true if \p M references any NVK kernel runtime symbol.
/// O(1) check for NEVERC_KRT_RT_VAR globals that every driver module
/// contains (created by including <nvkmod.h> → <linux/kallsyms.h>
/// and <nvk_log.h>).
inline bool hasNvkKernelRuntimeSymbols(const llvm::Module &M) {
  static constexpr const char *Markers[] = {
      "_neverc_krt_sym_resolver",
      "_neverc_krt_sym_cache",
      "_neverc_krt_log_level",
  };
  for (const char *Name : Markers)
    if (M.getGlobalVariable(Name))
      return true;
  return false;
}

} // namespace BuiltinNvkKernelNames
} // namespace neverc

#endif
