#ifndef NEVERC_PLUGIN_LINK_ANDROIDKERNELMODULEFINALIZER_H
#define NEVERC_PLUGIN_LINK_ANDROIDKERNELMODULEFINALIZER_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace neverc::plugin {

/// Host-owned policy applied after an ObjectMergeProvider has produced the
/// delivered Android kernel module.  `StripUnneededSymbols` is intentionally
/// narrower than strip-all: relocations, their symbol targets, defined public
/// symbols, and loader-facing sections remain mandatory.
struct AndroidKernelModuleFinalizationPolicy {
  bool DropDebugInfo = false;
  bool StripUnneededSymbols = false;
};

/// Atomically remove final-output tooling/debug metadata and symbols that no
/// retained relocation needs.  A retained relocation targeting anything that
/// would be removed is an error and leaves the graph unchanged.
llvm::Error finalizeAndroidKernelModuleObjectGraph(
    PluginObjectGraph &Object,
    AndroidKernelModuleFinalizationPolicy Policy,
    llvm::StringRef Boundary);

/// Host-owned invariants run after every mutable output phase.  Plugins may
/// preserve these invariants but cannot weaken or reintroduce stripped data.
llvm::Error verifyFinalAndroidKernelModuleObjectGraph(
    const PluginObjectGraph &Object,
    AndroidKernelModuleFinalizationPolicy Policy,
    llvm::StringRef Boundary);
llvm::Error verifyFinalAndroidKernelModuleImage(
    llvm::ArrayRef<uint8_t> Image,
    AndroidKernelModuleFinalizationPolicy Policy,
    llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_LINK_ANDROIDKERNELMODULEFINALIZER_H
