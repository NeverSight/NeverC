# NeverC Plugin ABI

NeverC's first public plugin ABI is a pure C, phase-based interface. Plugins
export `neverc_plugin_entry`, negotiate versioned capability tables, and run
inside explicit Process, Session, and Task scopes.

The unreleased prototype API and its `nevercGetPluginInfo` entry point have
been removed. Prototype binaries are rejected with a migration diagnostic;
recompile their source against the public headers.

## Start here

- [Core and driver overview](../../pluginsdk/examples/DriverTracePlugin.c)
- [Source API](source.md)
- [Preprocessor API](prep.md)
- [AST and semantic API](ast-sema.md)
- [IR API](ir.md)
- [MIR API](mir.md)
- [Target, MC, assembly, and object APIs](target-mc-object.md)
- [Phase coverage evidence](coverage.json)
- [Custom calling conventions](custom-callconv/README.md)

## Minimal workflow

Include either the aggregate header or only the capability headers you use:

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

Export the descriptor entry point:

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap,
    NevercPluginDescriptor *OutPlugin);
```

Build a shared module and load it with `-fplugin`:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c

neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

Use `.so`, `.dylib`, or `.dll` as appropriate for the host.

## ABI rules

- Query capability tables with `NevercBootstrapAPI.QueryInterface`.
- Require the matching major version and check `StructSize` before using a
  field.
- Initialize every public structure's header and reserved storage.
- Treat handles and borrowed views as scoped, opaque values.
- Return `NevercStatus`; do not pass exceptions or host-owned pointers across
  the C boundary.
- Declare plugin concurrency and reentrancy accurately.
- Perform IR, MIR, AST, graph, and artifact changes through transactional host
  APIs.

New functions are appended to independently versioned capability tables. The
stable prefix of a table does not change within the first ABI major.

## Examples

Build all examples:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

The SDK includes driver tracing, virtual source, AST rewrite, IR and MIR
passes, custom calling conventions, MC emission observation, transactional
ObjectGraph rewriting, a no-CRT example, and an ABI call microbenchmark under
`pluginsdk/examples`.

The phase schema in
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` is the normative source
for built-in phase IDs, policies, stability, and verifier gates.
