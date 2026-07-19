/*===-- PluginMC.h - NeverC machine-code plugin C ABI ------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINMC_H
#define NEVERC_PLUGIN_PLUGINMC_H

#include "neverc/Plugin/PluginMIR.h"    /* IWYU pragma: export */
#include "neverc/Plugin/PluginTarget.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MC_API_MAJOR UINT16_C(1)
#define NEVERC_MC_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_MC_HIGH UINT64_C(0x4e43504d43000001)
#define NEVERC_INTERFACE_MC_LOW UINT64_C(0x0000000000000001)
#define NEVERC_MC_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

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

typedef uint32_t NevercMCOperandKind;
#define NEVERC_MC_OPERAND_INVALID UINT32_C(0)
#define NEVERC_MC_OPERAND_REGISTER UINT32_C(1)
#define NEVERC_MC_OPERAND_IMMEDIATE UINT32_C(2)
#define NEVERC_MC_OPERAND_SINGLE_FLOAT UINT32_C(3)
#define NEVERC_MC_OPERAND_DOUBLE_FLOAT UINT32_C(4)
#define NEVERC_MC_OPERAND_EXPRESSION UINT32_C(5)
#define NEVERC_MC_OPERAND_INSTRUCTION UINT32_C(6)

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

typedef struct NevercMCInstructionInfo {
  NevercABITableHeader Header;
  uint32_t Opcode;
  uint32_t Flags;
  uint64_t OperandCount;
} NevercMCInstructionInfo;

typedef struct NevercMCOperandValue {
  NevercABITableHeader Header;
  NevercMCOperandKind Kind;
  uint32_t Reserved;
  union {
    uint32_t Register;
    int64_t Immediate;
    uint32_t SingleFloatBits;
    uint64_t DoubleFloatBits;
    NevercMCExprHandle Expression;
    NevercMCInstHandle Instruction;
  } Payload;
} NevercMCOperandValue;

typedef struct NevercMCAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterSchema)(
      void *Context, void *RegistrarContext,
      const NevercMCSchemaDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *BeginMutation)(
      void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
      NevercMCMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *CommitMutation)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbandonMutation)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation);
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
      NevercMCMutationHandle Mutation, uint32_t Opcode,
      NevercMCInstHandle *OutInstruction);
  NevercStatus(NEVERC_CALL *AppendOperand)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Instruction,
      const NevercMCOperandValue *Value);
  NevercStatus(NEVERC_CALL *InsertInstructionBefore)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation,
      NevercMCInstHandle Position,
      NevercMCInstHandle Instruction);
  NevercStatus(NEVERC_CALL *AppendInstruction)(
      void *Context, NevercTaskHandle Task,
      NevercMCMutationHandle Mutation, NevercMCUnitHandle Unit,
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
} NevercMCAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINMC_H */
