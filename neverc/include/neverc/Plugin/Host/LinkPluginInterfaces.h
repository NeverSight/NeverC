#ifndef NEVERC_PLUGIN_HOST_LINKPLUGININTERFACES_H
#define NEVERC_PLUGIN_HOST_LINKPLUGININTERFACES_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc::plugin {

class PluginLinkSnapshot;
class PluginProcessServices;

NevercInterfaceID linkInterfaceID();
NevercInterfaceID linkRegistrarInterfaceID();
NevercInterfaceID ltoInterfaceID();
NevercInterfaceID ltoRegistrarInterfaceID();

llvm::ArrayRef<NevercInterfaceID> builtInLinkPhaseIDs();

llvm::Error registerPluginLinkInterfaces(PluginProcessServices &Services);
std::shared_ptr<const PluginLinkSnapshot>
findPluginLinkSnapshot(PluginProcessServices &Services,
                       NevercSessionHandle Session);

} // namespace neverc::plugin

#endif
