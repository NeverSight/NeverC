# NeverC Plugin SDK

This directory packages the public, pure-C first-release plugin ABI and
buildable examples. Start with
[`docs/plugin-api/README.md`](../docs/plugin-api/README.md); the Target/MC/
assembly/object guide is
[`docs/plugin-api/target-mc-object.md`](../docs/plugin-api/target-mc-object.md).

Build every example with both the configured host C compiler and NeverC:

```sh
cmake --build build --target neverc-pluginsdk-examples
```

Notable examples:

- `DriverTracePlugin.c`: option registration, phase observation, and
  interception.
- `FunctionPass.c` / `MachinePass.c`: stable IR and MIR pass registration.
- `MCObserverPlugin.c`: read-only target-independent MC emission events.
- `ObjectRewritePlugin.c`: transactional ObjectGraph mutation before layout.
- `CustomCallConvPlugin.c`: schema-checked custom calling conventions.

Plugins should include `neverc/Plugin/NevercPluginAPI.h` or only the capability
headers they use. Do not link against LLVM or exchange C++ types across the
plugin boundary.
