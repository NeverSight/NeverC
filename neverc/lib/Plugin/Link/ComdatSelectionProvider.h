#ifndef NEVERC_PLUGIN_LINK_COMDATSELECTIONPROVIDER_H
#define NEVERC_PLUGIN_LINK_COMDATSELECTIONPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

struct LinkComdatSelectionRecord {
  std::string Name;
  NevercLinkComdatSelection Rule = NEVERC_LINK_COMDAT_ANY;
  uint64_t SelectedComdatID = 0;
  std::vector<uint64_t> CandidateComdatIDs;
};

llvm::Expected<std::vector<LinkComdatSelectionRecord>>
selectLinkComdats(PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
