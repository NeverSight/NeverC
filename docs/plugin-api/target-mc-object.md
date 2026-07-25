**Languages**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← NeverC Plugin ABI](README.md)

# NeverC Plugin Target, MC, Assembly, and Object API

The back end is four headers and twenty-nine phases. [`PluginTarget.h`]
describes a target and the routes through code generation.
[`PluginMC.h`] builds and observes machine code. Assembly parsing and printing
live in the same header. [`PluginObject.h`] turns a relocatable file into a
normalized graph and back.

Together they let a plugin add a target, replace one lowering step or all of
them, watch every instruction as it is emitted, define an assembly dialect,
or rewrite an object file — through a pure C ABI that never exposes an LLVM
`MCInst`, `MCSection`, or `object::ObjectFile`.

## Interfaces

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| Interface | Table | Slots | Purpose |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | Read and mutate an `MCUnit`; register encoders, decoders, backends |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | Emission events and layout snapshots |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | Replace MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | Replace the assembly parser or printer |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | Read and mutate an ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## Two compatibility tiers

This is the rule that governs everything else here.

**STABLE**, and safe to hard-code: target-independent descriptors, phase IDs,
artifact IDs, the MC and ObjectGraph containers, output transactions, and
every callback contract.

**LOCKSTEP**, and unsafe without a check: target-specific opcode, register,
operand, fixup, relocation, and calling-convention schemas. Their numeric
values are only meaningful against one exact schema revision.

Every place a LOCKSTEP value appears, a schema digest appears next to it.
Compare it before you read the value:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC also rejects a mismatched schema before invoking a provider, so the
check is belt and braces — but a plugin that skips it and reads a raw opcode
anyway will silently misinterpret instructions.

## The phases

Twenty-nine, in four domains.

### `codegen` — routing (4)

| Phase | Policy |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — machine code (13)

`neverc.mc.encode`, `neverc.mc.decode`, and `neverc.mc.layout` are
OBSERVABLE, INTERCEPTABLE, REPLACEABLE.

`neverc.mc.emission.pre_instruction` is the one emission event that is also
REPLACEABLE — that is where you substitute an instruction. The other nine
(`unit_begin`, `unit_end`, `section_change`, `post_instruction`,
`post_encode`, `fixup`, `relaxation_round`, `pre_layout`, `post_layout`) are
observation only.

### `assembly` (4)

`neverc.assembly.parse` and `neverc.assembly.print` are REPLACEABLE.
`neverc.assembly.final_verify` and `neverc.assembly.commit` are SEALED.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write`, and `post_layout` are
REPLACEABLE; `neverc.object.post_write` is INTERCEPTABLE only;
`neverc.object.final_verify` and `neverc.object.commit` are SEALED.

## Registering a target

`NevercTargetDescriptor` is the largest descriptor in the ABI because it
carries everything the front end and back end need to know:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` decide when the target is selected: each matcher names an
architecture, vendor, operating system, and environment, plus a `Priority`
that breaks ties against the built-in targets.

`Machine` is a `NevercTargetMachineDescriptor` — data layout, default and
tunable CPUs, the feature table, supported ABIs, calling conventions and
object formats, address spaces, relocation and code models (as both a default
and a supported-mask), exception model (`NONE`, `DWARF`, `SJLJ`, `SEH`,
`WASM`), unwind model, endianness, the width of pointer/int/long/long long,
stack alignment, maximum atomic and vector widths, `va_list` kind, execution
levels (`USER`, `KERNEL`, `HYPERVISOR`, `FIRMWARE`), and TLS support.

Target builtins carry their own lowering callback, which receives a live IR
builder:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI and calling conventions

An ABI classifies function signatures:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

Argument kinds are `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`, `EXPAND`,
`INDIRECT_ALIASED`, and `COERCE_AND_EXPAND`; flags are `BYVAL`, `REALIGN`,
`INREG`, `SRET_AFTER_THIS`, `CAN_BE_FLATTENED`, `SIGN_EXTEND`, and
`PADDING_INREG`. Coercion is `NONE`, `INTEGER`, `FLOAT`, or `POINTER`, and
`COERCE_AND_EXPAND` supplies an array of `NevercABICoercionElement`.

A calling convention goes one level lower and assigns actual locations:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` is a LOCKSTEP value — `RegisterNumber` only means
anything against the schema it names. See
[Custom calling conventions](custom-callconv/README.md) and
[`pluginsdk/examples/CustomCallConvPlugin.c`] for the full worked example.

## Code-generation routes

A route is chosen from the canonical `NevercTargetKey`: target ID, triple
parts, CPU, tune CPU, features, ABI, calling convention, object format,
relocation model, code model, execution level, pointer width, endianness, and
schema digest. Register the edges you can serve:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

Product kinds are `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`,
`OBJECT_IMAGE`, and `CUSTOM`. The fine-grained route is
`IR → MIR → MC → ObjectGraph → ObjectImage`.

Setting `NEVERC_CODEGEN_EDGE_COARSE` and supplying `CoarseLower` replaces the
whole `IR → ObjectImage` span in one step:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

A coarse route still passes `neverc.codegen.product_verify` and the
transactional output commit. `VerifyProduct` is called with the obligations
the host expects you to have met — `VERIFY_FINAL_IR`, `VERIFY_TARGET_KEY`,
`VERIFY_PRODUCT_KIND`, `VERIFY_PRODUCT_ID`, `VERIFY_STRUCTURE` — so a
provider cannot quietly skip a gate by taking a shortcut route.

## Building MC

An `MCUnit` holds sections, symbols, expressions, fragments, instructions,
operands, and fixups. Reading is first/next iteration:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

Mutation is transactional, same as everywhere else:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

Handles are task scoped and generation checked, so a handle from an abandoned
mutation is rejected rather than reused.

Section flags are `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`, and
`DEBUG`. Symbol bindings are `LOCAL`, `GLOBAL`, and `WEAK`; types are `NONE`,
`FUNCTION`, `OBJECT`, `SECTION`, and `TLS`; definitions are `UNDEFINED`,
`SECTION`, `ABSOLUTE`, and `COMMON`. Expressions support unary `PLUS`,
`MINUS`, `NOT` and binary `ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`, `AND`,
`OR`, `XOR`, `SHIFT_LEFT`, `SHIFT_RIGHT`. Pass `NEVERC_MC_AUTOMATIC_OFFSET`
where you want the host to place something for you.

`RegisterSchema` publishes a target MC schema, and `GetSchemaToken` /
`GetSchemaTokenInfo` resolve a name to a LOCKSTEP token and back.

## Observing emission

The emission stream reports eleven event kinds in order. Subscribe as an
observer and read the event:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` says which parts of the event are populated: `HAS_SECTION`,
`HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT`, and
`CAN_REPLACE_INSTRUCTION`. Check the flag before reading the corresponding
field — an event that has no encoding yet will not have one just because you
asked.

`GetLayoutSection`, `GetLayoutFragment`, `GetLayoutSymbol`, and
`GetLayoutFixup` give addresses and sizes once `HAS_LAYOUT` is set.

At `pre_instruction`, and only when `CAN_REPLACE_INSTRUCTION` is set, you can
substitute:

```c
Emission->BeginInstructionReplacement(Emission->Context, Frame, &Builder);
/* build the replacement through the MC builder */
Emission->PublishInstructionReplacement(Emission->Context, Frame, NewInstr);
```

[`pluginsdk/examples/MCObserverPlugin.c`] is the read-only version of this.

## Encoders, decoders, and layout

Three registrations extend the machine-code backend, all keyed by target and
schema digest:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

An encoder writes through a sink rather than returning a buffer, which keeps
ownership on the host side:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

A decoder reports one of `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`,
`_UNKNOWN`, or `_FAIL`. Fixup kinds describe themselves through
`NevercMCFixupKindInfo` with `PC_RELATIVE`, `SIGNED`, `RELAXABLE`, and
`TARGET` flags.

The asm backend owns relaxation. Layout emits a proof digest, and **any
mutation after layout invalidates that proof** and forces a relayout before
the object can be written — the same generation-checking pattern the link
graph uses.

## Assembly

A parser provider consumes source bytes and publishes an `MCUnit`:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, Unit, &Output);
```

Sources are either `NEVERC_ASSEMBLY_SOURCE_BUFFER` or
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. Preprocessed assembly (`.S`) runs
through the normal frontend preprocessor first and arrives as rendered
tokens; plain assembly (`.s`) enters the parser directly as a buffer.

A printer goes the other way — `GetPrintInput`, then `WritePrintOutput` into
the supplied output transaction, then `PublishAssemblyOutput`. Writing
anywhere else is not supported: parse/print verification and the host commit
gate run before bytes become visible, so a failed print leaves no partial
file behind.

## Object graphs

`NevercObjectAPI` normalizes a relocatable file into sections, symbols,
relocations, and COMDATs. Built-in adapters cover ELF, COFF, and Mach-O;
`RegisterFormat` adds another.

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

Mutation follows the create/replace/move/erase pattern for all four entity
kinds, staged inside `BeginMutation` … `CommitMutation` / `AbandonMutation`.

Section flags are `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`,
`STRINGS`, `TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE`, and `RETAIN`. Relocation
targets are `SYMBOL`, `SECTION`, `ABSOLUTE`, or `FORMAT_EXTENSION`.

Every descriptor has an `ExtensionOwner` / `ExtensionVersion` / `Extension`
triple. That is how a format keeps data the normalized graph has no field
for — the bytes travel with the entity and come back on write, instead of
being dropped by the round trip.

### Registering a format

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` reports a `Confidence` from 0 to `NEVERC_OBJECT_PROBE_MAX_CONFIDENCE`
(1000), the `NevercObjectArtifactKind` it recognized (`RELOCATABLE`,
`ARCHIVE`, `EXECUTABLE_IMAGE`, `SHARED_IMAGE`, `UNIVERSAL_BINARY`), and a
`ConsumedMinimum` — how many bytes it needed to be sure, capped at
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536). Highest confidence wins.

`Reader` is handed a graph and an open mutation and fills them in. `Writer`
is handed the graph, its layout proof, and the bounded binary builder.

### The write pipeline

1. probe and read bytes into an ObjectGraph;
2. run `object.pre_write` graph interceptors;
3. lay out, then run `object.post_layout` (relayout after any mutation);
4. write a bounded candidate image;
5. run `object.post_write` binary interceptors;
6. run the sealed `object.final_verify` and the atomic `object.commit`.

Image state moves `CANDIDATE` → `VERIFIED` → `COMMITTED`, or `ABORTED` /
`FAILED_PARTIAL`.

Observers receive read-only bridges; a mutation attempted from an observer is
rejected with `NEVERC_STATUS_POLICY_VIOLATION`. Writers and post-write
interceptors get only the bounded `NevercMutableBinaryAPI` builder —
`Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`, `Append`,
`Resize`. Overflow, a failed callback, or a failed verification aborts
staging, so a failure never leaves half a file on disk.

[`pluginsdk/examples/ObjectRewritePlugin.c`] is a complete transactional
rewrite.

## Rules

- Compare the schema digest before consuming any LOCKSTEP opcode, register,
  operand, fixup, relocation, or calling-convention value.
- Keep mutable state in the host-supplied process, session, and task state.
- Do not cache task handles or borrowed views after a callback returns.
- Invoke an interceptor's continuation at most once, on the callback thread.
- Every `BeginMutation` reaches exactly one commit or abandon.
- Relayout after mutating a laid-out MCUnit or ObjectGraph; the old layout
  proof is stale and the host will reject it.
- Check `NevercMCEmissionEventInfo.Flags` before reading an event field, and
  only replace an instruction when `CAN_REPLACE_INSTRUCTION` is set.
- Write output only through the supplied transaction or byte sink.
- Return the original `NevercStatus` on failure and publish nothing partial.
- Declare the narrowest truthful concurrency and reentrancy models.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify`, and `object.commit` are sealed. Observe only.

See [`PluginTarget.h`], [`PluginMC.h`], [`PluginObject.h`], and
[`Schema/PhaseSchema.json`] for the normative declarations, and
[`coverage.json`], which maps each of these stable phases to its positive,
negative, replacement, read-only-observer, and sealed-gate tests.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
