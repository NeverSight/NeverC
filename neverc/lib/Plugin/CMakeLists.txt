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
set(NEVERC_PLUGIN_IR_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-ir-schema.py")
set(NEVERC_PLUGIN_IR_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/IRSchema.json")
set(NEVERC_PLUGIN_IR_SCHEMA_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginIRSchema.inc.in")
set(NEVERC_PLUGIN_IR_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginIRSchema.inc")
set(NEVERC_PLUGIN_MIR_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-mir-schema.py")
set(NEVERC_PLUGIN_MIR_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/MIRSchema.json")
set(NEVERC_PLUGIN_MIR_SCHEMA_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginMIRSchema.inc.in")
set(NEVERC_PLUGIN_MIR_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginMIRSchema.inc")

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

add_custom_target(nevercPluginIRSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_IR_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_IR_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_IR_SCHEMA}
    ${NEVERC_PLUGIN_IR_SCHEMA_TEMPLATE}
    ${NEVERC_PLUGIN_IR_SCHEMA_OUTPUT}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/IR/Instruction.def"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/IR/Value.def"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/IR/Type.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/IR/InstrTypes.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/IR/GlobalValue.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/IR/CallingConv.h"
  COMMENT "Checking NeverC plugin IR schema"
  VERBATIM
  )

add_custom_target(nevercPluginMIRSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_MIR_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_MIR_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_MIR_SCHEMA}
    ${NEVERC_PLUGIN_MIR_SCHEMA_TEMPLATE}
    ${NEVERC_PLUGIN_MIR_SCHEMA_OUTPUT}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/CodeGen/MachineOperand.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/CodeGen/MachineFunction.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/Support/TargetOpcodes.def"
  COMMENT "Checking NeverC plugin MIR schema"
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
  Frontend/PluginTreeConsumer.cpp
  Frontend/PluginTokenLexer.cpp
  Frontend/SemaBridge.cpp
  Frontend/SemaConstEvalBridge.cpp
  Frontend/SemaLookupBridge.cpp
  Frontend/SemaPluginHooks.cpp
  Frontend/SemaProvider.cpp
  Frontend/SemaTypeBridge.cpp
  Frontend/SemanticUnitArtifact.cpp
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
  BitReader
  BitWriter
  CodeGen
  Core
  Support
  )

add_neverc_library(nevercPluginIR
  IR/IRAttributeBridge.cpp
  IR/IRBuilderBridge.cpp
  IR/IRConstantBridge.cpp
  IR/IRContextBridge.cpp
  IR/IRCoreAPI.cpp
  IR/IRFunctionBridge.cpp
  IR/IRGenProvider.cpp
  IR/IRHandleBridge.cpp
  IR/IRInstructionBridge.cpp
  IR/IRMetadataBridge.cpp
  IR/IRModuleArtifact.cpp
  IR/IRModuleBridge.cpp
  IR/IROptimizationProvider.cpp
  IR/IRPassPlugin.cpp
  IR/IRPluginInterfaces.cpp
  IR/IRSerialization.cpp
  IR/IRTypeBridge.cpp
  IR/IRValueBridge.cpp
  MIR/MIRBuilderBridge.cpp
  MIR/MIRConstantPoolBridge.cpp
  MIR/MIRFrameBridge.cpp
  MIR/MIRHandleBridge.cpp
  MIR/MIRCoreBridge.cpp
  MIR/MIRMemoryBridge.cpp
  MIR/MIRMutation.cpp
  MIR/MIROperandBridge.cpp
  MIR/MIRPassPlugin.cpp
  MIR/MIRPropertyBridge.cpp
  MIR/MIRRegisterBridge.cpp
  MIR/MIRSchemaBridge.cpp
  MIR/MIRVerifier.cpp

  LINK_LIBS
  nevercPluginCore
  nevercPluginFrontend
  )
add_dependencies(nevercPluginIR
  nevercPluginIRSchema
  nevercPluginMIRSchema
  )

set(LLVM_LINK_COMPONENTS
  CodeGen
  Core
  MC
  Support
  Target
  )

add_neverc_library(nevercPluginTarget
  Target/PluginABILowering.cpp
  Target/PluginTargetInfo.cpp
  Target/PluginTargetRegistry.cpp
  Target/TargetDescriptorVerifier.cpp
  Target/TargetKey.cpp
  Target/TargetPluginInterfaces.cpp
  Target/TargetRegistration.cpp

  LINK_LIBS
  nevercPluginCore
  nevercFoundation
  )

set(LLVM_LINK_COMPONENTS
  CodeGen
  MC
  Object
  Support
  )

add_neverc_library(nevercPluginMCObject
  MCObject/MCBridge.cpp
  MCObject/MCMutation.cpp
  MCObject/MCObjectPluginInterfaces.cpp

  LINK_LIBS
  nevercPluginCore
  nevercPluginIO
  nevercPluginIR
  nevercPluginTarget
  nevercFoundation
  )
