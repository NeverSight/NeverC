# Target, MC, Assembly, and Object Plugins

NeverC's first-release plugin ABI lets a C plugin describe a target, replace
code-generation routes, observe machine-code emission, parse or print
assembly, and read or write object files. The public boundary is a pure C ABI:
plugins must not exchange LLVM C++ objects, STL types, exceptions, or
host-owned pointers whose lifetime is not stated by an API table.

## Compatibility tiers

Target-independent descriptors, phase IDs, artifact IDs, MC containers,
ObjectGraph containers, output transactions, and callback contracts are
STABLE first-release ABI. Target-specific opcode, register, operand, fixup,
relocation, and calling-convention schemas are LOCKSTEP. A plugin must compare
the target schema ID and digest before consuming LOCKSTEP values. NeverC
rejects mismatched schemas before invoking the provider.

## Register a target and code-generation route

Query `NevercTargetAPI` during registration, register one or more
`NevercTargetDescriptor` records, and attach target-machine descriptors and
code-generation edges. A route is selected from the canonical target key:
target ID, triple, CPU, features, ABI, relocation model, code model, object
format, and schema digest.

Fine-grained routes use `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`.
A coarse edge may replace the whole `IR -> ObjectImage` route. Coarse output
still passes the host's mandatory product verifier and transactional output
commit; a provider cannot bypass either gate.

## Build and observe MC

`NevercMCAPI` owns task-local `MCUnit` mutations. Begin a mutation, create
sections, fragments, symbols, expressions, instructions, and operands, then
commit or abandon it. Handles are task scoped and generation checked.

The target-independent emission stream exposes ordered events for section
changes, labels, instructions, alignment, symbol attributes, CFI, debug
locations, and data. `neverc.mc.emission.pre_instruction` is replaceable; the
remaining event phases are read-only observation points. See
`pluginsdk/examples/MCObserverPlugin.c`.

Encoding, decoding, and layout providers operate on the same target key and
schema digest. Layout owns relaxation and emits a proof digest. Any mutation
after layout invalidates that proof and forces relayout before object writing.

## Replace assembly syntax

An assembly parser provider consumes source bytes and publishes an `MCUnit`.
An assembly printer consumes an `MCUnit` and writes only through the supplied
output transaction. Preprocessed assembly (`.S`) uses the normal frontend
preprocessor before the parser provider; plain assembly (`.s`) enters the
parser directly.

Providers stage output first. Parse/print verification and the host commit gate
run before bytes become visible, so failure leaves no partial output.

## Read, rewrite, and write objects

`NevercObjectAPI` represents a relocatable file as a normalized ObjectGraph:
sections, symbols, relocations, groups/COMDATs, imports/exports, TLS metadata,
unwind records, and debug records. Built-in adapters cover ELF, COFF, and
Mach-O, while plugins may register additional formats.

The object pipeline is:

1. probe and read bytes into an ObjectGraph;
2. run `object.pre_write` graph interceptors;
3. layout and run `object.post_layout` (relayout after mutation);
4. write a bounded candidate image;
5. run `object.post_write` binary interceptors;
6. execute the sealed final verifier and atomic host commit.

Observers receive read-only bridges. Mutations attempted from an observer are
rejected with `NEVERC_STATUS_POLICY_VIOLATION`. Writers and post-write
interceptors can access only the bounded transactional builder; overflow,
callback failure, or verification failure aborts staging. See
`pluginsdk/examples/ObjectRewritePlugin.c`.

## Concurrency and failure rules

- Keep mutable state in process/session/task state supplied by the host.
- Do not cache task handles or borrowed views after a callback returns.
- Invoke an interceptor continuation at most once and on the callback thread.
- Return the original `NevercStatus`; do not publish partial products.
- Declare the narrowest truthful concurrency and reentrancy modes.

The executable coverage contract is
`docs/plugin-api/coverage.json`. It maps each stable phase to positive,
negative, replacement, read-only observer, and sealed-gate tests.
