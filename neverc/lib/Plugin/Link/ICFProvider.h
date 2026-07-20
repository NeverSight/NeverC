#ifndef NEVERC_PLUGIN_LINK_ICFPROVIDER_H
#define NEVERC_PLUGIN_LINK_ICFPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

enum class LinkICFMode : uint8_t {
  None,
  Safe,
  All,
};

struct LinkICFOptions {
  LinkICFMode Mode = LinkICFMode::Safe;
};

struct LinkFoldRecord {
  uint64_t AtomID = 0;
  uint64_t LeaderID = 0;
  bool Eligible = false;
  std::string Reason;
};

llvm::Expected<std::vector<LinkFoldRecord>>
foldIdenticalLinkAtoms(PluginLinkGraph &Graph,
                       const LinkICFOptions &Options = {});

} // namespace neverc::plugin

#endif
