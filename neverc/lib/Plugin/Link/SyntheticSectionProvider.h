#ifndef NEVERC_PLUGIN_LINK_SYNTHETICSECTIONPROVIDER_H
#define NEVERC_PLUGIN_LINK_SYNTHETICSECTIONPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

struct LinkSyntheticRecord {
  uint64_t SyntheticID = 0;
  uint64_t SectionID = 0;
  uint64_t AtomID = 0;
  std::string Role;
};

llvm::Expected<std::vector<LinkSyntheticRecord>>
materializeLinkSynthetics(PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
