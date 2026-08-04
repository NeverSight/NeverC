#ifndef NEVERC_RUNTIME_RUNTIMEMANAGER_H
#define NEVERC_RUNTIME_RUNTIMEMANAGER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace runtime {

struct RuntimeTarget {
  llvm::StringLiteral Name;
  llvm::StringLiteral CheckDir;
  /// Additional managed directory supplied by this target's archive.
  llvm::StringLiteral SharedDir;
};

/// Canonical catalog of cross-compilation runtime release assets.
llvm::ArrayRef<RuntimeTarget> getRuntimeTargets();

const RuntimeTarget *findRuntimeTarget(llvm::StringRef Name);

bool isRuntimeTargetInstalled(llvm::StringRef RuntimeDirectory,
                              const RuntimeTarget &Target);

int runRuntime(int Argc, const char **Argv, const char *Argv0);

} // namespace runtime
} // namespace neverc

#endif // NEVERC_RUNTIME_RUNTIMEMANAGER_H
