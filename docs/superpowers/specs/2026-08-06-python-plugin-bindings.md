# Python Plugin Bindings v1 Specification

## Origin

This specification turns [NeverSight/NeverC issue #3](https://github.com/NeverSight/NeverC/issues/3)
into a buildable first release. It preserves the issue's goals while correcting
parts of the proposed architecture that do not compose with NeverC's existing
process/session/task lifecycle.

## Goal

Allow a user to pass a Python source file to `-fplugin=` and author a real
NeverC plugin in Python, while leaving the public C11 plugin ABI and all native
plugin behavior unchanged.

## v1 acceptance criteria

- `neverc -fplugin=/path/to/plugin.py ...` discovers, loads, activates, and
  destroys the Python plugin through the same registry and activation plan used
  by native plugins.
- Multiple Python scripts can be loaded together. Identity and deduplication are
  based on each script file, not on a shared bridge image.
- A Python plugin can declare its ID, display name, and semantic version with a
  class decorator.
- The complete process/session/task lifecycle is available through optional
  Python hooks.
- Registration supports plugin options and read-only phase observers. All 130
  built-in phase constants are generated from the normative phase schema.
- Observer frames expose generic phase/route/handle data, structured diagnostic
  helpers, cancellation checks, plugin option lookup, and raw driver argument
  inspection.
- Python exceptions become `NEVERC_STATUS_PLUGIN_EXCEPTION`; during session/task
  callbacks and observers, the formatted traceback is emitted as a structured
  plugin diagnostic.
- Native `.so`, `.dylib`, and `.dll` plugins remain byte-for-byte ABI compatible
  and mixed native/Python loading works.
- Python support is optional at build time. When enabled, the plugin ABI and
  runtime use NeverC's checksum-pinned CPython 3.12.10 distribution; the
  system Python remains only a build-script interpreter.
- The pure-Python SDK is copied into the build-tree Plugin SDK and installed as
  part of the existing `neverc-pluginsdk` component.

## Corrected architecture

### Host-internal runtime adapter, not a shared-library masquerade

The issue sketches a single bridge shared library exporting
`neverc_plugin_entry`, with a thread-local script path set only while the entry
point runs. That identifies a script while its descriptor is created, but the
later `ProcessBegin` callback has no descriptor or user-data argument. Multiple
descriptors using the same callback therefore cannot reliably recover which
script they belong to.

Instead, `PluginModule` gains host-internal lifecycle dispatch methods backed by
an optional runtime adapter. Native modules keep invoking their existing C
function pointers. Python modules own one `PythonPluginRuntime` each and invoke
the same lifecycle semantically through that adapter. This changes no public C
layout, symbol, or negotiation behavior.

The CPython interpreter is process-wide and shared, but script modules, plugin
instances, registrations, and lifecycle state are per `PluginModule`.

### Direct embedded binding, not generated `ctypes`

`pluginsdk/abi/plugin.json` is useful for consumers that must reproduce C
layouts. An in-host bridge already compiles against the authoritative headers,
so generating hundreds of `ctypes` definitions would add a second FFI layer,
callback lifetime hazards, and no useful compatibility boundary. v1 therefore
uses the CPython C API directly. Machine-readable generation is used where it
adds value: Python `Phase` constants are generated from `PhaseSchema.json`.

### Observer-first surface

v1 exposes every stable phase for read-only observation but only binds the
generic frame plus the raw-driver-arguments accessor. Interceptors, providers,
and mutation APIs are intentionally deferred: they require domain-specific
transaction wrappers and continuation lifetime enforcement, and a partial
unsafe mutation binding would be worse than an explicit unsupported surface.

### Explicit discovery and optional dependency

Only filesystem paths ending in `.py` are Python plugins in v1. Manifest files
and `package.module:factory` syntax are deferred until their resolution,
isolation, and packaging rules are specified. `NEVERC_ENABLE_PYTHON_PLUGINS`
defaults to `OFF`; when enabled, CMake requires `Python3 >= 3.10` with
`Development.Embed`.

## Python authoring API

Canonical package and distribution name: `neverc_plugin` / `neverc-plugin`.

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(
    id="org.neverc.example.driver-trace",
    name="Driver Trace (Python)",
    version="1.0.0",
)
class DriverTracePlugin:
    def register(self, ctx):
        ctx.option(
            "--driver-trace",
            kind="flag",
            value_type="bool",
            help="Trace driver phases",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.on_raw_arguments,
        )

    def on_raw_arguments(self, frame):
        if frame.option_values("--driver-trace"):
            frame.emit_remark(
                f"raw arguments: {frame.arguments}", code=1001
            )
```

`Plugin` is the sole declarative mechanism in v1. Registration remains an
imperative `register(ctx)` call because NeverC freezes the graph immediately
after that call.

Optional hooks are:

- `on_process_begin(ctx)`
- `on_session_begin(ctx)` / `on_session_end(ctx)`
- `on_task_begin(ctx)` / `on_task_end(ctx)`
- `on_destroy(ctx)`

Begin hooks may return a Python value, which becomes `ctx.state`. Contexts also
allow direct assignment to `state`. Hook and observer return values must
otherwise be `None`.

## Lifetime and concurrency rules

- Python plugins always advertise `NEVERC_CONCURRENCY_SESSION_SERIAL` and
  `NEVERC_REENTRANCY_NONE` in v1.
- Every transition from host code into Python acquires the GIL.
- Registration and frame objects carry checked native handles. Once their host
  callback returns, retaining and using them raises `RuntimeError` instead of
  dereferencing stale C pointers.
- Session and task contexts remain valid for their matching begin/end interval.
- Observer callback objects are owned by NeverC registration records and are
  decref'd through `DestroyUserData` before the Python runtime is destroyed.
- NeverC does not call `Py_Finalize`; finalizing an embedded interpreter while
  extension objects or foreign threads may still exist is unsafe. Per-plugin
  objects are still released deterministically on unload/shutdown.

## Diagnostics and errors

- Import, metadata, and activation errors include a formatted Python traceback
  in the loader/activation error text.
- Exceptions raised inside an active session/task callback produce an error
  diagnostic owned by that callback transaction and return
  `NEVERC_STATUS_PLUGIN_EXCEPTION` with the diagnostic detail token.
- Invalid metadata, duplicate IDs, option descriptors, and observer phase/point
  combinations are rejected by the existing host validation paths.
- Loading `.py` while support is disabled returns an actionable rebuild
  diagnostic rather than attempting `dlopen`.

## Packaging and discovery

The SDK lives under `pluginsdk/python/` with a PEP 517 `pyproject.toml`. The
existing `neverc-pluginsdk` build-tree copy stages it at
`<build>/pluginsdk/python`. Installation places it at
`<prefix>/pluginsdk/python`.

At runtime the bridge prepends the SDK path adjacent to the current NeverC
executable when present. Normal Python installation and `PYTHONPATH` continue to
work. The plugin script's directory is available while the script is imported
so local imports work during module initialization.

## Deferred work

- Interceptors, providers, continuation calls, and artifact mutation.
- Domain-specific Source/IR/MIR/Link object models.
- Thread-safe/free-threaded Python plugin concurrency.
- `.neverc-plugin` manifests and module/factory entry-point syntax.
- Wheel production in release CI and PyPI publication.
- Subinterpreter isolation and hot reload while sessions are active.
