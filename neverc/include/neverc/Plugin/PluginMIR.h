/*===-- PluginMIR.h - NeverC machine IR plugin C ABI -------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINMIR_H
#define NEVERC_PLUGIN_PLUGINMIR_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MIR_API_MAJOR UINT16_C(1)
#define NEVERC_MIR_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_MIR_HIGH UINT64_C(0x4e43504d49520001)
#define NEVERC_INTERFACE_MIR_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MIR_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_MIR_ANALYSIS_API_MAJOR UINT16_C(1)
#define NEVERC_MIR_ANALYSIS_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_MIR_ANALYSIS_HIGH UINT64_C(0x4e43504d49524101)
#define NEVERC_INTERFACE_MIR_ANALYSIS_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MIR_ANALYSIS_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_MIR_PASS_API_MAJOR UINT16_C(1)
#define NEVERC_MIR_PASS_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_MIR_PASS_HIGH UINT64_C(0x4e43504d49525001)
#define NEVERC_INTERFACE_MIR_PASS_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MIR_PASS_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

typedef NevercHandle NevercMIRModuleHandle;
typedef NevercHandle NevercMachineFunctionHandle;
typedef NevercHandle NevercMachineBasicBlockHandle;
typedef NevercHandle NevercMachineInstrHandle;
typedef NevercHandle NevercMachineOperandHandle;
typedef NevercHandle NevercMIRBuilderHandle;
typedef NevercHandle NevercMIRMutationHandle;
typedef NevercHandle NevercMIRAnalysisResultHandle;
typedef NevercHandle NevercMIRReferenceHandle;
typedef NevercHandle NevercMachineMemOperandHandle;

typedef uint32_t NevercMIREntityKind;
typedef uint32_t NevercMIROperandKind;
typedef uint32_t NevercMIRGenericOpcode;
typedef uint32_t NevercMIRMachineProperty;
typedef uint32_t NevercMIRLowLevelTypeKind;
typedef uint32_t NevercMIRRegisterAssignmentKind;
typedef uint32_t NevercMIRFrameObjectFlags;
typedef uint32_t NevercMIRConstantKind;
typedef uint32_t NevercMIRJumpTableEntryKind;
typedef uint32_t NevercMIRMemoryPointerKind;
typedef uint32_t NevercMIRAtomicOrdering;
typedef uint32_t NevercMIRBuiltinAnalysis;
typedef uint32_t NevercMIRPassLevel;
typedef uint32_t NevercMIRPropertyProofKind;
typedef uint64_t NevercMIRInstructionFlags;
typedef uint64_t NevercMIRRegisterFlags;
typedef uint64_t NevercMIRMemoryFlags;
typedef uint64_t NevercMIRPreservedAnalysisFlags;

#define NEVERC_MIR_INSTR_FLAG_FRAME_SETUP (UINT64_C(1) << 0)
#define NEVERC_MIR_INSTR_FLAG_FRAME_DESTROY (UINT64_C(1) << 1)
#define NEVERC_MIR_INSTR_FLAG_BUNDLED_PRED (UINT64_C(1) << 2)
#define NEVERC_MIR_INSTR_FLAG_BUNDLED_SUCC (UINT64_C(1) << 3)
#define NEVERC_MIR_INSTR_FLAG_FM_NO_NANS (UINT64_C(1) << 4)
#define NEVERC_MIR_INSTR_FLAG_FM_NO_INFS (UINT64_C(1) << 5)
#define NEVERC_MIR_INSTR_FLAG_FM_NSZ (UINT64_C(1) << 6)
#define NEVERC_MIR_INSTR_FLAG_FM_ARCP (UINT64_C(1) << 7)
#define NEVERC_MIR_INSTR_FLAG_FM_CONTRACT (UINT64_C(1) << 8)
#define NEVERC_MIR_INSTR_FLAG_FM_AFN (UINT64_C(1) << 9)
#define NEVERC_MIR_INSTR_FLAG_FM_REASSOC (UINT64_C(1) << 10)
#define NEVERC_MIR_INSTR_FLAG_NO_U_WRAP (UINT64_C(1) << 11)
#define NEVERC_MIR_INSTR_FLAG_NO_S_WRAP (UINT64_C(1) << 12)
#define NEVERC_MIR_INSTR_FLAG_EXACT (UINT64_C(1) << 13)
#define NEVERC_MIR_INSTR_FLAG_NO_FP_EXCEPT (UINT64_C(1) << 14)
#define NEVERC_MIR_INSTR_FLAG_NO_MERGE (UINT64_C(1) << 15)
#define NEVERC_MIR_INSTR_FLAG_UNPREDICTABLE (UINT64_C(1) << 16)
#define NEVERC_MIR_INSTR_FLAG_NO_CONVERGENT (UINT64_C(1) << 17)

#define NEVERC_MIR_REG_FLAG_DEF (UINT64_C(1) << 0)
#define NEVERC_MIR_REG_FLAG_IMPLICIT (UINT64_C(1) << 1)
#define NEVERC_MIR_REG_FLAG_KILL (UINT64_C(1) << 2)
#define NEVERC_MIR_REG_FLAG_DEAD (UINT64_C(1) << 3)
#define NEVERC_MIR_REG_FLAG_UNDEF (UINT64_C(1) << 4)
#define NEVERC_MIR_REG_FLAG_EARLY_CLOBBER (UINT64_C(1) << 5)
#define NEVERC_MIR_REG_FLAG_RENAMABLE (UINT64_C(1) << 6)
#define NEVERC_MIR_REG_FLAG_INTERNAL_READ (UINT64_C(1) << 7)
#define NEVERC_MIR_REG_FLAG_DEBUG (UINT64_C(1) << 8)

#define NEVERC_MIR_FLOAT_SEMANTICS_IEEE_HALF UINT32_C(1)
#define NEVERC_MIR_FLOAT_SEMANTICS_BFLOAT UINT32_C(2)
#define NEVERC_MIR_FLOAT_SEMANTICS_IEEE_SINGLE UINT32_C(3)
#define NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE UINT32_C(4)
#define NEVERC_MIR_FLOAT_SEMANTICS_X87_DOUBLE_EXTENDED UINT32_C(5)
#define NEVERC_MIR_FLOAT_SEMANTICS_IEEE_QUAD UINT32_C(6)
#define NEVERC_MIR_FLOAT_SEMANTICS_PPC_DOUBLE_DOUBLE UINT32_C(7)

#define NEVERC_MIR_LLT_INVALID UINT32_C(0)
#define NEVERC_MIR_LLT_SCALAR UINT32_C(1)
#define NEVERC_MIR_LLT_POINTER UINT32_C(2)
#define NEVERC_MIR_LLT_VECTOR UINT32_C(3)
#define NEVERC_MIR_LLT_POINTER_VECTOR UINT32_C(4)

#define NEVERC_MIR_REG_ASSIGNMENT_NONE UINT32_C(0)
#define NEVERC_MIR_REG_ASSIGNMENT_GENERIC UINT32_C(1)
#define NEVERC_MIR_REG_ASSIGNMENT_CLASS UINT32_C(2)
#define NEVERC_MIR_REG_ASSIGNMENT_BANK UINT32_C(3)

#define NEVERC_MIR_FRAME_FIXED (UINT32_C(1) << 0)
#define NEVERC_MIR_FRAME_SPILL_SLOT (UINT32_C(1) << 1)
#define NEVERC_MIR_FRAME_VARIABLE_SIZED (UINT32_C(1) << 2)
#define NEVERC_MIR_FRAME_IMMUTABLE (UINT32_C(1) << 3)
#define NEVERC_MIR_FRAME_ALIASED (UINT32_C(1) << 4)
#define NEVERC_MIR_FRAME_DEAD (UINT32_C(1) << 5)
#define NEVERC_MIR_FRAME_PREALLOCATED (UINT32_C(1) << 6)

#define NEVERC_MIR_CONSTANT_UNKNOWN UINT32_C(0)
#define NEVERC_MIR_CONSTANT_INTEGER UINT32_C(1)
#define NEVERC_MIR_CONSTANT_FLOAT UINT32_C(2)

#define NEVERC_MIR_JT_BLOCK_ADDRESS UINT32_C(1)
#define NEVERC_MIR_JT_GP_REL64_BLOCK_ADDRESS UINT32_C(2)
#define NEVERC_MIR_JT_GP_REL32_BLOCK_ADDRESS UINT32_C(3)
#define NEVERC_MIR_JT_LABEL_DIFFERENCE32 UINT32_C(4)
#define NEVERC_MIR_JT_LABEL_DIFFERENCE64 UINT32_C(5)
#define NEVERC_MIR_JT_INLINE UINT32_C(6)
#define NEVERC_MIR_JT_CUSTOM32 UINT32_C(7)

#define NEVERC_MIR_MEMORY_LOAD (UINT64_C(1) << 0)
#define NEVERC_MIR_MEMORY_STORE (UINT64_C(1) << 1)
#define NEVERC_MIR_MEMORY_VOLATILE (UINT64_C(1) << 2)
#define NEVERC_MIR_MEMORY_NON_TEMPORAL (UINT64_C(1) << 3)
#define NEVERC_MIR_MEMORY_DEREFERENCEABLE (UINT64_C(1) << 4)
#define NEVERC_MIR_MEMORY_INVARIANT (UINT64_C(1) << 5)
#define NEVERC_MIR_MEMORY_TARGET_FLAG1 (UINT64_C(1) << 16)
#define NEVERC_MIR_MEMORY_TARGET_FLAG2 (UINT64_C(1) << 17)
#define NEVERC_MIR_MEMORY_TARGET_FLAG3 (UINT64_C(1) << 18)

#define NEVERC_MIR_MEMORY_POINTER_UNKNOWN UINT32_C(0)
#define NEVERC_MIR_MEMORY_POINTER_IR_VALUE UINT32_C(1)
#define NEVERC_MIR_MEMORY_POINTER_FIXED_STACK UINT32_C(2)
#define NEVERC_MIR_MEMORY_POINTER_STACK UINT32_C(3)
#define NEVERC_MIR_MEMORY_POINTER_CONSTANT_POOL UINT32_C(4)
#define NEVERC_MIR_MEMORY_POINTER_JUMP_TABLE UINT32_C(5)
#define NEVERC_MIR_MEMORY_POINTER_GOT UINT32_C(6)
#define NEVERC_MIR_MEMORY_POINTER_UNKNOWN_STACK UINT32_C(7)
#define NEVERC_MIR_MEMORY_POINTER_TARGET_CUSTOM UINT32_C(8)

#define NEVERC_MIR_ATOMIC_NOT_ATOMIC UINT32_C(0)
#define NEVERC_MIR_ATOMIC_UNORDERED UINT32_C(1)
#define NEVERC_MIR_ATOMIC_MONOTONIC UINT32_C(2)
#define NEVERC_MIR_ATOMIC_ACQUIRE UINT32_C(3)
#define NEVERC_MIR_ATOMIC_RELEASE UINT32_C(4)
#define NEVERC_MIR_ATOMIC_ACQUIRE_RELEASE UINT32_C(5)
#define NEVERC_MIR_ATOMIC_SEQUENTIALLY_CONSISTENT UINT32_C(6)

#define NEVERC_MIR_ANALYSIS_LIVE_INTERVALS UINT32_C(1)
#define NEVERC_MIR_ANALYSIS_LIVE_VARIABLES UINT32_C(2)
#define NEVERC_MIR_ANALYSIS_SLOT_INDEXES UINT32_C(3)
#define NEVERC_MIR_ANALYSIS_DOMINATOR_TREE UINT32_C(4)
#define NEVERC_MIR_ANALYSIS_LOOP_INFO UINT32_C(5)
#define NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE UINT32_C(6)

#define NEVERC_MIR_PASS_LEVEL_MODULE UINT32_C(1)
#define NEVERC_MIR_PASS_LEVEL_FUNCTION UINT32_C(2)
#define NEVERC_MIR_PASS_LEVEL_BASIC_BLOCK UINT32_C(3)

#define NEVERC_MIR_PRESERVE_NONE UINT64_C(0)
#define NEVERC_MIR_PRESERVE_CFG UINT64_C(1)
#define NEVERC_MIR_PRESERVE_ALL (UINT64_C(1) << 63)

#define NEVERC_MIR_PROPERTY_PROOF_INVALIDATION UINT32_C(1)
#define NEVERC_MIR_PROPERTY_PROOF_STRUCTURAL_CHECK UINT32_C(2)

#include "neverc/Plugin/Schema/PluginMIRSchema.inc"

NEVERC_ABI_PACK_BEGIN

typedef struct NevercMIRSchemaEntry {
  NevercABITableHeader Header;
  uint32_t StableID;
  uint32_t LLVMValue;
  NevercBool RequiresTargetSchema;
  uint8_t Reserved[3];
  NevercStringView CanonicalName;
} NevercMIRSchemaEntry;

typedef struct NevercMIRInstructionInfo {
  NevercABITableHeader Header;
  NevercMIRGenericOpcode StableOpcode;
  uint32_t TargetOpcode;
  NevercBool RequiresTargetSchema;
  NevercBool IsBranch;
  NevercBool IsCall;
  NevercBool IsReturn;
  NevercBool IsTerminator;
  NevercBool IsBarrier;
  NevercBool IsInlineAssembly;
  NevercBool IsDebugInstruction;
  NevercBool IsPseudo;
  NevercBool IsBundle;
  uint8_t Reserved[2];
  NevercMIRInstructionFlags Flags;
  uint64_t OperandCount;
  uint64_t MemoryOperandCount;
} NevercMIRInstructionInfo;

typedef struct NevercMIRInstructionOpcode {
  NevercMIRGenericOpcode StableOpcode;
  uint32_t TargetOpcode;
  NevercBool RequiresTargetSchema;
  uint8_t Reserved[7];
} NevercMIRInstructionOpcode;

typedef struct NevercMIRDebugLocation {
  NevercABITableHeader Header;
  uint32_t Line;
  uint32_t Column;
  uint32_t Discriminator;
  NevercBool IsImplicitCode;
  uint8_t Reserved[3];
  NevercStringView Directory;
  NevercStringView Filename;
} NevercMIRDebugLocation;

typedef struct NevercMIRRegisterValue {
  uint32_t Number;
  uint32_t SubRegister;
  NevercMIRRegisterFlags Flags;
  NevercBool IsPhysical;
  NevercBool RequiresTargetSchema;
  uint8_t Reserved[6];
} NevercMIRRegisterValue;

typedef struct NevercMIRIndexOffsetValue {
  int32_t Index;
  uint32_t Reserved;
  int64_t Offset;
} NevercMIRIndexOffsetValue;

typedef struct NevercMIRReferenceOffsetValue {
  NevercMIRReferenceHandle Reference;
  int64_t Offset;
} NevercMIRReferenceOffsetValue;

typedef struct NevercMIRSymbolOffsetValue {
  NevercStringView Symbol;
  int64_t Offset;
} NevercMIRSymbolOffsetValue;

typedef struct NevercMIRWordView {
  const uint64_t *Data;
  uint64_t Count;
  uint32_t BitWidth;
  uint32_t Semantics;
} NevercMIRWordView;

typedef struct NevercMIRRegisterMaskView {
  const uint32_t *Data;
  uint64_t Count;
} NevercMIRRegisterMaskView;

typedef struct NevercMIRShuffleMaskView {
  const int32_t *Data;
  uint64_t Count;
} NevercMIRShuffleMaskView;

typedef struct NevercMIRDebugInstructionReference {
  uint32_t InstructionIndex;
  uint32_t OperandIndex;
} NevercMIRDebugInstructionReference;

typedef union NevercMIROperandPayload {
  NevercMIRRegisterValue Register;
  int64_t Immediate;
  NevercMIRWordView Constant;
  NevercMachineBasicBlockHandle BasicBlock;
  NevercMIRIndexOffsetValue IndexOffset;
  NevercMIRReferenceOffsetValue ReferenceOffset;
  NevercMIRSymbolOffsetValue SymbolOffset;
  NevercMIRRegisterMaskView RegisterMask;
  NevercMIRReferenceHandle Reference;
  uint32_t UnsignedValue;
  NevercMIRShuffleMaskView ShuffleMask;
  NevercMIRDebugInstructionReference DebugInstructionReference;
} NevercMIROperandPayload;

typedef struct NevercMIROperandValue {
  NevercABITableHeader Header;
  NevercMIROperandKind Kind;
  uint32_t TargetFlags;
  NevercMIROperandPayload Payload;
} NevercMIROperandValue;

typedef struct NevercMIRCFGEdge {
  NevercMachineBasicBlockHandle Block;
  uint32_t ProbabilityNumerator;
  uint32_t ProbabilityDenominator;
} NevercMIRCFGEdge;

typedef struct NevercMIRLiveIn {
  uint32_t Register;
  uint32_t Reserved;
  uint64_t LaneMask;
} NevercMIRLiveIn;

typedef struct NevercMIRLowLevelType {
  NevercMIRLowLevelTypeKind Kind;
  uint32_t ScalarSizeInBits;
  uint32_t ElementCount;
  uint32_t AddressSpace;
  NevercBool IsScalable;
  uint8_t Reserved[7];
} NevercMIRLowLevelType;

typedef struct NevercMIRVirtualRegisterDesc {
  NevercABITableHeader Header;
  NevercMIRRegisterAssignmentKind AssignmentKind;
  uint32_t TargetID;
  NevercMIRLowLevelType Type;
} NevercMIRVirtualRegisterDesc;

typedef struct NevercMIRRegisterInfo {
  NevercABITableHeader Header;
  uint32_t Number;
  NevercMIRRegisterAssignmentKind AssignmentKind;
  uint32_t TargetID;
  NevercBool IsPhysical;
  NevercBool RequiresTargetSchema;
  uint8_t Reserved[6];
  NevercMIRLowLevelType Type;
} NevercMIRRegisterInfo;

typedef struct NevercMIRFunctionLiveIn {
  uint32_t PhysicalRegister;
  uint32_t VirtualRegister;
} NevercMIRFunctionLiveIn;

typedef struct NevercMIRFrameObjectInfo {
  NevercABITableHeader Header;
  int32_t Index;
  NevercMIRFrameObjectFlags Flags;
  int64_t Size;
  int64_t Offset;
  uint64_t Alignment;
  uint32_t StackID;
  uint32_t Reserved;
} NevercMIRFrameObjectInfo;

typedef struct NevercMIRCalleeSavedInfo {
  uint32_t Register;
  int32_t FrameIndex;
  uint32_t DestinationRegister;
  NevercBool IsSpilledToRegister;
  NevercBool IsRestored;
  uint8_t Reserved[2];
} NevercMIRCalleeSavedInfo;

typedef struct NevercMIRConstantPoolEntryInfo {
  NevercABITableHeader Header;
  uint32_t Index;
  NevercMIRConstantKind Kind;
  uint64_t Alignment;
  uint64_t Size;
  NevercBool IsMachineSpecific;
  uint8_t Reserved[7];
  NevercMIRWordView Value;
} NevercMIRConstantPoolEntryInfo;

typedef struct NevercMIRConstantPoolEntryDesc {
  NevercABITableHeader Header;
  NevercMIRConstantKind Kind;
  uint32_t Reserved;
  uint64_t Alignment;
  NevercMIRWordView Value;
} NevercMIRConstantPoolEntryDesc;

typedef struct NevercMIRJumpTableInfo {
  NevercABITableHeader Header;
  uint32_t Index;
  NevercMIRJumpTableEntryKind EntryKind;
  uint64_t DestinationCount;
  NevercBool IsDeleted;
  uint8_t Reserved[7];
} NevercMIRJumpTableInfo;

typedef struct NevercMIRMemoryPointerInfo {
  NevercMIRMemoryPointerKind Kind;
  uint32_t AddressSpace;
  int32_t FrameIndex;
  uint32_t StackID;
  int64_t Offset;
  NevercMIRReferenceHandle Reference;
} NevercMIRMemoryPointerInfo;

typedef struct NevercMIRMemoryOperandDesc {
  NevercABITableHeader Header;
  NevercMIRMemoryFlags Flags;
  uint64_t Size;
  uint64_t BaseAlignment;
  NevercMIRMemoryPointerInfo Pointer;
  NevercMIRAtomicOrdering SuccessOrdering;
  NevercMIRAtomicOrdering FailureOrdering;
  NevercStringView SynchronizationScope;
  NevercMIRReferenceHandle TBAA;
  NevercMIRReferenceHandle TBAAStruct;
  NevercMIRReferenceHandle AliasScope;
  NevercMIRReferenceHandle NoAlias;
  NevercMIRReferenceHandle Ranges;
} NevercMIRMemoryOperandDesc;

typedef struct NevercMIRMemoryOperandInfo {
  NevercABITableHeader Header;
  NevercMIRMemoryFlags Flags;
  uint64_t Size;
  uint64_t BaseAlignment;
  uint64_t Alignment;
  NevercMIRMemoryPointerInfo Pointer;
  NevercMIRAtomicOrdering SuccessOrdering;
  NevercMIRAtomicOrdering FailureOrdering;
  NevercStringView SynchronizationScope;
  NevercMIRReferenceHandle TBAA;
  NevercMIRReferenceHandle TBAAStruct;
  NevercMIRReferenceHandle AliasScope;
  NevercMIRReferenceHandle NoAlias;
  NevercMIRReferenceHandle Ranges;
} NevercMIRMemoryOperandInfo;

typedef struct NevercMIRPropertyProof {
  NevercABITableHeader Header;
  NevercMIRMachineProperty Property;
  NevercMIRPropertyProofKind Kind;
  NevercBool Value;
  uint8_t Reserved[7];
} NevercMIRPropertyProof;

typedef struct NevercMIRLiveRangeSegment {
  uint64_t Start;
  uint64_t End;
} NevercMIRLiveRangeSegment;

typedef struct NevercMIRRegisterPressureInfo {
  NevercABITableHeader Header;
  uint32_t PressureSet;
  uint32_t Maximum;
  uint32_t Limit;
  uint32_t Reserved;
} NevercMIRRegisterPressureInfo;

typedef struct NevercMIRAnalysisAPI NevercMIRAnalysisAPI;
typedef struct NevercMIRAPI NevercMIRAPI;

typedef struct NevercMIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercMIRPassLevel Level;
  uint32_t Reserved;
  NevercMachineFunctionHandle Function;
  NevercMachineBasicBlockHandle BasicBlock;
  const NevercMIRAPI *Core;
  const NevercMIRAnalysisAPI *Analyses;
  uint64_t ReservedWords[2];
} NevercMIRPassInvocation;

typedef struct NevercMIRPreservedAnalyses {
  NevercABITableHeader Header;
  NevercMIRPreservedAnalysisFlags Flags;
  const NevercMIRBuiltinAnalysis *Analyses;
  uint64_t AnalysisCount;
  uint64_t Reserved[2];
} NevercMIRPreservedAnalyses;

typedef NevercStatus(NEVERC_CALL *NevercMIRPassRunFn)(
    const NevercMIRPassInvocation *Invocation,
    NevercMIRPreservedAnalyses *OutPreserved, void *UserData);

typedef struct NevercMIRPassDescriptor {
  NevercABITableHeader Header;
  NevercStringView PassID;
  NevercInterfaceID Phase;
  NevercMIRPassLevel Level;
  NevercBool Deterministic;
  uint8_t Reserved[3];
  const NevercMIRBuiltinAnalysis *RequiredAnalyses;
  uint64_t RequiredAnalysisCount;
  const NevercMIRBuiltinAnalysis *PreservedAnalyses;
  uint64_t PreservedAnalysisCount;
  NevercMIRPassRunFn Run;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercMIRPassDescriptor;

typedef struct NevercMIRAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetSchemaDigest)(void *Context,
                                             NevercStringView *OutDigest);
  NevercStatus(NEVERC_CALL *GetEntityInfo)(void *Context,
                                           NevercMIREntityKind Kind,
                                           NevercMIRSchemaEntry *OutInfo);
  NevercStatus(NEVERC_CALL *GetOperandKindInfo)(void *Context,
                                                NevercMIROperandKind Kind,
                                                NevercMIRSchemaEntry *OutInfo);
  NevercStatus(NEVERC_CALL *GetGenericOpcodeInfo)(
      void *Context, NevercMIRGenericOpcode Opcode,
      NevercMIRSchemaEntry *OutInfo);
  NevercStatus(NEVERC_CALL *GetMachinePropertyInfo)(
      void *Context, NevercMIRMachineProperty Property,
      NevercMIRSchemaEntry *OutInfo);
  NevercStatus(NEVERC_CALL *BeginMutation)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function,
      NevercMIRMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *EndMutation)(void *Context, NevercTaskHandle Task,
                                         NevercMIRMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *CommitMutation)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbortMutation)(void *Context,
                                           NevercTaskHandle Task,
                                           NevercMIRMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *GetMachineFunctionGeneration)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutGeneration);
  NevercStatus(NEVERC_CALL *GetBasicBlockCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetFirstBasicBlock)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function,
      NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *GetLastBasicBlock)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function,
      NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *GetNextBasicBlock)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *GetPreviousBasicBlock)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *CollectBasicBlocks)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function,
      NevercMachineBasicBlockHandle *OutBlocks, uint64_t Capacity,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetBasicBlockNumber)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      int64_t *OutNumber);
  NevercStatus(NEVERC_CALL *GetBasicBlockFunction)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      NevercMachineFunctionHandle *OutFunction);
  NevercStatus(NEVERC_CALL *GetInstructionCount)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetFirstInstruction)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      NevercMachineInstrHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetLastInstruction)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      NevercMachineInstrHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetNextInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction,
      NevercMachineInstrHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *GetPreviousInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction,
      NevercMachineInstrHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *CollectInstructions)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      NevercMachineInstrHandle *OutInstructions, uint64_t Capacity,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetInstructionInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction, NevercMIRInstructionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetInstructionBasicBlock)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction,
      NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *GetInstructionDebugLocation)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction,
      NevercMIRDebugLocation *OutLocation);
  NevercStatus(NEVERC_CALL *SetInstructionFlags)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineInstrHandle Instruction, NevercMIRInstructionFlags Flags);
  NevercStatus(NEVERC_CALL *GetInstructionOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction, uint64_t Index,
      NevercMachineOperandHandle *OutOperand);
  NevercStatus(NEVERC_CALL *GetOperandValue)(void *Context,
                                             NevercTaskHandle Task,
                                             NevercMachineOperandHandle Operand,
                                             NevercMIROperandValue *OutValue);
  NevercStatus(NEVERC_CALL *GetOperandInstruction)(
      void *Context, NevercTaskHandle Task, NevercMachineOperandHandle Operand,
      NevercMachineInstrHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *SetOperandValue)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineOperandHandle Operand, const NevercMIROperandValue *Value);
  NevercStatus(NEVERC_CALL *GetSuccessorCount)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetSuccessor)(void *Context, NevercTaskHandle Task,
                                          NevercMachineBasicBlockHandle Block,
                                          uint64_t Index,
                                          NevercMIRCFGEdge *OutEdge);
  NevercStatus(NEVERC_CALL *GetPredecessorCount)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetPredecessor)(
      void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
      uint64_t Index, NevercMachineBasicBlockHandle *OutPredecessor);
  NevercStatus(NEVERC_CALL *GetLiveInCount)(void *Context,
                                            NevercTaskHandle Task,
                                            NevercMachineBasicBlockHandle Block,
                                            uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetLiveIn)(void *Context, NevercTaskHandle Task,
                                       NevercMachineBasicBlockHandle Block,
                                       uint64_t Index,
                                       NevercMIRLiveIn *OutLiveIn);
  NevercStatus(NEVERC_CALL *CreateBasicBlock)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle InsertBefore,
      NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *MoveBasicBlock)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle Block,
      NevercMachineBasicBlockHandle InsertBefore);
  NevercStatus(NEVERC_CALL *EraseBasicBlock)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle Block);
  NevercStatus(NEVERC_CALL *CreateInstruction)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle Block,
      NevercMachineInstrHandle InsertBefore, NevercMIRInstructionOpcode Opcode,
      NevercMachineInstrHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *MoveInstruction)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineInstrHandle Instruction,
      NevercMachineBasicBlockHandle Block,
      NevercMachineInstrHandle InsertBefore);
  NevercStatus(NEVERC_CALL *EraseInstruction)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineInstrHandle Instruction);
  NevercStatus(NEVERC_CALL *AppendOperand)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineInstrHandle Instruction, const NevercMIROperandValue *Value,
      NevercMachineOperandHandle *OutOperand);
  NevercStatus(NEVERC_CALL *AddCFGEdge)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle From, NevercMachineBasicBlockHandle To,
      uint32_t ProbabilityNumerator, uint32_t ProbabilityDenominator);
  NevercStatus(NEVERC_CALL *RemoveCFGEdge)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle From, NevercMachineBasicBlockHandle To);
  NevercStatus(NEVERC_CALL *CreateVirtualRegister)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      const NevercMIRVirtualRegisterDesc *Desc, uint32_t *OutRegister);
  NevercStatus(NEVERC_CALL *GetRegisterInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Register,
      NevercMIRRegisterInfo *OutInfo);
  NevercStatus(NEVERC_CALL *SetVirtualRegisterAssignment)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint32_t Register, const NevercMIRVirtualRegisterDesc *Desc);
  NevercStatus(NEVERC_CALL *GetRegisterDefCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Register,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetRegisterDef)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Register, uint64_t Index,
      NevercMachineOperandHandle *OutOperand);
  NevercStatus(NEVERC_CALL *GetRegisterUseCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Register,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetRegisterUse)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Register, uint64_t Index,
      NevercMachineOperandHandle *OutOperand);
  NevercStatus(NEVERC_CALL *ReplaceRegister)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint32_t FromRegister, uint32_t ToRegister);
  NevercStatus(NEVERC_CALL *GetFunctionLiveInCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetFunctionLiveIn)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t Index,
      NevercMIRFunctionLiveIn *OutLiveIn);
  NevercStatus(NEVERC_CALL *AddFunctionLiveIn)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint32_t PhysicalRegister, uint32_t VirtualRegister);
  NevercStatus(NEVERC_CALL *RemoveFunctionLiveIn)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint32_t PhysicalRegister);
  NevercStatus(NEVERC_CALL *AddBasicBlockLiveIn)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle Block, uint32_t PhysicalRegister,
      uint64_t LaneMask);
  NevercStatus(NEVERC_CALL *RemoveBasicBlockLiveIn)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineBasicBlockHandle Block, uint32_t PhysicalRegister,
      uint64_t LaneMask);
  NevercStatus(NEVERC_CALL *GetFrameObjectCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetFrameObject)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t Ordinal,
      NevercMIRFrameObjectInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFrameObjectByIndex)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, int32_t FrameIndex,
      NevercMIRFrameObjectInfo *OutInfo);
  NevercStatus(NEVERC_CALL *CreateStackObject)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint64_t Size, uint64_t Alignment, NevercBool IsSpillSlot,
      uint32_t StackID, int32_t *OutFrameIndex);
  NevercStatus(NEVERC_CALL *CreateFixedStackObject)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint64_t Size, int64_t Offset, NevercBool IsImmutable,
      NevercBool IsAliased, int32_t *OutFrameIndex);
  NevercStatus(NEVERC_CALL *CreateVariableSizedStackObject)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint64_t Alignment, int32_t *OutFrameIndex);
  NevercStatus(NEVERC_CALL *SetFrameObjectSize)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      int32_t FrameIndex, uint64_t Size);
  NevercStatus(NEVERC_CALL *SetFrameObjectAlignment)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      int32_t FrameIndex, uint64_t Alignment);
  NevercStatus(NEVERC_CALL *SetFrameObjectOffset)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      int32_t FrameIndex, int64_t Offset);
  NevercStatus(NEVERC_CALL *GetCalleeSavedCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetCalleeSaved)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t Index,
      NevercMIRCalleeSavedInfo *OutInfo);
  NevercStatus(NEVERC_CALL *SetCalleeSaved)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      const NevercMIRCalleeSavedInfo *Entries, uint64_t Count);
  NevercStatus(NEVERC_CALL *GetConstantPoolCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetConstantPoolEntry)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Index,
      NevercMIRConstantPoolEntryInfo *OutInfo);
  NevercStatus(NEVERC_CALL *CreateConstantPoolEntry)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      const NevercMIRConstantPoolEntryDesc *Desc, uint32_t *OutIndex);
  NevercStatus(NEVERC_CALL *RemoveConstantPoolEntry)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint32_t Index);
  NevercStatus(NEVERC_CALL *GetJumpTableCount)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetJumpTable)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Index,
      NevercMIRJumpTableInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetJumpTableDestination)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, uint32_t Index,
      uint64_t DestinationIndex, NevercMachineBasicBlockHandle *OutBlock);
  NevercStatus(NEVERC_CALL *CreateJumpTable)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMIRJumpTableEntryKind EntryKind,
      const NevercMachineBasicBlockHandle *Destinations,
      uint64_t DestinationCount, uint32_t *OutIndex);
  NevercStatus(NEVERC_CALL *RemoveJumpTable)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      uint32_t Index);
  NevercStatus(NEVERC_CALL *GetInstructionMemoryOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMachineInstrHandle Instruction, uint64_t Index,
      NevercMachineMemOperandHandle *OutMemoryOperand);
  NevercStatus(NEVERC_CALL *GetMemoryOperandInfo)(
      void *Context, NevercTaskHandle Task,
      NevercMachineMemOperandHandle MemoryOperand,
      NevercMIRMemoryOperandInfo *OutInfo);
  NevercStatus(NEVERC_CALL *CreateMemoryOperand)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      const NevercMIRMemoryOperandDesc *Desc,
      NevercMachineMemOperandHandle *OutMemoryOperand);
  NevercStatus(NEVERC_CALL *AddInstructionMemoryOperand)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineInstrHandle Instruction,
      NevercMachineMemOperandHandle MemoryOperand);
  NevercStatus(NEVERC_CALL *RemoveInstructionMemoryOperand)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      NevercMachineInstrHandle Instruction, uint64_t Index);
  NevercStatus(NEVERC_CALL *GetMachineProperty)(
      void *Context, NevercTaskHandle Task,
      NevercMachineFunctionHandle Function, NevercMIRMachineProperty Property,
      NevercBool *OutValue);
  NevercStatus(NEVERC_CALL *SetMachinePropertyWithProof)(
      void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
      const NevercMIRPropertyProof *Proof);
} NevercMIRAPI;

typedef struct NevercMIRAnalysisAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *QueryBuiltin)(
      void *Context, NevercTaskHandle Task, NevercMIRBuiltinAnalysis Analysis,
      NevercMachineFunctionHandle Function,
      NevercMIRAnalysisResultHandle *OutResult);
  NevercStatus(NEVERC_CALL *DominatorTreeDominates)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle Dominator,
      NevercMachineBasicBlockHandle Dominated, NevercBool *OutDominates);
  NevercStatus(NEVERC_CALL *GetLoopCount)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetLoopHeader)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint64_t Index,
      NevercMachineBasicBlockHandle *OutHeader);
  NevercStatus(NEVERC_CALL *GetLoopForBlock)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, NevercMachineBasicBlockHandle Block,
      NevercMachineBasicBlockHandle *OutHeader, uint32_t *OutDepth);
  NevercStatus(NEVERC_CALL *GetSlotIndex)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineInstrHandle Instruction, uint64_t *OutIndex);
  NevercStatus(NEVERC_CALL *GetLiveIntervalSegmentCount)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint32_t Register,
      uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetLiveIntervalSegment)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint32_t Register, uint64_t Index,
      NevercMIRLiveRangeSegment *OutSegment);
  NevercStatus(NEVERC_CALL *IsRegisterLiveInBlock)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint32_t Register,
      NevercMachineBasicBlockHandle Block, NevercBool *OutLive);
  NevercStatus(NEVERC_CALL *GetRegisterPressureSetCount)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle Block, uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetRegisterPressure)(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle Block, uint32_t PressureSet,
      NevercMIRRegisterPressureInfo *OutInfo);
} NevercMIRAnalysisAPI;

typedef struct NevercMIRPassAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterPass)(
      void *Context, void *RegistrarContext,
      const NevercMIRPassDescriptor *Descriptor);
} NevercMIRPassAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NEVERC_PLUGIN_PLUGINMIR_H */
