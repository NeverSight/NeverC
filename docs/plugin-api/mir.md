**Languages**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# NeverC Plugin MIR API

The first public plugin ABI exposes Machine IR through `PluginMIR.h`. The API
uses stable C identifiers and opaque handles; plugins do not depend on LLVM
class layouts, enum numbers, or C++ ABI.

## Negotiation

Query `NEVERC_INTERFACE_MIR` for `NevercMIRAPI` and
`NEVERC_INTERFACE_MIR_PASS` for `NevercMIRPassAPI`. Check the returned table
size before using a function pointer and ignore fields appended by a newer
host.

The schema digest identifies the exact stable-to-host mapping in use.
`GetEntityInfo`, `GetOperandKindInfo`, `GetGenericOpcodeInfo`, and
`GetMachinePropertyInfo` expose canonical names and whether an operation needs
a target schema.

## Stable model

Opaque handles represent:

- machine functions and basic blocks;
- machine instructions and operands;
- mutation transactions;
- analysis results;
- constant-pool entries, frame objects, jump tables, memory operands, and
  target references.

Handles belong to one code-generation task. Erased entities, rolled-back
entities, and analysis results invalidated by a mutation become stale.

The generic schema covers target-independent opcodes, operand kinds, machine
properties, low-level types, instruction flags, register assignments, frame
objects, constants, jump tables, memory pointer forms, and atomic orderings.
Target-specific opcodes require an explicitly negotiated target schema.

## Reading MIR

`NevercMIRAPI` supports:

- machine-function properties and block traversal;
- predecessor, successor, live-in, instruction, and operand enumeration;
- instruction opcode and flag queries;
- all public machine operand forms;
- virtual and physical register information;
- frame, constant-pool, jump-table, and memory-operand state.

Use count/query pairs and bounded output buffers. Returned views are borrowed
for the current callback unless documented otherwise.

## Transactional mutation

MIR changes occur under a mutation lease:

1. `BeginMutation` for a machine function.
2. Create, move, or erase blocks and instructions.
3. Append or update operands and CFG edges.
4. Apply machine-property changes with the required proof.
5. `CommitMutation` or `AbortMutation`.

Commit performs structural preflight and Machine IR verification. Invalid
operands, CFG, generic opcode use, or property claims are rolled back
atomically. Abort restores block order, instructions, operands, CFG edges, and
machine properties.

Property changes use `NevercMIRPropertyProof`. A proof must either invalidate a
property whose assumptions are no longer valid or request a structural check
before establishing it.

## Passes and phases

`NevercMIRPassDescriptor.Level` supports MachineModule, MachineFunction, and
MachineBasicBlock adapters. Stable hooks are:

- post instruction selection;
- post legalization;
- pre/post scheduler;
- pre/post register allocation;
- post prologue/epilogue;
- pre-emit;
- final plugin slot.

Function passes may run in parallel code-generation partitions. Module-level
passes execute at serialized pipeline barriers. The plugin's concurrency and
reentrancy declarations still apply.

Every code-generation pipeline ends with a host-owned `MachineVerifier` after
the final plugin slot. It is a sealed gate and cannot be disabled by a plugin.

## Analyses

The analysis table exposes live variables, live intervals, slot indexes,
dominator tree, loop info, and register pressure. Availability depends on the
selected hook because some LLVM analyses do not exist before or after their
native pipeline stage.

Declare required and preserved analyses in the pass descriptor. A committed
mutation invalidates affected result handles. Claiming preserve-all after a
mutation is rejected.

## Minimal example

`pluginsdk/examples/MachinePass.c` registers a read-only machine-function pass
at the stable pre-emit hook.

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Use the platform module suffix produced by CMake.

## Safety requirements

- Do not retain task handles, MIR handles, or borrowed views after a callback.
- Do not manufacture handle values or LLVM opcode numbers.
- Do not mutate outside a lease.
- Initialize table headers and reserved storage.
- Return statuses across the C boundary; never allow a C++ exception to cross
  it.

See `PluginMIR.h`, `MIRSchema.json`, `PluginPhaseSchema.h`, and
`coverage.json` for normative declarations and coverage evidence.
