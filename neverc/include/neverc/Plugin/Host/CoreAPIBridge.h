#ifndef NEVERC_PLUGIN_HOST_COREAPIBRIDGE_H
#define NEVERC_PLUGIN_HOST_COREAPIBRIDGE_H

#include "neverc/Plugin/PluginCore.h"

namespace neverc::plugin {

class PluginProcessServices;

void initializeCoreAPI(NevercCoreAPI &API,
                       PluginProcessServices &ProcessServices);

} // namespace neverc::plugin

#endif
