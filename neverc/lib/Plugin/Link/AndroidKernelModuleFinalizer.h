#ifndef NEVERC_PLUGIN_LINK_ANDROIDKERNELMODULEFINALIZER_H
#define NEVERC_PLUGIN_LINK_ANDROIDKERNELMODULEFINALIZER_H

#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace neverc::plugin {

/// Host provenance for the symbol names entering final graph processing.
/// Plugins cannot select CanonicalRelease: only the built-in merger may assert
/// it after its byte-level release transform and independent verifier succeed.
enum class AndroidKernelSymbolNameState : uint8_t {
  Original,
  CanonicalRelease,
};

/// Host-owned policy applied after an ObjectMergeProvider has produced the
/// delivered Android kernel module.  `StripUnneededSymbols` is intentionally
/// narrower than strip-all: relocations, their symbol targets, defined public
/// symbols, and loader-facing sections remain mandatory. Surviving ordinary
/// defined/absolute names use deterministic IDA-style structural spellings
/// such as `fn_<EA>`, `code_<EA>`, and `obj_<EA>`; loader ABI names, imports,
/// and section
/// symbols remain exact. `EA` is the canonical final relocatable-section
/// coordinate, not a runtime load address and not encryption.
struct AndroidKernelModuleFinalizationPolicy {
  bool DropDebugInfo = false;
  bool StripUnneededSymbols = false;
  AndroidKernelSymbolNameState SymbolNameState =
      AndroidKernelSymbolNameState::Original;
};

/// Atomically remove final-output tooling/debug metadata, prune symbols that no
/// retained relocation needs, and structurally rename loader-safe definitions.
/// A retained relocation targeting anything selected for removal, an
/// unsupported symbol class, a name-plan collision, or output-name ownership
/// that the ELF writer cannot preserve leaves the graph unchanged. Same-name
/// undefined entries may share one writer symbol only when every observable
/// attribute is equivalent.
llvm::Error finalizeAndroidKernelModuleObjectGraph(
    PluginObjectGraph &Object,
    AndroidKernelModuleFinalizationPolicy Policy,
    llvm::StringRef Boundary,
    AndroidKernelReleaseSymbolMap *ReleaseSymbolMap = nullptr);

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

/// Binds a pre-serialization rename map to the authoritative final ELF image.
/// Writer lowering may replace symbol-target relocations with section targets,
/// making formerly required local symbols disappear. Such entries are removed;
/// every remaining release name must identify exactly one final symbol.
llvm::Error bindAndroidKernelReleaseSymbolMapToImage(
    AndroidKernelReleaseSymbolMap &Map, llvm::ArrayRef<uint8_t> Image,
    llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_LINK_ANDROIDKERNELMODULEFINALIZER_H
