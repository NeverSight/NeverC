#ifndef NEVERC_PLUGIN_LINK_SYNTHESISVERIFIER_H
#define NEVERC_PLUGIN_LINK_SYNTHESISVERIFIER_H

#include "LinkGraph.h"

namespace neverc::plugin {

llvm::Error verifyLinkSynthetics(const PluginLinkGraph &Graph);
llvm::Error verifyLinkRelaxation(const PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
