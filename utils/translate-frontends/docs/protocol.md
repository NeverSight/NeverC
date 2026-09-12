# Translate semantic protocol v1

This is the private, versioned semantic interchange between NeverC’s built-in full-Clang frontend child and its translation driver. It is independent of manifest, diagnostic-report, and source-map schema versions. JSON is UTF-8; no source snippets, AST pointers, absolute paths, or platform binary structures are executable protocol content. Maximum response size is 32 MiB; structural nesting and instruction counts are bounded by the consumer.

The driver invokes its current executable as `neverc __neverc_cpp_frontend --request request.json --output response.json`. This is a private mode, not a separately installed tool. A request contains `protocol: 1`, `profile: "cpp-core-v1"`, absolute `root` and `source` paths, a native `target` triple, and `arguments` (a JSON string array). Absolute request paths are diagnostic/process context only. Normalized artifact paths are relative to root. The driver validates source options before invocation; the frontend independently validates them before Clang. It never runs source programs or commands from a compilation database.

Requests are bounded regular files of at most 1 MiB and JSON nesting depth 32;
the main source is limited to 8 MiB. The request envelope is closed: unknown or
profile-inapplicable fields fail. Core has exactly the six fields above, project
adds `translation_unit`, `configuration_id` and `working_directory`, and math
also adds the two-field `sdk` object defined below. Existing response files are
never overwritten.

Both successful and failed frontend responses are checked against the 32 MiB
size limit and a structural nesting limit of 64 before recursive JSON parsing.
Malformed or over-deep failure responses cannot bypass the normal protocol
checks. Driver file inputs must be regular files; the bounded reader checks the
opened file identity and limits every read, including files that grow during
reading. FIFOs, directories and devices are rejected without waiting for input.

## Compiler execution environment

Every manifest declares `compiler_environment_policy: "neverc.translate.execution-env.v1"`. This driver-owned policy applies to every translator child process, including the frontend child, compiler resource lookup, generated-source validation, and runtime link probe. The child environment starts empty and inherits only these variables when they are present in the parent environment: `PATH`, `HOME`, `USERPROFILE`, `SystemRoot`, `SystemDrive`, `COMSPEC`, `PATHEXT`, `TMPDIR`, `TMP`, and `TEMP`. Their names are spelled exactly as listed. The driver then supplies `LC_ALL=C` and `NEVERC_NO_DEFAULT_CONFIG=1` with fixed values. All other inherited variables are omitted, including compiler include paths, deployment-target overrides, and loader overrides.

Compiler invocations and the manifest's `compiler_options` and `compilation_recipe` also include `--no-default-config`. A generated recipe is an argument vector and must be replayed with the declared environment policy, the recorded target and options, and the validated compiler/runtime inputs. Replaying it under a later user-supplied compiler environment is outside translation validation; the recipe's argument vector alone does not reproduce the validated environment. The environment policy is manifest metadata controlled by the driver, not frontend-provided authorization or semantic IR.

## Process deadlines and cancellation

Each translator child has a bounded execution deadline (60 seconds by default).
On timeout or invocation cancellation, the driver requests immediate termination
of that owned child, never a process group. It polls for at most another 200 ms
to reap the child and closes its Windows handle before returning. Ordinary
children are reaped; if the operating system keeps a killed process unreapable,
the diagnostic records this condition and translation cleanup still proceeds.
The runner reports failure, and no generated artifacts are accepted from that
process. Artifact publication and crash recovery follow the [design contract](design.md#artifact-publication-and-recovery).

## Frontend response

A successful response contains:

```json
{
  "protocol": 1,
  "profile": "cpp-core-v1",
  "frontend": {"name": "neverc-cpp-frontend", "version": "20.1.8", "build": "cpp-frontend-1"},
  "target": {"triple": "aarch64-apple-darwin", "int_bits": 32, "pointer_bits": 64, "little_endian": true},
  "dependencies": [{"path": "input.cpp", "sha256": "<64 lowercase hex digits>"}],
  "records": [],
  "globals": [],
  "functions": [],
  "mappings": [],
  "diagnostics": []
}
```

The wire frontend name `neverc-cpp-frontend` remains a semantic identity; it does not name an installed executable. The build ID carries a digest of the embedded frontend/build inputs.

Failure returns nonzero and a response containing protocol/profile/frontend and diagnostics when possible. No partially valid module is consumed. Each diagnostic has string `code`, `file`, `construct`, `reason`, `guidance` and positive integer `line`, `column`; driver-level diagnostics use the input path and location 1:1 when no more precise source location exists.

## Types, declarations and identity

Types are strings: `int`, `uint`, `bool`, `void`, or the identifier of a record. Core int/uint are exactly 32 bits, bool is a logical value, and void is only a function result. Record declarations are ordered by dependency, contain `id`, `fields` (array of `{name,type}`), and `loc`. No recursive/by-value cycles, empty records, padding inspection, foreign aggregate ABI, or duplicate fields are accepted. A `loc` is `{file,line,column}`.

Identifiers are ASCII C identifiers. Non-C-export declarations use an `nct_` prefix and a deterministic digest of their semantic identity. Internal-linkage identities include the normalized relative source path. Native C exports retain their explicit source name and must use scalar signatures; `main` retains its spelling, int return, and empty argument list. All emitted identifiers reject NC keywords and reserved runtime/compiler spellings, including the emitter-private `nct_emit_` prefix. Record typedefs, globals and functions occupy one disjoint ordinary-identifier namespace; parameters and locals are mutually distinct and cannot shadow module declarations. Field names are distinct within each record. Source C exports may not use the generated `nct_` namespace. Identifiers never depend on AST addresses or absolute roots.

Globals are `{name,type,value,loc}`. Their values must be fully folded literal/aggregate trees: the frontend resolves constant references, operators and conversions before serialization. They represent only checked compile-time constants and are never assignable. Functions contain `name`, `result`, boolean `internal`, boolean `c_export`, `params`, `locals`, `body`, and `loc`. Parameters/locals are `{name,type,loc}`. Each function has distinct local names; all storage declarations are emitted once at function entry, and initialization instructions remain in source execution order. This is sound only for the admitted trivial value types without references, addresses, destructors or variable-sized objects.

## Pure expressions

Every expression carries `kind`, `type`, and `loc`:

- `literal`: `value` is a canonical decimal integer JSON string for int/uint, or an actual JSON boolean (`true`/`false`) for bool. Integer strings have no leading plus sign, leading zeroes or negative zero. Int/uint ranges are checked.
- `var`: `name` refers to a parameter, local or global.
- `unary`: `operator` is `+`, `-`, `~`, or `!`; `args` has one expression.
- `binary`: `operator` is `+`, `-`, `*`, `/`, `%`, `<<`, `>>`, `&`, `|`, `^`, `==`, `!=`, `<`, `<=`, `>`, or `>=`; `args` has two expressions. Arithmetic operands already include source promotions/conversions. Comparisons produce bool. Shift operands can have distinct integer types; the result has the left operand's promoted type.
- `cast`: one argument in `args`; the destination `type` is an explicitly checked scalar conversion.
- `member`: `name` is the resolved field name; `args` contains the record base.
- `aggregate`: `args` has one explicitly initialized value per field in declaration order, including zero initialization required by C++ aggregate initialization.

Expressions have no calls, writes, increments, conditionals, short circuit operators or comma effects. The adapter materializes them through instructions, snapshots the accessed scalar/record value before later mutations, and selects a documented permitted order where C++ leaves it unspecified. A member read snapshots that member, without eagerly reading unrelated, possibly uninitialized fields. It evaluates assignment RHS before LHS; shift LHS before RHS; argument evaluations cannot interleave. Boolean/unsigned C++ conversions are explicit.

The core profile pins Clang's 32-bit two's-complement integer behavior. Conversion from uint to int preserves the low 32-bit value; emission implements this with `u <= 2147483647u ? (int)u : -1 - (int)(4294967295u - u)`, whose casts are representable. Signed left shift uses an unsigned shift followed by this conversion, preserving defined C++17 cases such as `1 << 31` without introducing C23 signed-shift undefined behavior. Signed right shift uses explicit arithmetic behavior: `a < 0 ? -1 - (int)((~(unsigned int)a) >> b) : (int)((unsigned int)a >> b)`. The emitter places these formulas in private static functions when needed, bounding generated text size without external runtime dependencies. These pure emitter calls are not additional protocol expression kinds. Shift counts and other arithmetic retain the source-defined-execution contract; this does not promise static diagnosis of every undefined source input. Other implementation-defined conversions remain unsupported until explicitly documented.

## Instructions and control flow

Each instruction has `op` and `loc`:

- `assign`: `target` is a var/member expression recursively rooted in mutable local/parameter storage; globals and temporary aggregates are not writable. `value` has the same type. Assigning an aggregate copies its value.
- `call`: `callee` refers to a defined function; `args` match its parameters. `target` is a `var` expression naming a local of the result type and is absent only for void results. Calls are standalone and ordered.
- `label`: a unique function-local `label` identifier.
- `jump`: a known destination `label`.
- `branch`: bool `condition`, destination strings `true` and `false`.
- `return`: `value` matches the result type, or is absent for void.

The body starts with a label. Each basic block ends with jump, branch or return; there is no implicit fallthrough, unreachable instruction after a terminator, or unknown label. Loops and break/continue use explicit labels, allowing the adapter to preserve condition, increment, and branch-local effects. Every nonvoid function must return on each reachable terminating path; an infinite loop is allowed. Default return 0 is emitted for the fallthrough of main only, following C++ semantics.

The consumer validates the entire module and all referenced symbols, types, fields, operator contracts, result/parameter types, globals, and block structure before NC emission. Calls outside the selected definition closure require a separately validated mapping; core has no mappings. Literal values never bypass expression validation as raw C text.

## Emission, validation and metadata

NC output declares records/prototypes before definitions, emits guarded native target/data-model requirements, and retains explicit sequencing statements. Maps contain generated line ranges and original source locations plus the generated-source hash. Manifests and reports each have schema major 1 and are separately documented in the design contract. Artifacts record frontend and NeverC build identity, target, normalized options and dependency hashes. Object validation uses the same target as source analysis. Generated programs are not run by translation.

## Core v2 integral representations

Core v2 adds canonical `i8`, `u8`, `i16`, `u16`, `i64` and `u64` type spellings;
`int`/`uint` remain the only canonical 32-bit forms. Literals are exact decimal
strings within the signed/unsigned range. Arithmetic and indices require
explicitly promoted 32- or 64-bit operands. Narrow storage, character values and
source enum types are normalized only after Clang resolves the source operations
and overloads. Integer conversions and shifts use width-matched emission helpers.
Constant `sizeof`/type-form `alignof` become exact literals after owned-source
inspection and layout verification; unevaluated operands produce no effects.
The [core v2 contract](cpp-core-v2.md#integer-widths-characters-and-size-queries)
defines the full admitted boundary. V1 profiles retain their scalar contracts.

## Core v2 layout evidence

`cpp-core-v2` requires `target.carrier_layout` and per-record `layout` evidence.
The driver derives expected carrier sizes and ABI alignments independently from
NeverC's own target configuration; the verifier also reconstructs plain-record
size, alignment and field offsets before emission. Generated static assertions
check these values during NC compilation. Manifests retain the carrier table
and `record_layouts`. See the [exact core v2 fields and compatibility rules](cpp-core-v2.md#verified-target-and-record-layout).
Older v1 profiles exclude this evidence.

## Gated mathematics extension

`cpp-math-v1` uses project schema 1 and the same owned declaration/ODR envelope
as `cpp-project-v1`. Admission requires independent SDK, exact-runtime and
link checks; differential and installed-output tests establish the published
support boundary. Core and project profiles reject math metadata, double types, and
mapped instructions.

The additional scalar type is `double`, with IEEE binary64 storage. Its literal
is `{kind:"literal",type:"double",bits:"<16 lowercase hex digits>",loc:...}`.
The bits encode the complete representation, including signed zeros, subnormals,
infinities, and NaN sign/payload/quiet bit. They never pass through a host JSON
number or decimal floating conversion. Global constants remain folded trees.
The emitter uses exact hexadecimal finite literals and compiler constant
expressions for infinity/NaN; no runtime initialization or type-punning helper is
needed for globals.

Admitted double operations are value copies, records, parameters/results,
explicit conversions to/from int32/uint32/bool, unary `+`/`-`, and the six ordered
or equality comparisons. Integer-to-double conversions are exact. Double-to-int
or uint truncates only on source-defined executions, where the truncated result
is representable; NaN, infinity, and out-of-range conversions gain no invented
saturation behavior. Double-to-bool treats either zero as false and NaN as true.
Double binary arithmetic, remainder, bitwise operations, shifts, increment,
compound assignment, float/long-double types, and custom source FP options or
FP-environment pragmas are outside this initial profile.

Every math unit includes these fields:

```json
{
  "fp_contract": "cpp.math.binary64.masked.v1",
  "sdk_distribution_id": "neverc-embedded-clang20.1.8-libcxx200100-macos15.5",
  "sdk_catalog_sha256": "<64 lowercase hex digits>",
  "sdk_dependencies": [
    {"root":"platform","path":"usr/include/math.h","sha256":"<64 lowercase hex digits>"}
  ],
  "mappings": [
    {
      "id":"cpp.math.floor.f64.v1",
      "declaration_id":"<64 lowercase hex digits>",
      "result":"double",
      "parameters":["double"],
      "origin":{"root":"platform","path":"usr/include/math.h",
                "sha256":"<64 lowercase hex digits>","line":466,"column":15}
    }
  ]
}
```

SDK roots are exactly `libcxx`, `resource`, and `platform`; their paths are
normalized relative to their approved embedded VFS trees. No host SDK root is
accepted. The request SDK envelope has only `distribution_id` and
`catalog_sha256`; the [SDK contract](p3-math-capabilities.md) defines the immutable
source inventory and the manifest’s `sdk.delivery: "builtin"` record. These entries do
not weaken owned-dependency path checks. A mapping's origin must match a consumed
SDK dependency, and mapped `std` calls also require the consumed `libcxx/cmath`
entry. The example omits unrelated SDK dependencies for space; actual responses
record the complete consumed closure.

`mapped_call` is a distinct instruction:
`{op:"mapped_call",mapping:"cpp.math.floor.f64.v1",target:<double local var>,args:[<double expression>],loc:...}`.
It never creates a placeholder function declaration or definition. Arguments are
already sequenced/snapshotted by the adapter. The only fixed IDs are
`cpp.math.fabs.f64.v1` and `cpp.math.floor.f64.v1`, both `double(double)`. A shared
consumer table owns `neverc_math_abs`/`neverc_math_floor`, their installed header,
and runtime module names. Every evidence entry must correspond to a mapped call.

`VerificationContext` separately authorizes the FP contract, SDK distribution,
and operation IDs. The driver sets those authorizations after checking its
compiled SDK inventory, exact consumed files, resolved declarations, and runtime
capability registry. Frontend-reported fields are evidence to validate, never
self-authorizing capability claims. The merger compares SDK/catalog/FP identity,
merges SDK dependencies and mapping evidence deterministically, and verifies the
closed result again.

The FP execution contract requires masked traps and normal IEEE subnormal
handling, with FTZ/DAZ disabled. Mapped fabs/floor support all four standard
externally selected rounding modes, preserve errno and preexisting exception
flags, and follow the tested operation-specific NaN quieting/new-exception
behavior. Source fenv APIs remain outside the admitted subset; a separately
compiled client can still observe the promised environment behavior. This does
not enable general FP arithmetic or unmasked traps.

A deliberate initial restriction rejects any module that combines a floor
mapping with a signaling-NaN literal anywhere in its definitions. Verification
of the merged project repeats this check, including constants supplied by another
unit. Clang O2 may fold a constant-sNaN floor without raising INVALID, whereas a
runtime call raises it. The module-wide restriction covers aliases/snapshots
without claiming to model every optimization. The admitted source subset cannot
construct NaN constants (builtin constructors, bit casts, pointers, unions,
`numeric_limits`, and floating arithmetic are excluded). Dynamic signaling NaNs
from external scalar parameters remain supported and differentially tested;
exact NaN literal representation remains available where this floor restriction
does not apply.
