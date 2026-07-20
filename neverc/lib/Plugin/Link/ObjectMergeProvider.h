#ifndef NEVERC_PLUGIN_LINK_OBJECTMERGEPROVIDER_H
#define NEVERC_PLUGIN_LINK_OBJECTMERGEPROVIDER_H

#include "PluginLinkRegistry.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <array>
#include <memory>
#include <string>

namespace neverc::plugin {

struct ObjectMergeResult {
  std::unique_ptr<PluginObjectGraph> Object;
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
