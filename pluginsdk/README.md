# NeverC Plugin SDK

This directory packages the public, pure-C first-release plugin ABI and
buildable examples. Start with
[`docs/plugin-api/README.md`](../docs/plugin-api/README.md); the Target/MC/
assembly/object guide is
[`docs/plugin-api/target-mc-object.md`](../docs/plugin-api/target-mc-object.md).
The optional Python authoring package lives in [`python/`](python/) and is
documented in [`docs/plugin-api/python.md`](../docs/plugin-api/python.md).

Build every example with both the configured host C compiler and NeverC:

```sh
cmake --build build --target neverc-pluginsdk-examples
```

Notable examples:

- [`DriverTracePlugin.c`](examples/DriverTracePlugin.c): option registration,
  phase observation, and interception.
- [`FunctionPass.c`](examples/FunctionPass.c) /
  [`MachinePass.c`](examples/MachinePass.c): stable IR and MIR pass
  registration.
- [`MCObserverPlugin.c`](examples/MCObserverPlugin.c): read-only
  target-independent MC emission events.
- [`ObjectRewritePlugin.c`](examples/ObjectRewritePlugin.c): transactional
  ObjectGraph mutation before layout.
- [`CustomCallConvPlugin.c`](examples/CustomCallConvPlugin.c): schema-checked
  custom calling conventions.
- [`DynCodeTracePlugin.c`](examples/DynCodeTracePlugin.c): observes the dyncode
  code-extraction pipeline.
- [`DynCodeEncoderPlugin.c`](examples/DynCodeEncoderPlugin.c): intercepts the
  dyncode charset-encode transition.

Plugins should include `neverc/Plugin/NevercPluginAPI.h` or only the capability
headers they use. Do not link against LLVM or exchange C++ types across the
plugin boundary.

## Header forms

The SDK ships two equivalent forms of the same pure-C ABI:

- **Single header** `neverc/Plugin/NevercPluginAPI.h` — a self-contained,
  generated aggregate
  ([`utils/plugin-api/gen-single-header.py`](../utils/plugin-api/gen-single-header.py))
  that needs no side-by-side module headers. Ideal for dropping into an
  existing project.
- **Modular headers** `neverc/Plugin/Plugin*.h` — include only the domains a
  plugin uses to minimize its compile surface.

[`manifest/plugin.json`](manifest/plugin.json)
([`utils/plugin-api/gen-sdk-manifest.py`](../utils/plugin-api/gen-sdk-manifest.py))
records the ABI version, every public interface ID/version/stability, schema
digests, and the supported targets, so a consumer can validate an SDK against a
host.

[`abi/plugin.json`](abi/plugin.json)
([`utils/plugin-api/gen-abi-manifest.py`](../utils/plugin-api/gen-abi-manifest.py))
records the measured
size, alignment and field offsets of every public struct, grouped by host ABI
key (`{arch}-{endian}-{pointer width}-{calling convention}`). Every supported
host is listed, so a plugin's build can assert its layout against the key it
will load into. The layouts are in fact identical across those keys — that is
what the fixed-width types and `NEVERC_ABI_PACK_BEGIN/END` are for — but a
plugin binary is still only promised compatibility with its own host ABI key.

## Installing and consuming the SDK

```sh
cmake --build <build> --target neverc-pluginsdk
cmake --install <build> --prefix <prefix> --component neverc-pluginsdk
```

The install tree under `<prefix>/pluginsdk` contains `include/`, `schemas/`,
`manifest/`, `examples/`, `templates/`, a CMake package config
(`cmake/NevercPluginSDKConfig.cmake`) and a pkg-config file
(`pkgconfig/neverc-plugin.pc`).

Build a plugin against the installed SDK with CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

or with pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Copy [`templates/minimal/`](templates/minimal) to scaffold a new plugin
project. The consumer check
[`utils/plugin-api/test-installed-sdk.py`](../utils/plugin-api/test-installed-sdk.py)
builds fixtures and the template against an installed prefix (`--prefix
<prefix>`) with an independent compiler.
