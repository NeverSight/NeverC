set(LLVM_LINK_COMPONENTS Support)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(NEVERC_PLUGIN_PREP_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-prep-schema.py")
set(NEVERC_PLUGIN_PREP_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PrepSchema.json")
set(NEVERC_PLUGIN_PREP_SCHEMA_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginPrepSchema.inc.in")
set(NEVERC_PLUGIN_PREP_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginPrepSchema.inc")
set(NEVERC_TOKEN_KIND_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Foundation/Core/TokenKinds.def")
set(NEVERC_ATTRIBUTE_TOKEN_KIND_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Foundation/AttrTokenKinds.td.h")
set(NEVERC_PLUGIN_AST_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-ast-schema.py")
set(NEVERC_PLUGIN_AST_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/ASTSchema.json")
set(NEVERC_PLUGIN_AST_SCHEMA_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginASTSchema.inc.in")
set(NEVERC_PLUGIN_AST_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginASTSchema.inc")
set(NEVERC_DECL_NODE_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Tree/DeclNodes.td.h")
set(NEVERC_STMT_NODE_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Tree/StmtNodes.td.h")
set(NEVERC_TYPE_NODE_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Tree/TypeNodes.td.h")
set(NEVERC_ATTRIBUTE_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Foundation/AttrList.td.h")
set(NEVERC_ATTRIBUTE_CLASS_DEFINITIONS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Tree/Attrs.td.h")

add_custom_target(nevercPluginPrepSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_PREP_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_PREP_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_PREP_SCHEMA}
    ${NEVERC_PLUGIN_PREP_SCHEMA_TEMPLATE}
    ${NEVERC_PLUGIN_PREP_SCHEMA_OUTPUT}
    ${NEVERC_TOKEN_KIND_DEFINITIONS}
    ${NEVERC_ATTRIBUTE_TOKEN_KIND_DEFINITIONS}
  COMMENT "Checking NeverC plugin preprocessor schema"
  VERBATIM
  )

add_custom_target(nevercPluginASTSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_AST_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_AST_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_AST_SCHEMA}
    ${NEVERC_PLUGIN_AST_SCHEMA_TEMPLATE}
    ${NEVERC_PLUGIN_AST_SCHEMA_OUTPUT}
    ${NEVERC_DECL_NODE_DEFINITIONS}
    ${NEVERC_STMT_NODE_DEFINITIONS}
    ${NEVERC_TYPE_NODE_DEFINITIONS}
    ${NEVERC_ATTRIBUTE_DEFINITIONS}
    ${NEVERC_ATTRIBUTE_CLASS_DEFINITIONS}
  COMMENT "Checking NeverC plugin AST schema"
  VERBATIM
  )

add_neverc_library(nevercPluginCore
  Core/CoreAPIBridge.cpp
  Core/PluginArtifactRegistry.cpp
  Core/PluginDiagnostics.cpp
  Core/PluginHandleArena.cpp
  Core/PluginInterfaceRegistry.cpp
  Core/PluginLLVMOptionSnapshot.cpp
  Core/PluginOptionRegistry.cpp
  Core/PluginPhaseExecutor.cpp
  Core/PluginPhaseGraph.cpp
  Core/PluginProcessServices.cpp
  Core/PluginRegistration.cpp
  Core/PluginRegistry.cpp
  Core/PluginSession.cpp
  Core/PluginTaskContext.cpp

  LINK_LIBS
  nevercFoundation
  )

if(MSVC)
  set_source_files_properties(
    Core/PluginRegistration.cpp
    Core/PluginRegistry.cpp
    Core/PluginArtifactRegistry.cpp
    Core/PluginHandleArena.cpp
    Core/PluginPhaseExecutor.cpp
    Core/PluginSession.cpp
    Core/PluginTaskContext.cpp
    PROPERTIES COMPILE_OPTIONS "/EHsc"
  )
else()
  set_source_files_properties(
    Core/PluginRegistration.cpp
    Core/PluginRegistry.cpp
    Core/PluginArtifactRegistry.cpp
    Core/PluginHandleArena.cpp
    Core/PluginPhaseExecutor.cpp
    Core/PluginSession.cpp
    Core/PluginTaskContext.cpp
    PROPERTIES COMPILE_OPTIONS "-fexceptions;-funwind-tables"
  )
endif()

set(LLVM_LINK_COMPONENTS Support)

add_neverc_library(nevercPluginIO
  IO/DependencyBridge.cpp
  IO/OutputSink.cpp
  IO/PluginFileSystem.cpp
  IO/VFSBridge.cpp

  LINK_LIBS
  nevercPluginCore
  nevercFoundation
  )

target_include_directories(nevercPluginIO
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )

set(LLVM_LINK_COMPONENTS Support)

add_neverc_library(nevercPluginFrontend
  Frontend/ASTBridge.cpp
  Frontend/ASTUnitArtifact.cpp
  Frontend/FrontendPluginInterfaces.cpp
  Frontend/MacroBridge.cpp
  Frontend/ParserPluginHooks.cpp
  Frontend/PrepBridge.cpp
  Frontend/PrepHookBridge.cpp
  Frontend/PrepPluginHooks.cpp
  Frontend/PluginPrepObserver.cpp
  Frontend/PluginTokenLexer.cpp
  Frontend/SemaBridge.cpp
  Frontend/SemaConstEvalBridge.cpp
  Frontend/SemaLookupBridge.cpp
  Frontend/SemaPluginHooks.cpp
  Frontend/SemaTypeBridge.cpp
  Frontend/SourceLocationBridge.cpp
  Frontend/SourcePhases.cpp
  Frontend/TokenBridge.cpp
  Frontend/TokenStream.cpp

  LINK_LIBS
  nevercPluginIO
  nevercPluginCore
  nevercFoundation
  nevercScan
  nevercTree
  nevercAnalyze
  nevercSyntax
  )
add_dependencies(nevercPluginFrontend
  nevercPluginASTSchema
  nevercPluginPrepSchema
  )

set(LLVM_LINK_COMPONENTS
  Analysis
  CodeGen
  Core
  Support
  TransformUtils
  )

add_neverc_library(nevercPlugin
  Core/PluginLoader.cpp
  Core/PluginPassAdaptor.cpp

  Host/HostAPIBridge.cpp

  Bridge/BridgeAnalysis.cpp
  Bridge/BridgeDataStructures.cpp
  Bridge/BridgeIR.cpp
  Bridge/BridgeIRBuilder.cpp
  Bridge/BridgeLinker.cpp
  Bridge/BridgeMIR.cpp
  Bridge/BridgeString.cpp

  LINK_LIBS
  nevercFoundation
  nevercPluginCore

  DEPENDS
  NeverCDriverOptions
  )

target_include_directories(nevercPlugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
