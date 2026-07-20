#ifndef NEVERC_PLUGIN_LINK_LINKRELAXATIONPROVIDER_H
#define NEVERC_PLUGIN_LINK_LINKRELAXATIONPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

struct LinkRelaxationRecord {
  uint64_t EdgeID = 0;
  uint32_t OriginalWidth = 0;
  uint32_t RelaxedWidth = 0;
  int64_t SizeDelta = 0;
  std::string Reason;
};

llvm::Error assignProvisionalLinkAddresses(PluginLinkGraph &Graph);
llvm::Expected<std::vector<LinkRelaxationRecord>>
relaxLinkEdges(PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
