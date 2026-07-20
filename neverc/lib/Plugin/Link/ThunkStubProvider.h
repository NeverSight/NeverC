#ifndef NEVERC_PLUGIN_LINK_THUNKSTUBPROVIDER_H
#define NEVERC_PLUGIN_LINK_THUNKSTUBPROVIDER_H

#include "LinkGraph.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

struct LinkThunkRecord {
  uint64_t SourceEdgeID = 0;
  uint64_t TargetAtomID = 0;
  uint64_t ThunkAtomID = 0;
  uint64_t ThunkSyntheticID = 0;
  std::string Reason;
};

llvm::Expected<std::vector<LinkThunkRecord>>
insertRequiredLinkThunks(PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
