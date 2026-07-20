/*===-- PluginMC.h - NeverC machine-code plugin C ABI ------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINMC_H
#define NEVERC_PLUGIN_PLUGINMC_H

#include "neverc/Plugin/PluginMIR.h"    /* IWYU pragma: export */
#include "neverc/Plugin/PluginTarget.h" /* IWYU pragma: export */
#include "neverc/Plugin/Schema/PluginMCSchema.inc" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MC_API_MAJOR UINT16_C(1)
#define NEVERC_MC_API_MINOR UINT16_C(1)
#define NEVERC_INTERFACE_MC_HIGH UINT64_C(0x4e43504d43000001)
#define NEVERC_INTERFACE_MC_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MC_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_MC_EMISSION_API_MAJOR UINT16_C(1)
#define NEVERC_MC_EMISSION_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_MC_EMISSION_HIGH UINT64_C(0x4e43504d43454d01)
#define NEVERC_INTERFACE_MC_EMISSION_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MC_EMISSION_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_ARTIFACT_MC_UNIT_HIGH UINT64_C(0x4e434152544d4f01)
#define NEVERC_ARTIFACT_MC_UNIT_LOW UINT64_C(0x0000000000000001)
#define NEVERC_ARTIFACT_MC_ENCODED_HIGH UINT64_C(0x4e434152544d4f01)
#define NEVERC_ARTIFACT_MC_ENCODED_LOW UINT64_C(0x0000000000000003)
#define NEVERC_ARTIFACT_MC_LAYOUT_HIGH UINT64_C(0x4e434152544d4f01)
#define NEVERC_ARTIFACT_MC_LAYOUT_LOW UINT64_C(0x0000000000000004)
#define NEVERC_ARTIFACT_MC_INSTRUCTION_HIGH \
  UINT64_C(0x4e434152544d4f01)
#define NEVERC_ARTIFACT_MC_INSTRUCTION_LOW UINT64_C(0x000000000000000b)

#define NEVERC_MC_PROVIDER_API_MAJOR UINT16_C(1)
#define NEVERC_MC_PROVIDER_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_MC_PROVIDER_HIGH UINT64_C(0x4e43504d43505201)
#define NEVERC_INTERFACE_MC_PROVIDER_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MC_PROVIDER_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_ASSEMBLY_PROVIDER_API_MAJOR UINT16_C(1)
#define NEVERC_ASSEMBLY_PROVIDER_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_ASSEMBLY_PROVIDER_HIGH \
  UINT64_C(0x4e435041534d5001)
#define NEVERC_INTERFACE_ASSEMBLY_PROVIDER_LOW \
  UINT64_C(0x0000000000000001)
#define NEVERC_ASSEMBLY_PROVIDER_INTERFACE_STABILITY \
  NEVERC_INTERFACE_STABLE

typedef NevercHandle NevercMCUnitHandle;
typedef NevercHandle NevercMCSectionHandle;
typedef NevercHandle NevercMCFragmentHandle;
typedef NevercHandle NevercMCInstHandle;
typedef NevercHandle NevercMCOperandHandle;
typedef NevercHandle NevercMCExprHandle;
typedef NevercHandle NevercMCSymbolHandle;
typedef NevercHandle NevercMCFixupHandle;
typedef NevercHandle NevercMCBuilderHandle;
typedef NevercHandle NevercMCMutationHandle;
typedef NevercHandle NevercMCLayoutHandle;
typedef NevercHandle NevercMCSchemaTokenHandle;
typedef NevercHandle NevercAssemblySourceCursorHandle;

typedef uint32_t NevercMCOperandKind;
typedef uint32_t NevercMCExpressionKind;
typedef uint32_t NevercMCFragmentKind;
typedef uint32_t NevercMCFixupKind;
typedef uint32_t NevercMCLayoutState;

typedef uint64_t NevercMCSectionFlags;
#define NEVERC_MC_SECTION_ALLOCATED (UINT64_C(1) << 0)
#define NEVERC_MC_SECTION_EXECUTABLE (UINT64_C(1) << 1)
#define NEVERC_MC_SECTION_WRITABLE (UINT64_C(1) << 2)
#define NEVERC_MC_SECTION_MERGEABLE (UINT64_C(1) << 3)
#define NEVERC_MC_SECTION_DEBUG (UINT64_C(1) << 4)

typedef uint32_t NevercMCSymbolBinding;
#define NEVERC_MC_SYMBOL_BINDING_LOCAL UINT32_C(1)
#define NEVERC_MC_SYMBOL_BINDING_GLOBAL UINT32_C(2)
#define NEVERC_MC_SYMBOL_BINDING_WEAK UINT32_C(3)

typedef uint32_t NevercMCSymbolVisibility;
#define NEVERC_MC_SYMBOL_VISIBILITY_DEFAULT UINT32_C(1)
#define NEVERC_MC_SYMBOL_VISIBILITY_HIDDEN UINT32_C(2)
#define NEVERC_MC_SYMBOL_VISIBILITY_PROTECTED UINT32_C(3)

typedef uint32_t NevercMCSymbolType;
#define NEVERC_MC_SYMBOL_TYPE_NONE UINT32_C(0)
#define NEVERC_MC_SYMBOL_TYPE_FUNCTION UINT32_C(1)
#define NEVERC_MC_SYMBOL_TYPE_OBJECT UINT32_C(2)
#define NEVERC_MC_SYMBOL_TYPE_SECTION UINT32_C(3)
#define NEVERC_MC_SYMBOL_TYPE_TLS UINT32_C(4)

typedef uint32_t NevercMCSymbolDefinition;
#define NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED UINT32_C(1)
#define NEVERC_MC_SYMBOL_DEFINITION_SECTION UINT32_C(2)
#define NEVERC_MC_SYMBOL_DEFINITION_ABSOLUTE UINT32_C(3)
#define NEVERC_MC_SYMBOL_DEFINITION_COMMON UINT32_C(4)

typedef uint32_t NevercMCExpressionOperator;
#define NEVERC_MC_UNARY_PLUS UINT32_C(1)
#define NEVERC_MC_UNARY_MINUS UINT32_C(2)
#define NEVERC_MC_UNARY_NOT UINT32_C(3)
#define NEVERC_MC_BINARY_ADD UINT32_C(16)
#define NEVERC_MC_BINARY_SUBTRACT UINT32_C(17)
#define NEVERC_MC_BINARY_MULTIPLY UINT32_C(18)
#define NEVERC_MC_BINARY_DIVIDE UINT32_C(19)
#define NEVERC_MC_BINARY_AND UINT32_C(20)
#define NEVERC_MC_BINARY_OR UINT32_C(21)
#define NEVERC_MC_BINARY_XOR UINT32_C(22)
#define NEVERC_MC_BINARY_SHIFT_LEFT UINT32_C(23)
#define NEVERC_MC_BINARY_SHIFT_RIGHT UINT32_C(24)

#define NEVERC_MC_AUTOMATIC_OFFSET UINT64_MAX

NEVERC_ABI_PACK_BEGIN

typedef struct NevercMCSchemaValueDescriptor {
  NevercABITableHeader Header;
  uint32_t StableID;
  uint32_t BackendValue;
  NevercStringView CanonicalName;
  uint64_t Flags;
} NevercMCSchemaValueDescriptor;

typedef struct NevercMCSchemaDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID SchemaID;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringView Digest;
  NevercStructArrayView Opcodes;
  NevercStructArrayView Registers;
  NevercStructArrayView OperandKinds;
  NevercStructArrayView Relocations;
  NevercStructArrayView Variants;
  uint64_t Flags;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercMCSchemaDescriptor;

typedef struct NevercMCSchemaTokenInfo {
  NevercABITableHeader Header;
  NevercInterfaceID SchemaID;
  NevercTargetID TargetID;
  NevercStringView Digest;
  uint64_t UnitGeneration;
} NevercMCSchemaTokenInfo;

typedef struct NevercMCUnitInfo {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView TargetSchemaDigest;
  uint64_t Generation;
  uint64_t SectionCount;
  uint64_t SymbolCount;
  uint64_t ExpressionCount;
  uint64_t FragmentCount;
  uint64_t InstructionCount;
  uint64_t FixupCount;
} NevercMCUnitInfo;

typedef struct NevercMCSectionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  uint64_t Alignment;
  NevercMCSectionFlags Flags;
} NevercMCSectionDescriptor;

typedef NevercMCSectionDescriptor NevercMCSectionInfo;

typedef struct NevercMCSymbolDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercMCSymbolBinding Binding;
  NevercMCSymbolVisibility Visibility;
  NevercMCSymbolType Type;
  NevercMCSymbolDefinition Definition;
  NevercMCSectionHandle Section;
  uint64_t Value;
  uint64_t Size;
  uint64_t Alignment;
  uint64_t Flags;
} NevercMCSymbolDescriptor;

typedef NevercMCSymbolDescriptor NevercMCSymbolInfo;

typedef struct NevercMCExpressionDescriptor {
  NevercABITableHeader Header;
  NevercMCExpressionKind Kind;
  NevercMCExpressionOperator Operator;
  int64_t Constant;
  NevercMCSymbolHandle Symbol;
  NevercMCExprHandle Left;
  NevercMCExprHandle Right;
  NevercMCSchemaTokenHandle SchemaToken;
  uint32_t TargetVariant;
  uint32_t Reserved;
  NevercInterfaceID ExtensionOwner;
  NevercByteView Extension;
} NevercMCExpressionDescriptor;

typedef NevercMCExpressionDescriptor NevercMCExpressionInfo;

typedef struct NevercMCFragmentDescriptor {
  NevercABITableHeader Header;
  NevercMCFragmentKind Kind;
  uint32_t FillValue;
  NevercMCSchemaTokenHandle SchemaToken;
  NevercMCSectionHandle Section;
  uint64_t ExplicitOffset;
  uint64_t Alignment;
  NevercByteView Contents;
  NevercInterfaceID ExtensionOwner;
  NevercByteView Extension;
} NevercMCFragmentDescriptor;

typedef NevercMCFragmentDescriptor NevercMCFragmentInfo;

typedef struct NevercMCFixupDescriptor {
  NevercABITableHeader Header;
  NevercMCFragmentHandle Fragment;
  NevercMCExprHandle Expression;
  uint64_t Offset;
  uint32_t Width;
  NevercBool IsPCRelative;
  NevercBool IsSigned;
  NevercBool MayRelax;
  uint8_t Reserved8;
  NevercMCFixupKind Kind;
  NevercMCSchemaTokenHandle SchemaToken;
  uint32_t TargetKind;
  uint32_t Reserved;
} NevercMCFixupDescriptor;

typedef NevercMCFixupDescriptor NevercMCFixupInfo;

typedef struct NevercMCSourceLocation {
  NevercABITableHeader Header;
  uint64_t FileID;
  uint64_t ByteOffset;
  uint32_t Line;
  uint32_t Column;
} NevercMCSourceLocation;

typedef struct NevercMCLayoutStateInfo {
  NevercABITableHeader Header;
  NevercMCLayoutState State;
  uint32_t Reserved;
  uint64_t UnitGeneration;
  uint64_t TotalSize;
} NevercMCLayoutStateInfo;

struct NevercMCAPI;

typedef uint32_t NevercMCEmissionEventKind;
#define NEVERC_MC_EMISSION_UNIT_BEGIN UINT32_C(1)
#define NEVERC_MC_EMISSION_UNIT_END UINT32_C(2)
#define NEVERC_MC_EMISSION_SECTION_CHANGE UINT32_C(3)
#define NEVERC_MC_EMISSION_PRE_INSTRUCTION UINT32_C(4)
#define NEVERC_MC_EMISSION_POST_INSTRUCTION UINT32_C(5)
#define NEVERC_MC_EMISSION_POST_ENCODE UINT32_C(6)
#define NEVERC_MC_EMISSION_FIXUP UINT32_C(7)
#define NEVERC_MC_EMISSION_RELAXATION_ROUND UINT32_C(8)
#define NEVERC_MC_EMISSION_PRE_LAYOUT UINT32_C(9)
#define NEVERC_MC_EMISSION_POST_LAYOUT UINT32_C(10)
#define NEVERC_MC_EMISSION_PRE_OBJECT_WRITE UINT32_C(11)

typedef uint64_t NevercMCEmissionEventFlags;
#define NEVERC_MC_EMISSION_HAS_SECTION (UINT64_C(1) << 0)
#define NEVERC_MC_EMISSION_HAS_INSTRUCTION (UINT64_C(1) << 1)
#define NEVERC_MC_EMISSION_HAS_ENCODING (UINT64_C(1) << 2)
#define NEVERC_MC_EMISSION_HAS_FIXUP (UINT64_C(1) << 3)
#define NEVERC_MC_EMISSION_HAS_LAYOUT (UINT64_C(1) << 4)
#define NEVERC_MC_EMISSION_CAN_REPLACE_INSTRUCTION (UINT64_C(1) << 5)

typedef struct NevercMCEmissionSectionLayoutInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  uint64_t AddressSize;
  uint64_t FileSize;
  uint64_t FragmentCount;
} NevercMCEmissionSectionLayoutInfo;

typedef struct NevercMCEmissionFragmentLayoutInfo {
  NevercABITableHeader Header;
  uint64_t SectionIndex;
  uint64_t FragmentIndex;
  uint64_t Offset;
  uint64_t Size;
  NevercMCFragmentKind Kind;
  uint32_t Reserved;
} NevercMCEmissionFragmentLayoutInfo;

typedef struct NevercMCEmissionSymbolLayoutInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  uint64_t Value;
  NevercBool IsDefined;
  NevercBool IsResolved;
  uint8_t Reserved8[6];
} NevercMCEmissionSymbolLayoutInfo;

typedef struct NevercMCEmissionFixupLayoutInfo {
  NevercABITableHeader Header;
  uint64_t SectionIndex;
  uint64_t FragmentIndex;
  uint64_t Offset;
  uint64_t Value;
  uint32_t Kind;
  NevercBool IsResolved;
  uint8_t Reserved8[3];
} NevercMCEmissionFixupLayoutInfo;

typedef struct NevercMCEmissionEventInfo {
  NevercABITableHeader Header;
  NevercMCEmissionEventKind Kind;
  uint32_t RelaxationRound;
  NevercMCEmissionEventFlags Flags;
  const struct NevercMCAPI *MC;
  NevercMCUnitHandle Unit;
  NevercMCInstHandle Instruction;
  NevercStringView SectionName;
  NevercByteView EncodedBytes;
  NevercBool LayoutChanged;
  uint8_t Reserved8[7];
  NevercMCEmissionFixupLayoutInfo Fixup;
  uint64_t LayoutSectionCount;
  uint64_t LayoutFragmentCount;
  uint64_t LayoutSymbolCount;
  uint64_t LayoutFixupCount;
} NevercMCEmissionEventInfo;

typedef struct NevercMCInstructionInfo {
  NevercABITableHeader Header;
  NevercMCSchemaTokenHandle SchemaToken;
  uint32_t Opcode;
  uint32_t Flags;
  uint64_t OperandCount;
} NevercMCInstructionInfo;

typedef struct NevercMCOperandValue {
  NevercABITableHeader Header;
  NevercMCOperandKind Kind;
  uint32_t Reserved;
  NevercMCSchemaTokenHandle SchemaToken;
  union {
    uint32_t Register;
    int64_t Immediate;
    uint32_t SingleFloatBits;
    uint64_t DoubleFloatBits;
    NevercMCExprHandle Expression;
    NevercMCInstHandle Instruction;
    struct {
      uint32_t Kind;
      uint32_t Reserved;
      NevercByteView Payload;
    } TargetExtension;
  } Payload;
} NevercMCOperandValue;

typedef uint32_t NevercMCDecodeOutcome;
#define NEVERC_MC_DECODE_SUCCESS UINT32_C(1)
#define NEVERC_MC_DECODE_SOFT_FAIL UINT32_C(2)
#define NEVERC_MC_DECODE_UNKNOWN UINT32_C(3)
#define NEVERC_MC_DECODE_FAIL UINT32_C(4)

#define NEVERC_MC_NO_FIXUP_OPERAND UINT32_MAX

typedef struct NevercMCEncodedFixup {
  NevercABITableHeader Header;
  uint64_t Offset;
  uint32_t Width;
  NevercBool IsPCRelative;
  NevercBool IsSigned;
  NevercBool MayRelax;
  uint8_t Reserved8;
  NevercMCFixupKind Kind;
  NevercMCSchemaTokenHandle SchemaToken;
  uint32_t TargetKind;
  uint32_t OperandIndex;
  int64_t Addend;
} NevercMCEncodedFixup;

typedef struct NevercMCEncodeRequest {
  NevercABITableHeader Header;
  const struct NevercMCAPI *MC;
  NevercTaskHandle Task;
  NevercMCUnitHandle Unit;
  NevercMCInstHandle Instruction;
  NevercMCSchemaTokenHandle SchemaToken;
  uint64_t Address;
  NevercStringArrayView Features;
} NevercMCEncodeRequest;

typedef struct NevercMCEncodeSink {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *WriteBytes)(
      void *Context, NevercByteView Bytes);
  NevercStatus(NEVERC_CALL *AddFixup)(
      void *Context, const NevercMCEncodedFixup *Fixup);
} NevercMCEncodeSink;

typedef NevercStatus(NEVERC_CALL *NevercMCEncodeInstructionFn)(
    void *UserData, const NevercMCEncodeRequest *Request,
    const NevercMCEncodeSink *Sink);

typedef struct NevercMCDecodeRequest {
  NevercABITableHeader Header;
  const struct NevercMCAPI *MC;
  NevercTaskHandle Task;
  NevercMCUnitHandle Unit;
  NevercMCMutationHandle Mutation;
  NevercMCSchemaTokenHandle SchemaToken;
  NevercByteView Bytes;
  uint64_t Address;
  NevercStringArrayView Features;
} NevercMCDecodeRequest;

typedef struct NevercMCDecodeResult {
  NevercABITableHeader Header;
  NevercMCDecodeOutcome Outcome;
  uint32_t Reserved;
  uint64_t ConsumedBytes;
  NevercMCInstHandle Instruction;
} NevercMCDecodeResult;

typedef NevercStatus(NEVERC_CALL *NevercMCDecodeInstructionFn)(
    void *UserData, const NevercMCDecodeRequest *Request,
    NevercMCDecodeResult *OutResult);

typedef struct NevercMCEncoderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID ProviderID;
  NevercTargetID TargetID;
  NevercInterfaceID SchemaID;
  uint32_t MaximumInstructionLength;
  uint32_t Reserved;
  uint64_t Flags;
  NevercMCEncodeInstructionFn EncodeInstruction;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercMCEncoderDescriptor;

typedef struct NevercMCDecoderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID ProviderID;
  NevercTargetID TargetID;
  NevercInterfaceID SchemaID;
  uint32_t MaximumInstructionLength;
  uint32_t Reserved;
  uint64_t Flags;
  NevercMCDecodeInstructionFn DecodeInstruction;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercMCDecoderDescriptor;

typedef struct NevercMutableByteView {
  uint8_t *Data;
  uint64_t Length;
} NevercMutableByteView;

typedef struct NevercMCByteSink {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *WriteBytes)(
      void *Context, NevercByteView Bytes);
} NevercMCByteSink;

typedef uint64_t NevercMCFixupInfoFlags;
#define NEVERC_MC_FIXUP_INFO_PC_RELATIVE (UINT64_C(1) << 0)
#define NEVERC_MC_FIXUP_INFO_SIGNED (UINT64_C(1) << 1)
#define NEVERC_MC_FIXUP_INFO_RELAXABLE (UINT64_C(1) << 2)
#define NEVERC_MC_FIXUP_INFO_TARGET (UINT64_C(1) << 3)

typedef struct NevercMCFixupKindInfo {
  NevercABITableHeader Header;
  uint32_t TargetOffset;
  uint32_t TargetSize;
  NevercMCFixupInfoFlags Flags;
  uint64_t Reserved;
} NevercMCFixupKindInfo;

typedef struct NevercMCLayoutFixupRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercMCFixupKind Kind;
  uint32_t TargetKind;
  uint64_t FixupOffset;
  uint64_t FragmentOffset;
  uint64_t FragmentSize;
  uint64_t Place;
  uint32_t Width;
  NevercBool IsPCRelative;
  NevercBool IsSigned;
  NevercBool MayRelax;
  NevercBool IsResolved;
  int64_t Value;
  NevercStringView SymbolName;
} NevercMCLayoutFixupRequest;

typedef struct NevercMCRelaxationResult {
  NevercABITableHeader Header;
  NevercBool Changed;
  uint8_t Reserved8[7];
  NevercByteView ReplacementBytes;
  uint64_t NewFixupOffset;
  uint32_t NewFixupWidth;
  NevercMCFixupKind NewFixupKind;
  uint32_t NewTargetKind;
  uint32_t Reserved;
} NevercMCRelaxationResult;

typedef NevercStatus(NEVERC_CALL *NevercMCGetFixupKindInfoFn)(
    void *UserData, NevercMCFixupKind Kind, uint32_t TargetKind,
    NevercMCFixupKindInfo *OutInfo);
typedef NevercStatus(NEVERC_CALL *NevercMCMapRelocationFn)(
    void *UserData, const NevercMCLayoutFixupRequest *Request,
    uint32_t *OutRelocationKind);
typedef NevercStatus(NEVERC_CALL *NevercMCShouldRelaxFixupFn)(
    void *UserData, const NevercMCLayoutFixupRequest *Request,
    NevercBool *OutRelax);
typedef NevercStatus(NEVERC_CALL *NevercMCRelaxFragmentFn)(
    void *UserData, const NevercMCLayoutFixupRequest *Request,
    NevercMCRelaxationResult *OutResult);
typedef NevercStatus(NEVERC_CALL *NevercMCApplyFixupFn)(
    void *UserData, const NevercMCLayoutFixupRequest *Request,
    NevercMutableByteView Bytes);
typedef NevercStatus(NEVERC_CALL *NevercMCWriteNopsFn)(
    void *UserData, uint64_t Count, const NevercMCByteSink *Sink);

typedef struct NevercMCAsmBackendDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID ProviderID;
  NevercTargetID TargetID;
  NevercInterfaceID SchemaID;
  uint32_t MaximumLayoutIterations;
  uint32_t MinimumInstructionAlignment;
  uint64_t Flags;
  NevercMCGetFixupKindInfoFn GetFixupKindInfo;
  NevercMCMapRelocationFn MapRelocation;
  NevercMCShouldRelaxFixupFn ShouldRelaxFixup;
  NevercMCRelaxFragmentFn RelaxFragment;
  NevercMCApplyFixupFn ApplyFixup;
  NevercMCWriteNopsFn WriteNops;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercMCAsmBackendDescriptor;

typedef uint32_t NevercAssemblySourceRepresentation;
#define NEVERC_ASSEMBLY_SOURCE_BUFFER UINT32_C(1)
#define NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS UINT32_C(2)

typedef struct NevercAssemblySourceInfo {
  NevercABITableHeader Header;
  NevercAssemblySourceRepresentation Representation;
  NevercBool Preprocessed;
  uint8_t Reserved8[3];
  NevercStringView Identifier;
  NevercStringView Buffer;
  NevercAssemblySourceCursorHandle Cursor;
  uint64_t Generation;
} NevercAssemblySourceInfo;

typedef struct NevercAssemblyTokenInfo {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  uint64_t FileID;
  uint64_t ByteOffset;
  uint32_t Line;
  uint32_t Column;
  NevercBool StartOfLine;
  NevercBool LeadingSpace;
  uint8_t Reserved8[6];
} NevercAssemblyTokenInfo;

typedef struct NevercAssemblyParseInputInfo {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercInterfaceID SchemaID;
  NevercStringView TargetSchemaDigest;
  NevercAssemblySourceInfo Source;
} NevercAssemblyParseInputInfo;

typedef struct NevercAssemblyPrintInputInfo {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercInterfaceID SchemaID;
  NevercStringView TargetSchemaDigest;
  uint64_t UnitGeneration;
} NevercAssemblyPrintInputInfo;

typedef struct NevercAssemblyOutputMetadata {
  NevercABITableHeader Header;
  NevercStringView Syntax;
  NevercStringView Comment;
  uint64_t Flags;
} NevercAssemblyOutputMetadata;

typedef struct NevercAssemblyProviderAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetParseInput)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercAssemblyParseInputInfo *OutInfo);
  NevercStatus(NEVERC_CALL *PeekSourceToken)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercAssemblySourceCursorHandle Cursor,
      NevercAssemblyTokenInfo *OutToken);
  NevercStatus(NEVERC_CALL *AdvanceSourceToken)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercAssemblySourceCursorHandle Cursor);
  NevercStatus(NEVERC_CALL *GetParseMCBuilder)(
      void *Context, const NevercPhaseFrame *Frame,
      const struct NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit);
  NevercStatus(NEVERC_CALL *PublishParsedMCUnit)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle *OutUnit);
  NevercStatus(NEVERC_CALL *GetPrintInput)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercAssemblyPrintInputInfo *OutInfo,
      const struct NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit);
  NevercStatus(NEVERC_CALL *WritePrintOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercStringView Text);
  NevercStatus(NEVERC_CALL *PublishAssemblyOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercAssemblyOutputMetadata *Metadata,
      NevercArtifactHandle *OutOutput);
} NevercAssemblyProviderAPI;

typedef struct NevercMCEmissionAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetEvent)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercMCEmissionEventInfo *OutInfo);
  NevercStatus(NEVERC_CALL *BeginInstructionReplacement)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      const struct NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit,
      NevercMCInstHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *PublishInstructionReplacement)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      NevercArtifactHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetLayoutSection)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionSectionLayoutInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetLayoutFragment)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionFragmentLayoutInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetLayoutSymbol)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionSymbolLayoutInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetLayoutFixup)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionFixupLayoutInfo *OutInfo);
} NevercMCEmissionAPI;

typedef struct NevercMCAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterSchema)(
      void *Context, void *RegistrarContext,
      const NevercMCSchemaDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *GetSchemaToken)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCSchemaTokenHandle *OutToken);
  NevercStatus(NEVERC_CALL *GetSchemaTokenInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMCSchemaTokenHandle Token, NevercMCSchemaTokenInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetUnitInfo)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCUnitInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstSection)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCSectionHandle *OutSection);
  NevercStatus(NEVERC_CALL *GetNextSection)(
      void *Context, NevercTaskHandle Task, NevercMCSectionHandle Section,
      NevercMCSectionHandle *OutSection);
  NevercStatus(NEVERC_CALL *GetSectionInfo)(
      void *Context, NevercTaskHandle Task, NevercMCSectionHandle Section,
      NevercMCSectionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstSymbol)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCSymbolHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *GetNextSymbol)(
      void *Context, NevercTaskHandle Task, NevercMCSymbolHandle Symbol,
      NevercMCSymbolHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *GetSymbolInfo)(
      void *Context, NevercTaskHandle Task, NevercMCSymbolHandle Symbol,
      NevercMCSymbolInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetExpressionInfo)(
      void *Context, NevercTaskHandle Task, NevercMCExprHandle Expression,
      NevercMCExpressionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstFragment)(
      void *Context, NevercTaskHandle Task, NevercMCSectionHandle Section,
      NevercMCFragmentHandle *OutFragment);
  NevercStatus(NEVERC_CALL *GetNextFragment)(
      void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
      NevercMCFragmentHandle *OutFragment);
  NevercStatus(NEVERC_CALL *GetFragmentInfo)(
      void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
      NevercMCFragmentInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstFragmentInstruction)(
      void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
      NevercMCInstHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetFirstFixup)(
      void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
      NevercMCFixupHandle *OutFixup);
  NevercStatus(NEVERC_CALL *GetNextFixup)(
      void *Context, NevercTaskHandle Task, NevercMCFixupHandle Fixup,
      NevercMCFixupHandle *OutFixup);
  NevercStatus(NEVERC_CALL *GetFixupInfo)(
      void *Context, NevercTaskHandle Task, NevercMCFixupHandle Fixup,
      NevercMCFixupInfo *OutInfo);
  NevercStatus(NEVERC_CALL *BeginMutation)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *CommitMutation)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbandonMutation)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *CreateSection)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      const NevercMCSectionDescriptor *Descriptor,
      NevercMCSectionHandle *OutSection);
  NevercStatus(NEVERC_CALL *MoveSectionBefore)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCSectionHandle Section,
      NevercMCSectionHandle Position);
  NevercStatus(NEVERC_CALL *EraseSection)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCSectionHandle Section);
  NevercStatus(NEVERC_CALL *CreateSymbol)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      const NevercMCSymbolDescriptor *Descriptor,
      NevercMCSymbolHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *MoveSymbolBefore)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCSymbolHandle Symbol,
      NevercMCSymbolHandle Position);
  NevercStatus(NEVERC_CALL *EraseSymbol)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCSymbolHandle Symbol);
  NevercStatus(NEVERC_CALL *CreateExpression)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      const NevercMCExpressionDescriptor *Descriptor,
      NevercMCExprHandle *OutExpression);
  NevercStatus(NEVERC_CALL *SetExpressionOperands)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCExprHandle Expression,
      NevercMCExprHandle Left, NevercMCExprHandle Right);
  NevercStatus(NEVERC_CALL *EraseExpression)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCExprHandle Expression);
  NevercStatus(NEVERC_CALL *CreateFragment)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCSectionHandle Section,
      const NevercMCFragmentDescriptor *Descriptor,
      NevercMCFragmentHandle *OutFragment);
  NevercStatus(NEVERC_CALL *MoveFragmentBefore)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
      NevercMCFragmentHandle Position);
  NevercStatus(NEVERC_CALL *EraseFragment)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment);
  NevercStatus(NEVERC_CALL *CreateFixup)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
      const NevercMCFixupDescriptor *Descriptor,
      NevercMCFixupHandle *OutFixup);
  NevercStatus(NEVERC_CALL *MoveFixupBefore)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCFixupHandle Fixup,
      NevercMCFixupHandle Position);
  NevercStatus(NEVERC_CALL *EraseFixup)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCFixupHandle Fixup);
  NevercStatus(NEVERC_CALL *GetFirstInstruction)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCInstHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetNextInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMCInstHandle Instruction,
      NevercMCInstHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetInstructionInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMCInstHandle Instruction,
      NevercMCInstructionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetInstructionOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMCInstHandle Instruction, uint64_t Index,
      NevercMCOperandHandle *OutOperand);
  NevercStatus(NEVERC_CALL *GetOperandValue)(
      void *Context, NevercTaskHandle Task,
      NevercMCOperandHandle Operand,
      NevercMCOperandValue *OutValue);
  NevercStatus(NEVERC_CALL *CreateInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCSchemaTokenHandle SchemaToken, uint32_t Opcode,
      NevercMCInstHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *AppendOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Instruction,
      const NevercMCOperandValue *Value);
  NevercStatus(NEVERC_CALL *InsertOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Instruction, uint64_t Index,
      const NevercMCOperandValue *Value,
      NevercMCOperandHandle *OutOperand);
  NevercStatus(NEVERC_CALL *EraseOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Instruction, uint64_t Index);
  NevercStatus(NEVERC_CALL *InsertInstructionBefore)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Position,
      NevercMCInstHandle Instruction);
  NevercStatus(NEVERC_CALL *AppendInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCUnitHandle Unit,
      NevercMCInstHandle Instruction);
  NevercStatus(NEVERC_CALL *AppendInstructionToFragment)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
      NevercMCInstHandle Instruction);
  NevercStatus(NEVERC_CALL *ReplaceInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Instruction,
      NevercMCInstHandle Replacement);
  NevercStatus(NEVERC_CALL *EraseInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Instruction);
  NevercStatus(NEVERC_CALL *RegisterEncoder)(
      void *Context, void *RegistrarContext,
      const NevercMCEncoderDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterDecoder)(
      void *Context, void *RegistrarContext,
      const NevercMCDecoderDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterAsmBackend)(
      void *Context, void *RegistrarContext,
      const NevercMCAsmBackendDescriptor *Descriptor);
} NevercMCAPI;

typedef struct NevercMIRToMCInputInfo {
  NevercABITableHeader Header;
  NevercMIRModuleHandle Module;
  NevercTargetID TargetID;
  NevercStringView CompatibilityKey;
  NevercStringView TargetSchemaDigest;
  uint64_t DefinedFunctionCount;
} NevercMIRToMCInputInfo;

typedef struct NevercMCProviderAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetMIRToMCInput)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercMIRToMCInputInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetMachineFunction)(
      void *Context, const NevercPhaseFrame *Frame, uint64_t Index,
      const NevercMIRAPI **OutMIR,
      NevercMachineFunctionHandle *OutFunction);
  NevercStatus(NEVERC_CALL *GetMCBuilder)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit);
  NevercStatus(NEVERC_CALL *PublishMCUnit)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle *OutUnit);
} NevercMCProviderAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINMC_H */
