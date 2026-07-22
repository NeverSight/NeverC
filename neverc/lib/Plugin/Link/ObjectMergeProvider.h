#ifndef NEVERC_PLUGIN_LINK_OBJECTMERGEPROVIDER_H
#define NEVERC_PLUGIN_LINK_OBJECTMERGEPROVIDER_H

#include "PluginLinkRegistry.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace neverc::plugin {

struct ObjectMergeResult {
  std::unique_ptr<PluginObjectGraph> Object;
  /// Raw bytes of the merged relocatable object, when the merger produced a
  /// concrete image (the built-in byte merger always does; typed plugin
  /// providers that only publish a graph leave this empty).  Callers use it to
  /// write the final output losslessly via the native-image passthrough,
  /// avoiding a second graph->assembly->object round-trip.
  llvm::SmallVector<char, 0> MergedImage;
  NevercInterfaceID ProductID{};
  std::array<uint8_t, 32> ProducerRouteDigest{};
  std::string PluginID;
  std::string ProviderID;
};

/// Executes one planned object-merge provider with task-scoped, read-only
/// bridges for every input and one host-owned output transaction.
llvm::Expected<ObjectMergeResult>
executeObjectMergeProvider(
    PluginTaskContext &Task,
    const PluginLinkSnapshot::ObjectMergeProviderRecord &Provider,
    OwnedTargetKey Target, llvm::ArrayRef<PluginObjectGraph *> Objects,
    NevercLinkOptionFlags Flags = NEVERC_LINK_OPTION_NONE);

} // namespace neverc::plugin

#endif
