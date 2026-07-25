**Languages**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← NeverC Plugin ABI](README.md)

# NeverC Plugin IR API

[`PluginIR.h`] exposes LLVM IR through six capability tables and a generated
schema. A plugin reads and rewrites IR, registers passes at five stable
pipeline points, defines its own analyses, or replaces IR generation and the
optimization pipeline outright — without including a single LLVM header.

Opcodes, type kinds, and instruction properties are **stable schema IDs**,
not LLVM enum values. That indirection is what lets a plugin compiled today
keep working when the host moves to a new LLVM release.

## Interfaces

```c
#include "neverc/Plugin/PluginIR.h"
```

| Interface | Table | Slots | Purpose |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | Read and edit modules, values, types, constants, metadata, attributes |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | Transactional construction |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | Built-in and plugin analyses |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | Replace SemanticUnit → IR lowering |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | Replace the whole optimization pipeline |

Each is `NEVERC_INTERFACE_STABLE` at major 1. Negotiate with the matching
`NEVERC_IR_*_API_MAJOR` / `_MINOR` and verify `TableSize` reaches the last
slot you call, exactly as [`pluginsdk/examples/FunctionPass.c`] does:

```c
Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &StructSize);
if (!Table ||
    StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                     sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

## Phases

Eight IR phases:

| Phase | Policy |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **SEALED HOST GATE** |

The five `pass.*` phases are where `NevercIRPassDescriptor.Phase` points.
`neverc.ir.final_verify` runs the LLVM verifier and cannot be intercepted,
replaced, or skipped by anything — including an optimization provider.

## The schema

[`Schema/PluginIRSchema.inc`] is generated and included by [`PluginIR.h`]. It
publishes a digest and the constant sets:

```c
#define NEVERC_IR_SCHEMA_CAPABILITY_MAJOR   UINT16_C(1)
#define NEVERC_IR_SCHEMA_DIGEST             "4302919d…"
#define NEVERC_IR_TYPE_KIND_COUNT           UINT32_C(22)
#define NEVERC_IR_VALUE_KIND_COUNT          UINT32_C(29)
#define NEVERC_IR_OPCODE_COUNT              UINT32_C(67)
#define NEVERC_IR_PREDICATE_COUNT           UINT32_C(26)
#define NEVERC_IR_LINKAGE_COUNT             UINT32_C(11)
#define NEVERC_IR_CALLING_CONVENTION_COUNT  UINT32_C(21)
#define NEVERC_IR_PROPERTY_COUNT            UINT32_C(23)
```

IDs are tagged by domain in their high byte — `0x41……` for types, `0x42……`
for value kinds, `0x43……` for opcodes, `0x49……` for properties — so a value
used in the wrong position is rejected rather than misread.

## Handles and ownership

IR handles are opaque `{Owner, Value}` pairs scoped to one task, and the host
owns everything behind them.

- Never retain a handle after its callback or task ends.
- Never use a handle in another session or task.
- A committed replacement invalidates handles for the objects it replaced.
- An aborted mutation makes the handles that mutation created stale.
- Errors are `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE`, or `WRONG_TYPE` —
  never a raw LLVM pointer.

Strings and byte views from a query are borrowed for the callback. The one
exception is `ExportModule`, which returns a
`NevercIRSerializedBufferHandle` you must hand back to
`ReleaseSerializedBuffer`.

## Walking a module

Collections are read through a cursor that carries its own generation, so a
mutation mid-walk is detected instead of silently skipping entries:

```c
NevercIRValueCursor Cursor = {0};
Cursor.Header = (NevercABITableHeader){sizeof(Cursor),
                                       NEVERC_IR_CORE_API_MAJOR,
                                       NEVERC_IR_CORE_API_MINOR, 0};
Core->BeginValueCursor(Core->Context, Task, Module,
                       NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);

NevercIRValueHandle Batch[32];
uint64_t Count = 0;
for (;;) {
  Core->CollectValueCursor(Core->Context, Task, &Cursor, Batch, 32, &Count);
  if (Count == 0)
    break;
  for (uint64_t I = 0; I != Count; ++I) {
    NevercStringView Name;
    Core->GetValueName(Core->Context, Task, Batch[I], &Name);
  }
}
```

Repeat until `Count` comes back zero. The seven collections are
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS`, and `BLOCK_INSTRUCTIONS`.

Everything else is a direct query: `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*`, and the module-level `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly`
with their setters.

## Types and constants

Types are interned, so asking twice yields the same handle:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` takes a schema kind such as `NEVERC_IR_TYPE_VOID`,
`_FLOAT`, `_DOUBLE`, or `_TOKEN`; `GetArrayType`, `GetVectorType` (with a
`Scalable` flag), and `GetStructType` (named or literal, packed or not) cover
the rest.

Integer and floating constants are built from little-endian 64-bit words, so
an `i128` needs no special path:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant`, and `GetGlobalAddressConstant` cover the simple
cases; `CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression`, and `CreateConstantGEPExpression` build
constant expressions.

## Instruction properties

Rather than one accessor per flag, instruction detail goes through a tagged
property value keyed by schema ID:

```c
typedef struct NevercIRPropertyValue {
  NevercABITableHeader Header;
  NevercIRPropertyValueKind Kind;   /* BOOL, UINT, ENUM, FLAGS, STRING, TYPE */
  uint32_t Reserved;
  uint64_t UnsignedValue;
  NevercIRTypeHandle TypeValue;
  NevercStringView StringValue;
} NevercIRPropertyValue;

NevercIRPropertyValue Value = {0};
Value.Header = /* … */;
Core->GetInstructionProperty(Core->Context, Task, Instruction,
                             NEVERC_IR_PROPERTY_ALIGNMENT, &Value);
```

The 23 properties are `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`,
`DISJOINT`, `VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`,
`PREDICATE`, `CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP`, and `NUSW`. Atomic orderings run
from `NOT_ATOMIC` through `SEQUENTIALLY_CONSISTENT`; tail-call kinds are
`NONE`, `TAIL`, `MUST_TAIL`, and `NO_TAIL`; fast-math flags are the usual
seven bits from `ALLOW_REASSOC` to `APPROX_FUNC`.

## Attributes

Attributes are values you create and then attach, which keeps the four kinds
(`ENUM`, `INTEGER`, `STRING`, `TYPE`) uniform:

```c
NevercIRAttributeHandle NoInline;
Core->CreateEnumAttribute(Core->Context, Task, SV("noinline"), &NoInline);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION,
                           /*ParameterIndex=*/0, NoInline);

NevercBool Present = NEVERC_FALSE;
Core->HasFunctionAttribute(Core->Context, Task, Function, SV("noinline"),
                           &Present);
```

[`pluginsdk/examples/CustomCallConvPlugin.c`] uses this together with
`GetFunctionStringAttribute` to drive a data-defined calling convention.

## Transactional mutation

Structural change goes through `NevercIRBuilderAPI`. A mutation is the
transaction; a builder is a cursor inside it.

```c
NevercIRMutationHandle Mutation;
NevercIRBuilderHandle Builder;

Builders->BeginMutation(Builders->Context, Task,
                        NEVERC_IR_MUTATION_SCOPE_FUNCTION, Function,
                        &Mutation);
Builders->CreateBuilder(Builders->Context, Task, Mutation, &Builder);
Builders->SetInsertBefore(Builders->Context, Task, Builder, Terminator);

NevercIRValueHandle Sum;
Builders->BuildBinary(Builders->Context, Task, Builder,
                      NEVERC_IR_OPCODE_ADD, Left, Right, SV("sum"), &Sum);

Status = Builders->CommitMutation(Builders->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Builders->AbortMutation(Builders->Context, Task, Mutation);

Builders->DestroyBuilder(Builders->Context, Task, Builder);
Builders->DestroyMutation(Builders->Context, Task, Mutation);
```

Scopes are `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION`, and `_LOOP`;
`ScopeRoot` names the function or loop header. Commit verifies the candidate
and publishes atomically — on verifier failure the host rolls back and the
previous module survives untouched.

The build calls are `BuildBinary`, `BuildUnary`, `BuildCompare`, `BuildCast`,
`BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`,
`BuildGetElementPtr`, `BuildCall`, `BuildPhi`, `BuildBranch`,
`BuildConditionalBranch`, `BuildUnreachable`, `BuildReturn`, and
`BuildReturnVoid`. `SetDebugLocation` and `SetFastMathFlags` apply to
everything the builder emits afterwards.

Note the asymmetry: `AddPhiIncoming`, `CreateFunction`, and
`CreateBasicBlock` take the **mutation**, not the builder, because they are
not tied to an insertion point.

`DestroyMutation` is separate from commit and abort. Every `BeginMutation`
needs exactly one `DestroyMutation`, whichever way the transaction ended.

## Passes

```c
NevercIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                                     NEVERC_IR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.function-pass");
Pass.Phase         = (NevercInterfaceID){
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW};
Pass.Level         = NEVERC_IR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Cacheable     = NEVERC_TRUE;
Pass.Run           = run_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

Levels are `MODULE`, `CGSCC`, `FUNCTION`, and `LOOP`. The invocation carries
only the handles valid for its level:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION and LOOP     */
  NevercIRValueHandle LoopHeader;               /* LOOP only             */
  const NevercIRValueHandle *SCCFunctions;      /* CGSCC only            */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

The three API pointers come with the invocation, so a pass body needs no
stored table.

Report what survived through `OutPreserved`:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* or _NONE, or _CFG */
```

`NEVERC_IR_PRESERVE_CFG` means the control-flow graph is intact even though
instructions changed. Custom analyses are preserved by listing them in
`CustomAnalyses`. Do not claim `PRESERVE_ALL` after changing IR — the adapter
compares the module generation and rejects a false claim.

Function and loop passes may run concurrently, so mutable plugin state must
match the `NevercConcurrencyModel` the plugin declared.

## Analyses

Seven built-in analyses are queryable by ID: `DOMINATOR_TREE`,
`POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`, `MEMORY_SSA`,
`CALL_GRAPH`, and `ALIAS`.

```c
NevercIRAnalysisResultHandle Loops;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_IR_ANALYSIS_LOOP_INFO, Function, &Loops);

uint64_t LoopCount = 0;
Analyses->GetLoopCount(Analyses->Context, Task, Loops, &LoopCount);
for (uint64_t I = 0; I != LoopCount; ++I) {
  NevercIRValueHandle Header;
  Analyses->GetLoopHeader(Analyses->Context, Task, Loops, I, &Header);
}
```

Each has typed accessors rather than an opaque blob:
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind` (`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee`, and `Alias` (`NO`, `MAY`,
`PARTIAL`, `MUST`).

A plugin analysis is registered with its own lifecycle:

```c
NevercIRAnalysisDescriptor Analysis = {0};
Analysis.Header          = /* … */;
Analysis.AnalysisID      = MyAnalysisID;
Analysis.Name            = SV("example.my-analysis");
Analysis.Level           = NEVERC_IR_PASS_LEVEL_FUNCTION;
Analysis.Dependencies    = Deps;
Analysis.DependencyCount = DepCount;
Analysis.Compute         = compute;
Analysis.Query           = query;
Analysis.Invalidate      = invalidate;
Analysis.Destroy         = destroy;
Analyses->RegisterAnalysis(Analyses->Context, RegistrarContext, &Analysis);
```

`Invalidate` is told why — `INVALIDATED_BY_PASS` or
`INVALIDATED_BY_PLAN_DESTROY`. Results are cached per invocation and dropped
according to what the running pass preserved. Dependency cycles are rejected
at registration, and mutating IR from inside an analysis callback is refused.

## Replacing generation and optimization

`NevercIRGenAPI` replaces `neverc.ir.generate`:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … build the module … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` starts from bitcode or textual IR instead of an empty module.
`NevercIROptimizationAPI` has the same shape for `neverc.ir.optimize`, plus
`GetInputModule` to reach the incoming module and `RunBuiltinPipeline` to
delegate to the built-in pipeline and then post-process its result.

Both routes publish through the host rather than returning a pointer, both
verify target compatibility, and both keep the old module atomically if
publication fails. `neverc.ir.final_verify` still runs afterwards.

## Examples

| File | Shows |
|---|---|
| [`pluginsdk/examples/FunctionPass.c`] | A read-only function pass, ABI negotiation included |
| [`pluginsdk/examples/ExamplePlugin.c`] | Module-level pass walking functions with a value cursor |
| [`pluginsdk/examples/CustomCallConvPlugin.c`] | Attributes and call-site properties |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Use the module suffix CMake produced for your platform.

## Rules

- Return a `NevercStatus` from every callback. A plugin failure becomes a
  structured diagnostic; never let an exception cross the C boundary.
- Zero every output struct and set its `Header` before a call that fills it.
- Do not hard-code numeric opcode, type, or property values. Use the
  [`PluginIRSchema.inc`] names so a schema revision is a compile error.
- Every `BeginMutation` reaches exactly one `DestroyMutation`, and every
  `CreateBuilder` exactly one `DestroyBuilder`, including on error paths.
- Release what `ExportModule` hands you with `ReleaseSerializedBuffer`.
- Never claim `NEVERC_IR_PRESERVE_ALL` after modifying IR.
- Assume function and loop passes run in parallel unless the plugin declared
  `NEVERC_CONCURRENCY_SESSION_SERIAL`.
- `neverc.ir.final_verify` is sealed. Nothing a plugin does can skip it.

See [`PluginIR.h`], [`Schema/PluginIRSchema.inc`], [`Schema/PhaseSchema.json`], and
[`coverage.json`] for the normative declarations, schema constants, phase
policies, and test evidence.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`pluginsdk/examples/FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
