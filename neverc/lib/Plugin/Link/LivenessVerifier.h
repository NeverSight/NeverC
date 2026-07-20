#ifndef NEVERC_PLUGIN_LINK_LIVENESSVERIFIER_H
#define NEVERC_PLUGIN_LINK_LIVENESSVERIFIER_H

#include "LinkGraph.h"

namespace neverc::plugin {

llvm::Error verifyLinkLiveness(const PluginLinkGraph &Graph);
llvm::Error verifyLinkFolding(const PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
