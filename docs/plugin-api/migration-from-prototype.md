# Migrating from the prototype plugin API

The unreleased prototype plugin API — its `nevercGetPluginInfo` entry point, the
single `NevercHostAPI` vtable, the `Register*Pass` calls, the `NEVERC_INTERPOSE_*`
hooks, and the `-fplugin-pass=` loader — has been removed before the first public
release. The first public ABI is the phase-based descriptor ABI documented in
[README.md](README.md): plugins export `neverc_plugin_entry` and negotiate
independently versioned capability tables.

There is no compatibility shim and no `v1`/`v2` split. Recompile the plugin
*source* against the public headers; this page maps every prototype construct to
its first-version replacement, a semantic change, or an explicit non-carry-over.

## Prototype binaries are rejected

Loading a prototype shared object fails with a stable diagnostic:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

A library that exports neither entry point fails with
`plugin has no 'neverc_plugin_entry' entry`. Nothing is loaded until the source
is ported.

## Entry point

| Prototype | First public ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

The entry no longer *returns* a struct by value. It fills a caller-provided
`NevercPluginDescriptor`, honoring `OutPlugin->Header.StructSize`, and returns a
`NevercStatus`. Query the capability tables you need from `Bootstrap` before you
advertise support for them.

## `NevercPluginInfo` fields

| Prototype field | First-version mapping |
|---|---|
| `APIVersion` | `Descriptor.Header` (`NevercABITableHeader` with `StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`), plus a stable reverse-DNS `Descriptor.PluginID` used to key per-scope state |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`, plus the lifecycle callbacks `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(no prototype equivalent)* | `Descriptor.Concurrency` and `Descriptor.Reentrancy` must be declared truthfully (for example `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## Host access: one vtable → capability tables

The prototype passed one 200-plus-entry `NevercHostAPI` vtable to every callback
and guarded new fields with `NEVERC_API_FN`. The first version replaces it with
independently versioned capability tables, queried on demand:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

Require the matching major and check `TableSize` with `offsetof` before reading a
field. Interfaces are scoped per domain: Core, Driver, Source, Prep, AST, Sema,
IR, MIR, Target, MC, Object, Link, LTO, and DynCode.

## Registration: `Register*Pass` + hooks → observers/interceptors/providers

Prototype registration attached a callback to a hook:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

The first version registers, inside `Register`, a typed handler against a phase
identified by a 128-bit `NevercInterfaceID`:

| Prototype call | First-version registrar call |
|---|---|
| read-only pass | `Registrar->RegisterObserver(NevercObserverDescriptor)` with `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` points |
| pass that wraps or short-circuits a phase | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`; call `Continuation->InvokeNext` at most once and set `OutResult->Action` |
| pass that replaces a built-in transform | `Registrar->RegisterProvider(...)` on a `REPLACEABLE` phase |
| `-fplugin-pass-arg=` reading | `Registrar->RegisterOption(NevercOptionDescriptor)` to declare a real driver option |

A prototype "module pass at `PRE_OPT`" becomes an observer, interceptor, or
provider on the IR phase `neverc.ir.pass.pre_opt`.

## Hook → phase mapping

| Prototype hook | First-version phase (name) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | LTO phases `neverc.link.lto_resolve` / `neverc.link.lto_generate` (see [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `neverc.link.layout` observed at `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | the typed dyncode phases in [dyncode.md](dyncode.md) |

The normative list of phase IDs, policies, stability tiers, and verifier gates is
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`; the executable coverage
contract is [coverage.json](coverage.json). A hook that used to be a single point
may map to more than one phase ID, each with its own policy and proof.

## Pass callbacks, handles, and byte edits

| Prototype | First version |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` and friends | callbacks receive a `NevercPhaseFrame`; IR/MIR/AST/graph objects are typed, scoped, opaque handles obtained from the relevant capability table (see [ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md)) |
| generic `NevercValueRef` | removed in favor of typed IR handles |
| in-place mutation of a live `Ref` | all changes go through transactional host APIs |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | removed; dyncode byte edits use the checked image builder (read/write/insert/append/resize), see [dyncode.md](dyncode.md) |

Handles and borrowed views are valid only for the callback scope, exactly as
before; do not cache them after the callback returns.

## Removed convenience layers

The prototype bundled general-purpose helpers into the vtable. These are **not**
part of the first public ABI:

| Prototype | First version |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | not carried over; use `Core->Allocate`/`Core->Deallocate` with your own containers, or the typed domain APIs |
| `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` macros | replaced by the typed iteration in each domain's capability table |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | declare options with `RegisterOption` and read them through the Driver API |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## Loading and command line

| Prototype | First version |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | the option spelling you declare in `RegisterOption` (for example `--driver-trace` or `--my-opt=value`) |
| two loaders (`-fplugin` vs `-fplugin-pass`) | one loader; a module is handed to a single loader |

## Versioning

The prototype relied on a single, monotonically growing vtable plus
`NEVERC_API_FN` guards. In the first version each capability table is versioned
on its own: require the matching major, and check `StructSize`/`TableSize` before
reading an appended field. New functions are appended to a table's stable prefix
within the first ABI major, so a plugin built against an earlier minor keeps
working against a later host.

## Worked example

`pluginsdk/examples/DriverTracePlugin.c` shows the full first-version shape: the
`neverc_plugin_entry` descriptor, `ProcessBegin`/`Session`/`Task` lifecycle,
`RegisterOption` for a real CLI flag, a `RegisterObserver` on
`neverc.driver.raw_arguments`, and a `RegisterInterceptor` on
`neverc.driver.execute_job` that calls `InvokeNext` exactly once.
`pluginsdk/examples/ExamplePlugin.c` covers IR, MIR, object, and link phases.
