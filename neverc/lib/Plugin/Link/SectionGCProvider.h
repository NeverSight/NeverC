#ifndef NEVERC_PLUGIN_LINK_SECTIONGCPROVIDER_H
#define NEVERC_PLUGIN_LINK_SECTIONGCPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

struct LinkLivenessRecord {
  uint64_t AtomID = 0;
  bool Live = false;
  std::vector<std::string> KeepReasons;
};

llvm::Expected<std::vector<LinkLivenessRecord>>
markLiveLinkAtoms(PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
