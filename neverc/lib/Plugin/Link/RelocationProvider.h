#ifndef NEVERC_PLUGIN_LINK_RELOCATIONPROVIDER_H
#define NEVERC_PLUGIN_LINK_RELOCATIONPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace neverc::plugin {

struct LinkRelocationValue {
  uint64_t Place = 0;
  uint64_t Target = 0;
  uint64_t EncodedValue = 0;
  bool Dynamic = false;
};

struct LinkRelocationRecord {
  uint64_t EdgeID = 0;
  uint64_t Place = 0;
  uint64_t Target = 0;
  uint64_t EncodedValue = 0;
  uint32_t Width = 0;
  bool Applied = false;
  bool Dynamic = false;
};

llvm::Expected<LinkRelocationValue>
evaluateLinkRelocation(const PluginLinkGraph &Graph,
                       const PluginLinkEdge &Edge);
llvm::Expected<std::vector<LinkRelocationRecord>>
applyLinkRelocations(PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
