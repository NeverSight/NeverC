#ifndef NEVERC_LIB_PLUGIN_IO_DEPENDENCYBRIDGE_H
#define NEVERC_LIB_PLUGIN_IO_DEPENDENCYBRIDGE_H

#include "neverc/Plugin/PluginSource.h"

namespace neverc::plugin {

class PluginIOProcessBridge;

void initializePluginDependencyAPI(NevercIOAPI &API,
                                   PluginIOProcessBridge &Bridge);

} // namespace neverc::plugin

#endif
