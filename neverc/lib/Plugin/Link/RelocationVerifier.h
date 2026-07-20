#ifndef NEVERC_PLUGIN_LINK_RELOCATIONVERIFIER_H
#define NEVERC_PLUGIN_LINK_RELOCATIONVERIFIER_H

#include "LinkGraph.h"

namespace neverc::plugin {

llvm::Error verifyLinkRelocations(const PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
