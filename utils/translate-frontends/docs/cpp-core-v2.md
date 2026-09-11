# C++ core v2: declarations, pointers and references

`cpp-core-v2` is an experimental, explicitly selected extension of the
single-source `cpp-core-v1` contract. It adds the declarations and bounded pointer
and reference operations below. It is a step toward broader C++17 translation;
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
| Scoped and unscoped enums | Accept an established `int` or `unsigned int` underlying type, each exactly 32 bits. Other underlying types remain rejected. |
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
runtime string literals, `std::string` or array types.

## Object pointers and lvalue references

- Local variables, free-function parameters and results may use object pointers
  and lvalue references. Supported pointees are `int`, `unsigned int`, `bool`,
  the admitted enums and complete trivial records; `void *` is supported without
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

## Remaining scope and wire representation

Arrays, additional integer widths, floating-point types,
classes with nontrivial lifetime behavior, templates, exceptions, STL headers
and library mappings are not implemented by core v2. Project translation
and the bounded math profile remain separate v1 profiles; selecting core v2
does not implicitly combine their capabilities.

The transport protocol and artifact schemas retain version 1. Core v2 adds
canonical recursive type strings `ptr:<type>` and `cptr:<type>` for mutable and
const pointees, and explicit `null`, `address` and `dereference` expression nodes.
Type depth is limited to 64 pointer components and spelling to 4096 bytes; types
also consume the protocol's node budget. The consumer verifies pointee identity,
addressability, const writes and the restricted cast/operator rules before emission.
The manifest records `profile: cpp-core-v2`
and `profile_version: 2`; existing profiles continue to record profile version
1. A frontend response for another profile is rejected.

The regression cases cover generated execution at O0/O2, scoped and unscoped
enums, signed/unsigned boundary values, overloads, global/aggregate values,
local declarations, pointer/reference aliasing with inlining disabled, nested
const, nulls, reference-return assignment, unsupported bindings, malformed IR,
old-profile rejection and consumer profile/version boundaries. CI evidence must
be recorded against the
revision that runs these cases; earlier core v1 CI results do not establish
core v2 execution support.
