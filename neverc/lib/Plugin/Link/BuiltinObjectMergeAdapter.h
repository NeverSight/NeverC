#ifndef NEVERC_PLUGIN_LINK_BUILTINOBJECTMERGEADAPTER_H
#define NEVERC_PLUGIN_LINK_BUILTINOBJECTMERGEADAPTER_H

#include "ObjectMergeProvider.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>

namespace neverc::plugin {

/// Driver-derived knobs that make the built-in merge byte-identical to the
/// native relocatable link.  Ordinary `-r` keeps all strip knobs disabled; a
/// delivered Android `.ko` may request the narrower module-safe policy.
struct BuiltinObjectMergeConfig {
  /// Fold per-symbol ELF sections and preserve `.ko` metadata sections, matching
  /// the native ELF driver's `-fandroid-kernel-driver-mode` `-r` behavior.
  bool AndroidKernelModule = false;

  /// Drop the intermediate profile contract for a delivered `.ko`.  Partial
  /// Android-kernel links keep it for the next checked link.
  bool FinalizeAndroidKernelModule = false;

  /// Remove DWARF only while finalizing the delivered `.ko`.
  bool DropDebugInfo = false;

  /// Remove relocation-unneeded local/undefined symbols and `.comment` while
  /// preserving the ET_REL symbol table, imports, and relocations.
  bool StripUnneededSymbols = false;
};

/// Adapts the existing verified byte-oriented relocatable-object merger to the
/// typed ObjectGraph provider contract:
///
///   ObjectGraph[] -> Writer/native passthrough -> byte merger -> built-in Reader
///
/// \p InputImages, when non-empty, supplies the original on-disk bytes for each
/// object in \p Objects (parallel array).  Unmodified inputs are then serialized
/// straight back to those bytes instead of through the lossy
/// graph->assembly->object Writer, so the merge input is byte-identical to the
/// native link.  A missing/empty entry falls back to the Writer.
///
/// The intermediate images are task-owned memory outputs. The returned graph is
/// independently parsed and verified before it can be published, and its raw
/// bytes are returned in ObjectMergeResult::MergedImage for lossless output.
llvm::Expected<ObjectMergeResult>
executeBuiltinObjectMergeAdapter(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Snapshot,
    OwnedTargetKey Target, llvm::ArrayRef<PluginObjectGraph *> Objects,
    llvm::ArrayRef<llvm::ArrayRef<uint8_t>> InputImages = {},
    NevercLinkOptionFlags Flags = NEVERC_LINK_OPTION_NONE,
    BuiltinObjectMergeConfig Config = {});

} // namespace neverc::plugin

#endif
