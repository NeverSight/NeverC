#ifndef NEVERC_LIB_PLUGIN_FRONTEND_FRONTENDPLUGININTERFACES_H
#define NEVERC_LIB_PLUGIN_FRONTEND_FRONTENDPLUGININTERFACES_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc {
class PrepEngine;
class PrepPluginHooks;
class ParserPluginHooks;
class SemaPluginHooks;
} // namespace neverc

namespace neverc::plugin {

class PluginArtifactRegistry;
class PluginASTBridge;
class FrontendPluginBridge;
class PluginProcessServices;
class PluginPhaseExecutor;
class PluginPrepBridge;
class PluginSemaBridge;
class PluginTaskContext;

NevercInterfaceID sourceLocationPluginInterfaceID();
NevercInterfaceID prepPluginInterfaceID();
NevercInterfaceID astPluginInterfaceID();
NevercInterfaceID parserPluginInterfaceID();
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
NevercInterfaceID parserExtensionArtifactID();
NevercInterfaceID semaExtensionArtifactID();

llvm::Error registerPluginASTInterface(PluginProcessServices &Services);
llvm::Error registerPluginParserInterface(PluginProcessServices &Services);
llvm::Error registerPrepTokenArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error
registerPrepTokenStreamArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error registerPrepHookArtifactTypes(PluginArtifactRegistry &Artifacts);
llvm::Error
registerParserExtensionArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error
registerSemaExtensionArtifactType(PluginArtifactRegistry &Artifacts);
llvm::Error registerParserBuiltinProviders(PluginTaskContext &Task,
                                           PluginPhaseExecutor &Executor);
llvm::Error registerSemaBuiltinProviders(PluginTaskContext &Task,
                                         PluginPhaseExecutor &Executor);
bool hasParserExtensionBindings(const PluginPhaseExecutor &Executor);
bool hasSemaExtensionBindings(const PluginPhaseExecutor &Executor);
llvm::Expected<std::unique_ptr<PrepPluginHooks>>
createPrepPluginHooks(PluginTaskContext &Task, PrepEngine &Prep,
                      PluginArtifactRegistry &Artifacts,
                      PluginPhaseExecutor &Executor, PluginPrepBridge &Bridge);
llvm::Expected<std::unique_ptr<ParserPluginHooks>> createParserPluginHooks(
    PluginTaskContext &Task, PluginArtifactRegistry &Artifacts,
    PluginPhaseExecutor &Executor, PluginPrepBridge &PrepBridge,
    PluginASTBridge &ASTBridge, FrontendPluginBridge &Locations);
llvm::Expected<std::unique_ptr<SemaPluginHooks>> createSemaPluginHooks(
    PluginTaskContext &Task, PluginArtifactRegistry &Artifacts,
    PluginPhaseExecutor &Executor, PluginASTBridge &ASTBridge,
    FrontendPluginBridge &Locations, PluginSemaBridge &SemaBridge);

} // namespace neverc::plugin

#endif
