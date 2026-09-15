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

## Concrete parameter-pack representation

Core v2 serializes instantiated type/scalar packs as ordinary concrete parameters,
records and constants. It adds no pack node, runtime template argument, opaque
source or new opcode. Each expanded parameter gets a unique storage name;
references remain typed pointers to the original storage. Record results keep the
initial hidden destination and members keep their receiver. Resolved `sizeof...`
uses the target size integer carrier; scalar pack substitutions reuse Clang's
selected typed replacement, without treating its reverse pack index as a forward
argument index. Concrete folds use existing expression, branch, call and cleanup
operations, preserving builtin short circuit and selected operator lifetimes.

The producer checks at most 64 primary parameters and 64 elements per concrete
pack, charges expansion work and verifies every materialized element and exact
pack origin. The narrow dependent count TypeLoc exception remains source metadata
and cannot be emitted. Empty fold patterns stay uninstantiated, while seeds and
generated elements retain source checks. Consumers continue checking complete
signatures, unique storage, types and call closure. Equivalent packs reuse canonical
identities and relocation stays deterministic. See the [source contract](cpp-core-v2.md#concrete-parameter-packs)
for the supported boundaries; complete C++17/STL remains unfinished.

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

Globals are `{name,type,value,loc}`. Their values must be fully folded constant trees: literals, aggregates, and the profile-specific null or address constants described below. The frontend resolves constant references, operators and conversions before serialization. The v1 profiles represent only checked compile-time constants and never permit global writes. Core v2 additionally accepts an optional boolean `mutable` field, defaulting to `false`: supported numeric, boolean, null, object-pointer and callback scalar globals, complete fixed arrays and records may set it to `true`, with a folded constant or static-zero initializer. The consumer permits writes and mutable addresses only for those exact global identities. Other profiles reject the field, and const globals remain read-only. Mutable records follow their static-object contract below. Functions contain `name`, `result`, boolean `internal`, boolean `c_export`, `params`, `locals`, `body`, and `loc`. Parameters/locals are `{name,type,loc}`. Each function has distinct local names; all storage declarations are emitted once at function entry, and initialization instructions remain in source execution order. The v1 profiles restrict this representation to their admitted trivial value types. Core v2 represents source aliases, addressable storage and lifetimes through explicit operations described below; declarations at entry do not perform initialization or start object lifetimes.

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

- `assign`: `target` is a var/member expression recursively rooted in mutable local/parameter storage, or an explicitly mutable core-v2 scalar global. Const globals are writable only while CFG-proven dynamic initialization owns their direct storage root; temporary aggregates are not writable. `value` has the same type. Assigning an aggregate copies its value.
- `call`: `callee` refers to a defined function; `args` match its parameters. `target` is a `var` expression naming a local of the result type and is absent only for void results. Calls are standalone and ordered.
- `label`: a unique function-local `label` identifier.
- `jump`: a known destination `label`.
- `branch`: bool `condition`, destination strings `true` and `false`.
- Core-v2 `static_init_begin`: only `global`, `true`, `false`, `op` and `loc`;
  acquires initialization or registration ownership of the named guarded global and terminates
  the block, selecting initialize (`true`) or already published (`false`).
- Core-v2 `static_init_end`: only `global`, `op` and `loc`; publishes initialization
  with release semantics and continues in the current block.
- Core-v2 `register_static_destructor`: only `global`, `op` and `loc`; registers
  the named complete object's checked cleanup inside its owner's acquired region.
- `return`: `value` matches the result type, or is absent for void.

The body starts with a label. Each basic block ends with jump, branch, static initialization begin or return; there is no implicit fallthrough, unreachable instruction after a terminator, or unknown label. Loops and break/continue use explicit labels, allowing the adapter to preserve condition, increment, and branch-local effects. Every nonvoid function must return on each reachable terminating path; an infinite loop is allowed. Default return 0 is emitted for the fallthrough of main only, following C++ semantics.

The consumer validates the entire module and all referenced symbols, types, fields, operator contracts, result/parameter types, globals, and block structure before NC emission. Calls outside the selected definition closure require a separately validated mapping; core has no mappings. Literal values never bypass expression validation as raw C text.

## Emission, validation and metadata

NC output declares records/prototypes before definitions, emits guarded native target/data-model requirements, and retains explicit sequencing statements. Maps contain generated line ranges and original source locations plus the generated-source hash. Manifests and reports each have schema major 1 and are separately documented in the design contract. Artifacts record frontend and NeverC build identity, target, normalized options and dependency hashes. Object validation uses the same target as source analysis. Generated programs are not run by translation.

## Core v2 dynamic static initialization

The optional global field `dynamic_initialization` must be `true` and requires
core v2. Its complete ordinary initializer must contain only semantic zero:
integer zero, false, positive floating zero, null pointers/callbacks and recursive
zero aggregates. Type, arity, payload and depth checks still apply. `mutable`
retains its source-storage meaning; a const dynamic global has writable physical
C storage only to implement initialization.

A dynamically bound static reference uses the same readonly pointer
carrier and guard operations. Its initialization assignment stores the result of
binding the original glvalue; ordinary uses dereference the carrier. This grants
no reassignment permission for the binding after publication and no new lifetime
for an existing referent. No distinct reference opcode is needed.

A permanent temporary constructed for a dynamic extending declaration has a
nonempty `initialization_owner` string naming its dynamic root global. This
optional field requires core v2. The child has a complete semantic-zero static
initializer and cannot also have `dynamic_initialization`. Its owner must exist
and have no owner of its own. An owner is either a readonly pointer to a complete
object, or a complete record/fixed array with ordinary mutable/const storage.
The child type need not equal the owner's type or pointed-to type: a reference
can bind a larger temporary's subobject, and aggregate reference members can
extend several different temporaries. These checks exclude missing owners, chains,
self-ownership and cycles. Associations resolve after all globals are collected,
so declaration order does not affect validity. Zero children can remain after
unselected branches or dead-code pruning; every declared owner must still exist.

The owner alone has guard operations. A CFG-proven grant for that owner permits
direct-root construction of its declared children, including const children.
Both tentative and initialized C definitions use writable physical storage for
these children while ordinary accesses preserve source constness. Children never
acquire independent guards or exception behavior; required static destruction
registers separately on each child's completion. The verifier keeps
resolved stable global identities; associations cannot override ordinary alias
qualifiers, layout checks or protocol resource limits.

Each present `static_init_begin` site uniquely owns its global module-wide.
There is at most one `static_init_end`, in the same function. A pruned declaration
may leave zero storage without a site. A nonterminating initializer may have no
end if no finite path escapes its ownership. The begin's initialize edge grants
the global, its ready edge grants nothing, and a matching end clears the grant.
Nested acquisition in one function, incompatible incoming grants, cross-object
publication, returns or fallthrough while owned are rejected. Stable CFG cycles
are allowed. Disconnected components are conservatively seeded with no grant in
instruction order; the frontend prunes unreachable blocks before emission.

The verifier computes these states once before expression checking, without
recharging expression budgets during its worklist. Only the proven owner permits
direct-root writes and mutable address formation for a const dynamic global or
its declared initialization children.
Pointer aliases use ordinary pointee qualifiers; the IR does not claim a lifetime
or alias nonescape proof. Guard identities or extra semantic operands attached
to other operations are rejected in both parsed and synthetic input.

The consumer independently establishes always-lock-free native 32-bit int
atomics; frontend metadata cannot grant the capability. Generated C23 uses
`_Atomic(unsigned int)` states 0/1/2, acquire reads and compare-exchange, and
release publication after full-expression cleanup. On AArch64, per-function
`target("no-outline-atomics")` together with `noinline` prevents foreign CAS
helper calls during normal downstream compilation. Atomics use compiler
builtins without headers. No source atomic operation, guard address or exception
retry permission is introduced. See the [source contract](cpp-core-v2.md#dynamic-local-static-initialization).

## Core v2 static destruction

A global's optional `destructor` is a nonempty core-v2 function identifier naming
an internal, non-exported `void()` definition in the same module. The global is
a complete record or fixed array; the callback cannot be `main` or module startup.
Missing definitions, wrong signatures/profiles and malformed wire types reject.
The producer supplies no native symbol name or ABI: `VerificationContext` derives
`CxaAtExit` for hosted Linux/Darwin or `CAtExit` for an explicit MSVC Windows
environment independently from NeverC's native target. Unsupported environments
reject. C exports cannot collide with the selected fixed runtime symbols.

A root needs a guard when dynamically initialized or when it has a destructor.
A child with `initialization_owner` never acquires its own guard and still requires
a dynamic root. Constant roots retain their full ordinary initializer; semantic
zero remains mandatory only for dynamic roots and their children. A registration-only
acquisition cannot grant construction writes to a const constant root.

`register_static_destructor` names an existing destructor-bearing global with exact
operands and no expressions, result, call target or branch labels. Its CFG owner
must be that root, or the child's resolved `initialization_owner`. There is at
most one textual registration site per global module-wide. Verification retains
same-owner CFG edges and cuts publication outgoing edges, computes SCCs with an
explicit DFS stack, and rejects a registration in a cyclic component. An outer
loop around an entire declaration remains valid because the guard skips a
published acquisition. Two may-state bits union at joins; every path reaching a
root's publication must have registered its destructor. Conditional child
registration, dead static metadata and nonterminating initialization remain valid.
The passes use linear storage and avoid recursive traversal of untrusted CFGs.

Emission uses a real `void(void*)` thunk for `__cxa_atexit`, passing null callback
data and the address of hidden `__dso_handle`. Windows uses an exact `void()` thunk;
on x86, the runtime function, callback type and thunk all carry `cdecl`. Thunks
call the validated internal cleanup definition without function-pointer coercion.
Registration occurs at each completion and its return value is ignored, matching
the existing pinned frontend ABI. Saved NC checks `__STDC_HOSTED__` and the
independent `__NEVERC_WINDOWS_MSVC_ABI__` predefine for MSVC registration. The
`__NEVERC_DYNCODE__` mode predefine rejects this native CRT lifecycle in DynCode.

Physical storage with destructor metadata is writable even when source-const.
Ordinary accesses remain const-checked; generated destruction uses a const address
followed by the existing checked cv pointer cast. No new alias provenance or
arbitrary const-write authority is claimed. See the [source contract](cpp-core-v2.md#static-destruction).

## Core v2 nonlocal startup

The optional module property `startup` must be a nonempty string in core v2.
It names a function definition in the same module with internal linkage, no
C export, a void result and zero parameters; `main`, declarations alone, missing
names, invalid signatures and other profiles reject. Parsing checks presence,
profile and wire type; independent semantic verification resolves the complete
function table. Invalid input leaves output artifacts unchanged.

Only the selected definition receives the native C23 `constructor` attribute,
with a `__has_attribute(constructor)` guard. Ordinary native program/module startup
owns invocation. Source initializers remain checked instructions using the same
`dynamic_initialization`, `initialization_owner` and `static_init_begin/end`
contracts. Startup grants no additional write, ownership or lifetime permissions.
Zero/constant data is emitted normally. No user-callable startup wrapper, public
export or new runtime ABI is introduced. The frontend orders admitted nonlocal
initializers and cleans each full expression before publication and the next
initializer. Local statics remain initialized at first passage.

This describes ordinary hosted native linking/loading. DynCode's current
`llvm.global_ctors` rejection is unchanged; manual loaders need their own contract.
See the [source contract](cpp-core-v2.md#nonlocal-dynamic-initialization).

## Core v2 deleted declarations

Deleted functions participate in source semantic checking and produce no IR
function or generated lifetime/assignment helper. This adds no protocol opcode
or callable type. A valid use of a different overload emits only the selected
function; a use of a deleted function is a source diagnostic. Defaulted moves
that are defined as deleted preserve Clang's selected copy fallback. Written
signatures and eagerly visited default/noexcept expressions remain checked before
erasure. See the [deleted-declaration contract](cpp-core-v2.md#deleted-function-declarations).

## Core v2 const member storage

Const data members use the ordinary unqualified field carrier, as const local
objects do, so initialization and copy/move construction can target their final
storage. Source checking controls which member writes and special-member
operations are legal; this adds no field qualifier or initialization opcode.
Pointers, references, method receivers and array decay preserve the source
qualification with `cptr:`. A const pointer field retains its pointee type while
its own address adds the corresponding const pointer layer. The consumer still
checks pointer qualification conversions, types, globals and layout. See the
[const-member contract](cpp-core-v2.md#const-data-members).

## Core v2 delegating construction

A delegating constructor uses an ordinary void function and the same typed
receiver pointer as its selected target. Its body calls the target with that
pointer and source-normalized arguments, then performs the delegation's
full-expression cleanup before executing its own source body. No record
intermediate, second member initialization, new opcode or raw source is emitted.
Target identity, complete signatures, receiver provenance and relocation are
checked by the source/protocol fixtures. See [delegating constructors](cpp-core-v2.md#delegating-constructors)
for source admission and lifetime boundaries.

## Core v2 null values

Core v2 adds the distinct scalar type spelling `nullptr`; it composes with `ptr:`,
`cptr:`, `arr:` and length-delimited `fnptr:` types. A null value is
`{kind: "null", type: "nullptr", loc: ...}` with no value, symbol or operands.
It is accepted in folded global/aggregate initializers and in mutable scalar
storage. Old profiles reject this type. Synthetic types with record identities,
integer widths, child types, qualifiers or array counts are rejected too.

The consumer accepts same-type null equality/inequality, boolean conversion and
identity conversion, and rejects arithmetic, ordering, integer literals of null
type and casts between null and pointer/integer representations. Source conversions
to object/function pointers serialize an already typed pointer `null` after all
source effects have been emitted as instructions. C++ lvalue conversion evaluates
the null object's expression without requiring a load of its representation.
All `null` expressions, including ordinary pointer nulls, reject extraneous
payload fields. Independent carrier/layout verification and emitted C23
`typeof(nullptr)` guards follow the [source contract](cpp-core-v2.md#null-pointer-values).

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

Defined core-v2 scalar static data members reuse ordinary typed globals and the
optional `mutable` flag. A canonical definition yields one global with that
definition's location; a checked initializer may belong to its in-class
redeclaration. Static members never enter a record's fields/layout. Member access
retains receiver effects before using global storage, whose lifetime is
independent of any temporary receiver. See [defined scalar static data members](cpp-core-v2.md#defined-scalar-static-data-members)
for const permissions, source closure and initialization restrictions.

Declaration-only core-v2 static constants use frontend-only checked integer
values, emitted as ordinary typed literals and, where glvalue control flow
requires it, fresh local value carriers. They emit no source global or field.
Receiver effects and cleanup stay explicit; discarded and unevaluated values
emit no scalar storage. Source address/reference uses still require a real
definition. See [declaration-only static constants](cpp-core-v2.md#declaration-only-static-constant-values).

Concrete core-v2 standard-layout class-template instances use ordinary record/layout
IR and existing field, array, copy and cleanup operations. Actual arguments
participate in canonical record/field identity; equivalent arguments share a
type, while different values, types or primaries remain distinct. Value arguments
become typed constants, with no runtime template parameters. By-value dependencies
remain ordered before their owners. See [concrete class templates](cpp-core-v2.md#concrete-aggregate-class-templates)
for source checks, declaration shape and remaining member/template limits.

Ordinary named methods of admitted standard-layout class-template instances use the
existing function ABI: a cv-qualified receiver for instance methods, hidden
storage for record results, and no receiver for static methods. Class arguments
participate in method, local record/field and scalar static-storage identities.
Equivalent instances share identities; differing values/types/primaries do not.
No runtime template parameter is emitted. See [class-template methods](cpp-core-v2.md#ordinary-class-template-member-functions)
for lazy bodies/defaults, source checks and definition requirements.

Scalar non-type template defaults produce the same concrete argument identities
and typed functions, records and globals as equivalent explicit arguments. No
runtime parameter is added. Successful template-use events retain original and
converted defaults with their exact selected declaration or type-use evidence.
Validation happens at reached source uses and explicit declarations/directives;
unselected overload candidates and uninstantiated dependent source remain lazy.
Scalar static member variable templates emit the existing typed globals and
storage operations. Canonical inner and outer arguments determine shared or
distinct identities. Checked declaration-only non-inline const reads carry values
without invented storage; evaluated addresses and references require definitions.
Member expressions preserve receiver evaluation and temporary cleanup. The
frontend-private type record retains the exact previous concrete declaration
when Sema creates a separate out-of-line definition; both source types remain
checked, without adding a wire operation or replaying substitution. A copied
class-scope full specialization also joins its exact argument event, type event,
original full declaration and selected outer instance. The original primary
argument frame checks resolved default/parameter-type sources; the full
initializer has no inner generic slots. This remains frontend-private metadata.
See [member variable templates](cpp-core-v2.md#concrete-member-variable-templates).

Member alias templates erase to the same supported types as their underlying
source. They add no runtime template declarations or new wire operations.
Concrete enclosing class records keep their normal identities; aliases preserve
existing function signatures, references, fields and lifetime operations. Outer
class and inner alias substitution sources remain separate private validation
metadata. See [member alias templates](cpp-core-v2.md#concrete-member-alias-templates).

Concrete member function templates emit existing typed functions, explicit
receiver/result pointers and canonical scalar static globals. Their enclosing
record and own argument identities jointly determine concrete code and storage.
Equivalent calls share both; distinct instances do not merge. Successful
constructor/conversion expression identities and deduction locations are private
source validation metadata. Definition/default frames, parameter packs and
written primaries add no runtime template operations or opaque payloads. See
[concrete member function templates](cpp-core-v2.md#concrete-member-function-templates).

Namespace scalar variable templates emit only existing concrete scalar globals,
literals, variable references and ordinary function operations. Canonical
primary/argument identity deduplicates storage; separate specializations keep
separate globals. Fixed-type unevaluated uses without materialized definitions
emit no object or unresolved global reference. Selected partial argument frames,
per-use written arguments, defaults, and first/definition type-source records are
private validation metadata, never wire operations. Resolved `auto` types and
instantiated initializers are checked before emission. See
[namespace scalar variable templates](cpp-core-v2.md#namespace-scalar-variable-templates).

Namespace class partial specializations also use existing concrete record,
function, scalar-global, reference and lifetime operations. The primary argument
identity is separate from the selected partial's deduced slots and actual
substituted pattern source. Exact selected deduction evidence validates both;
canonical argument equality alone cannot identify the candidate. These private
source records never appear as wire operations. Empty deduced non-type packs
retain their checked type substitution without inventing an argument element.
See [class partial specializations](cpp-core-v2.md#class-template-partial-specializations).

Namespace alias templates preserve canonical types and produce no wire opcode,
wrapper function, duplicate record or static object. Written arguments, selected
defaults and substituted underlying TypeLocs retain source checks, including
ignored arguments and folded expressions. Selected non-type arguments also
preserve their actual parameter type source, including preliminary explicit
function conversions and empty deduced pack type substitutions. These source
records add no runtime parameters or wire operations. See [namespace alias templates](cpp-core-v2.md#namespace-alias-templates)
for exact-use matching, caller/default contexts and bounded source evidence. See
[scalar template parameter defaults](cpp-core-v2.md#scalar-template-parameter-defaults).

Scalar static members of admitted class-template instances use ordinary typed
globals for actual definitions, with canonical class/member identity and checked
constant or zero values. They add no record fields. Declaration-only constant
reads and unmaterialized unused members create no global. Receiver effects and
temporary cleanup retain existing operations; evaluated address/reference uses
require actual storage. Each explicit static directive supplies independent
written type/qualifier/attribute evidence, sharing the function-directive source
budget. See [class-template scalar static data](cpp-core-v2.md#class-template-scalar-static-data).

User-provided class-template constructors use ordinary void functions with a
destination pointer followed by runtime parameters. Copy/move source references,
member declaration order, existing field/array cleanup and per-instance local
identities are preserved. Selected constructors need materialized definitions;
unused bodies/defaults remain lazy. See [class-template constructors](cpp-core-v2.md#class-template-constructors)
for declaration, default-argument and lifetime boundaries.

User-provided class-template destructors use internal void functions with one
concrete-record pointer. Bodies run before reverse member/array cleanup. Required
definitions are checked even for direct object returns; unused type queries keep
bodies lazy while selected exception specifications remain checked. Helpers for
actual cleanup and materialized user bodies form a bounded deduplicated work list.
See [class-template destructors](cpp-core-v2.md#class-template-destructors) for
definition, lifetime and source boundaries.

Defaulted class-template special members reuse generated constructor/assignment
and destruction IR. Constructor destinations precede source references;
assignments return the receiver pointer, and destruction reverses member/array
order. First-declaration versus out-of-line defaulting preserves value zeroing
semantics. Trivial operations need no helper; unused queries do not invent one.
Materialized generated bodies retain source closure, concrete instance identities
and complete typed call signatures, without runtime template parameters or an
opaque fallback. See [defaulted class-template special members](cpp-core-v2.md#defaulted-class-template-special-members).

Class-template operators and conversion functions use the same direct typed call
IR as ordinary members. A hidden record-result destination precedes the receiver;
references retain actual aliases and template arguments add no runtime parameters.
Operator notation preserves C++17 sequencing, including RHS-first assignment and
evaluation of both overloaded logical operands. Explicit member notation captures
the receiver first. Instance/overload identities, static local storage and complete
call-target signatures remain canonical across relocation. Source and definition
checks include selected unevaluated calls and all materialized bodies. See
[class-template operators and conversions](cpp-core-v2.md#class-template-operators-and-conversions).

Instantiated local-class members retain ordinary receiver/result IR and distinct
owning-function instance identities, including local records, fields and scalar
static storage. Written explicit function-instantiation directives are source
validation metadata, not extra runtime declarations: each argument, function type,
conversion name, qualifier and parsed attribute check survives canonical reuse and
no-effect repetitions. Collection and traversal remain bounded. See
[instantiated local classes and written directives](cpp-core-v2.md#instantiated-local-classes-and-written-directives).

Namespace operator function templates use the same canonical instance and typed
call representation. A free operator has its explicit parameters and, for record
results, the existing hidden result pointer; no receiver or runtime template
parameter is added. Operator sequencing, reference identity and all temporary,
parameter and result cleanup follow the ordinary call rules. Source directives
remain independent validation evidence. See
[namespace operator function templates](cpp-core-v2.md#namespace-operator-function-templates).

Concrete core-v2 free function-template instances use ordinary function, record,
scalar-global and call IR. Primary-template ordinals and source identities
separate otherwise colliding specializations, including their local records and
static variables. Scalar non-type arguments lower to existing integer/bool/enum
literals and never add runtime template parameters. Equal constant arguments
share a specialization; different values or deduced argument types keep distinct
identities, including static storage. Patterns and uninstantiated defaults emit
no runtime entities. See [concrete free function templates](cpp-core-v2.md#concrete-free-function-templates)
for mixed type/value parameter limits, written-source checks, deferred non-type
defaults and remaining definition limits.

Resolved core-v2 constexpr-if emits only its selected substatement after normal
init-statement and condition-variable initialization. It adds no condition
evaluation or runtime selection branch; normal cleanup flags may still branch.
Discarded automatic declarations do not become locals, including during outer
switch pre-registration. No new IR operation is needed. See
[resolved constexpr-if](cpp-core-v2.md#resolved-constexpr-if) for the complete-source
and definition-closure limits that remain in this profile.

Core-v2 inline namespaces retain their canonical namespace-qualified source
identities. Parent and explicitly qualified uses share original symbols; distinct
version namespaces do not merge. Inline visibility and bidirectional ADL require
no IR fields or runtime wrappers. See [inline namespaces](cpp-core-v2.md#inline-namespaces)
for C++17 reopening and C++20 nested-spelling boundaries.

Core-v2 namespace aliases and resolved using imports are lookup-only source
declarations. They produce no IR entity, wrapper, storage, initializer or
cleanup; uses retain original canonical type/object/function IDs and the exact
overload selected at the source use. No protocol field or consumer extension is
needed. See [resolved namespace imports](cpp-core-v2.md#resolved-namespace-imports)
for ownership, source closure and C++17 lookup boundaries.

Statically initialized core-v2 scalar locals reuse canonical typed globals and
the optional `mutable` flag. Their source declaration locations and identities
distinguish functions, overloads and lexical scopes. Local declarations and
switch storage pre-registration create no automatic shadow, repeated initializer,
guard or lexical cleanup. Addresses and returned aliases designate the static
object. See [statically initialized scalar locals](cpp-core-v2.md#statically-initialized-scalar-locals)
for the constant-initialization and C++17 function boundaries.

Core-v2 named nested records use the existing record IDs, fields, layouts and
ordinary member signatures. Canonical source scopes distinguish equal-spelled
types. No enclosing-object field or extra receiver is added. The producer orders
by-value record/array dependencies before users; the verifier still rejects
forward or cyclic by-value dependencies. Pointer dependencies retain forward
declarations. See [named nested records](cpp-core-v2.md#named-nested-records)
for source access, bounded sorting and object lifetime rules.

Resolved core-v2 non-template friend declarations are source-level grants,
checked by embedded Clang and the producer's complete declaration/body traversal.
They introduce no IR access flag or runtime operation. Hidden free friends retain
only source parameters; member friends retain their ordinary receiver. Canonical
function identities, typed field accesses and result destinations use the
existing representation. See [non-template friends](cpp-core-v2.md#non-template-friends)
for lookup, source closure and template restrictions.

Core-v2 private/protected fields use the same typed record fields and checked
target layout as public fields. Source access is enforced by embedded Clang
before protocol production; no access flag or runtime privacy mechanism is
added to the IR. Constructors, methods and generated special members refer to
the same canonical storage. The [nonpublic data member contract](cpp-core-v2.md#nonpublic-data-members)
lists the remaining class and field restrictions.

Core-v2 range-based `for` uses the existing local storage, ordinary calls,
`branch`, `jump` and cleanup instructions. The producer checks the identities of
the range reference and begin/end declarations, evaluates initialization once,
and emits separate range and iteration cleanup scopes. Value loop variables
and iterator results keep their typed destinations; reference loop variables
keep aliases. Abrupt exits clean up the scopes they leave. No range opcode,
untyped payload or external compiler process is added. This increment does not
admit templates or STL containers; see the [core v2 contract](cpp-core-v2.md#range-based-for).

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

Deduced reference conversions use the same direct call contract. A mutable
reference result is `ptr:T`, and a const reference is `cptr:T`; the receiver
independently preserves its constness. Reference collapsing changes the selected
signature before emission and introduces no wire opcode or runtime template
parameter. Constructors and destructors still receive mutable storage even when
a temporary is subsequently passed as a const reference. See the
[deduced reference contract](cpp-core-v2.md#deduced-reference-conversions).
Fresh argument fixtures require the conversion call to precede the receiving call,
with its exact `ptr:int` or `cptr:int` result passed through to the reference
parameter. They check the receiver's field address, final reference result,
complete call signatures and relocated output. These structural checks supplement
native O0/O2 mutation and full-expression lifetime checkpoints.


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

Reference-result checks recognize a typed address(dereference(pointer)) pair as
forwarding the same pointer carrier. Pointee and pointer types must match, and
assignment chains still resolve to the exact selected call's result storage.
This normalization does not create an owner or change the lifetime contract.

## Core v2 default argument evaluation

Default arguments are expanded at the selected call site using checked semantic
expressions from source-owned parameters. The wire signature has no optional
parameters or default metadata: ordinary calls contain every argument, with the
same reference carriers and actual record parameter destinations as explicit
arguments. Defaults are never executed in a callee prologue or cached by AST
identity. Explicit arguments omit their default's runtime instructions while
source admission still inspects the default expression.

Default-argument temporaries use the existing typed storage and cleanup guards.
Array elements with omitted initializers and generated whole-array copying use
a cleanup frame per element; explicit array clauses keep the enclosing complete
expression. The containing array retains element ownership. No opcode, schema
or protocol version is added. See the
[default argument source contract](cpp-core-v2.md#default-arguments).

## Core v2 void expression effects

Void casts and value initialization have no semantic value carrier. Source effects
use existing calls, stores and branches; a void return has no `value`, and a void
call has no `target`. No local, parameter, literal, aggregate, cast result or other
expression operand can acquire a void type. Function result metadata can still be
`void`. The checked empty list from source `void{}` never enters the protocol.

Discarded nonvolatile glvalues retain address/index/receiver effects without a
value load. Record and array prvalues retain real construction destinations and
existing full-expression cleanup guards, including selected conditional branches.
Casting a record lvalue to void introduces no copy, unrelated user conversion or
extra owner. There is no new opcode, schema field or version. See the
[void source contract](cpp-core-v2.md#void-and-discarded-value-expressions).

## Core v2 empty record layout

A core-v2 empty record keeps `fields: []` and layout evidence
`size_bits: 8`, `abi_align_bits: 8`, `field_offsets_bits: []`. Independent verification
requires exactly that layout. Record storage accounting assigns at least one unit
to an empty object, including array elements and containing-record members.
Protocol major 1 and the record schema fields are unchanged; the manifest schema
now permits empty offset arrays and retains its core-v2-only `record_layouts` rule.

The NC emitter supplies one internal unsigned-char storage member and retains
size/alignment assertions. The member has no protocol field entry or field-offset
assertion and cannot be selected by a verified `member` expression. Existing byte
pointer conversions can reach representation; exact representation contents are
outside the source contract. No source-field operation reads that internal byte.
Zero-field aggregates have arity zero. Trivial copies preserve operand evaluation
without data-field stores; selected calls, actual destinations, aliases and cleanup
use the ordinary record ABI. See the [empty record source contract](cpp-core-v2.md#empty-record-storage-and-operations).

## Core v2 array temporary storage

A standalone array temporary uses one `arr:<extent>:<element>` local, including
nested array carriers. Element initialization uses typed index places under that
array; record constructor destination pointers refer directly to those places.
Reference parameters carry `ptr:arr:...` or `cptr:arr:...`; array decay retains the
same array address. Discarded array prvalues also receive real storage. The
frontend charges actual storage and generated initialization against its budgets.

One live flag guards complete-array cleanup. Destructor calls target the same
element places in reverse order, including multidimensional rows and shared
semantic default fillers. Full-expression and exact automatic reference owners
use their existing cleanup frames; element aliases add no owner. There is no
array by-value call convention, new opcode or protocol version. See the
[source array lifetime contract](cpp-core-v2.md#standalone-fixed-array-temporary-lifetimes).

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
they add no protocol call kind. Relational `binary` nodes accept equally typed
complete-object pointers and a `bool` result only when the consumer independently
verifies a native flat-address unsigned carrier of the recorded pointer width.
The emitter uses that private carrier to preserve C++ ordering without introducing
C undefined behavior for unrelated pointers. Source/wire pointer-integer casts
remain rejected. No schema version or frontend-supplied ABI permission is added.
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

## Core v2 explicit lifetimes and memory aliases

The optional core-v2 module property `array_cookie_abi` is one of the exact
strings `itanium`, `apple-arm64` or `msvc`. Empty, unknown, nonstring and v1 values
are rejected. It additionally requires `memory_lifetimes: true` and a matching
independently constructed native target/size_t capability. Producer/consumer
agreement must also match the requested target family. Saved NC checks size_t
width/alignment/unsignedness and the Windows MSVC/GNU distinction. Existing OS
and architecture guards distinguish Apple ARM64's two-word cookie from generic
Itanium's right-justified count and MSVC's leading count. This metadata introduces
no generic pointer casts, dynamic array type, allocation instruction, foreign
symbol, or ownership authority. Array cookie stores, construction and reverse
destruction loops use existing checked typed operations; metadata does not prove
all of those operations semantically correct. See the
[array allocation contract](cpp-core-v2.md#constant-array-allocation).

The optional module property `memory_lifetimes` must be boolean `true` and requires
`cpp-core-v2`. Absence defaults to false; an explicitly false/nonboolean property
is rejected. The verifier independently rejects this flag in other profiles.
It changes only the emitted aliasing contract. Existing operation/type validation,
layout/callback checks, const storage permissions and initialization ownership
remain authoritative; it supplies no runtime provenance proof.

The emitter uses deterministic private `may_alias` typedefs for every nonvoid,
nonrecord object carrier. Recursive pointer, array and callback declarations use
child aliases; record forward declarations and definitions mark the record tag,
and fields use the corresponding aliases. Types appear before use, with complete
record dependencies ordered before array aliases. All source globals, locals,
parameters/results, casts, compound literals and pointer helpers share those
types. A compiler attribute-support guard accompanies the existing ABI guards.
Private arithmetic helper values and atomic guard storage remain unchanged.

Source explicit nontrivial record destruction lowers to the existing
`void(ptr:Record)` helper and retains the actual captured receiver. Trivial
record destruction and scalar pseudo-destruction preserve only source receiver
effects. Scalar dot operands do not acquire a load; arrow operands evaluate the
pointer. Calls in unevaluated expressions introduce no runtime destruction or
otherwise unused template-body instantiation. Automatic cleanup flags remain
registered after explicit calls. No new destruction opcode, foreign ABI or exception behavior is added.
Source-defined single-object allocation uses the checked operations below. See the
[source contract](cpp-core-v2.md#explicit-destruction).

## Core v2 source-defined allocation

Single-object new/delete set `memory_lifetimes: true` and reuse existing checked
calls, captures, casts, initialization, branches and destruction helpers. There is
no opaque allocation opcode or foreign runtime escape. The ordinary consumer
verifies every callee's source function closure, argument/result types, layout,
const permissions and control flow as before.

New passes a typed size literal and an optional selected alignment literal, then
captured placement arguments to the allocator. Its saved `void*` result becomes
the cv-qualified source result pointer. Initialization uses an internal mutable
view at that exact address. A semantic null-check branch skips initialization for
failed nonthrowing allocation. Placement temporaries keep their existing
full-expression flags; the new object acquires no lexical cleanup flag.

Delete saves its operand before branching. The nonnull block destroys through
that saved address, then calls the selected deallocator with the saved pointer and
optional typed size/preferred-alignment literals. A source destructor cannot
reseat this private capture. Missing source definitions are rejected before
emission. Default heap/runtime, arrays and exceptions remain unfinished; see the
[source contract](cpp-core-v2.md#single-object-allocation-and-placement-reuse).

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

## Core v2 string and constant-array storage

String literal lvalues use ordinary read-only globals with `arr:<count>:<integer>`
types and complete `aggregate` initializer trees. Every decoded code unit,
embedded zero and final terminator is an exact typed integer literal. UTF-16 and
UTF-32 retain their source code units; no host JSON string conversion, encoding
guess or opaque string opcode is involved. Literal-object names use the reserved
`nct_string_` prefix with deterministic discovery ordinals. These names identify
storage, not a promise that different source occurrences share an object.

Core v2 accepts folded array globals with either mutability and const records
containing arrays. The consumer verifies every extent, element type, initializer
arity and folded leaf, together with normal carrier/record layout checks. Array
aggregates remain initializer-only; this does not admit runtime array assignment,
array value parameters/results. Constant pointer relocations follow the checked
static-address contract below.
Array decay and addresses require const-qualified pointers when rooted in
read-only globals; writes retain ordinary const-storage rejection. The emitter
declares complete `static` or `static const` arrays before functions according
to each object's mutability.

Source character-array initializers lower to typed element stores into their
own destination, including zero-filled trailing elements. Evaluated literal
addresses use static globals instead. Literal definitions discovered while
lowering functions or cleanup helpers are included before final serialization.
No dynamic allocation, initialization call or string runtime mapping is added.
Source const arrays use initialization of the actual declaration for constant
evaluation; the initializer expression's lvalue address is not an array value.
See the [string and array contract](cpp-core-v2.md#string-literals-and-constant-arrays).

The same array globals represent namespace objects, function-local statics and
defined static data members, including admitted concrete template instances.
Their source owner and canonical identity determine the generated name; no
automatic local or block-entry initialization represents them. Constant
initialization evaluates the actual source-owned definition and retains its
complete typed object value. A mutable array may contain record/array subtrees;
the verifier checks every folded leaf, extent, element type and layout. Runtime
whole-array assignment remains invalid. Literal objects always retain their
read-only permission. See [static arrays](cpp-core-v2.md#fixed-array-static-storage).

## Core v2 constant object addresses

Pointer-valued folded initializers can use `null` or `address` with a checked
global-object path, optionally wrapped in admitted pointer `cast` nodes. The
addressed path uses existing typed `var`, `member` and `index` expressions.
An indexed address must start from `array_decay` of an actual array or `address`
of one complete object and use an exact nonnegative promoted integer literal.
The final index may equal the extent; intermediate subobjects must be within
the object. All ordinary type, member and const-address checks still apply.

This path validation does not allow loading another pointer global, dereferencing
a stored pointer, executing a call, converting an integer address or calculating
an arbitrary runtime expression as a global initializer. The producer folds
source constant pointer values to their actual global/literal target and checks
the retained APValue subobject path against its byte offset. Missing source
definitions retain `TR0203`; malformed protocol paths are rejected by the consumer.

Mutable and const object-pointer globals use the existing `mutable` permission,
independently from pointer qualifiers. Complete aggregate initializers can
contain these pointer constants. Static storage declarations precede initialized
definitions when an address can name a later object; generated identifiers retain
internal linkage. Addressed array elements emit C23 constant pointer arithmetic
without a runtime helper call. Runtime pointer arithmetic retains its existing
helpers. No new opcode, arbitrary byte relocation, allocator or initializer
function is introduced. See the [source contract](cpp-core-v2.md#static-object-pointer-storage).

## Core v2 static reference carriers

A statically bound object reference is an ordinary pointer-typed global with
`mutable` absent/false and a checked constant object address. The producer
requires an actual source-owned reference definition and constant initializer,
rejecting null, one-past, automatic and nonpermanent temporary targets.
The existing pointer nodes encode the canonical target and full typed path;
no protocol reference opcode or writable binding flag is added.

Uses dereference the immutable carrier. Thus assignment changes the referred
object, while assignment to the carrier itself is rejected as a global-constant
write. Pointee constness independently controls writes through the alias. Array
reference carriers retain complete array extents and string aliases retain
read-only storage. A local static reference creates no automatic binding shadow
or block-entry initialization. Return snapshots and normal expression effects
still use the ordinary lowering rules. See the
[source contract](cpp-core-v2.md#static-reference-bindings).

## Core v2 static temporary objects

Constant-initialized lifetime-extended temporaries use ordinary typed globals,
with scalar or complete aggregate values and the actual object's `mutable`
permission. Their reference carriers remain immutable pointer globals. Checked
addresses use the existing `var`, `member`, `index` and `address` nodes, including
self pointers and references to array/record subobjects. The producer requires
Clang's exact static owner descriptor and retained constant value, and registers
the object's identity before serializing its fields. The shared verifier checks
these globals and address paths with its existing rules; no new opcode or
lifetime flag is added. See the [source contract](cpp-core-v2.md#static-reference-temporary-lifetime-extension).

## Core v2 static record storage

Record globals in core v2 may use either mutability, with complete folded
aggregate initializers checked against their declared field types and target
layout. Pointer fields may hold checked object addresses, including addresses
of the containing record or its fields. Internal tentative declarations precede
initialized definitions when needed for these addresses. Mutable member writes
and admitted record assignments use existing operations; const objects retain
the global write prohibition.

The producer evaluates the actual source definition and retains canonical
identity across namespace redeclarations, local/class statics and template
instances. Trivial default construction at static storage supplies zero values
before any ordinary runtime use; automatic uninitialized storage retains its
existing rules. Dynamic initialization, source-owned allocation and static destruction
follow their dedicated contracts. See the [source contract](cpp-core-v2.md#static-record-objects).

## Core v2 binary floating-point values

Core v2 accepts `float` (IEEE binary32) and `double` (IEEE binary64). A literal is
exactly `{kind:"literal",type:"float",bits:"<8 lowercase hex digits>",loc:...}`
or the corresponding `double` object with 16 digits. Decimal JSON values,
incorrect widths, uppercase digits and extra payload fields are rejected.
Representations preserve signed zero, subnormals, infinity and NaN bits through
typed compiler constants; they never pass through host JSON floating numbers.

Floating unary signs, arithmetic `+`, `-`, `*`, `/` and comparisons require
matching operand types after explicit source-selected conversions. Arithmetic
retains that type and comparisons produce bool. Scalar casts express integer,
floating and boolean conversions. Increment and compound assignment reuse
checked loads, arithmetic, conversions and stores. Remainder, bitwise and shift
instructions do not accept floating operands. Constant trees and mutable scalar
globals admit either type, using ordinary permissions and storage identity.

The carrier table appends `float` and `double` after the existing `default-pointer`
entry; their required sizes are 32 and 64 bits. The driver independently checks
the source formats and layouts against NeverC, and the consumer reconstructs
record layouts. Generated code checks the target and strict evaluation policy
and disables contraction. This profile uses no math metadata or mapping calls.
See the [floating contract](cpp-core-v2.md#binary-floating-point-values) for the
default floating environment and remaining source restrictions. Older experimental
v2 responses lacking the two carrier entries must be regenerated.

## Gated mathematics extension

`cpp-math-v1` uses project schema 1 and the same owned declaration/ODR envelope
as `cpp-project-v1`. Admission requires independent SDK, exact-runtime and
link checks; differential and installed-output tests establish the published
support boundary. Core and project profiles reject math metadata and mapped
instructions. Core v1 and project v1 also reject double types; core v2 admits
general floating values through its separate contract above.

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

Member class templates in ordinary record scopes use the existing canonical
record IDs, dependency-first field layout, method call signatures and scalar
globals. Ordinary enclosing records add no implicit receiver or runtime template
object. Full member declarations retain their exact argument source in the
frontend; this adds no wire field or opcode. Templated outer record owners use the
[dependent-owner contract](cpp-core-v2.md#dependent-outer-member-class-templates). See [member class templates](cpp-core-v2.md#ordinary-owner-member-class-templates).

Partial declaration checks retain successful primary argument metadata in the
embedded frontend before lowering. Written and copied declarations are distinct
from selected-partial deduction events. Resolved parameter type sources are
checked even while another argument or pack length is pending. Unknown tail
positions cannot authorize concrete substitution edges. A declaration must still
pass C++ specialization ordering and deduction checks before producing output. This does not add public
protocol fields or opaque fallback nodes; emitted concrete values and storage
identities retain their existing checks and relocation guarantees.

Nested member class templates preserve canonical outer/inner record identity,
dependency-first fields, concrete receiver signatures and scalar global names.
Semantic parameter owners remain distinct from effective body origins. Hidden
copied partial declaration checks reuse the existing source contract; no wire
field, runtime template object or implicit enclosing receiver is added. Different
outer arguments retain separate inner types and statics even when their final
field types match. The paired protocol fixture checks receiver/call closure,
shared equivalent storage and relocated-source equality. Copied class-scope full declarations follow their separate exact source and
body-origin contract. Ordinary nested records follow their own checked member-body contract.

### Copied full member class source and body identity

[Class-scope full copies](cpp-core-v2.md#copied-class-scope-full-specializations)
use the existing typed record/function/global schema. The full body contributes
no template parameter level; argument ownership remains with its actual primary
and body substitutions remain associated with actual outer instances. Distinct
outer instances keep distinct full-record and scalar static identities even
when inner arguments agree. The embedded source event identifies the actual
copy and its exact original full declaration; it is not a public wire object.

Paired fixtures check actual method receivers and signatures, dependency-first
record fields, closed value/call references, shared equivalent static addresses,
distinct outer static addresses and byte-equivalent output after relocation.
O0/O2 runtime checks cover full/partial selection, copies, moves, assignment and
destruction. These fixtures require native CI from the implementing revision.

### Ordinary nested member source and body identity

[Ordinary nested member classes](cpp-core-v2.md#ordinary-nested-classes-in-generic-owners)
use existing typed records, functions and globals. Their concrete copies retain
actual source origins without a synthetic template argument level. Own written
specializations supply their actual field order and method set; an unused
ordinary member declaration does not create a generic runtime object or force
its body. Existing copied full declarations keep their eager behavior.

The internal explicit member-class event preserves every successful directive's
actual target/origin, qualifier, locations and parsed attributes, including
extern and no-effect exits. The event is not exported as a protocol record.
Paired fixtures verify separate outer/nested identities, own specialization
layout, dependency-first by-value fields, actual method receivers, closed calls
and values, shared equivalent static storage, distinct instance storage and
relocated-source equality. Runtime checks include copy/move/assignment and
destruction at O0/O2. Native CI of the implementing revision must validate the
protocol assertions and runtime behavior.

Instantiated non-template friends use ordinary concrete free function entries,
with their explicit parameters and no receiver or runtime template level. Actual
namespace merging keeps one function identity; distinct overloads and static
locals remain distinct. The paired fixture checks closed calls/references,
receiver-free signatures, instance-local scalar storage, friend-local constructors/
methods with real outer-pack counts, and root relocation.
Original/selected/granting source events are internal to the embedded frontend.
See [the friend contract](cpp-core-v2.md#instantiated-non-template-friend-functions)
for lazy definitions, normalized source limitations and native evidence scope.

Instantiated non-template friend types add no wire entries or implicit arguments.
Their selected methods use existing concrete record/field types and ordinary
calls. The protocol fixture checks private reads/writes, static method signatures,
closed references and calls, and equality after root relocation. Exact original
and substituted type sources remain internal to the embedded frontend; see the
[type-friend contract](cpp-core-v2.md#instantiated-non-template-friend-types).

Friend function templates use the existing concrete free-function protocol.
Their primary and source events are internal and add no receiver or runtime
template argument. The actual canonical primary prefixes concrete functions,
static locals and local records; a copied primary without independent written
identity also includes its proven granting class. Source declarations remain
checked after canonical merging. Fixtures check typed arguments/results, shared
and distinct targets/storage, closed calls/references and root relocation; the
implementing revision must validate native counts. See the
[friend function template contract](cpp-core-v2.md#friend-function-templates).

Friend class-template grants use the existing class-instance protocol and add no
runtime access declaration, receiver or storage. The semantic target class and
its methods/statics are shared across equivalent repeated grants. Actual target
specializations retain distinct ordinary identities; the granting source is never
an extra identity component. Exact original/copied declarations and selected
parameter/default headers stay internal and independently checked. Typed call,
field, storage and relocation fixtures require implementing-revision native CI.
See the [friend class template contract](cpp-core-v2.md#friend-class-templates).

Ordinary callbacks in standalone `cpp-core-v2` use the canonical type spelling
`fnptr:<parameter-count>:<byte-length>:<result><byte-length>:<parameter>...`.
Each length covers exactly one complete nested type spelling, measured in bytes;
counts and lengths are unsigned decimal without leading zeroes. Counts range from
0 through 64; component lengths are positive. The complete spelling remains at
most 4096 bytes and obeys the existing depth/node budgets. For example,
`fnptr:1:3:int3:int` is `int (*)(int)` and `fnptr:0:4:void` is `void (*)(void)`.
Result and parameters use admitted scalar/reference carriers, with void only as a
result. Function pointers are distinct from object pointer types; `ptr:fnptr:...`
is the address of callback storage, not the address of a function.

A `function_address` expression contains exactly `kind`, `type`, `name` and `loc`.
Its symbol must name a function definition with exactly the encoded result and
parameter types. It never refers to data storage. Null callback expressions use
`kind: null` with a function-pointer type. Globals may contain folded null or
symbolic callbacks; runtime reads and calls are not constant initializers.

An `indirect_call` instruction contains exactly `op`, `loc`, `callable`, `args`
and, for a nonvoid result, `target`. It contains no direct `callee` or mapping ID.
`callable` is an ordinary checked function-pointer expression; arguments match its
signature exactly and a nonvoid target is matching local result storage. The
source frontend captures the entire postfix value before lowering arguments.
Synthetic IR independently validates the callable's declared storage and typed
operands; it does not introduce a new definite-initialization analysis.

The emitter uses recursive typed declarators, prior function prototypes and
signature-specific layout guards. New enum values are appended, preserving old
project fingerprints. The existing project and math profiles reject callback
types before symbol merging, map publication or emission. Cross-TU remapping and
project inline-address identity are not implied by this standalone addition.
See the [ordinary function pointer contract](cpp-core-v2.md#ordinary-function-pointers).

### Concrete template callback identity

Supported concrete function-template callbacks reuse `fnptr`, `function_address`
and `indirect_call`. Source admission requires the actual selected specialization,
its successful complete source event and an owned definition. The address symbol
is the same canonical emitted function used for a direct call. Repeated selections
share one definition and its static-local storage; differing template arguments
retain distinct functions and storage even when their normalized signatures match.
No source-proof data or extra template identity is invented in the wire format.
All ordinary callback validation and previous-profile exclusions remain in force.
See the [template callback contract](cpp-core-v2.md#concrete-function-template-pointers).

### Callback variable-template storage

A concrete callback variable template emits the existing global declaration with
its canonical variable name, `fnptr` type, mutability and null or `function_address`
constant. Equivalent arguments/redeclarations reuse one global. Distinct variable
instances retain separate globals even when their address constants name the same
function. A storage address has `ptr:fnptr:...` or `cptr:fnptr:...` type and retains
the variable identity; loading and calling the value uses `indirect_call`.
Actual declaration/type/initializer source checks remain frontend obligations;
normal global and callback signature/definition validation remains unchanged.
See the [callback variable-template contract](cpp-core-v2.md#callback-variable-templates).

## Core v2 reference-member carriers

An admitted source reference field is represented by its object-pointer carrier
in the checked record layout. No new reference opcode or wire type is introduced.
Construction writes the carrier; ordinary member access dereferences it. The
referent's qualifiers and extent remain in the pointer type. Constness of the
containing record restricts its stored carrier, not a mutable referenced object.
Copying carrier fields preserves their addresses, including self-references to
the original object. Source-deleted special members are rejected before lowering.

Constant fields serialize the checked actual referent address. Runtime aggregate
initializers may have zero pointer carriers before guarded construction, and
extended static temporaries name that aggregate's root initialization group.
Nested temporary groups remain flat; children do not acquire their own guards.
Materializing default array elements have distinct source identities and storage
for constant and runtime initialization. The source frontend records each generated
omitted element so direct default construction retains its element cleanup
boundary after semantic-list expansion. Constructors of fields inside aggregate
elements retain the enclosing full-expression; nested array constructors apply
their own element rule. No omission flag is accepted from the IR protocol; ordinary instructions still
express the actual initialization and cleanup order. Source expansion remains
bounded, and unexpected shared static temporary identities are diagnosed. See the
[source contract](cpp-core-v2.md#reference-members).
