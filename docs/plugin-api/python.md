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
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`. Configuration needs CPython 3.10 or newer,
including its embedding headers and shared library. Installation automatically
copies that exact selected interpreter into the adjacent `python/` directory.
The raw build-tree executable may still use the build-time Python, but the
installed compiler needs no external Python, `PYTHONHOME`, or `PYTHONPATH` at
run time. Official NeverC archives select and bundle CPython 3.12.

On Linux the install-time bundler requires `patchelf` on `PATH`. Automatic
bundling is rejected when `CMAKE_CROSSCOMPILING` because a host interpreter
cannot be shipped with a target-architecture compiler; disable bundling and
package a target runtime explicitly for that case. To intentionally build a
Python-free compiler, pass both `-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` and
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
return `None`. v1 Python plugins are session-serial and non-reentrant.

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

## Errors, security, and current scope

An uncaught Python exception becomes `NEVERC_STATUS_PLUGIN_EXCEPTION`. NeverC
formats the traceback into a structured plugin diagnostic when a session/task
callback is active; import and activation failures include it in the loader
error. The embedded interpreter is process-wide and intentionally is not
finalized by NeverC, while per-plugin objects are released on unload.

Python plugins are trusted compiler extensions. They run in-process, can import
arbitrary modules, and have the same filesystem and process permissions as
NeverC. There is no sandbox.

v1 is deliberately read-only beyond option registration. It does not expose
interceptors, providers, artifact mutation, domain-specific IR/MIR/Link object
models, subinterpreters, manifests, or module/factory entry points. These need
transaction and continuation wrappers whose lifetimes can be enforced; the
native C ABI remains available when those capabilities are required.
