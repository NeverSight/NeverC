#ifndef NEVERC_LIB_PLUGIN_FRONTEND_FRONTENDPLUGININTERFACES_H
#define NEVERC_LIB_PLUGIN_FRONTEND_FRONTENDPLUGININTERFACES_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc {
class PrepEngine;
class PrepPluginHooks;
} // namespace neverc

namespace neverc::plugin {

class PluginArtifactRegistry;
class PluginProcessServices;
class PluginPhaseExecutor;
class PluginPrepBridge;
class PluginTaskContext;

NevercInterfaceID sourceLocationPluginInterfaceID();
NevercInterfaceID prepPluginInterfaceID();
NevercInterfaceID astPluginInterfaceID();
NevercInterfaceID semaPluginInterfaceID();
NevercInterfaceID prepTokenPhaseID();
NevercInterfaceID prepTokenArtifactID();
NevercInterfaceID prepBuildTokenStreamPhaseID();
NevercInterfaceID prepTokenStreamArtifactID();
NevercInterfaceID prepIncludePhaseID();
NevercInterfaceID prepIncludeArtifactID();
NevercInterfaceID prepMacroPhaseID();
NevercInterfaceID prepMacroArtifactID();
NevercInterfaceID prepPragmaPhaseID();
NevercInterfaceID prepPragmaArtifactID();
NevercInterfaceID prepFeatureQueryPhaseID();
NevercInterfaceID prepFeatureQueryArtifactID();

llvm::Error registerPluginASTInterface(PluginProcessServices &Services);
llvm::Error registerPrepTokenArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error
registerPrepTokenStreamArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error registerPrepHookArtifactTypes(PluginArtifactRegistry &Artifacts);
llvm::Expected<std::unique_ptr<PrepPluginHooks>>
createPrepPluginHooks(PluginTaskContext &Task, PrepEngine &Prep,
                      PluginArtifactRegistry &Artifacts,
                      PluginPhaseExecutor &Executor, PluginPrepBridge &Bridge);

} // namespace neverc::plugin

#endif
