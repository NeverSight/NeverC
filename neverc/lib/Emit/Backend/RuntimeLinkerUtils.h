#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIMELINKERUTILS_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIMELINKERUTILS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace neverc {

/// Strip host-specific target-cpu / target-features / tune-cpu attributes
/// from every definition in \p Mod.
///
/// Precompiled bitcode bakes the build host's CPU and feature set into
/// per-function attributes.  When cross-compiling to a different arch,
/// Linker::linkModules preserves these even after we reset the module
/// triple.  The mismatched backend then rejects unknown features.
/// Stripping lets merged functions inherit the user module's defaults.
inline void stripHostTargetAttributes(llvm::Module &Mod) {
  for (llvm::Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    F.removeFnAttr("target-cpu");
    F.removeFnAttr("target-features");
    F.removeFnAttr("tune-cpu");
  }
}

} // namespace neverc

#endif
