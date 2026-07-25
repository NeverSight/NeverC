**Languages**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC Plugin IR API

The first public plugin ABI exposes LLVM IR through stable C tables. Plugins do
not include LLVM headers and must not cast NeverC handles to LLVM objects.

## Interfaces

Query interfaces from `neverc_plugin_entry` with
`NevercBootstrapAPI.QueryInterface`:

- `NEVERC_INTERFACE_IR_CORE` — module, type, value, CFG, metadata, attribute,
  constant, and serialization queries.
- `NEVERC_INTERFACE_IR_BUILDER` — transactional IR construction and mutation.
- `NEVERC_INTERFACE_IR_ANALYSIS` — built-in and plugin-defined analyses.
- `NEVERC_INTERFACE_IR_PASS` — Module, CGSCC, Function, and Loop passes.
- `NEVERC_INTERFACE_IR_GEN` — SemanticUnit-to-IR replacement.
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — complete optimization-pipeline
  replacement.

Always request the header's major/minor pair and verify that the returned
`StructSize` reaches the last function pointer used by the plugin. A newer host
may append fields; a plugin must ignore unknown tails.

## Handles and ownership

IR handles are opaque `{Owner, Value}` pairs scoped to a task. The host owns all
objects referenced by them.

- Never retain a task-scoped handle after its callback or task ends.
- Never use a handle in another session or task.
- A committed replacement invalidates handles for replaced objects.
- An aborted mutation makes handles created by that mutation stale.
- APIs report `NEVERC_STATUS_STALE_HANDLE`, `WRONG_OWNER`, or `WRONG_TYPE`
  instead of exposing an LLVM pointer.

Strings and byte views returned by query calls are borrowed unless an API
explicitly returns a releasable buffer.

## Reading IR

`NevercIRCoreAPI` provides:

- module identifier, triple, data layout, and inline assembly;
- stable value cursors for functions, globals, blocks, instructions, uses, and
  operands;
- stable type and opcode IDs;
- function, global, instruction, metadata, and attribute properties;
- integer, floating-point, aggregate, null, poison, and undef constants;
- bitcode export/import and verified module artifacts.

Collection cursors are bounded: pass an output capacity and repeat collection
until the returned count is zero.

## Transactional mutation

All structural mutation uses `NevercIRBuilderAPI`:

1. Begin a module or function mutation.
2. Create a builder bound to that mutation.
3. Set the insertion point and build instructions, functions, or blocks.
4. Commit the mutation.
5. Destroy builders and the mutation handle.

Commit verifies the candidate IR and publishes it atomically. On verifier
failure, the host rolls the mutation back and keeps the previous module.
`AbortMutation` always rolls back staged changes.

Do not claim `NEVERC_IR_PRESERVE_ALL` after changing IR. The pass adapter checks
the module generation and rejects an inconsistent preservation declaration.

## Pass levels and phases

`NevercIRPassDescriptor.Level` supports:

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

Stable insertion phases are `PRE_OPT`, `PIPELINE_START`, `OPTIMIZER_LAST`,
`POST_OPT`, and `PRE_CODEGEN`. The invocation contains only the handles valid
for its level. Function and loop passes may execute concurrently, so mutable
plugin state must follow the declared concurrency contract.

The host always executes the final sealed IR verifier. Plugins cannot replace,
intercept, or skip that gate.

## Analyses

Built-in analysis IDs cover the call graph, dominator tree, post-dominator tree,
loop info, scalar evolution, MemorySSA, and alias analysis.

Plugin analyses declare dependencies and lifecycle callbacks. Results are
cached per invocation and invalidated according to the pass preservation
result. Recursive dependency cycles and mutation from an analysis callback are
rejected.

## Complete providers

An IR-generation provider can replace built-in lowering and publish a verified
module artifact. An optimization provider can replace the complete built-in
optimization pipeline. Both routes:

- consume explicit phase input;
- publish through a host API instead of returning an LLVM pointer;
- verify target compatibility and module validity;
- atomically retain the old module when publication fails.

The final verifier remains mandatory after an optimization provider.

## Minimal example

`pluginsdk/examples/FunctionPass.c` is a read-only function pass.
`pluginsdk/examples/ExamplePlugin.c` shows module enumeration, and
`pluginsdk/examples/CustomCallConvPlugin.c` demonstrates attributes and
call-site properties.

Build and load an example:

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Use the platform module suffix produced by CMake.

## Failure rules

Return a `NevercStatus` from every callback. Plugin failures become structured
diagnostics; do not throw exceptions across the C boundary. Initialize every
output table header and reserved field, and return `INVALID_ARGUMENT` for a
missing required pointer.

See `PluginIR.h`, `PluginPhaseSchema.h`, and `coverage.json` for the normative
ABI declarations, phase policies, and test evidence.
