**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Plugin ABI

A NeverC plugin is a shared module that exports exactly one function,
negotiates versioned capability tables by 128-bit interface ID, and attaches
itself to a frozen graph of named compiler phases. The whole interface is
pure C11. A plugin never includes an LLVM header, never links the compiler,
and never passes a C++ type across the boundary.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

That signature, declared in `PluginCore.h`, is the entire linkage contract.
Everything else — reading IR, rewriting an object graph, replacing the
optimization pipeline — is reached through tables you ask the host for by ID.

The unreleased prototype API and its `nevercGetPluginInfo` entry point have
been removed. Prototype binaries are rejected with a migration diagnostic;
recompile their source against the public headers. See
[Migrating from the prototype API](migration-from-prototype.md).

## Guides

| Guide | Covers |
|---|---|
| [Driver API](driver.md) | Command line, toolchain selection, action graph, job graph |
| [Source and I/O API](source.md) | VFS providers, source locations, buffers, output sinks, dependencies |
| [Preprocessor API](prep.md) | Tokens, macros, pragmas, includes, feature queries, 39 event kinds |
| [AST and semantic API](ast-sema.md) | Parser extension, AST mutation, name lookup, types, constants |
| [IR API](ir.md) | LLVM IR reading, transactional building, analyses, passes, providers |
| [MIR API](mir.md) | Machine functions, registers, frames, MIR passes and analyses |
| [Target, MC, assembly, object](target-mc-object.md) | Target registration, calling conventions, MC encoding, object graphs |
| [Link and LTO API](link-lto.md) | Link graph, symbol resolution, GC/ICF, linker and LTO providers |
| [DynCode API](dyncode.md) | Flat position-independent images, import lowering, charset encoding |
| [Custom calling conventions](custom-callconv/README.md) | Data-driven calling-convention plugins |
| [Migrating from the prototype API](migration-from-prototype.md) | Old-to-new mapping |
| [Phase coverage evidence](coverage.json) | Test mapping for every stable phase |

## Execution model

The host drives a plugin through three nested scopes. Each scope hands the
plugin an opaque state pointer that the plugin allocates and owns, so a
correctly written plugin needs no global mutable state.

| Scope | Callbacks | Meaning |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | One compiler process. Query interfaces and register capabilities here. |
| Session | `SessionBegin`, `SessionEnd` | One driver invocation. |
| Task | `TaskBegin`, `TaskEnd` | One unit of work, identified by `NevercTaskKind`. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

Only `PluginID` and `Register` are practically mandatory; every callback slot
may stay `NULL`. Task kinds are `NEVERC_TASK_INVOCATION`,
`TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`, `OBJECT`, and `DYNCODE`.

The host calls `ProcessBegin` first, then `Register` exactly once.
Registration is the only place where options, observers, interceptors, and
providers may be added; the phase graph is frozen afterwards.

State is retrieved inside a callback rather than captured:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## Phases

A phase is a named, versioned transition from an input artifact to an output
artifact. NeverC ships **130 built-in phases**, plus 8 extension-ID families
reserved for plugin-defined phases:

| Domain | Phases | Domain | Phases |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

Every one of the 130 is stability tier `stable` in ABI major 1. Each phase
advertises a policy, and a plugin may attach itself only in the ways that
policy permits:

| Policy flag | Phases | What a plugin may do |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | Register an observer for read-only notification. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | Wrap the phase and decide whether to call the rest of the chain. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | Register a provider that supplies the output itself. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | Skip the transition while supplying a proof handle. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | Nothing. Verifiers and commits are host-owned. |

The 14 sealed gates are `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify`, and `dyncode.commit`. They can
be observed but never intercepted, replaced, or skipped.

Observers are delivered at the points a phase declares:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER`, and
`NEVERC_OBSERVER_AFTER_COMMIT`. An interceptor receives a
`NevercPhaseContinuation` and must call `InvokeNext` **at most once**, on the
callback thread, then report `NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE`,
or `NEVERC_PHASE_SKIP` in `NevercPhaseResult.Action`.

Every phase callback receives the same frame:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json` is the normative source for phase IDs, policies,
stability tiers, and verifier gates. The generated
`Schema/PluginPhaseSchema.inc` exposes each of them as compile-time
constants — for phase `neverc.ir.pass.pipeline_start`:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT` and the per-domain
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` constants let a plugin assert the graph
it was compiled against.

## A complete minimal plugin

This is `pluginsdk/templates/minimal/Plugin.c` verbatim. It loads, negotiates
the ABI, registers nothing, and unloads cleanly — copy the directory and grow
it from here.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Register options, observers, interceptors, or providers here. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` is a caller-owned buffer. On entry its `Header.StructSize` is the
writable capacity; the plugin writes at most that many bytes and reports the
size it actually produced. Writing the descriptor's own `Header` first, then
truncating the copy, satisfies both halves of that rule.

## Interface negotiation

Capability tables are fetched by 128-bit interface ID, not by symbol. Ask for
the major version you compiled against and the lowest minor you can work with:

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

Checking `TableSize` against the offset of the last function you call is the
rule that makes the ABI extensible: a newer host appends fields, and an older
plugin keeps working because it never reads past the prefix it verified. The
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` macro applies the same test
to a struct you received. The same `QueryInterface` signature is also on
`NevercCoreAPI`, so you can negotiate late instead of at entry.

The public interfaces, their tables, and their ID macros:

| Interface macro pair | Table | Header |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | six IR tables | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | four MIR tables | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | four MC tables | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | three object tables | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | three link tables | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | three dyncode tables | `PluginDynCode.h` |

Each header also defines the matching `NEVERC_<DOMAIN>_API_MAJOR` and
`_MINOR` you should pass to `QueryInterface`.

An interface is either `NEVERC_INTERFACE_STABLE` (a newer host may only
append) or `NEVERC_INTERFACE_LOCKSTEP` (target-specific schemas that must
match exactly). Compare the schema digest before consuming LOCKSTEP values.

## Registration

`Register` receives a `NevercRegistrarAPI` and an opaque `RegistrarContext`:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

Domain registrars — `NevercIRPassAPI.RegisterPass`,
`NevercTargetAPI.RegisterTarget`, `NevercObjectFormatAPI.RegisterFormat`, and
the rest — take that same `RegistrarContext` as their second argument, which
is how the host attributes a registration to your plugin.

A provider additionally declares its determinism contract, which the build
cache relies on:

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

## Building

Include the aggregate header or only the domains you use:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

Build a shared module with NeverC itself:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Or against an installed SDK with CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Or with pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Use `.so`, `.dylib`, or `.dll` as appropriate for the host. The SDK links no
LLVM and no NeverC runtime — `NevercPluginSDK::headers` is header-only.

## Loading and configuring

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Option | Form | Purpose |
|---|---|---|
| `-fplugin=<path>` | repeatable | Load a full-toolchain plugin shared module. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | repeatable | Pass a namespaced value to a registered plugin option. |
| `-fplugin-provider=<phase>:<plugin-id>` | repeatable | Select which plugin provides a replaceable phase. |
| `-fplugin-pass=<dsopath>` | repeatable | Load a C-ABI out-of-tree pass plugin. |
| `-fplugin-pass-arg=<key>=<value>` | repeatable | Pass an argument to C-ABI pass plugins. |

The `<plugin-id>:` qualifier may be omitted only when exactly one plugin is
active. Options a plugin registers with `RegisterOption` are also accepted
directly by their declared spelling, in flag, joined, separate, or
multi-argument form. Plugin arguments and provider selections without a
matching `-fplugin=` are a hard error rather than a silent no-op.

A registered option is read back at any time through the core table:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## ABI rules

- Query capability tables through `QueryInterface`; require the matching major
  and check `StructSize` before touching a field.
- Initialize every public structure's `Header` and reserved storage. Zero the
  struct, then set `StructSize`, `Major`, `Minor`, and `Flags`.
- Treat handles and borrowed views as scoped, opaque values. Never retain a
  task-scoped handle past its callback, never use it in another session or
  task, and never manufacture a handle value.
- Return `NevercStatus` from every callback. Do not let a C++ exception or a
  host-owned pointer cross the C boundary.
- Declare the narrowest truthful `NevercConcurrencyModel`
  (`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`) and
  `NevercReentrancyModel` (`NONE`, `ALLOWED`).
- Perform IR, MIR, AST, graph, and artifact changes through the transactional
  host APIs: begin a mutation, stage changes, then commit or abort. Commit
  verifies and publishes atomically; a failed commit leaves the previous state
  intact.
- Allocate through `NevercCoreAPI.Allocate` / `Reallocate` / `Deallocate` when
  the host should account for the memory.
- Keep mutable state in the host-supplied process/session/task state. Global
  mutable state is checked by `utils/plugin-api/check-global-state.py`.

All public structs are laid out under `NEVERC_ABI_PACK_BEGIN` (8-byte
packing) with fixed-width types only. New functions are appended to
independently versioned capability tables; the stable prefix of a table does
not change within the first ABI major (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Status and diagnostics

`NevercStatus` carries a `Code`, `Flags`, and a `Detail` word. The complete
code set:

| Code | Meaning |
|---|---|
| `NEVERC_STATUS_OK` | Success. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | A required pointer or value was missing or malformed. |
| `NEVERC_STATUS_ABI_MISMATCH` | The negotiated table is too small or the major differs. |
| `NEVERC_STATUS_MISSING_INTERFACE` | The host does not publish the requested interface. |
| `NEVERC_STATUS_VERSION_MISMATCH` | The requested major/minor cannot be satisfied. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | A descriptor failed structural validation. |
| `NEVERC_STATUS_DUPLICATE_ID` | An ID was already registered. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | A declared dependency is absent. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | Registration order cannot be satisfied. |
| `NEVERC_STATUS_BUSY` | A resource is held elsewhere. |
| `NEVERC_STATUS_CANCELLED` | Cooperative cancellation was requested. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | A budget or limit was hit. |
| `NEVERC_STATUS_STALE_HANDLE` | A handle outlived the object it named. |
| `NEVERC_STATUS_WRONG_SESSION` | A handle was used in another session. |
| `NEVERC_STATUS_WRONG_SCOPE` | A handle was used outside its scope. |
| `NEVERC_STATUS_WRONG_TYPE` | A handle named a different entity kind. |
| `NEVERC_STATUS_INVALID_STATE` | The operation is not legal in the current state. |
| `NEVERC_STATUS_POLICY_VIOLATION` | The phase policy forbids the operation. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | A sealed host verifier rejected the product. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | The host cannot offer the capability here. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | The plugin reported a generic failure. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | An exception escaped a plugin callback. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | The output was written only in part. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | A reentrant call was refused. |
| `NEVERC_STATUS_NOT_FOUND` | The named entity does not exist. |

The flag bits describe what happened to the output, which is what a build
system needs to decide whether a retry is safe:
`NEVERC_STATUS_FLAG_RECOVERABLE`, `_OUTPUT_ALREADY_COMMITTED`,
`_OUTPUT_MAY_BE_PARTIAL`, `_OUTPUT_RECOVERY_REQUIRED`, and
`_DURABILITY_UNCONFIRMED`.

Report problems with `NevercCoreAPI.EmitDiagnostic` and a
`NevercDiagnosticDescriptor` carrying severity (`NOTE`, `REMARK`, `WARNING`,
`ERROR`, `FATAL`), code, plugin ID, phase ID, message, notes, source location,
ranges, and fix-its. Call `CheckCancelled` before expensive work.

## Examples

Build all of them:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Every example is compiled twice — once with the configured host C compiler and
once with the freshly built NeverC — so the ABI is proven from both sides.
Modules land in `build-neverc/neverc/pluginsdk/examples/host/`.

| Example | CMake target | Shows |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Option registration, phase observation, job interception |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | A VFS provider serving an in-memory header |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Parser interception and atomic AST mutation |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | A module-level IR pass walking the function list with a value cursor |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | A stable IR function pass |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | A stable MIR pass at the pre-emit hook |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Read-only MC emission events |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Transactional ObjectGraph rewriting |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Data-driven calling conventions |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Observing the dyncode pipeline |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Intercepting dyncode charset encoding |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | A plugin with zero CRT dependency |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI call-throughput microbenchmark |

Load one:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Normative sources

| File | Guarantees |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | Phase IDs, policies, stability, verifier gates |
| `pluginsdk/manifest/plugin.json` | ABI version, interface IDs/versions/stability, schema digests, supported targets |
| `pluginsdk/abi/plugin.json` | Measured size, alignment, and field offsets of every public struct, per host ABI key |
| `docs/plugin-api/coverage.json` | Maps each stable phase to positive, negative, replacement, observer, and sealed-gate tests |

An SDK can therefore be validated against a host mechanically, and a plugin
build can assert its struct layout against the ABI key it will load into.
