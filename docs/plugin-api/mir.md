**Languages**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← NeverC Plugin ABI](README.md)

# NeverC Plugin MIR API

`PluginMIR.h` exposes Machine IR: machine functions, blocks, instructions,
operands, virtual and physical registers, the stack frame, the constant pool,
jump tables, and memory operands. A plugin attaches passes at nine stable
code-generation hooks, or replaces IR-to-MIR lowering entirely.

Two schemas meet here. The **generic schema** is target independent and
always available. Anything target specific — a real opcode, a register
number, a register class — requires a negotiated **target schema**, and every
value that needs one says so through a `RequiresTargetSchema` flag.

## Interfaces

```c
#include "neverc/Plugin/PluginMIR.h"
```

| Interface | Table | Slots | Purpose |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | Read and mutate machine functions |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | Liveness, dominators, loops, pressure |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | Replace IR → MIR lowering |

All four are `NEVERC_INTERFACE_STABLE` at major 1. Check the returned
`TableSize` against the offset of the last slot you use and ignore anything a
newer host appended past it.

## Phases

Ten MIR phases, nine of them pass hooks:

| Phase | When |
|---|---|
| `neverc.mir.pass.post_isel` | After instruction selection |
| `neverc.mir.pass.post_legalize` | After legalization |
| `neverc.mir.pass.pre_scheduler` | Before scheduling |
| `neverc.mir.pass.post_scheduler` | After scheduling |
| `neverc.mir.pass.pre_regalloc` | Before register allocation |
| `neverc.mir.pass.post_regalloc` | After register allocation |
| `neverc.mir.pass.post_prolog_epilog` | After prologue/epilogue insertion |
| `neverc.mir.pass.preemit` | Just before emission |
| `neverc.mir.pass.final` | The last plugin slot |
| `neverc.mir.final_verify` | **Sealed** host `MachineVerifier` |

All nine hooks are `OBSERVABLE | INTERCEPTABLE`. Which analyses exist depends
on where you attach: live intervals are not available before register
allocation, and virtual registers are gone after it.

`neverc.mir.final_verify` runs LLVM's `MachineVerifier` after the final
plugin slot. No plugin can disable, replace, or skip it.

## The schema

`Schema/PluginMIRSchema.inc` is generated and included by `PluginMIR.h`:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

Four calls describe the schema at runtime, each returning a
`NevercMIRSchemaEntry` with the canonical name, the underlying LLVM value,
and whether a target schema is needed:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

The others are `GetEntityInfo`, `GetOperandKindInfo`, and
`GetMachinePropertyInfo`. `GetSchemaDigest` returns the digest of the mapping
actually in use — compare it against `NEVERC_MIR_SCHEMA_DIGEST` before
trusting any target-specific value.

## Reading MIR

Traversal is doubly linked rather than cursor based:

```c
NevercMachineBasicBlockHandle Block;
MIR->GetFirstBasicBlock(MIR->Context, Task, Function, &Block);

while (!neverc_handle_is_null(Block)) {
  NevercMachineInstrHandle Instruction;
  MIR->GetFirstInstruction(MIR->Context, Task, Block, &Instruction);

  while (!neverc_handle_is_null(Instruction)) {
    NevercMIRInstructionInfo Info = {0};
    Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_MIR_API_MAJOR,
                                         NEVERC_MIR_API_MINOR, 0};
    MIR->GetInstructionInfo(MIR->Context, Task, Instruction, &Info);
    /* Info.StableOpcode, .TargetOpcode, .RequiresTargetSchema,
       .IsBranch, .IsCall, .IsReturn, .IsTerminator, .IsBarrier,
       .IsInlineAssembly, .IsDebugInstruction, .IsPseudo, .IsBundle,
       .Flags, .OperandCount, .MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` and `CollectInstructions` fill a bounded array instead,
and `GetLastBasicBlock` / `GetPreviousInstruction` walk backwards. CFG
queries are `GetSuccessorCount` / `GetSuccessor` (which yields a
`NevercMIRCFGEdge` carrying branch probability as a numerator/denominator
pair), `GetPredecessorCount` / `GetPredecessor`, and `GetLiveInCount` /
`GetLiveIn`.

Instruction flags are the 18 bits from `FRAME_SETUP` and `FRAME_DESTROY`
through the fast-math group to `NO_MERGE`, `UNPREDICTABLE`, and
`NO_CONVERGENT`.

## Operands

All 21 operand kinds come back through one tagged union:

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number, .SubRegister, .Flags, .IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol, .Offset */
  break;
}
```

The kinds are `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK`, and `DBG_INSTR_REF`.

Register operand flags are `DEF`, `IMPLICIT`, `KILL`, `DEAD`, `UNDEF`,
`EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ`, and `DEBUG`. Floating-point
immediates arrive as a `NevercMIRWordView` — little-endian words plus a bit
width and one of seven float semantics from `IEEE_HALF` to
`PPC_DOUBLE_DOUBLE` — so no host float type is involved.

## Registers

A virtual register is described by a low-level type plus an assignment:

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* needs the target schema */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

Assignment kinds are `NONE`, `GENERIC`, `CLASS`, and `BANK`; low-level type
kinds are `INVALID`, `SCALAR`, `POINTER`, `VECTOR`, and `POINTER_VECTOR`,
with `IsScalable` for scalable vectors.

Def-use queries are `GetRegisterDefCount` / `GetRegisterDef` and
`GetRegisterUseCount` / `GetRegisterUse`; `ReplaceRegister` rewrites every
occurrence in one staged operation. Function-level live-ins pair a physical
register with the virtual register it was copied into
(`GetFunctionLiveIn`, `AddFunctionLiveIn`, `RemoveFunctionLiveIn`), while
block-level live-ins carry a lane mask (`AddBasicBlockLiveIn`,
`RemoveBasicBlockLiveIn`).

## The stack frame

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` places an object at a known offset (with
`IsImmutable` and `IsAliased`), and `CreateVariableSizedStackObject` handles
dynamic allocation. `SetFrameObjectSize`, `SetFrameObjectAlignment`, and
`SetFrameObjectOffset` adjust one afterwards.

`NevercMIRFrameObjectInfo` reports `Index`, `Flags`, `Size`, `Offset`,
`Alignment`, and `StackID`; frame flags are `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD`, and `PREALLOCATED`.
Callee-saved state is read with `GetCalleeSaved` and replaced wholesale with
`SetCalleeSaved`.

## Constant pool, jump tables, memory operands

Constant-pool entries carry their value as a `NevercMIRWordView`, so an
integer and a float entry use the same shape:

```c
NevercMIRConstantPoolEntryDesc Desc = {0};
Desc.Header       = /* … */;
Desc.Kind         = NEVERC_MIR_CONSTANT_INTEGER;
Desc.Alignment    = 8;
Desc.Value.Data   = Words;
Desc.Value.Count  = 1;
Desc.Value.BitWidth = 64;

uint32_t Index = 0;
MIR->CreateConstantPoolEntry(MIR->Context, Task, Mutation, &Desc, &Index);
```

Jump tables are created from an array of destination blocks with one of seven
entry kinds (`BLOCK_ADDRESS`, `GP_REL64_BLOCK_ADDRESS`,
`GP_REL32_BLOCK_ADDRESS`, `LABEL_DIFFERENCE32`, `LABEL_DIFFERENCE64`,
`INLINE`, `CUSTOM32`).

Memory operands are the richest descriptor: flags (`LOAD`, `STORE`,
`VOLATILE`, `NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT`, plus three target
flags), size and alignment, a pointer of one of nine kinds (`IR_VALUE`,
`FIXED_STACK`, `STACK`, `CONSTANT_POOL`, `JUMP_TABLE`, `GOT`,
`UNKNOWN_STACK`, `TARGET_CUSTOM`, `UNKNOWN`), success and failure atomic
orderings, a synchronization scope, and TBAA, alias-scope, no-alias, and
range references. Attach one with `AddInstructionMemoryOperand`.

## Transactional mutation

Every change is staged inside a mutation bound to one machine function:

```c
NevercMIRMutationHandle Mutation;
MIR->BeginMutation(MIR->Context, Task, Function, &Mutation);

NevercMIRInstructionOpcode Opcode = {0};
Opcode.StableOpcode = MyGenericOpcode;

NevercMachineInstrHandle New;
MIR->CreateInstruction(MIR->Context, Task, Mutation, Block,
                       /*InsertBefore=*/Terminator, Opcode, &New);

NevercMIROperandValue Op = {0};
Op.Header = /* … */;
Op.Kind   = NEVERC_MIR_OPERAND_IMMEDIATE;
Op.Payload.Immediate = 42;
MIR->AppendOperand(MIR->Context, Task, Mutation, New, &Op, &Operand);

Status = MIR->CommitMutation(MIR->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MIR->AbortMutation(MIR->Context, Task, Mutation);
MIR->EndMutation(MIR->Context, Task, Mutation);
```

Commit runs a structural preflight and then the Machine IR verifier. Invalid
operands, a broken CFG, generic opcodes used where the target schema demands
a real one, or an unsupported property claim all roll back atomically. Abort
restores block order, instructions, operands, CFG edges, and machine
properties to exactly what they were.

`EndMutation` releases the handle and is separate from commit and abort —
call it on both paths.

The staged operations are `CreateBasicBlock`, `MoveBasicBlock`,
`EraseBasicBlock`, `CreateInstruction`, `MoveInstruction`,
`EraseInstruction`, `AppendOperand`, `SetOperandValue`,
`SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, the register and frame
calls above, the constant-pool and jump-table calls, the memory-operand
calls, and `SetMachinePropertyWithProof`.

## Machine properties need a proof

The eleven machine properties — `IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`,
`NO_V_REGS`, `FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION`, and
`TRACKS_DEBUG_USER_VALUES` — are read freely but never set freely:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

A proof is one of two kinds. `INVALIDATION` clears a property whose
assumptions your change broke — that is always accepted, because giving up a
guarantee is safe. `STRUCTURAL_CHECK` asks the host to verify the property
before establishing it, so claiming `IS_SSA` costs an actual check rather
than a promise.

## Passes

```c
NevercMIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                                     NEVERC_MIR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.machine-pass");
Pass.Phase         = (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                                         NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
Pass.Level         = NEVERC_MIR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Run           = run_machine_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

That is `pluginsdk/examples/MachinePass.c` verbatim. Levels are `MODULE`,
`FUNCTION`, and `BASIC_BLOCK`. `RequiredAnalyses` and `PreservedAnalyses` are
arrays of `NevercMIRBuiltinAnalysis`, and `RequiredTargetSchemaDigest` makes
the pass refuse to run against a schema it was not built for.

The invocation carries `Task`, `Phase`, `PassID`, `Level`, the `Function` and
`BasicBlock` valid for that level, the `Core` and `Analyses` tables, and the
active `TargetSchemaDigest`.

Report preservation through `OutPreserved` — `NEVERC_MIR_PRESERVE_NONE`,
`_CFG`, or `_ALL`, plus an explicit list in `Analyses`. Claiming
`PRESERVE_ALL` after a committed mutation is rejected.

Function passes may run in parallel code-generation partitions; module-level
passes run at serialized pipeline barriers. The plugin's declared concurrency
and reentrancy models still govern your own state.

## Analyses

Six built-ins: `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO`, and `REGISTER_PRESSURE`.

```c
NevercMIRAnalysisResultHandle Intervals;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Function,
                       &Intervals);

uint64_t SegmentCount = 0;
Analyses->GetLiveIntervalSegmentCount(Analyses->Context, Task, Intervals,
                                      Register, &SegmentCount);
for (uint64_t I = 0; I != SegmentCount; ++I) {
  NevercMIRLiveRangeSegment Segment;
  Analyses->GetLiveIntervalSegment(Analyses->Context, Task, Intervals,
                                   Register, I, &Segment);
  /* Segment.Start, Segment.End */
}
```

Also available: `DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetSlotIndex`, `IsRegisterLiveInBlock`, and
`GetRegisterPressureSetCount` / `GetRegisterPressure`.

Availability depends on the hook. Asking for live intervals at
`post_isel` fails with `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` because the
underlying LLVM analysis does not exist yet. A committed mutation
invalidates the result handles it affects.

## Replacing IR-to-MIR lowering

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module, .IR, .TargetID, .CompatibilityKey, .TargetSchemaDigest,
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … build the machine function … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

The coverage descriptor is how a partial provider stays honest: declare only
what you actually lowered, and the host handles the rest itself instead of
silently dropping globals, constructors, debug info, or unwind tables.

## Example

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Use the module suffix CMake produced for your platform.

## Rules

- Do not retain task handles, MIR handles, or borrowed views after a callback
  returns, and never manufacture a handle value or an LLVM opcode number.
- Compare `GetSchemaDigest` against your compiled-in digest before consuming
  any value whose `RequiresTargetSchema` flag is set.
- Mutate only inside a mutation. Every `BeginMutation` reaches exactly one
  `EndMutation`, after a commit or an abort.
- Do not claim a machine property without a proof, and prefer
  `INVALIDATION` over `STRUCTURAL_CHECK` when your change gave one up.
- Never claim `NEVERC_MIR_PRESERVE_ALL` after a committed mutation.
- Check the analysis you need is actually available at the hook you chose.
- Initialize every table header and reserved field; return statuses across
  the C boundary and never let a C++ exception cross it.
- `neverc.mir.final_verify` is sealed. It runs no matter what.

See `PluginMIR.h`, `Schema/PluginMIRSchema.inc`, `Schema/PhaseSchema.json`,
and `coverage.json` for the normative declarations, schema constants, phase
policies, and coverage evidence.
