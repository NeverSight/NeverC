set(LLVM_LINK_COMPONENTS Support)
set(NEVERC_PLUGIN_PHASE_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-phase-schema.py")
set(NEVERC_PLUGIN_PHASE_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PhaseSchema.json")
set(NEVERC_PLUGIN_PHASE_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginPhaseSchema.inc")
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
set(NEVERC_PLUGIN_MC_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-mc-schema.py")
set(NEVERC_PLUGIN_MC_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/MCSchema.json")
set(NEVERC_PLUGIN_MC_SCHEMA_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginMCSchema.inc.in")
set(NEVERC_PLUGIN_MC_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginMCSchema.inc")
set(NEVERC_PLUGIN_OBJECT_SCHEMA_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-object-schema.py")
set(NEVERC_PLUGIN_OBJECT_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/ObjectSchema.json")
set(NEVERC_PLUGIN_OBJECT_SCHEMA_TEMPLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginObjectSchema.inc.in")
set(NEVERC_PLUGIN_OBJECT_SCHEMA_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/neverc/Plugin/Schema/PluginObjectSchema.inc")
set(NEVERC_PLUGIN_PYTHON_PHASE_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/gen-python-phases.py")
set(NEVERC_PLUGIN_PYTHON_PHASE_OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../pluginsdk/python/neverc_plugin/phases.py")

add_custom_target(nevercPluginPhaseSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_PHASE_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_PHASE_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_PHASE_SCHEMA}
    ${NEVERC_PLUGIN_PHASE_SCHEMA_OUTPUT}
  COMMENT "Checking NeverC plugin phase schema"
  VERBATIM
  )

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

add_custom_target(nevercPluginMCSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_MC_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_MC_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_MC_SCHEMA}
    ${NEVERC_PLUGIN_MC_SCHEMA_TEMPLATE}
    ${NEVERC_PLUGIN_MC_SCHEMA_OUTPUT}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/MC/MCInst.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/MC/MCExpr.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/MC/MCFragment.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../llvm/include/llvm/MC/MCFixup.h"
  COMMENT "Checking NeverC plugin MC schema"
  VERBATIM
  )

add_custom_target(nevercPluginObjectSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_OBJECT_SCHEMA_GENERATOR}
          --check
  DEPENDS
    ${NEVERC_PLUGIN_OBJECT_SCHEMA_GENERATOR}
    ${NEVERC_PLUGIN_OBJECT_SCHEMA}
    ${NEVERC_PLUGIN_OBJECT_SCHEMA_TEMPLATE}
    ${NEVERC_PLUGIN_OBJECT_SCHEMA_OUTPUT}
  COMMENT "Checking NeverC plugin object schema"
  VERBATIM
  )

add_custom_target(nevercPythonPluginPhaseSchema
  COMMAND ${Python3_EXECUTABLE}
          ${NEVERC_PLUGIN_PYTHON_PHASE_GENERATOR}
          --check
          --schema ${NEVERC_PLUGIN_PHASE_SCHEMA}
          --output ${NEVERC_PLUGIN_PYTHON_PHASE_OUTPUT}
  DEPENDS
    ${NEVERC_PLUGIN_PYTHON_PHASE_GENERATOR}
    ${NEVERC_PLUGIN_PHASE_SCHEMA}
    ${NEVERC_PLUGIN_PYTHON_PHASE_OUTPUT}
  COMMENT "Checking NeverC Python plugin phase bindings"
  VERBATIM
  )

set(NEVERC_PLUGIN_PYTHON_SOURCES)
if(NEVERC_ENABLE_PYTHON_PLUGINS)
  list(APPEND NEVERC_PLUGIN_PYTHON_SOURCES
    Python/PythonPluginFFI.cpp
    Python/PythonPluginLoader.cpp
  )
endif()

add_neverc_library(nevercPluginCore
  Core/CoreAPIBridge.cpp
  Core/PluginArtifactRegistry.cpp
  Core/PluginCallbackStats.cpp
  Core/PluginCapabilityInventory.cpp
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

  ${NEVERC_PLUGIN_PYTHON_SOURCES}

  LINK_LIBS
  nevercFoundation
  )

add_dependencies(nevercPluginCore nevercPythonPluginPhaseSchema)

if(NEVERC_ENABLE_PYTHON_PLUGINS)
  # add_neverc_library compiles sources through an object-library target;
  # definitions placed only on the archive wrapper do not reach those sources.
  target_compile_definitions(obj.nevercPluginCore
    PRIVATE NEVERC_ENABLE_PYTHON_PLUGINS=1)
  target_link_libraries(nevercPluginCore PRIVATE NeverCPython::Python)
endif()

if(MSVC)
  set_source_files_properties(
    Core/PluginRegistration.cpp
    Core/PluginRegistry.cpp
    Core/PluginArtifactRegistry.cpp
    Core/PluginHandleArena.cpp
    Core/PluginPhaseExecutor.cpp
    Core/PluginProcessServices.cpp
    Core/PluginSession.cpp
    Core/PluginTaskContext.cpp
    Python/PythonPluginFFI.cpp
    Python/PythonPluginLoader.cpp
    PROPERTIES COMPILE_OPTIONS "/EHsc"
  )
else()
  set_source_files_properties(
    Core/PluginRegistration.cpp
    Core/PluginRegistry.cpp
    Core/PluginArtifactRegistry.cpp
    Core/PluginHandleArena.cpp
    Core/PluginPhaseExecutor.cpp
    Core/PluginProcessServices.cpp
    Core/PluginSession.cpp
    Core/PluginTaskContext.cpp
    Python/PythonPluginFFI.cpp
    Python/PythonPluginLoader.cpp
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
  MIR/IRToMIRProvider.cpp
  MIR/MIRBuilderBridge.cpp
  MIR/MIRConstantPoolBridge.cpp
  MIR/MIRFrameBridge.cpp
  MIR/MIRHandleBridge.cpp
  MIR/MIRCoreBridge.cpp
  MIR/MIRMemoryBridge.cpp
  MIR/MIRModuleArtifact.cpp
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
  Target/BuiltinLLVMTargetProvider.cpp
  Target/BuiltinNeverCTargetInfoProvider.cpp
  Target/BuiltinTargetSchema.cpp
  Target/BuiltinTargetRoutes.cpp
  Target/CallingConventionMaterializePass.cpp
  Target/CallingConventionPlan.cpp
  Target/CodeGenArtifacts.cpp
  Target/CodeGenRoutePlanner.cpp
  Target/PluginABILowering.cpp
  Target/PluginTargetInfo.cpp
  Target/PluginTargetRegistry.cpp
  Target/TargetAsmConstraintBridge.cpp
  Target/TargetDescriptorVerifier.cpp
  Target/TargetFeatureBridge.cpp
  Target/TargetKey.cpp
  Target/TargetLanguageBridge.cpp
  Target/TargetPluginInterfaces.cpp
  Target/TargetRegistration.cpp
  Target/TargetSchemaDigest.cpp

  LINK_LIBS
  nevercPluginCore
  nevercFoundation
  )

set(_neverc_target_schema_gen_libs LLVMSupport LLVMMC LLVMTarget)
if("X86" IN_LIST LLVM_TARGETS_TO_BUILD)
  list(APPEND _neverc_target_schema_gen_libs
    LLVMX86CodeGen
    LLVMX86Desc
    LLVMX86Info
  )
endif()
if("AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
  list(APPEND _neverc_target_schema_gen_libs
    LLVMAArch64CodeGen
    LLVMAArch64Desc
    LLVMAArch64Info
    LLVMAArch64Utils
  )
endif()

add_executable(neverc-plugin-target-schema-gen
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/plugin-api/TargetSchemaGen.cpp
)
llvm_update_compile_flags(neverc-plugin-target-schema-gen)
target_link_libraries(neverc-plugin-target-schema-gen PRIVATE
  ${_neverc_target_schema_gen_libs}
)
target_compile_features(neverc-plugin-target-schema-gen PRIVATE cxx_std_17)
set_target_properties(neverc-plugin-target-schema-gen PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${LLVM_RUNTIME_OUTPUT_INTDIR}"
)

set(LLVM_LINK_COMPONENTS
  CodeGen
  MC
  Object
  Support
  )

add_neverc_library(nevercPluginMCObject
  MC/AssemblyArtifacts.cpp
  MC/AssemblyProviderPipeline.cpp
  MC/BuiltinLLVMAsmParser.cpp
  MC/LLVMComponentProviderBridge.cpp
  MC/MCAsmParserProvider.cpp
  MC/MCAsmPrinterProvider.cpp
  MC/MCBuilder.cpp
  MC/MCEmissionBridge.cpp
  MC/MCEmissionPlan.cpp
  MC/MCEncoderRegistry.cpp
  MC/MCHandleBridge.cpp
  MC/MCInstBuilder.cpp
  MC/MCLayoutEngine.cpp
  MC/MachineEmissionBridge.cpp
  MC/MCSchemaBridge.cpp
  MC/MCUnit.cpp
  MC/MCUnitBridge.cpp
  MC/MCVerifier.cpp
  MC/MIRToMCProvider.cpp
  MIR/MIRPipelineHooks.cpp
  MCObject/MCBridge.cpp
  MCObject/MCMutation.cpp
  MCObject/MCObjectPluginInterfaces.cpp
  Object/AndroidKernelReleaseWriterPreflight.cpp
  Object/BuiltinELFTableCanonicalizer.cpp
  Object/BuiltinObjectWriterPreflight.cpp
  Object/BuiltinLLVMObjectReader.cpp
  Object/BuiltinLLVMObjectWriter.cpp
  Object/MutableBinaryBuilder.cpp
  Object/ObjectBuilder.cpp
  Object/ObjectFormatRegistry.cpp
  Object/ObjectGraph.cpp
  Object/ObjectHandleBridge.cpp
  Object/ObjectImage.cpp
  Object/ObjectMutation.cpp
  Object/ObjectPhaseBridge.cpp
  Object/ObjectPhaseHooks.cpp
  Object/ObjectProbe.cpp
  Object/ObjectReaderProvider.cpp
  Object/ObjectVerifier.cpp
  Object/ObjectWriterProvider.cpp

  LINK_LIBS
  nevercPluginCore
  nevercPluginIO
  nevercPluginIR
  nevercPluginTarget
  nevercFoundation
  nevercMerge
  )
add_dependencies(nevercPluginMCObject
  nevercPluginMCSchema
  nevercPluginObjectSchema
  )
if(MSVC)
  set_source_files_properties(
    MC/MCEncoderRegistry.cpp
    MC/MCLayoutEngine.cpp
    PROPERTIES COMPILE_OPTIONS "/EHsc"
    )
else()
  set_source_files_properties(
    MC/MCEncoderRegistry.cpp
    MC/MCLayoutEngine.cpp
    PROPERTIES COMPILE_OPTIONS "-fexceptions;-funwind-tables"
    )
endif()

set(LLVM_LINK_COMPONENTS
  Object
  Support
  )

add_neverc_library(nevercPluginLink
  Link/AndroidKernelModuleFinalizer.cpp
  Link/AndroidKernelProfileContractVerifier.cpp
  Link/AndroidKernelReleaseInputVerifier.cpp
  Link/ArchiveGraph.cpp
  Link/BinaryImage.cpp
  Link/BinaryImageCAPI.cpp
  Link/BuiltinObjectMergeAdapter.cpp
  Link/ComdatSelectionProvider.cpp
  Link/ICFProvider.cpp
  Link/LinkExecutionHooksBridge.cpp
  Link/LinkGraph.cpp
  Link/LinkGraphCAPI.cpp
  Link/LinkInputReader.cpp
  Link/LinkInvalidation.cpp
  Link/LinkLayoutProvider.cpp
  Link/LinkRelaxationProvider.cpp
  Link/LayoutVerifier.cpp
  Link/LivenessVerifier.cpp
  Link/LinkerScriptProvider.cpp
  Link/LinkMutation.cpp
  Link/LinkOutputBundle.cpp
  Link/LinkPhaseCAPI.cpp
  Link/LinkPhaseExecutor.cpp
  Link/LinkPhaseRegistry.cpp
  Link/ObjectGraphImporter.cpp
  Link/ObjectMergeProvider.cpp
  Link/LinkProvenance.cpp
  Link/LinkRequest.cpp
  Link/LinkRoutePlanner.cpp
  Link/LinkPluginInterfaces.cpp
  Link/LinkVerifier.cpp
  Link/PluginLinkRegistry.cpp
  Link/SharedLibraryGraph.cpp
  Link/ResolutionVerifier.cpp
  Link/RelaxationExecutor.cpp
  Link/RelocationProvider.cpp
  Link/RelocationVerifier.cpp
  Link/SectionGCProvider.cpp
  Link/SymbolResolutionProvider.cpp
  Link/SynthesisVerifier.cpp
  Link/SyntheticSectionProvider.cpp
  Link/ThunkStubProvider.cpp
  LTO/LTOInputSet.cpp

  LINK_LIBS
  LLVMBitReader
  LLVMCore
  LLVMLTO
  nevercPluginCore
  nevercPluginIO
  nevercPluginIR
  nevercPluginTarget
  nevercPluginMCObject
  nevercFoundation
  nevercMerge
  )
add_dependencies(nevercPluginLink
  nevercPluginPhaseSchema
  )
