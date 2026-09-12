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

Globals are `{name,type,value,loc}`. Their values must be fully folded literal/aggregate trees: the frontend resolves constant references, operators and conversions before serialization. They represent only checked compile-time constants and are never assignable. Functions contain `name`, `result`, boolean `internal`, boolean `c_export`, `params`, `locals`, `body`, and `loc`. Parameters/locals are `{name,type,loc}`. Each function has distinct local names; all storage declarations are emitted once at function entry, and initialization instructions remain in source execution order. The v1 profiles restrict this representation to their admitted trivial value types. Core v2 represents source aliases, addressable storage and lifetimes through explicit operations described below; declarations at entry do not perform initialization or start object lifetimes.

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

## Core v2 live rvalue references

Supported live-object rvalue references use the same `ptr:`/`cptr:` carriers as
lvalue references. Source categories and cv/ref overload selection remain in the
canonical function identity before normalization. No additional wire type,
reference opcode or ownership flag is introduced. Reference parameters/results
retain addresses; the result of a reference-returning call is a place even if its
source expression is an xvalue. Conditional glvalues join pointers and no record
copy is introduced merely to change value category.

Ordinary rvalue-qualified methods retain the receiver-before-arguments convention.
Full-expression temporary arguments/receivers follow the separate contract below;
reference uses add no cleanup owner. Protocol major 1 remains unchanged. See the
[live-object rvalue reference contract](cpp-core-v2.md#live-object-rvalue-references).

## Core v2 overloaded operator calls

Ordinary selected operators use `call`, with the same scalar/reference/record
parameter and result conventions as other functions. A member operator receives
its object pointer before explicit parameters; a non-member operator has no
receiver. A record result adds its hidden destination before either form.
Distinct C++ overloads retain distinct source identities even when signatures
normalize to the same pointer types. Selected logical overloads are calls,
not builtin short-circuit branches.

Operand evaluation order is represented by preceding captures, independently
of argument positions in the final call. Operator-notation assignments capture
RHS first; other admitted operators preserve required left-to-right sequencing.
Explicit member calls capture the receiver first. Non-member assignments choose
RHS-first even in explicit function-call syntax, so their callee registers value
parameters in that order and destroys them in reverse. All other ordinary
parameters use the existing order. This function-wide convention needs no new
wire flag or signature and covers early returns and record results. By-value
argument objects, discarded record results and reference aliases keep their
normal ownership rules. See the [source contract](cpp-core-v2.md#ordinary-overloaded-operators).

## Core v2 conversion-function calls

A checked user-defined conversion emits the selected ordinary member `call`,
with its receiver and no explicit source parameters. A record prvalue result
adds the actual hidden destination before the receiver. Returned references use
the existing ptr/cptr carrier and alias their referenced storage; they introduce
no owning object. Converting that reference to a record value keeps the selected
copy/move call. Direct prvalue destinations and discarded-result cleanup retain
the existing object lifetime rules.

The AST wrapper's result type and value category must match its direct selected
call. Standard conversions after that call remain distinct typed operations.
Builtin logical conditions keep conversion calls in their appropriate CFG
branches. No conversion body runs for noexcept queries. Conversion identities
remain distinct even when const/ref overloads normalize to identical signatures.
No new wire expression or opcode is needed. See the
[source contract](cpp-core-v2.md#user-defined-conversion-functions).

## Core v2 temporary call storage

Materialized full-expression scalar/record temporaries use actual locals and
initialization instructions. Reference arguments receive their ptr/cptr addresses;
temporary receivers use the same object pointer as their constructor and cleanup.
Subobjects and reference results keep aliases without another owning object.
Object results retain the caller's hidden destination before the receiver.

The frontend verifies materialization duration and shape before erasing or
lowering an operand. Each evaluation initializes its own storage, including
repeated semantic array fillers. Existing live flags guard cleanup on conditional
paths and reset across loop iterations. Caller-owned temporaries are destroyed
in reverse completion order after the selected call and callee-owned parameters;
no call argument transfers a temporary's ownership merely by being a reference.
No wire opcode or additional lifetime flag is needed. See the
[source contract](cpp-core-v2.md#full-expression-temporary-calls).

## Core v2 automatic reference storage

An admitted automatic reference uses its usual `ptr:`/`cptr:` alias to an actual
local scalar or record destination. The frontend verifies the exact canonical
extending variable, storage duration and initializer shape. Transparent semantic
reference lists retain that address; their hidden materializations undergo the
same source inspection as written expressions. No aggregate copy is introduced.

Destructible extended objects use the existing live flags and lexical scope
cleanup calls. Nested full-expression temporaries retain separate cleanup frames.
Reference rebinding adds no owner; copied or moved values keep their own selected
construction and destination. Conditions, loop increments, repeated evaluations
and early exits use the existing control-flow cleanup. Protocol major 1 and its
opcodes remain unchanged. See the
[source lifetime contract](cpp-core-v2.md#automatic-local-reference-lifetime-extension).

## Core v2 noexcept queries

Resolved `noexcept(expression)` uses the existing pure `literal` representation
with type `bool`, a JSON boolean value and its source location. The value is
computed by the embedded frontend from C++17 exception specifications and any
required temporary destruction. No operand instructions, helpers, cleanup owners
or runtime exception probe are added to the querying function. Standard written
specifications affect source semantics and query results without changing the
ordinary emitted function signature or the protocol version.

Source admission still visits every written specification and operand, including
unused declarations and nested/short-circuited queries. The accepted executable
subset rejects throw/try/catch and unsupported foreign throwing calls; this wire
convention does not implement exception propagation, catch, termination or stack
unwinding. See the [source contract](cpp-core-v2.md#noexcept-declarations-and-queries).

## Core v2 pointer arithmetic

Typed `binary` `+`/`-` nodes admit complete-object pointers and explicitly
promoted 32/64-bit offsets, plus cv-compatible pointer differences. Offset
results match the pointer operand exactly; difference results must match the
independently verified signed native ptrdiff width. This expectation belongs to
the consumer context, not to frontend-supplied target evidence. Existing `index`
and `address` nodes retain their typing rules; direct address-of-index emission
uses guarded offsets and direct address-of-dereference emission cancels the pair.
Pure typed emitter helpers preserve null/zero rules and bound output growth;
they add no protocol call kind. Relational pointer operations remain rejected.
See the [source and emission contract](cpp-core-v2.md#pointer-offsets-and-differences).

## Core v2 method calls

Ordinary methods reuse the existing function/`call` representation. A nonstatic
method has an explicit `ptr:<record>` or `cptr:<record>` receiver parameter,
after a record-result pointer when present; callers capture the receiver before
explicit arguments. Static methods have no receiver parameter, while any written
object expression is evaluated/discarded first.
The consumer checks the complete ordinary signature, including receiver type,
qualifications and arity. No special member-call node, implicit receiver or
unchecked class ABI is introduced. Method names preserve canonical declaration
identity before source type normalization; methods are never C exports.
See the [source admission and lifetime rules](cpp-core-v2.md#ordinary-record-methods).

## Core v2 constructor calls

Admitted constructors also reuse ordinary functions and `call`: result `void`,
first parameter `ptr:<record>`, followed by the explicit source parameters.
They have no C export ABI. Call signatures and destination addressability pass
through the existing verifier; no implicit construction node bypasses checking.
The frontend places field initialization in declaration order before the body.
Local, field and array-element construction passes the actual destination address;
array fillers produce one call per element. Direct initialization and temporary
materialization do not insert an intermediate record copy. Intentional source
copies use the selected trivial-value or user-copy call path. Record parameters/results
use the explicit call-storage and destruction conventions below.
See the [constructor and lifetime contract](cpp-core-v2.md#ordinary-record-constructors).

## Core v2 record call storage

For a source record result, the wire function result is `void` and the first
parameter is mutable `ptr:<record>` result storage. An instance receiver follows
it; constructors retain only their existing destination pointer. Each by-value
record parameter is normalized to mutable `ptr:<record>` for a distinct object
initialized by the caller. Source references continue to carry aliases and source
constness remains checked before lowering. No new IR node is introduced.

Record-result calls omit `target` and pass the actual destination address;
record returns initialize that destination and emit a value-less `return`.
Nested direct returns forward the same place. Intentional lvalue argument copies
and named-object return copies use ordinary checked record assignments for trivial
copying, or selected user copy/move and generated copy calls described below; source and destination remain distinct. Signatures, arity, pointee identities and
record layouts are validated by the existing consumer. This convention applies
to core v2 only and does not admit a foreign ABI or exception unwinding. Normal cleanup follows the explicit destruction convention below. See the [source contract](cpp-core-v2.md#record-arguments-and-results).

## Core v2 user copy calls

An admitted copy constructor is an ordinary `void` function with mutable
`ptr:<record>` destination followed by the checked `ptr:<record>` or
`cptr:<record>` source reference. An admitted copy assignment function returns
`ptr:<record>` and takes the mutable receiver followed by that source reference.
User copying is emitted as a call to the source-selected declaration, not as a
record assignment. The returned pointer is dereferenced as a source reference;
it need not point at the receiver if the source body returns another live object.

Source operator notation evaluates the RHS argument before the LHS receiver;
explicit member call notation evaluates the receiver before arguments. The wire
argument order remains receiver then source after those effects are captured.
No new copy opcode or foreign ABI bypass is introduced. Existing signature,
arity, const qualification, storage and layout checks apply, with the normal
parameter/result lifetime convention preserved. Implicit trivial copies retain
ordinary value assignments; generated copy assignment and user moves follow below. See the [source copy contract](cpp-core-v2.md#user-defined-copy-operations).

## Core v2 user move calls

An admitted user move constructor uses `void(ptr:Record, ptr/cptr:Record)`;
move assignment uses `ptr:Record(ptr:Record, ptr/cptr:Record)`. The first pointer
is the actual destination or receiver and the second is the live source reference.
Copy/move and const-source overloads retain distinct canonical function identities
even where their complete signatures coincide. Calls retain the selected body
and assignment's actual returned reference.

Existing constructor destinations, operator-versus-member evaluation order and
by-value parameter/result storage conventions apply. No move opcode, foreign ABI,
record snapshot or implicit ownership transfer is introduced. A source object
remains alive after moving, and the existing complete-object owners retain normal
cleanup. Protocol major 1 is unchanged. Generated/defaulted moves follow the
contract below; automatic local extension follows its separate contract. See the
[user move contract](cpp-core-v2.md#user-defined-move-operations).

## Core v2 generated move calls

Generated nontrivial move constructors and assignment use the same complete
pointer signatures as user moves. Canonical selected definitions retain source
locations and are emitted once. The generated body's selected leaf calls remain
copy or move calls according to source overload resolution; matching pointer
carriers do not merge distinct function identities.

Semantic member-array moves preserve source addresses and increasing element
order. A checked generated trivial array assignment becomes bounded typed
stores, including selected copy fallback; no memory-copy import or move opcode
is introduced. Trivial complete-record operations remain ordinary value stores
after reference capture, preserving stored pointers. Existing implicit trivial
inline temporary operations retain full-expression materialization without a
helper call or a new lifetime-extension rule. Complete objects retain their
normal cleanup ownership after moving. Protocol major 1 remains unchanged. See
the [generated move contract](cpp-core-v2.md#generated-move-construction-and-assignment).

## Core v2 generated copy calls

Implicit and defaulted nontrivial copy constructors use the existing checked
`void` constructor convention: a mutable record destination pointer followed by
one mutable or const record source pointer. Canonical generated definitions are
emitted once. Semantic field initialization calls each selected member copy at
its actual destination; nested records and arrays preserve source declaration
and element order. Trivial copies use existing value assignments, preserving
stored pointer values without repair.

Clang's semantic array loops are expanded into ordinary address captures,
dereferences, array decay, indices, assignments and calls. The common source
array address is evaluated once. Each element index uses the source size type;
a nested common expression sees its enclosing element index before the inner
index is introduced. Opaque source bindings and implicit index expressions are
accepted only in a checked generated copy initializer; they introduce no wire
node, opcode or fallback source blob. Existing extent/storage/expansion limits
bound the expansion. Full-expression cleanup and complete-object ownership are
unchanged. Unused or unevaluated copies emit no invented function body or runtime
call. See the [generated copy contract](cpp-core-v2.md#generated-copy-construction).

## Core v2 default member initialization

Checked default member initializers use the existing typed member/index stores,
addresses and direct calls. Protocol major 1 and the core v2 version remain unchanged.
No default-initializer opcode, opaque source object or external compiler call is
introduced. Selected defaults use the actual destination receiver; explicit
aggregate clauses keep their enclosing lexical `this`. Nested defaults restore
both contexts after their own initialization.

Generated copy and assignment instructions preserve the source values and selected
member operations without rerunning defaults. Ordinary user copy constructors can
select defaults for omitted members. Constructor member full expressions and whole
aggregate initialization retain their existing temporary cleanup boundaries and
complete-object ownership. Written defaults and selected semantic expressions are
both checked, even if the source never uses the default at runtime. See the
[default member initializer contract](cpp-core-v2.md#default-member-initializers).

## Core v2 generated assignment calls

Generated nontrivial copy assignment uses the existing record-reference result,
mutable receiver and mutable/const source-pointer signature. Its checked body
retains selected member calls and counted array loops, followed by the receiver
reference return. Each selected canonical method is emitted once. Ordinary
member assignment return aliases are preserved but do not replace the containing
assignment's receiver result. No new operator or ABI representation is introduced.

Only in a checked generated copy assignment may the adapter recognize the pinned
Clang builtin for copying the same direct array field from the source parameter
to `this`. Recognition checks builtin identity, address/member/parameter identity,
array types, exact byte count and trivial element assignment. The implicit builtin
callee conversion is allowed only on that recognized callee path. The operation
becomes captured array addresses and bounded typed element assignments with
source constness preserved; no builtin, memory-copy import or mapped-call opcode
is transported. Nontrivial construction/destruction of a trivially assigned
member does not cause extra lifecycle operations.

Trivial assignment uses captured source and receiver references without an emitted
helper. Source operator order is RHS then receiver; explicit member-call order is
receiver then source. Values are read only after those captures and effects. The
result remains the receiver lvalue. Unevaluated assignments introduce no runtime
calls. Existing signature, constness, layout, storage and expansion validation
applies. See the [generated assignment contract](cpp-core-v2.md#generated-copy-assignment).

## Core v2 defaulted lifecycle

Generated/defaulted default constructors use the existing `void(ptr:Record)`
constructor signature, checked call instructions and explicit destination
addresses. Their semantic member initialization discovers selected definitions
transitively; each canonical definition is emitted once. Trivial default
constructors need no function body. Unevaluated construction can have no Clang
body and produces no runtime call. Zero-initialization remains an ordinary
verified initialization only when required by the source C++ initialization;
out-of-line defaulting does not receive an invented zero pass.

A defaulted destructor uses the same `Record_destroy` helper as implicit member
cleanup, with no user body. No new opcode, lifecycle field, profile version or
opaque operation is introduced. See the [defaulted lifecycle contract](cpp-core-v2.md#default-construction-and-defaulted-destruction).

## Core v2 destruction

A record requiring destruction emits one deterministic internal function named
`<record-id>_destroy`, returning `void` and taking mutable `ptr:<record-id>`.
Its body contains ordinary verified operations: user destructor statements,
body-local cleanup and reverse member/array destruction. Implicit containing
record destructors use the same representation. No opaque cleanup opcode or
unchecked C++ ABI call is introduced.

The frontend emits bool live flags, initialized at function entry, set after
object initialization completes and cleared before each cleanup call. Checked
branches, labels and jumps select only live objects on normal exits and at
full-expression boundaries. Cleanup expansion uses the existing generated-node
budget. Source locals and temporaries have separate cleanup ownership; callees
own by-value record parameters and callers own result destinations. Conditions,
selectors and return values are captured before cleanup can mutate their source
storage. The consumer validates flags, control flow, calls and signatures using
its ordinary rules; source lifetime scheduling is the frontend's responsibility.
See the [normal lifetime contract](cpp-core-v2.md#record-destruction-and-normal-lifetimes)
for initialization order, temporary boundaries and unsupported unwind paths.

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
