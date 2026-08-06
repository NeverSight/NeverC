**Languages**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC Plugin ABI](README.md)

# Python plugins

NeverC can load a Python source file through the same `-fplugin=` option used
for native plugins. A normal source build enables Python plugins and bundled
runtime installation by default:

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

Fresh builds default `NEVERC_ENABLE_PYTHON_PLUGINS=ON` and
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`. CMake may use the system Python for build
scripts, but that interpreter does not select the plugin ABI. NeverC separately
downloads a checksum-pinned CPython 3.12.10 development/runtime distribution,
links the plugin bridge to it, stages it in `build/python`, and installs that
same runtime in the adjacent `python/` directory. Both ordinary source builds
and official archives therefore run plugins on CPython 3.12.10; neither the
build-tree nor installed compiler needs an external Python runtime,
`PYTHONHOME`, or `PYTHONPATH`.

For an offline build, set `-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10` to a pre-extracted exact
CPython 3.12.10 development/runtime tree. NeverC validates and copies that tree
into the build directory without modifying the supplied source.

On Linux the install-time bundler requires `patchelf` on `PATH`. Managed Python
plugin builds currently require a native build because CMake executes an ABI
probe; cross builds must disable the Python feature or provide a separate
native target packaging stage. To intentionally build a Python-free compiler,
pass both `-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` and
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF`.

Install the authoring package with `python3 -m pip install
./pluginsdk/python`, set `PYTHONPATH` to that directory, or build/install the
`neverc-pluginsdk` component. NeverC also discovers a staged SDK at
`<directory containing neverc>/../pluginsdk/python`.

## A minimal plugin

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

Load it by filesystem path:

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

The decorator accepts one canonical plugin ID, a non-empty display name, and a
strict semantic version. A script declares exactly one plugin class. Different
scripts are independent modules and may be mixed with native plugins.

## Lifecycle

All hooks are optional:

- `on_process_begin(ctx)` and `on_destroy(ctx)` bracket the compiler process.
- `register(ctx)` registers options and observers before the phase graph is
  frozen.
- `on_session_begin(ctx)` and `on_session_end(ctx)` bracket an invocation.
- `on_task_begin(ctx)` and `on_task_end(ctx)` bracket a unit of compiler work.

A begin hook may return a Python value or assign `ctx.state`; that value is
available on the matching end hook. Other hooks and observer callbacks must
return `None`. The default descriptor is session-serial and non-reentrant;
`@Plugin` can select the same concurrency and reentrancy models as a native
plugin.

## Options and observers

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.trace", name="Trace", version="1.0.0")
class TracePlugin:
    def register(self, ctx):
        ctx.option(
            "--trace-python",
            kind="flag",
            value_type="bool",
            help="Trace raw driver arguments",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe,
        )

    def observe(self, frame):
        if frame.option_values("--trace-python"):
            frame.check_cancelled()
            frame.emit_remark(f"arguments: {frame.arguments}", code=1001)
```

`neverc_plugin.phases` contains all 130 built-in phase constants generated
from the normative phase schema. Observer frames expose phase and route data,
opaque input/output handles, parsed plugin option values, diagnostics,
cancellation, and raw arguments for `driver.RAW_ARGUMENTS`. Native context and
frame handles are lifetime checked: using a retained object after its callback
raises `RuntimeError`.

Option kinds are `flag`, `joined`, `separate`, and `multi_arg`; value types are
`bool`, `int`, `uint`, `string`, `enum`, and `path`; multiplicities are
`single`, `last_wins`, and `append`. Enum options pass an `enum_values={name:
integer}` mapping. `argument_count` applies only to `multi_arg`.

## Complete C ABI access

The high-level lifecycle, option, and observer helpers sit on top of the full
public C plugin ABI. The checked-in Python ABI is generated from the same
headers and Clang layouts as the C SDK. The current inventory contains all 36
official interface tables, 366 public records, 815 public function-pointer
fields, more than 5,000 constants, and all 75 `UserData` callback slots. CI
fails if a C header changes without regenerating or testing the Python view.

```python
from neverc_plugin import abi
from neverc_plugin.domains import ir
from neverc_plugin.ffi import bind_callbacks, require_ok


def register(self, context):
    scope = context.ffi
    core = ir.CORE.query(scope)
    builder = ir.BUILDER.query(scope)
    passes = ir.PASS.query(scope)
    # core.function("GetValueKind") has the exact generated C signature.
```

`neverc_plugin.abi` exports the `ctypes` record, union, enum, typedef,
constant, callback, function signature, and host-layout definition for every
public C declaration. Each module under `neverc_plugin.domains` exposes thin
descriptors for its official tables; `Interface.query()` performs the native
`QueryInterface` call and checks version and `StructSize`. `TableView.function`
and `TableView.call` invoke any generated slot without a feature-specific
Python shim.

Use `bind_callbacks(scope, descriptor, callbacks)` for observers,
interceptors, providers, passes, analyses, target/MC/object/link/LTO/dynamic-code
providers, and every other descriptor carrying `UserData`. The native bridge
installs generated C-callable trampolines, owns transferred callback state,
holds the GIL while calling Python, converts exceptions to structured plugin
diagnostics, and invalidates the callback `Scope` on return. A callback receives
that scope first, followed by exact integer values, pointer addresses, or owned
bytes for by-value records. `decode_record()` converts record bytes back into a
generated `ctypes` value. Output pointers remain writable through `ctypes`.
Returning `None`, `True`, or a zero status means success; a status integer,
three-item `(code, flags, detail)` tuple, or generated status record returns an
explicit native status.

`Transaction` provides exactly-once commit/abort/destroy handling and
`OneShotContinuation` prevents a continuation from being invoked twice. All
tables, pointers, and scopes are lifetime checked. Do not retain raw host
pointers after their callback, and do not call a table from a context that does
not advertise the required capability.

`@Plugin` also maps the complete native descriptor metadata: ABI flags,
concurrency, reentrancy, required and optional interfaces, dependency kind,
semantic-version ranges, and prerelease policy. See the generated
`neverc_plugin.abi` module and the native C headers for the authoritative field
contracts.

## Python OLLVM example

The SDK ships a compiler-transforming plugin written only against this public
binding at
[`pluginsdk/python/examples/ollvm`](../../pluginsdk/python/examples/ollvm/README.md).
It implements deterministic classic instruction substitution (SUB), bogus
control flow (BCF), and control-flow flattening (FLA):

```sh
neverc -fplugin=/path/to/ollvm_plugin.py \
  --ollvm-sub --ollvm-bcf --ollvm-fla \
  --ollvm-seed 42 --ollvm-probability 80 \
  input.c -o output
```

## Errors, security, and current scope

An uncaught Python exception becomes `NEVERC_STATUS_PLUGIN_EXCEPTION`. NeverC
formats the traceback into a structured plugin diagnostic when a session/task
callback is active; import and activation failures include it in the loader
error. The embedded interpreter is process-wide and intentionally is not
finalized by NeverC, while per-plugin objects are released on unload.

Python plugins are trusted compiler extensions. They run in-process, can import
arbitrary modules, and have the same filesystem and process permissions as
NeverC. There is no sandbox.

The Python binding is not a sandbox and it is not a second, reduced compiler
API. It exposes the same stable C interface tables and mutation operations as a
native plugin through generated `ctypes` definitions and checked native
trampolines. The Python decorator and script loader replace the C shared-library
entry point; once activated, both plugin forms participate in the same phase
graph, registration system, capability checks, transactions, and diagnostics.
