#ifndef NEVERC_PLUGIN_LINK_RESOLUTIONVERIFIER_H
#define NEVERC_PLUGIN_LINK_RESOLUTIONVERIFIER_H

#include "SymbolResolutionProvider.h"

namespace neverc::plugin {

llvm::Error verifyLinkSymbolResolution(
    const PluginLinkGraph &Graph,
    const SymbolResolutionOptions &Options = {});
llvm::Error verifyLinkComdatSelection(const PluginLinkGraph &Graph);

} // namespace neverc::plugin

#endif
