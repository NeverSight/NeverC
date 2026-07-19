/*===-- PluginIR.h - NeverC LLVM IR plugin C ABI ------------------ C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINIR_H
#define NEVERC_PLUGIN_PLUGINIR_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_IR_CORE_API_MAJOR UINT16_C(1)
#define NEVERC_IR_CORE_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IR_CORE_HIGH UINT64_C(0x4e43504952430001)
#define NEVERC_INTERFACE_IR_CORE_LOW UINT64_C(0x0000000000000001)
#define NEVERC_IR_CORE_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_IR_GEN_API_MAJOR UINT16_C(1)
#define NEVERC_IR_GEN_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IR_GEN_HIGH UINT64_C(0x4e43504952470001)
#define NEVERC_INTERFACE_IR_GEN_LOW UINT64_C(0x0000000000000001)
#define NEVERC_IR_GEN_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_IR_OPTIMIZATION_API_MAJOR UINT16_C(1)
#define NEVERC_IR_OPTIMIZATION_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IR_OPTIMIZATION_HIGH UINT64_C(0x4e435049524f0001)
#define NEVERC_INTERFACE_IR_OPTIMIZATION_LOW UINT64_C(0x0000000000000001)
#define NEVERC_IR_OPTIMIZATION_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_IR_BUILDER_API_MAJOR UINT16_C(1)
#define NEVERC_IR_BUILDER_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IR_BUILDER_HIGH UINT64_C(0x4e43504952420001)
#define NEVERC_INTERFACE_IR_BUILDER_LOW UINT64_C(0x0000000000000001)
#define NEVERC_IR_BUILDER_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_IR_ANALYSIS_API_MAJOR UINT16_C(1)
#define NEVERC_IR_ANALYSIS_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IR_ANALYSIS_HIGH UINT64_C(0x4e43504952410001)
#define NEVERC_INTERFACE_IR_ANALYSIS_LOW UINT64_C(0x0000000000000001)
#define NEVERC_IR_ANALYSIS_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_IR_PASS_API_MAJOR UINT16_C(1)
#define NEVERC_IR_PASS_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IR_PASS_HIGH UINT64_C(0x4e43504952500001)
#define NEVERC_INTERFACE_IR_PASS_LOW UINT64_C(0x0000000000000001)
#define NEVERC_IR_PASS_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

typedef NevercHandle NevercIRContextHandle;
typedef NevercHandle NevercIRModuleHandle;
typedef NevercHandle NevercIRValueHandle;
typedef NevercHandle NevercIRTypeHandle;
typedef NevercHandle NevercIRMetadataHandle;
typedef NevercHandle NevercIRNamedMetadataHandle;
typedef NevercHandle NevercIRAttributeHandle;
typedef NevercHandle NevercIRComdatHandle;
typedef NevercHandle NevercIRBuilderHandle;
typedef NevercHandle NevercIRMutationHandle;
typedef NevercHandle NevercIRAnalysisResultHandle;
typedef NevercHandle NevercIRPassPlanHandle;
typedef NevercHandle NevercIRSerializedBufferHandle;

typedef uint32_t NevercIRTypeKind;
typedef uint32_t NevercIRValueKind;
typedef uint32_t NevercIROpcode;
typedef uint32_t NevercIRPredicate;
typedef uint32_t NevercIRLinkage;
typedef uint32_t NevercIRVisibility;
typedef uint32_t NevercIRCallingConvention;
typedef uint32_t NevercIRAttributeLocation;
typedef uint32_t NevercIRPropertyID;
typedef uint32_t NevercIROpcodeCategory;
typedef uint32_t NevercIRResultConstraint;
typedef uint32_t NevercIRSideEffectClass;
typedef uint64_t NevercIRPropertyFlags;
typedef uint32_t NevercIRMetadataKind;
typedef uint32_t NevercIRAttributeValueKind;
typedef uint32_t NevercIRValueCollection;
typedef uint32_t NevercIRPropertyValueKind;
typedef uint32_t NevercIRAtomicOrdering;
typedef uint32_t NevercIRTailCallKind;
typedef uint32_t NevercIRMutationScope;
typedef uint32_t NevercIRSerializationFormat;
typedef uint32_t NevercIRBuiltinAnalysis;
typedef uint32_t NevercIRMemoryAccessKind;
typedef uint32_t NevercIRAliasResult;
typedef uint64_t NevercIRFastMathFlags;

#define NEVERC_IR_TYPE_UNKNOWN UINT32_C(0)
#define NEVERC_IR_VALUE_UNKNOWN UINT32_C(0)
#define NEVERC_IR_OPCODE_UNKNOWN UINT32_C(0)
#define NEVERC_IR_PREDICATE_UNKNOWN UINT32_C(0)
#define NEVERC_IR_LINKAGE_UNKNOWN UINT32_C(0)
#define NEVERC_IR_VISIBILITY_UNKNOWN UINT32_C(0)
#define NEVERC_IR_CALLING_CONVENTION_UNKNOWN UINT32_C(0)
#define NEVERC_IR_ATTRIBUTE_LOCATION_UNKNOWN UINT32_C(0)
#define NEVERC_IR_PROPERTY_UNKNOWN UINT32_C(0)
#define NEVERC_IR_METADATA_UNKNOWN UINT32_C(0)
#define NEVERC_IR_METADATA_STRING UINT32_C(1)
#define NEVERC_IR_METADATA_NODE UINT32_C(2)
#define NEVERC_IR_METADATA_VALUE UINT32_C(3)
#define NEVERC_IR_METADATA_DEBUG_LOCATION UINT32_C(4)
#define NEVERC_IR_ATTRIBUTE_ENUM UINT32_C(1)
#define NEVERC_IR_ATTRIBUTE_INTEGER UINT32_C(2)
#define NEVERC_IR_ATTRIBUTE_STRING UINT32_C(3)
#define NEVERC_IR_ATTRIBUTE_TYPE UINT32_C(4)

#define NEVERC_IR_COLLECTION_MODULE_FUNCTIONS UINT32_C(1)
#define NEVERC_IR_COLLECTION_MODULE_GLOBALS UINT32_C(2)
#define NEVERC_IR_COLLECTION_MODULE_ALIASES UINT32_C(3)
#define NEVERC_IR_COLLECTION_MODULE_I_FUNCS UINT32_C(4)
#define NEVERC_IR_COLLECTION_FUNCTION_ARGUMENTS UINT32_C(5)
#define NEVERC_IR_COLLECTION_FUNCTION_BLOCKS UINT32_C(6)
#define NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS UINT32_C(7)

#define NEVERC_IR_PROPERTY_VALUE_BOOL UINT32_C(1)
#define NEVERC_IR_PROPERTY_VALUE_UINT UINT32_C(2)
#define NEVERC_IR_PROPERTY_VALUE_ENUM UINT32_C(3)
#define NEVERC_IR_PROPERTY_VALUE_FLAGS UINT32_C(4)
#define NEVERC_IR_PROPERTY_VALUE_STRING UINT32_C(5)
#define NEVERC_IR_PROPERTY_VALUE_TYPE UINT32_C(6)

#define NEVERC_IR_ATOMIC_ORDERING_NOT_ATOMIC UINT32_C(0)
#define NEVERC_IR_ATOMIC_ORDERING_UNORDERED UINT32_C(1)
#define NEVERC_IR_ATOMIC_ORDERING_MONOTONIC UINT32_C(2)
#define NEVERC_IR_ATOMIC_ORDERING_ACQUIRE UINT32_C(3)
#define NEVERC_IR_ATOMIC_ORDERING_RELEASE UINT32_C(4)
#define NEVERC_IR_ATOMIC_ORDERING_ACQUIRE_RELEASE UINT32_C(5)
#define NEVERC_IR_ATOMIC_ORDERING_SEQUENTIALLY_CONSISTENT UINT32_C(6)

#define NEVERC_IR_TAIL_CALL_NONE UINT32_C(0)
#define NEVERC_IR_TAIL_CALL_TAIL UINT32_C(1)
#define NEVERC_IR_TAIL_CALL_MUST_TAIL UINT32_C(2)
#define NEVERC_IR_TAIL_CALL_NO_TAIL UINT32_C(3)

#define NEVERC_IR_MUTATION_SCOPE_MODULE UINT32_C(1)
#define NEVERC_IR_MUTATION_SCOPE_FUNCTION UINT32_C(2)
#define NEVERC_IR_MUTATION_SCOPE_LOOP UINT32_C(3)

#define NEVERC_IR_SERIALIZATION_BITCODE UINT32_C(1)
#define NEVERC_IR_SERIALIZATION_TEXT UINT32_C(2)

#define NEVERC_IR_ANALYSIS_DOMINATOR_TREE UINT32_C(1)
#define NEVERC_IR_ANALYSIS_LOOP_INFO UINT32_C(2)
#define NEVERC_IR_ANALYSIS_SCALAR_EVOLUTION UINT32_C(3)
#define NEVERC_IR_ANALYSIS_MEMORY_SSA UINT32_C(4)
#define NEVERC_IR_ANALYSIS_CALL_GRAPH UINT32_C(5)
#define NEVERC_IR_ANALYSIS_ALIAS UINT32_C(6)
#define NEVERC_IR_ANALYSIS_POST_DOMINATOR_TREE UINT32_C(7)

#define NEVERC_IR_MEMORY_ACCESS_NONE UINT32_C(0)
#define NEVERC_IR_MEMORY_ACCESS_USE UINT32_C(1)
#define NEVERC_IR_MEMORY_ACCESS_DEF UINT32_C(2)
#define NEVERC_IR_MEMORY_ACCESS_PHI UINT32_C(3)
#define NEVERC_IR_MEMORY_ACCESS_LIVE_ON_ENTRY UINT32_C(4)

#define NEVERC_IR_ALIAS_NO UINT32_C(1)
#define NEVERC_IR_ALIAS_MAY UINT32_C(2)
#define NEVERC_IR_ALIAS_PARTIAL UINT32_C(3)
#define NEVERC_IR_ALIAS_MUST UINT32_C(4)

#define NEVERC_IR_FAST_MATH_ALLOW_REASSOC UINT64_C(1)
#define NEVERC_IR_FAST_MATH_NO_NANS UINT64_C(2)
#define NEVERC_IR_FAST_MATH_NO_INFS UINT64_C(4)
#define NEVERC_IR_FAST_MATH_NO_SIGNED_ZEROS UINT64_C(8)
#define NEVERC_IR_FAST_MATH_ALLOW_RECIPROCAL UINT64_C(16)
#define NEVERC_IR_FAST_MATH_ALLOW_CONTRACT UINT64_C(32)
#define NEVERC_IR_FAST_MATH_APPROX_FUNC UINT64_C(64)

#define NEVERC_IR_OPCODE_CATEGORY_TERM UINT32_C(1)
#define NEVERC_IR_OPCODE_CATEGORY_UNARY UINT32_C(2)
#define NEVERC_IR_OPCODE_CATEGORY_BINARY UINT32_C(3)
#define NEVERC_IR_OPCODE_CATEGORY_MEMORY UINT32_C(4)
#define NEVERC_IR_OPCODE_CATEGORY_CAST UINT32_C(5)
#define NEVERC_IR_OPCODE_CATEGORY_FUNCLETPAD UINT32_C(6)
#define NEVERC_IR_OPCODE_CATEGORY_OTHER UINT32_C(7)
#define NEVERC_IR_OPCODE_CATEGORY_USER UINT32_C(8)

#define NEVERC_IR_RESULT_VOID UINT32_C(1)
#define NEVERC_IR_RESULT_DECLARED_TYPE UINT32_C(2)
#define NEVERC_IR_RESULT_SAME_AS_OPERAND_0 UINT32_C(3)
#define NEVERC_IR_RESULT_I1 UINT32_C(4)
#define NEVERC_IR_RESULT_POINTER UINT32_C(5)
#define NEVERC_IR_RESULT_TOKEN UINT32_C(6)
#define NEVERC_IR_RESULT_AGGREGATE UINT32_C(7)

#define NEVERC_IR_SIDE_EFFECT_NEVER UINT32_C(0)
#define NEVERC_IR_SIDE_EFFECT_CONDITIONAL UINT32_C(1)
#define NEVERC_IR_SIDE_EFFECT_ALWAYS UINT32_C(2)

#define NEVERC_IR_OPERAND_VARIADIC UINT32_MAX

#include "neverc/Plugin/Schema/PluginIRSchema.inc" /* IWYU pragma: export */

/*
 * The first ABI minor publishes only independently negotiable table prefixes
 * and opaque handles. Function slots are appended by the task that implements
 * them; a table is not registered with the host until its required prefix is
 * usable.
 */
NEVERC_ABI_PACK_BEGIN

typedef struct NevercIRValueCursor {
  NevercABITableHeader Header;
  NevercHandle Container;
  uint64_t MutationGeneration;
  uint64_t Position;
  NevercIRValueCollection Collection;
  uint32_t Reserved;
} NevercIRValueCursor;

typedef struct NevercIRUseInfo {
  NevercABITableHeader Header;
  NevercIRValueHandle User;
  uint64_t OperandIndex;
} NevercIRUseInfo;

typedef struct NevercIRPhiIncoming {
  NevercABITableHeader Header;
  NevercIRValueHandle Value;
  NevercIRValueHandle Block;
} NevercIRPhiIncoming;

typedef struct NevercIRPropertyValue {
  NevercABITableHeader Header;
  NevercIRPropertyValueKind Kind;
  uint32_t Reserved;
  uint64_t UnsignedValue;
  NevercIRTypeHandle TypeValue;
  NevercStringView StringValue;
} NevercIRPropertyValue;

typedef struct NevercIRDebugLocationInfo {
  uint32_t Size;
  uint32_t Version;
  uint32_t Line;
  uint32_t Column;
  uint8_t IsImplicitCode;
  uint8_t Reserved[7];
  NevercIRMetadataHandle Scope;
  NevercIRMetadataHandle InlinedAt;
} NevercIRDebugLocationInfo;

typedef struct NevercIRCoreAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetContext)(void *Context,
                                        NevercTaskHandle Task,
                                        NevercIRContextHandle *OutContext);
  NevercStatus(NEVERC_CALL *GetModule)(void *Context, NevercTaskHandle Task,
                                       NevercIRModuleHandle *OutModule);
  NevercStatus(NEVERC_CALL *GetValueKind)(void *Context,
                                          NevercTaskHandle Task,
                                          NevercIRValueHandle Value,
                                          NevercIRValueKind *OutKind);
  NevercStatus(NEVERC_CALL *ReplaceAllUsesWith)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Original,
      NevercIRValueHandle Replacement);
  NevercStatus(NEVERC_CALL *EraseValue)(void *Context, NevercTaskHandle Task,
                                        NevercIRValueHandle Value);
  NevercStatus(NEVERC_CALL *GetTypeKind)(void *Context,
                                         NevercTaskHandle Task,
                                         NevercIRTypeHandle Type,
                                         NevercIRTypeKind *OutKind);
  NevercStatus(NEVERC_CALL *GetPrimitiveType)(
      void *Context, NevercTaskHandle Task, NevercIRTypeKind Kind,
      NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetIntegerType)(void *Context,
                                            NevercTaskHandle Task,
                                            uint32_t BitWidth,
                                            NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetPointerType)(void *Context,
                                            NevercTaskHandle Task,
                                            uint32_t AddressSpace,
                                            NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetArrayType)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle ElementType,
      uint64_t ElementCount, NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetVectorType)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle ElementType,
      uint32_t MinimumElementCount, uint8_t Scalable,
      NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetStructType)(
      void *Context, NevercTaskHandle Task, NevercStringView Name,
      const NevercIRTypeHandle *ElementTypes, uint64_t ElementCount,
      uint8_t Packed, NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetFunctionType)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle ReturnType,
      const NevercIRTypeHandle *ParameterTypes, uint64_t ParameterCount,
      uint8_t Variadic, NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *CreateIntegerConstant)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
      const uint64_t *LittleEndianWords, uint64_t WordCount,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *CreateFloatingConstant)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
      const uint64_t *LittleEndianWords, uint64_t WordCount,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetNullConstant)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetPoisonConstant)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetUndefConstant)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *CreateAggregateConstant)(
      void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
      const NevercIRValueHandle *Elements, uint64_t ElementCount,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *CreateConstantBinaryExpression)(
      void *Context, NevercTaskHandle Task, NevercIROpcode Opcode,
      NevercIRValueHandle Left, NevercIRValueHandle Right,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *CreateConstantCastExpression)(
      void *Context, NevercTaskHandle Task, NevercIROpcode Opcode,
      NevercIRValueHandle Operand, NevercIRTypeHandle DestinationType,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *CreateConstantCompareExpression)(
      void *Context, NevercTaskHandle Task, NevercIRPredicate Predicate,
      NevercIRValueHandle Left, NevercIRValueHandle Right,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *CreateConstantGEPExpression)(
      void *Context, NevercTaskHandle Task,
      NevercIRTypeHandle SourceElementType, NevercIRValueHandle Pointer,
      const NevercIRValueHandle *Indices, uint64_t IndexCount,
      uint8_t InBounds, NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetGlobalAddressConstant)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetMetadataKind)(
      void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
      NevercIRMetadataKind *OutKind);
  NevercStatus(NEVERC_CALL *CreateMetadataString)(
      void *Context, NevercTaskHandle Task, NevercStringView Bytes,
      NevercIRMetadataHandle *OutMetadata);
  NevercStatus(NEVERC_CALL *GetMetadataStringBytes)(
      void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
      NevercStringView *OutBytes);
  NevercStatus(NEVERC_CALL *CreateMetadataNode)(
      void *Context, NevercTaskHandle Task,
      const NevercIRMetadataHandle *Operands, uint64_t OperandCount,
      uint8_t Distinct, NevercIRMetadataHandle *OutMetadata);
  NevercStatus(NEVERC_CALL *GetValueAsMetadata)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      NevercIRMetadataHandle *OutMetadata);
  NevercStatus(NEVERC_CALL *GetMetadataAsValue)(
      void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
      NevercIRValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetMetadataOperandCount)(
      void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetMetadataOperand)(
      void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
      uint64_t Index, NevercIRMetadataHandle *OutOperand);
  NevercStatus(NEVERC_CALL *GetOrInsertNamedMetadata)(
      void *Context, NevercTaskHandle Task, NevercStringView Name,
      NevercIRNamedMetadataHandle *OutMetadata);
  NevercStatus(NEVERC_CALL *AppendNamedMetadata)(
      void *Context, NevercTaskHandle Task,
      NevercIRNamedMetadataHandle NamedMetadata,
      NevercIRMetadataHandle Metadata);
  NevercStatus(NEVERC_CALL *GetNamedMetadataOperandCount)(
      void *Context, NevercTaskHandle Task,
      NevercIRNamedMetadataHandle NamedMetadata, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetNamedMetadataOperand)(
      void *Context, NevercTaskHandle Task,
      NevercIRNamedMetadataHandle NamedMetadata, uint64_t Index,
      NevercIRMetadataHandle *OutOperand);
  NevercStatus(NEVERC_CALL *GetDebugLocationInfo)(
      void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Location,
      NevercIRDebugLocationInfo *OutInfo);
  NevercStatus(NEVERC_CALL *CreateEnumAttribute)(
      void *Context, NevercTaskHandle Task, NevercStringView Kind,
      NevercIRAttributeHandle *OutAttribute);
  NevercStatus(NEVERC_CALL *CreateIntegerAttribute)(
      void *Context, NevercTaskHandle Task, NevercStringView Kind,
      uint64_t Value, NevercIRAttributeHandle *OutAttribute);
  NevercStatus(NEVERC_CALL *CreateStringAttribute)(
      void *Context, NevercTaskHandle Task, NevercStringView Kind,
      NevercStringView Value, NevercIRAttributeHandle *OutAttribute);
  NevercStatus(NEVERC_CALL *CreateTypeAttribute)(
      void *Context, NevercTaskHandle Task, NevercStringView Kind,
      NevercIRTypeHandle Type, NevercIRAttributeHandle *OutAttribute);
  NevercStatus(NEVERC_CALL *GetAttributeValueKind)(
      void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
      NevercIRAttributeValueKind *OutKind);
  NevercStatus(NEVERC_CALL *GetAttributeKindName)(
      void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
      NevercStringView *OutName);
  NevercStatus(NEVERC_CALL *GetAttributeIntegerValue)(
      void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
      uint64_t *OutValue);
  NevercStatus(NEVERC_CALL *GetAttributeStringValue)(
      void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
      NevercStringView *OutValue);
  NevercStatus(NEVERC_CALL *GetAttributeTypeValue)(
      void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
      NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *AddFunctionAttribute)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercIRAttributeLocation Location, uint32_t ParameterIndex,
      NevercIRAttributeHandle Attribute);
  NevercStatus(NEVERC_CALL *HasFunctionAttribute)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercStringView Kind, NevercBool *OutPresent);
  NevercStatus(NEVERC_CALL *GetFunctionStringAttribute)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercStringView Kind, NevercStringView *OutValue);
  NevercStatus(NEVERC_CALL *GetModuleIdentifier)(
      void *Context, NevercTaskHandle Task, NevercStringView *OutIdentifier);
  NevercStatus(NEVERC_CALL *SetModuleIdentifier)(
      void *Context, NevercTaskHandle Task, NevercStringView Identifier);
  NevercStatus(NEVERC_CALL *GetModuleTargetTriple)(
      void *Context, NevercTaskHandle Task, NevercStringView *OutTriple);
  NevercStatus(NEVERC_CALL *SetModuleTargetTriple)(
      void *Context, NevercTaskHandle Task, NevercStringView Triple);
  NevercStatus(NEVERC_CALL *GetModuleDataLayout)(
      void *Context, NevercTaskHandle Task, NevercStringView *OutDataLayout);
  NevercStatus(NEVERC_CALL *SetModuleDataLayout)(
      void *Context, NevercTaskHandle Task, NevercStringView DataLayout);
  NevercStatus(NEVERC_CALL *GetModuleInlineAssembly)(
      void *Context, NevercTaskHandle Task, NevercStringView *OutAssembly);
  NevercStatus(NEVERC_CALL *SetModuleInlineAssembly)(
      void *Context, NevercTaskHandle Task, NevercStringView Assembly);
  NevercStatus(NEVERC_CALL *BeginValueCursor)(
      void *Context, NevercTaskHandle Task, NevercHandle Container,
      NevercIRValueCollection Collection, NevercIRValueCursor *OutCursor);
  NevercStatus(NEVERC_CALL *CollectValueCursor)(
      void *Context, NevercTaskHandle Task, NevercIRValueCursor *Cursor,
      NevercIRValueHandle *OutValues, uint64_t Capacity,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetValueName)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      NevercStringView *OutName);
  NevercStatus(NEVERC_CALL *SetValueName)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      NevercStringView Name);
  NevercStatus(NEVERC_CALL *GetValueType)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      NevercIRTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetValueUseCount)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetValueUse)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      uint64_t Index, NevercIRUseInfo *OutUse);
  NevercStatus(NEVERC_CALL *GetOperandCount)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetOperand)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      uint64_t Index, NevercIRValueHandle *OutOperand);
  NevercStatus(NEVERC_CALL *SetOperand)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
      uint64_t Index, NevercIRValueHandle Operand);
  NevercStatus(NEVERC_CALL *GetGlobalLinkage)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRLinkage *OutLinkage);
  NevercStatus(NEVERC_CALL *SetGlobalLinkage)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRLinkage Linkage);
  NevercStatus(NEVERC_CALL *GetGlobalVisibility)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRVisibility *OutVisibility);
  NevercStatus(NEVERC_CALL *SetGlobalVisibility)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRVisibility Visibility);
  NevercStatus(NEVERC_CALL *GetGlobalSection)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercStringView *OutSection);
  NevercStatus(NEVERC_CALL *SetGlobalSection)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercStringView Section);
  NevercStatus(NEVERC_CALL *GetOrInsertComdat)(
      void *Context, NevercTaskHandle Task, NevercStringView Name,
      NevercIRComdatHandle *OutComdat);
  NevercStatus(NEVERC_CALL *GetGlobalComdat)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRComdatHandle *OutComdat);
  NevercStatus(NEVERC_CALL *SetGlobalComdat)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
      NevercIRComdatHandle Comdat);
  NevercStatus(NEVERC_CALL *GetFunctionCallingConvention)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercIRCallingConvention *OutCallingConvention);
  NevercStatus(NEVERC_CALL *SetFunctionCallingConvention)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercIRCallingConvention CallingConvention);
  NevercStatus(NEVERC_CALL *GetFunctionPersonality)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercIRValueHandle *OutPersonality);
  NevercStatus(NEVERC_CALL *SetFunctionPersonality)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercIRValueHandle Personality);
  NevercStatus(NEVERC_CALL *GetFunctionGC)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercStringView *OutGC);
  NevercStatus(NEVERC_CALL *SetFunctionGC)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercStringView GC);
  NevercStatus(NEVERC_CALL *GetFunctionSection)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercStringView *OutSection);
  NevercStatus(NEVERC_CALL *SetFunctionSection)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
      NevercStringView Section);
  NevercStatus(NEVERC_CALL *GetTerminator)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
      NevercIRValueHandle *OutTerminator);
  NevercStatus(NEVERC_CALL *GetPredecessorCount)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetPredecessor)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
      uint64_t Index, NevercIRValueHandle *OutPredecessor);
  NevercStatus(NEVERC_CALL *GetSuccessorCount)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetSuccessor)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
      uint64_t Index, NevercIRValueHandle *OutSuccessor);
  NevercStatus(NEVERC_CALL *GetInstructionOpcode)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Instruction,
      NevercIROpcode *OutOpcode);
  NevercStatus(NEVERC_CALL *GetInstructionProperty)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Instruction,
      NevercIRPropertyID Property, NevercIRPropertyValue *OutValue);
  NevercStatus(NEVERC_CALL *SetInstructionProperty)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Instruction,
      NevercIRPropertyID Property, NevercIRPropertyValue Value);
  NevercStatus(NEVERC_CALL *GetPHIIncomingCount)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Phi,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetPHIIncoming)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Phi,
      uint64_t Index, NevercIRPhiIncoming *OutIncoming);
  NevercStatus(NEVERC_CALL *SetPHIIncoming)(
      void *Context, NevercTaskHandle Task, NevercIRValueHandle Phi,
      uint64_t Index, NevercIRPhiIncoming Incoming);
  NevercStatus(NEVERC_CALL *ImportModule)(
      void *Context, NevercTaskHandle Task,
      NevercIRSerializationFormat Format, NevercByteView Bytes);
  NevercStatus(NEVERC_CALL *ExportModule)(
      void *Context, NevercTaskHandle Task,
      NevercIRSerializationFormat Format,
      NevercIRSerializedBufferHandle *OutBuffer);
  NevercStatus(NEVERC_CALL *GetSerializedBufferView)(
      void *Context, NevercTaskHandle Task,
      NevercIRSerializedBufferHandle Buffer, NevercByteView *OutBytes);
  NevercStatus(NEVERC_CALL *ReleaseSerializedBuffer)(
      void *Context, NevercTaskHandle Task,
      NevercIRSerializedBufferHandle Buffer);
} NevercIRCoreAPI;

typedef struct NevercIRBuilderAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *BeginMutation)(
      void *Context, NevercTaskHandle Task, NevercIRMutationScope Scope,
      NevercIRValueHandle ScopeRoot, NevercIRMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *CommitMutation)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbortMutation)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *DestroyMutation)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *CreateBuilder)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRBuilderHandle *OutBuilder);
  NevercStatus(NEVERC_CALL *DestroyBuilder)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder);
  NevercStatus(NEVERC_CALL *SetInsertBlock)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Block);
  NevercStatus(NEVERC_CALL *SetInsertBefore)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Instruction);
  NevercStatus(NEVERC_CALL *SetDebugLocation)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRMetadataHandle Location);
  NevercStatus(NEVERC_CALL *SetFastMathFlags)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRFastMathFlags Flags);
  NevercStatus(NEVERC_CALL *BuildBinary)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIROpcode Opcode, NevercIRValueHandle Left,
      NevercIRValueHandle Right, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildUnary)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIROpcode Opcode, NevercIRValueHandle Operand,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildCompare)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRPredicate Predicate, NevercIRValueHandle Left,
      NevercIRValueHandle Right, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildCast)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIROpcode Opcode, NevercIRValueHandle Operand,
      NevercIRTypeHandle DestinationType, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildSelect)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Condition, NevercIRValueHandle TrueValue,
      NevercIRValueHandle FalseValue, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildAlloca)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle AllocatedType, uint32_t AddressSpace,
      NevercIRValueHandle ArraySize, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildLoad)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle LoadedType, NevercIRValueHandle Pointer,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildStore)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle StoredValue, NevercIRValueHandle Pointer,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildGetElementPtr)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle SourceElementType, NevercIRValueHandle Pointer,
      const NevercIRValueHandle *Indices, uint64_t IndexCount,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildCall)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle FunctionType, NevercIRValueHandle Callee,
      const NevercIRValueHandle *Arguments, uint64_t ArgumentCount,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildPhi)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle Type, uint32_t ReservedIncomingCount,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *AddPhiIncoming)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRValueHandle Phi, NevercIRValueHandle IncomingValue,
      NevercIRValueHandle IncomingBlock);
  NevercStatus(NEVERC_CALL *BuildBranch)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Destination, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildConditionalBranch)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Condition, NevercIRValueHandle TrueDestination,
      NevercIRValueHandle FalseDestination,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildUnreachable)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildReturn)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Value, NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *BuildReturnVoid)(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *CreateFunction)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRTypeHandle FunctionType, NevercStringView Name,
      NevercIRValueHandle *OutFunction);
  NevercStatus(NEVERC_CALL *CreateBasicBlock)(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRValueHandle Function, NevercStringView Name,
      NevercIRValueHandle *OutBlock);
} NevercIRBuilderAPI;

typedef struct NevercIRGeneratePhaseInput {
  NevercABITableHeader Header;
  NevercArtifactHandle SemanticUnit;
  NevercInterfaceID SemanticProduct;
  NevercStringView TargetTriple;
  NevercStringView DataLayout;
  NevercStringView SourceIdentity;
  NevercByteView SourceDigest;
} NevercIRGeneratePhaseInput;

typedef struct NevercIRModuleArtifactDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Product;
  NevercByteView DependencyDigest;
  uint64_t Reserved[2];
} NevercIRModuleArtifactDescriptor;

typedef struct NevercIRModuleArtifactInfo {
  NevercABITableHeader Header;
  NevercInterfaceID Product;
  NevercStringView TargetTriple;
  NevercStringView DataLayout;
  uint64_t Generation;
  NevercByteView DependencyDigest;
} NevercIRModuleArtifactInfo;

typedef struct NevercIRGenAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetGeneratePhaseInput)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercIRGeneratePhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateModule)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercStringView ModuleIdentifier,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus(NEVERC_CALL *ImportModule)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercIRSerializationFormat Format, NevercByteView Bytes,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus(NEVERC_CALL *PublishModule)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercIRModuleArtifactDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetModuleArtifactInfo)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Module, NevercIRModuleArtifactInfo *OutInfo);
} NevercIRGenAPI;

typedef struct NevercIROptimizationPhaseInput {
  NevercABITableHeader Header;
  NevercArtifactHandle Module;
  NevercInterfaceID Product;
  NevercStringView TargetTriple;
  NevercStringView DataLayout;
  uint32_t OptimizationLevel;
  NevercBool DisableLLVMPasses;
  uint8_t Reserved[3];
  NevercByteView InputDigest;
} NevercIROptimizationPhaseInput;

typedef struct NevercIROptimizationAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetOptimizationPhaseInput)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercIROptimizationPhaseInput *OutInput);
  NevercStatus(NEVERC_CALL *GetInputModule)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus(NEVERC_CALL *CreateModule)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercStringView ModuleIdentifier,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus(NEVERC_CALL *ImportModule)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercIRSerializationFormat Format, NevercByteView Bytes,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus(NEVERC_CALL *PublishModule)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercIRModuleArtifactDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetModuleArtifactInfo)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Module, NevercIRModuleArtifactInfo *OutInfo);
  NevercStatus(NEVERC_CALL *RunBuiltinPipeline)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle *OutOutput);
} NevercIROptimizationAPI;

typedef uint32_t NevercIRPassLevel;
#define NEVERC_IR_PASS_LEVEL_MODULE UINT32_C(1)
#define NEVERC_IR_PASS_LEVEL_CGSCC UINT32_C(2)
#define NEVERC_IR_PASS_LEVEL_FUNCTION UINT32_C(3)
#define NEVERC_IR_PASS_LEVEL_LOOP UINT32_C(4)

typedef uint32_t NevercIROptimizationLevel;
#define NEVERC_IR_OPTIMIZATION_O0 UINT32_C(0)
#define NEVERC_IR_OPTIMIZATION_O1 UINT32_C(1)
#define NEVERC_IR_OPTIMIZATION_O2 UINT32_C(2)
#define NEVERC_IR_OPTIMIZATION_O3 UINT32_C(3)
#define NEVERC_IR_OPTIMIZATION_OS UINT32_C(4)
#define NEVERC_IR_OPTIMIZATION_OZ UINT32_C(5)

typedef uint64_t NevercIRPreservedAnalysisFlags;
#define NEVERC_IR_PRESERVE_NONE UINT64_C(0)
#define NEVERC_IR_PRESERVE_CFG UINT64_C(1)
#define NEVERC_IR_PRESERVE_ALL (UINT64_C(1) << 63)

typedef struct NevercIRAnalysisAPI NevercIRAnalysisAPI;

typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;
  NevercIRValueHandle LoopHeader;
  const NevercIRValueHandle *SCCFunctions;
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;

typedef struct NevercIRPreservedAnalyses {
  NevercABITableHeader Header;
  NevercIRPreservedAnalysisFlags Flags;
  uint64_t Reserved[2];
  const NevercInterfaceID *CustomAnalyses;
  uint64_t CustomAnalysisCount;
} NevercIRPreservedAnalyses;

typedef NevercStatus(NEVERC_CALL *NevercIRPassRunFn)(
    const NevercIRPassInvocation *Invocation,
    NevercIRPreservedAnalyses *OutPreserved, void *UserData);

typedef NevercStatus(NEVERC_CALL *NevercIRAnalysisComputeFn)(
    const NevercIRPassInvocation *Invocation, void **OutResult,
    void *UserData);
typedef NevercStatus(NEVERC_CALL *NevercIRAnalysisQueryFn)(
    const void *Result, NevercByteView *OutData, void *UserData);
typedef uint32_t NevercIRAnalysisInvalidationReason;
#define NEVERC_IR_ANALYSIS_INVALIDATED_BY_PASS UINT32_C(1)
#define NEVERC_IR_ANALYSIS_INVALIDATED_BY_PLAN_DESTROY UINT32_C(2)
typedef NevercStatus(NEVERC_CALL *NevercIRAnalysisInvalidateFn)(
    void *Result, NevercIRAnalysisInvalidationReason Reason, void *UserData);
typedef void(NEVERC_CALL *NevercIRAnalysisDestroyFn)(void *Result,
                                                     void *UserData);

typedef struct NevercIRAnalysisDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID AnalysisID;
  NevercStringView Name;
  NevercIRPassLevel Level;
  uint32_t Reserved;
  const NevercInterfaceID *Dependencies;
  uint64_t DependencyCount;
  NevercIRAnalysisComputeFn Compute;
  NevercIRAnalysisQueryFn Query;
  NevercIRAnalysisInvalidateFn Invalidate;
  NevercIRAnalysisDestroyFn Destroy;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercIRAnalysisDescriptor;

typedef struct NevercIRPassDescriptor {
  NevercABITableHeader Header;
  NevercStringView PassID;
  NevercInterfaceID Phase;
  NevercIRPassLevel Level;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint8_t Reserved[2];
  NevercByteView ExternalDependencyDigest;
  const NevercInterfaceID *RequiredAnalyses;
  uint64_t RequiredAnalysisCount;
  NevercIRPassRunFn Run;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercIRPassDescriptor;

typedef struct NevercIRAnalysisAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *QueryBuiltin)(
      void *Context, NevercTaskHandle Task, NevercIRBuiltinAnalysis Analysis,
      NevercIRValueHandle Scope, NevercIRAnalysisResultHandle *OutResult);
  NevercStatus(NEVERC_CALL *DominatorTreeDominates)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Dominator,
      NevercIRValueHandle Dominated, NevercBool *OutDominates);
  NevercStatus(NEVERC_CALL *GetLoopCount)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetLoopHeader)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, uint64_t Index,
      NevercIRValueHandle *OutHeader);
  NevercStatus(NEVERC_CALL *GetLoopForBlock)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Block,
      NevercIRValueHandle *OutHeader, uint32_t *OutDepth);
  NevercStatus(NEVERC_CALL *GetScalarEvolutionConstantTripCount)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle LoopHeader,
      NevercBool *OutKnown, uint64_t *OutTripCount);
  NevercStatus(NEVERC_CALL *GetMemoryAccessKind)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Instruction,
      NevercIRMemoryAccessKind *OutKind);
  NevercStatus(NEVERC_CALL *GetDirectCalleeCount)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Function,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetDirectCallee)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Function,
      uint64_t Index, NevercIRValueHandle *OutCallee);
  NevercStatus(NEVERC_CALL *Alias)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Left,
      uint64_t LeftBytes, NevercIRValueHandle Right, uint64_t RightBytes,
      NevercIRAliasResult *OutAlias);
  NevercStatus(NEVERC_CALL *RegisterAnalysis)(
      void *Context, void *RegistrarContext,
      const NevercIRAnalysisDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *QueryCustom)(
      void *Context, NevercTaskHandle Task, NevercInterfaceID Analysis,
      NevercIRAnalysisResultHandle *OutResult);
  NevercStatus(NEVERC_CALL *GetCustomResultData)(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercByteView *OutData);
} NevercIRAnalysisAPI;

typedef struct NevercIRPassAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterPass)(
      void *Context, void *RegistrarContext,
      const NevercIRPassDescriptor *Descriptor);
} NevercIRPassAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NEVERC_PLUGIN_PLUGINIR_H */
