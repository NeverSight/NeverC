# C++ core v2: integral types, declarations and object storage

`cpp-core-v2` is an experimental, explicitly selected extension of the
single-source `cpp-core-v1` contract. It adds the declarations, bounded pointer
and reference operations, fixed arrays, integer widths and size queries below. It is a step toward broader C++17 translation;
it does not claim complete C++17 or STL support. The existing core, project and math v1 profiles retain
their accepted-input contracts.

```sh
neverc translate --from cpp --profile cpp-core-v2 input.cpp -o output.nc
neverc output.nc -c -o output.o
```

The frontend remains statically built into NeverC. There is no external Clang
selection or installation requirement. The profile accepts one self-contained
C++17 source without includes and uses the existing native hosted target rules.
`--check`, output ownership, diagnostics and artifact validation follow the
[existing design](design.md).

## Accepted declarations

| Construct | Translation behavior |
| --- | --- |
| `typedef` and non-template `using` aliases | Resolve to the supported underlying type. Namespace and local aliases, alias chains, and aliases for supported records or `void` are checked before erasure. An unused alias cannot introduce an unsupported type. |
| Scoped and unscoped enums | Accept an established supported integral underlying type, including narrow/wide integers and `bool`. |
| Enum constants and values | Preserve resolved enumerator values, source conversions, parameters/results, locals, compile-time globals and supported aggregate fields using the corresponding scalar IR type. Clang resolves distinct enum types and overloads before lowering. |
| `static_assert` | Clang checks the assertion. The translator inspects its condition for unsupported types and operations before erasing it; the optional diagnostic message is not runtime string input. |

```cpp
using Count = unsigned int;
enum class Mode : Count { low = 1u, high = 0xffffffffu };
static_assert(static_cast<Count>(Mode::high) == 0xffffffffu, "enum width");

int main() {
  using Local = Mode;
  Local mode = Mode::high;
  return static_cast<Count>(mode) == 0xffffffffu ? 0 : 1;
}
```

Enum source names do not become C++ enum declarations in the generated source.
The generated representation uses the verified underlying scalar type. This
preserves the supported source behavior; it does not promise arbitrary C++
binary ABI or foreign-object identity.

## Boundaries and artifact versions

Unsupported operations remain errors even inside constant-evaluated assertions
and enumerator initializers. For example, a cast to `void` is not yet admitted
by this profile. A successful `static_assert(true, "message")` does not admit
runtime string literals or `std::string`.

## Integer widths, characters and size queries

Core v2 admits signed and unsigned 8-, 16-, 32- and 64-bit integer storage.
This covers `char`, `signed char`, `unsigned char`, `short`, `int`, `long`,
`long long`, their unsigned forms, `wchar_t`, `char16_t` and `char32_t`.
`bool` keeps its distinct boolean representation. Enums may use any admitted
integral underlying type, including `bool`; opaque fixed-underlying enums are
checked before erasure. Ordinary and wide/UTF character literals use the values
resolved by the pinned Clang frontend. Runtime string literals remain outside
this increment.

Clang resolves the source types, promotions and overloads first. The typed IR
then records signedness and width using canonical `int`, `uint`, `i8`, `u8`,
`i16`, `u16`, `i64` and `u64` spellings. `i32` and `u32` are not aliases.
The corresponding native emission carriers are signed/unsigned char, short,
int and long long. Every source type must match its carrier's size and ABI
alignment. Target-dependent `long`, `wchar_t` and the type of `sizeof` therefore
follow the selected target; normalization does not promise C++ nominal or
foreign binary ABI identity. Source-derived overload names remain distinct.

Narrow arithmetic is explicitly promoted before execution, including narrow
increment/decrement. Integer literals remain exact decimal strings, including
64-bit extrema. Out-of-range signed conversions follow the pinned Clang
frontend's two's-complement result: conversion to an N-bit unsigned carrier
is followed by a representable signed mapping of the upper half. Signed left
shift and arithmetic right shift use helpers of the same width. The existing
source-defined-execution boundary still applies to signed overflow, division
by zero and invalid shift counts.

Standard constant `sizeof` and type-form `alignof` are admitted for supported
types. Operand types and expressions are inspected for unsupported constructs;
operand side effects are never lowered (`sizeof(++value)` leaves `value`
unchanged). References use their referent's size/alignment. Record and array
queries use the verified source/target layout evidence below. Queries on void,
functions, unsupported or variable-length types, GNU preferred alignment,
expression-form alignment and parameter packs are rejected.

## Object pointers and lvalue references

- Local variables, free-function parameters and results may use object pointers
  and lvalue references. Supported pointees are the admitted integer and character types, `bool`,
  the admitted enums, complete trivial records and admitted fixed arrays; `void *` is supported without
  dereferencing `void`. Function and member pointers remain unsupported.
- Address, dereference, `->`, pointer equality/inequality, conversion to `bool`,
  null initialization (`nullptr`, zero and value initialization), qualification
  conversions and pointer/`void *` round trips preserve the admitted source
  behavior. `const_cast` may adjust qualifications on supported pointer and
  lvalue-reference types. Pointer arithmetic, difference, ordering and integer
  reinterpretation remain rejected.
- A reference binds to the original storage. Returning a reference, assigning
  through that result, references to pointer variables (`int *&`) and conditional,
  comma, assignment and preincrement lvalues preserve identity and sequencing.
- `const` is retained at each pointee level. A pointer to a const record cannot
  reseat its pointer field, but can mutate that field's mutable pointee. Trivial
  records may contain pointers and refer to other complete records, including
  themselves, through pointers; by-value record cycles remain invalid.

```cpp
int &choose(bool first, int &left, int &right) {
  return first ? left : right;
}
int main() {
  int left = 1, right = 2;
  choose(false, left, right) = 7;
  int *pointer = &right;
  return *pointer == 7 ? 0 : 1;
}
```

Rvalue references, temporary reference bindings (including conversion-created
temporaries and temporary subobjects), reference fields and pointer/reference
globals (including pointer-valued global aggregate fields) are rejected.
Unused/dead bindings are checked too. Standalone
`nullptr_t` variables are not supported. The contract covers accesses to live
objects; it does not define dangling/invalid pointer behavior or remove C++
undefined behavior. In particular, casting away `const` does not make an
originally const object writable. Generated `.nc` source targets NeverC's
pointer aliasing behavior, not an arbitrary C compiler's alias rules.

## Fixed arrays and initialization

- Local arrays and record array fields may have nonzero constant extents. Each
  dimension is limited to 65536 elements, with at most 200000 expanded storage
  units and a separate 200000-node initializer/assignment expansion budget.
  Nested arrays and records count toward these limits. Initializing a large
  array can exceed the work budget even when its extent alone is permitted.
- Support includes array-to-pointer decay, `a[i]` and `i[a]`, multidimensional
  arrays, adjusted array parameters, and pointers/lvalue references to arrays
  as parameters and results. The syntactic left operand is evaluated before
  the right operand, as required by C++17; aliases keep the original storage.
- List/value initialization stores each element in source order and zero-fills
  omitted elements, including nested record fields. Default initialization does
  not initialize scalar elements. Accessing one initialized element never copies
  unrelated uninitialized elements or record fields. Trivial record copies
  include their array fields; direct array assignment is not permitted.
- Array element qualification is preserved through decay and pointers/references
  to arrays. A const record's array cannot be used to obtain a mutable element
  pointer. Reads such as `make_record().values[0]` materialize a trivial temporary
  for the full expression; reference binding to temporary subobjects remains
  rejected by the existing lifetime boundary.

```cpp
using Row = int[3];
Row &row(Row &value) { return value; }
int main() {
  Row values{5, values[0] + 2};
  row(values)[2] = 11;
  return values[0] == 5 && values[1] == 7 && values[2] == 11 ? 0 : 1;
}
```

Global arrays and global records containing arrays, zero-length and variable-length
arrays, unsupported element types and nontrivial element construction/destruction
remain rejected, including in unused or dead code. Indexing requires the same
valid storage and in-bounds accesses as the source program; the translator does
not add a runtime bounds-check guarantee. Constant `sizeof` and type-form
`alignof` are supported under the integral-query rules above; pointer arithmetic
and pointer difference still require later operation support.

## Switch control flow

Core v2 accepts `switch`, `case` and `default` with supported promoted 32- or 64-bit
integer/enum selectors. C++17 switch init-statements and condition variables are
initialized once, and the selector is evaluated once before dispatch. Case values
are checked constant expressions; normal fallthrough and Clang-validated
`[[fallthrough]];` annotations are preserved.

Dispatch can enter cases nested in blocks, conditionals or loops. Storage is
registered for declarations that a case entry can bypass, without executing their
initializers or zeroing uninitialized objects. Clang still rejects illegal jumps
past initialization. A constant-expression selector selects only its matching
case/default/exit in the generated control-flow graph.

`break` exits the nearest loop or switch. `continue` selects the nearest enclosing
loop even when a switch lies between it and that loop. Nested switches keep
independent case labels and exits. GNU case ranges, other statement attributes,
`goto` and ordinary named labels remain outside this profile. Unsupported case
expressions are diagnosed even in dead or unused code.

```cpp
int main() {
  int result = 0;
  switch (int value = 1; value) {
  case 1: result = 3; [[fallthrough]];
  case 2: result += 4; break;
  default: return 1;
  }
  return result == 7 ? 0 : 1;
}
```

## Verified target and record layout

Experimental core v2 now requires `target.carrier_layout` in frontend responses
and manifests. It contains `char_bits: 8` and exact `size_bits`/`abi_align_bits`
entries named `bool`, `i8`, `u8`, `i16`, `u16`, `int`, `uint`, `i64`, `u64` and
`default-pointer`. These name the native emission carriers for the admitted source types.

The driver independently constructs NeverC's target model for its recorded
C23 validation options. The verifier compares all source carrier evidence
against this model; a matching triple alone is insufficient. Missing, unknown
or mismatching layout fields are rejected. Source sizes and ABI alignments
must agree, with supported byte sizes and power-of-two alignment.

Each response record also requires `layout` containing `size_bits`,
`abi_align_bits` and `field_offsets_bits` in declaration order. The verifier
reconstructs the natural layout of the currently supported plain aggregates
from matched carriers, arrays and earlier records. Arithmetic is bounded;
layout size is at most 25,600,000 bits and field offsets must agree exactly.
This does not admit packing, custom alignment, bases or bitfields.

Generated `sizeof`, `alignof` and `__builtin_offsetof` static assertions check
these values again in both syntax and object validation. The manifest retains
the same record evidence in `record_layouts`, with each record's emitted `id`.
Existing v1 profiles reject the new metadata. Older experimental v2 responses
without layout evidence must be regenerated; protocol/schema major 1 and
profile version 2 remain unchanged. Core v2 still emits a single source file;
a separate generated header belongs to project mode.

## Remaining scope and wire representation

128-bit and extended integers, floating-point types,
classes with nontrivial lifetime behavior, templates, exceptions, STL headers
and library mappings are not implemented by core v2. Project translation
and the bounded math profile remain separate v1 profiles; selecting core v2
does not implicitly combine their capabilities.

The transport protocol and artifact schemas retain version 1. Core v2 adds
canonical recursive type strings `ptr:<type>` and `cptr:<type>` for mutable and
const pointees, plus `arr:<positive-count>:<element>` for arrays. Expression nodes
include `null`, `address`, `dereference`, `array_decay` and `index`. Array aggregate
nodes are permitted only as nested initializer trees, never as assignable array
values. Array elements must be complete even behind an outer pointer.
Type depth is limited to 64 derived components and spelling to 4096 bytes; types
also consume the protocol's node budget. The consumer verifies pointee identity,
addressability, const writes and the restricted cast/operator rules before emission.
The manifest records `profile: cpp-core-v2`
and `profile_version: 2`; existing profiles continue to record profile version
1. A frontend response for another profile is rejected.

The regression cases cover generated execution at O0/O2, narrow/wide integer
promotions and conversions, character literals, size queries, scoped and unscoped
enums, signed/unsigned boundary values, overloads, global/aggregate values,
local declarations, pointer/reference aliasing with inlining disabled, nested
const, nulls, reference-return assignment, array initialization and indexing order,
multidimensional arrays, temporary array reads, switch dispatch/fallthrough,
nested case entry and loop control, constant selectors, resource limits, unsupported
bindings, malformed IR, independent carrier/record layout evidence,
compiled layout assertions,
old-profile rejection and consumer profile/version boundaries. CI evidence must
be recorded against the
revision that runs these cases; earlier core v1 CI results do not establish
core v2 execution support.
