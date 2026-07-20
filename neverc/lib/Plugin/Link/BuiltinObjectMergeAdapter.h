#ifndef NEVERC_PLUGIN_LINK_BUILTINOBJECTMERGEADAPTER_H
#define NEVERC_PLUGIN_LINK_BUILTINOBJECTMERGEADAPTER_H

#include "ObjectMergeProvider.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

/// Adapts the existing verified byte-oriented relocatable-object merger to the
/// typed ObjectGraph provider contract:
///
///   ObjectGraph[] -> built-in Writer -> byte merger -> built-in Reader
///
/// The intermediate images are task-owned memory outputs. The returned graph
/// is independently parsed and verified before it can be published.
llvm::Expected<ObjectMergeResult>
executeBuiltinObjectMergeAdapter(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Snapshot,
    OwnedTargetKey Target, llvm::ArrayRef<PluginObjectGraph *> Objects,
    NevercLinkOptionFlags Flags = NEVERC_LINK_OPTION_NONE);

} // namespace neverc::plugin

#endif
