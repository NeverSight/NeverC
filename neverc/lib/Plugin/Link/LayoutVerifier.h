#ifndef NEVERC_PLUGIN_LINK_LAYOUTVERIFIER_H
#define NEVERC_PLUGIN_LINK_LAYOUTVERIFIER_H

#include "LinkGraph.h"

namespace neverc::plugin {

llvm::Error verifyLinkLayout(const PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
