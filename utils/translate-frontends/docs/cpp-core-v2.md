# C++ core v2: scalar types, declarations and object storage

`cpp-core-v2` is an experimental, explicitly selected extension of the
single-source `cpp-core-v1` contract. It adds the declarations, bounded pointer
and reference operations, fixed arrays, integer and floating types, size queries and ordinary
record methods and constructors below. It is a step toward broader C++17 translation; it does not
claim complete C++17 or STL support. The existing core, project and math v1
profiles retain their accepted-input contracts.

```sh
neverc translate --from cpp --profile cpp-core-v2 input.cpp -o output.nc
neverc output.nc -c -o output.o
```

The frontend remains statically built into NeverC. There is no external Clang
selection or installation requirement. The profile accepts one C++17 source,
the bounded standard-header surface below, and the existing native hosted target rules.
`--check`, output ownership, diagnostics and artifact validation follow the
[existing design](design.md).

## Compile-time `<type_traits>`

Core v2 accepts an exact top-level `#include <type_traits>` using the immutable
libc++ 20.1.8 and Clang resource headers embedded in NeverC. The header closure
is read from the built-in VFS and recorded as exact `libcxx`/`resource` paths and
SHA-256 hashes in the semantic module and output manifest. Platform headers,
host include directories and environment-selected SDKs cannot participate.

Aliases and integral or enum constant results are available when their resolved
types and operands already satisfy core v2. This includes forms such as
`std::remove_cv_t`, `std::is_same_v`, `std::is_constructible_v`,
`std::is_nothrow_destructible_v` and `std::integral_constant<T, V>::value`.
Clang performs normal C++17 substitution and constant evaluation; the translator
then emits the concrete type or literal and does not copy libc++ declarations
into generated C23.

```cpp
#include <type_traits>
using Raw = std::remove_cv_t<const int>;
using Three = std::integral_constant<int, 3>;
static_assert(std::is_same_v<Raw, int>);

int main() {
  return Three::value + std::is_constructible_v<int, int> == 4 ? 0 : 1;
}
```

This surface is compile-time-only. Constructing a standard-library object,
calling a standard-library function or taking the address/reference identity of
a trait constant is rejected. Quoted `"type_traits"`, unapproved standard
headers, platform headers and user shadow headers remain outside this boundary.
The same closure is checked on the supported macOS, Linux and Windows x86-32,
x86-64 and AArch64 targets. Broader standard-library and STL support remains in
development.

## Fixed-width integers from `<cstdint>`

Core v2 also accepts an exact top-level `#include <cstdint>` from the same
immutable embedded VFS. The standard fixed-width, least-width, fast-width,
pointer-width and maximum-width aliases are available whenever the target
provides them, including `std::int32_t`, `std::uint64_t`, `std::intptr_t` and
`std::uintmax_t`. The corresponding limit and constant macros, such as
`INT32_MAX`, `UINT64_MAX`, `INT32_C` and `UINT64_C`, retain Clang's target
specific C++17 types and values.

```cpp
#include <cstdint>
static_assert(sizeof(std::intptr_t) == sizeof(void *));
static_assert(INT32_MAX == 2147483647);

int main() {
  std::uint64_t wide = UINT64_C(4294967296);
  std::int32_t answer = INT32_C(41);
  return wide == UINT64_C(4294967296) && answer + 1 == 42 ? 0 : 1;
}
```

The standalone closure contains nine authenticated libc++/resource files on
each supported target. Including `<type_traits>` and `<cstdint>` together is
order independent and records their deduplicated union. Quoted `"cstdint"`, the
C header `<stdint.h>`, platform headers and user shadow headers are rejected.

## Numeric bounds from `<limits>`

Core v2 accepts an exact top-level `#include <limits>` and exposes
`std::numeric_limits<T>` for the admitted integer, `float` and `double` types.
Its integral/enum static data members are compile-time constants. The zero-argument
`min`, `max`, `lowest`, `epsilon`, `round_error`, `infinity`, `quiet_NaN`,
`signaling_NaN` and `denorm_min` queries are evaluated by pinned Clang and
lowered to exact integer or IEEE bit-pattern literals.

```cpp
#include <limits>
using I = std::numeric_limits<int>;
using D = std::numeric_limits<double>;
static_assert(I::digits == 31 && I::min() + I::max() == -1);

int main() {
  double quiet = D::quiet_NaN();
  return I::max() == 2147483647 && D::infinity() > D::max() &&
                 quiet != quiet
             ? 0
             : 1;
}
```

The authenticated closure contains 16 libc++/resource files on every supported
target. Standard-library objects, data-member storage identity, method addresses,
object-qualified method calls, `long double` results, quoted `"limits"` and user
shadow headers remain rejected. The generated program contains only the folded
values; it does not call or link libc++ at runtime.

## Compile-time rational arithmetic from `<ratio>`

Core v2 accepts an exact top-level `#include <ratio>` from the pinned embedded
VFS. `std::ratio<N, D>` exposes its normalized signed numerator, positive
denominator and normalized `type`. The C++17 `ratio_add`, `ratio_subtract`,
`ratio_multiply` and `ratio_divide` aliases and all six comparison traits and
`*_v` variable templates are evaluated by pinned Clang. The standard SI aliases
from `atto` through `exa` are available when their values fit `intmax_t`.

```cpp
#include <ratio>
using A = std::ratio<6, -8>;
using B = std::ratio<5, 6>;
using Sum = std::ratio_add<A, B>;
static_assert(A::num == -3 && A::den == 4);
static_assert(Sum::num == 1 && Sum::den == 12);
static_assert(std::ratio_less_v<A, B>);
```

The authenticated 15-file libc++/resource closure is identical and
platform-free on all eight supported targets. All results are compile-time
types or integral constants and add no runtime libc++ dependency. Runtime ratio
objects, static-member addresses, quoted `"ratio"` and user shadow headers are
rejected.

## Fundamental types and bytes from `<cstddef>`

Core v2 accepts an exact top-level `#include <cstddef>` from the pinned embedded
VFS. `std::size_t`, `std::ptrdiff_t` and `std::nullptr_t` retain the selected
target's concrete types. The exact pinned `std::size_t` alias may also appear
in resolved function signatures; its authenticated SDK declaration supplies
the source proof for its target-specific type. Type-form `sizeof` and `alignof` queries on
`std::max_align_t` are folded by pinned Clang, including targets where its
carrier is otherwise outside core v2. `NULL` is accepted only when it expands
from the pinned header.

`offsetof` folds fields of an owned non-union standard-layout record, including
nested fields and in-bounds constant indices into fixed arrays. The record,
fields, array bounds and index expressions still receive ordinary source and
type checks. Raw `__builtin_offsetof`, unions, bases, dependent designators and
nonconstant or out-of-bounds indices are rejected.

```cpp
#include <cstddef>
struct Packet { char tag; unsigned words[3]; };
static_assert(alignof(std::max_align_t) >= alignof(void *));
static_assert(offsetof(Packet, words[2]) > offsetof(Packet, words));

int main() {
  std::byte value{0x32};
  value = ~((value | std::byte{0x0f}) ^ (value & std::byte{0x0f}));
  value >>= static_cast<unsigned char>(2);
  return std::to_integer<unsigned>(value) == 48u ? 0 : 1;
}
```

`std::byte` has ordinary scalar storage. Its `|`, `&`, `^`, `~`, `<<`, `>>`
and compound-assignment operators, plus `std::to_integer<T>`, lower directly to
checked integer operations. The selected function must be the exact pinned
libc++ declaration and must be called directly; function addresses and forged
lookalikes are rejected. The standalone authenticated closure contains 29
libc++/resource files on all eight supported targets. Quoted `"cstddef"`, the C
header `<stddef.h>`, raw GNU null expressions, platform headers and user shadow
headers remain outside this boundary. Generated programs do not call or link
libc++ for these operations.

## Scalar utilities and pairs from `<utility>`

Core v2 accepts an exact top-level `#include <utility>` from the pinned embedded
VFS. `std::move`, `std::forward`, `std::move_if_noexcept`, `std::as_const`,
`std::exchange` and scalar `std::swap` lower to the existing typed value,
reference and assignment operations. The selected function must be the exact
pinned libc++ declaration and must be called directly; function addresses,
forged declarations and array overloads are rejected.

`std::pair` supports default, value, converting and copy/move construction,
copy/move assignment, member and free `swap`, `std::make_pair`, index-based
`std::get`, `tuple_size` and `tuple_element`. Elements may be admitted integral
or enum scalars up to 64 bits, `float`, `double`, `nullptr_t`, non-function
object pointers, source-owned trivial standard-layout records, authenticated
`std::reference_wrapper` values, admitted `std::array` values, or recursively
admitted `std::pair` values. Mutation
requires every recursive leaf to be assignable. Pair objects retain their two
fields and ordinary value behavior. Type-based `get` is accepted only when
libc++ resolves it unambiguously.

Authenticated `std::reference_wrapper` values are admitted in ordinary
and mixed pair value fields, including nested pairs. Direct, same-type
copy/move and compatible heterogeneous construction copy the stored
binding. Assignment copies that binding into the destination wrapper,
and member/free swap exchanges bindings only when the selected element
operations are exact implicit instantiations of the pinned SDK swap. Nested
pair and array dispatch is authenticated recursively; selected move functions
must also come from the pinned SDK and copy/move construction and assignment
must be trivial. These operations do not assign the referred-to objects.
Every supported pair and array swap requires the selected-operation proof,
including source-record leaves, reference and mixed pairs, and nested arrays.
Array swap authenticates its data projections, range loop, iterator adapters,
result-pair construction and the element swap selected after ADL. Wrapper pairs
with array siblings are supported. Zero-length arrays instantiate no element
swap and still evaluate both caller operands once; element and source checks
remain in force. User ADL swaps and source specializations remain rejected.
Optional swaps containing wrapper-bearing pairs and tuples use the separate
engagement, selected value-swap and trivial transfer proofs described below.
The wrapper element must retain the exact
unqualified type for composite construction or assignment. Const wrapper
fields remain copyable but not assignable, and volatile wrapper fields
remain rejected. Wrapper referents retain the existing type and source
checks, and user-defined conversions or forged specializations do not
satisfy the authenticated wrapper boundary.

Reference-valued pairs admit exact compatible direct construction for supported
referent types and checked lvalue/rvalue categories. Same-type trivial
copy/move construction copies the stored bindings. Public `first`/`second`
access and index- or unique-type `get` recover the referents, so mutations write
through to the original objects. Same-type copy/move assignment evaluates the
source and destination once, assigns each source referent to the corresponding
destination referent in field order and returns the destination pair without
changing either pair's bindings. All six comparisons dereference pair fields
before applying the existing recursive equality or lexicographic rules,
including heterogeneous scalar leaves and mixed reference/value operands.
Member and free swap evaluate both pair objects once and exchange corresponding
referent values in field order without changing bindings. Compatible converting
construction from value or reference pairs preserves checked cv and value
categories. Heterogeneous assignment converts admitted scalar field values or
copies exact admitted composite values before writing through destination
bindings. Exact `make_pair` calls unwrap authenticated `ref`/`cref` arguments,
copy their stored pointers into the resulting lvalue-reference fields and
preserve const qualification. A result with exactly one such argument retains
its other admitted value field and supports ordinary public `first`/`second`
access. These mixed reference/value pairs also admit exact compatible direct
construction, same-type trivial copy/move construction and index- or
unique-type `get`; reference fields retain their bindings while value fields
retain ordinary pair value semantics. Same-type and compatible heterogeneous
assignment preserve that split, writing through reference fields and assigning
value fields in field order. All six comparisons read reference and value fields
through the existing recursive heterogeneous comparison rules. Member and free
swap exchange the corresponding referent or stored value in field order without
changing reference bindings. Compatible converting construction from value,
reference or mixed pairs binds destination reference fields with checked cv and
value categories and initializes destination value fields with admitted copies
or scalar conversions.

Ordinary value pairs likewise accept heterogeneous converting construction and
assignment from authenticated value, reference or mixed pairs. Scalar fields
use the existing arithmetic and object-pointer conversions; composite fields
require the same unqualified type. Reference sources are read as referents and
copied into independent destination storage. Construction evaluates the source
once. Assignment evaluates the source once before the destination, writes
`first` before `second`, and returns a reference to the destination pair.
References into destination fields observe preceding field writes. User-defined
element conversions and heterogeneous nested composite conversions remain
outside this boundary.

All six C++17 comparisons recurse through authenticated arrays and nested pairs
and compare their scalar leaves in lexicographic order. Corresponding scalar
leaves may use the documented heterogeneous arithmetic or compatible
object-pointer comparison type. Empty recursive leaves retain the standard
equality and ordering results. Source-owned record comparisons and
comparisons involving wrapper-valued leaves stay rejected because their
element operations or conversions are outside this direct-lowering
boundary. The exact `std::minmax` reference-pair result remains separately
constrained as documented under the algorithm surface.

`std::integer_sequence`, `index_sequence`, their generator aliases and
`integer_sequence::size()` remain compile-time types and values. The standalone
authenticated closure contains 87 libc++/resource files on all eight supported
targets. Combining `<array>` and `<utility>` for composite pairs has a 222-file
platform-free union closure on those targets. Generated programs do not call or
link libc++ for these operations; quoted `"utility"`, user shadow headers and
platform header roots remain rejected.

## Value tuples from `<tuple>`

Core v2 accepts the exact top-level angled `<tuple>` entry from the pinned
embedded VFS. Its authenticated 98-file libc++/resource closure is identical on
all eight supported targets and contains no platform headers. A retained
`std::tuple<T...>` may be empty or contain up to 64 admitted values. Elements
may be admitted scalars, source-owned nonempty trivial standard-layout records,
authenticated `std::reference_wrapper` values, admitted `std::array` values,
recursively admitted `std::pair` values, or nonempty nested tuples. For
nonempty tuples, the frontend authenticates libc++'s private `__base_` field,
indexed leaf base classes and template arguments, private leaf values, native
size and alignment, and every ABI field offset before exposing a flat protocol
record. A nested tuple makes its
containing leaf non-standard-layout, so that case is admitted only after the
same concrete field and layout checks. For the dedicated empty specialization,
the frontend authenticates the explicit specialization kind, zero bases and
fields, trivial lifecycle, standard layout, and one-byte size and alignment
before exposing a record with no fields.

Default, element-wise, trivial copy/move and compatible per-element converting
construction lower directly. Scalar elements retain the documented arithmetic
and object-pointer conversions. Composite elements require the same
unqualified source and destination type. Assignment supports same-type tuples
and same-length converting tuples through those rules; assignment and swap
require every recursive destination leaf to be assignable. `std::make_tuple`,
member and free `swap`, `tuple_size`, `tuple_element`, and index-based or
unique-type `std::get` use the same authenticated records. `get` preserves
const and lvalue/rvalue reference categories; type selection requires exactly
one matching element, as in C++17.

Member and free tuple swap authenticate the concrete SDK delegation through
`__tuple_impl`, each indexed `__tuple_leaf`, its exact value projection and the
selected element swap. Source ADL functions, source redeclarations and explicit
specializations of these operations are rejected. This includes wrapper-valued
elements and nested ordinary pairs containing wrappers: their bindings exchange
without assigning their referents. Reference elements exchange referent values
in element order without rebinding, including aliasing and self-swap; both outer
operands are evaluated once. Nested reference-valued containers remain outside
the ordinary value carrier boundary. The empty tuple performs no element work.

Exact `std::make_tuple` also unwraps authenticated `ref`/`cref` arguments. A
result containing both reference and admitted value elements retains reference
bindings and ordinary value storage in the same authenticated tuple layout.
Exact compatible direct construction and same-type trivial copy/move
construction preserve that split, and index- or unique-type `get` recover either
kind of element. Same-type and compatible heterogeneous assignment from tuples
or pairs writes through reference fields and assigns value fields in element
order. All six comparisons read reference and value elements through the
existing recursive heterogeneous comparison rules. Member and free swap
exchange each referent or stored value in
element order without changing reference bindings.
Compatible converting construction from authenticated tuples or pairs binds
each reference element using the checked cv and value category rules, while
independently copying or converting each value element. Exact `std::apply`
recovers reference elements as referents and passes value elements through the
same checked callable parameter and result rules documented below. Conversely,
ordinary value tuples may use converting construction and heterogeneous
assignment from authenticated all-reference or mixed tuple/pair sources,
copying each referent value into independent destination storage. Assignment
evaluates the source once before the destination, writes elements in order,
and returns a reference to the destination tuple. When source references alias
destination elements, later reads observe earlier element writes.

Exact `std::tie` calls construct an authenticated tuple whose elements are
lvalue references to supported scalar, native fixed-array, authenticated
`std::array` or source-owned record objects.
Each argument is evaluated once, and each tuple field stores the bound address.
Index- and type-based `std::get` recover the referent, including through a
const tuple object, and `std::apply` passes those referents to admitted named
functions or stored function pointers. Mutations therefore write through to
the original objects without a libc++ runtime helper.

Exact `std::forward_as_tuple` calls use the same authenticated reference-tuple
layout. Lvalue arguments bind `T&` fields and xvalue or materialized temporary
arguments bind `T&&` fields after an exact value-category check. `get` and
`apply` preserve reference collapsing from the tuple's own lvalue/rvalue
category. A temporary referent remains alive through its enclosing full
expression, including a direct `apply`, and is destroyed afterward; storing a
tuple that refers to an expired temporary retains ordinary C++ dangling-reference
semantics.

Authenticated reference tuples also support direct element construction when
each selected binding has the same unqualified referent type, compatible cv
qualification and an admissible lvalue/rvalue category. Same-type trivial
copy/move construction copies the stored bindings, so both tuple objects keep
referring to the original objects. Same-type copy/move assignment evaluates
the source and destination once, then assigns each source referent into the
corresponding destination referent without changing either tuple's bindings.
The assignment result aliases the destination tuple. Converting construction
from another authenticated tuple admits exact unqualified referent types with
compatible cv and value categories, including a `const T&` view over a value
tuple field. Heterogeneous assignment admits the documented scalar conversion
boundary and writes converted source element values through destination
references. All six tuple comparisons dereference reference fields before
applying the existing recursive equality or lexicographic comparison, including
heterogeneous scalar leaves and mixed reference/value tuple operands. Pair
conversion follows the same split: construction binds compatible value-pair
fields with checked cv and value categories, while assignment converts field
values and writes through the existing destination bindings. Member and free
swap evaluate both tuple objects once and exchange corresponding referent
values in element order; their stored bindings remain unchanged.

Exact `std::apply` calls accept an authenticated empty or nonempty tuple,
value/reference/mixed pair, or fixed array whose elements satisfy the existing
scalar or recursively composite value boundary. They lower to the selected
scalar operation, member projection or ordinary callback call. The callable
may be a named function, a stored function pointer with a fixed nonvariadic
signature, or an exact source-owned function object whose selected `operator()`
has a checked body. Authenticated typed or transparent standard scalar
arithmetic, bitwise, comparison and logical function objects, plus admitted
scalar hashes, retain their documented operand restrictions. Typed function
objects convert each tuple element to the selected `operator()` parameter type
before the scalar operation, including narrowing integral conversions. Exact
`std::reference_wrapper` forms around these named functions, stored function
pointers, source function objects and standard function objects use the same
admitted parameter and result boundary. Typed standard objects in a wrapper
also convert arguments to the selected `operator()` parameter types for direct
calls, `std::invoke` and `std::apply`. Exact direct or stored source member
pointers, and temporary or stored `std::mem_fn` wrappers around them, take their
receiver from the first source element, including an authenticated
`std::reference_wrapper` where the source element type is admitted, and preserve
method or field reference and record results. The callable's source-owned
checks, selected cv/ref-qualified method and mutable receiver storage remain
unchanged.

By-value argument reads discard only the source object's top-level `const`,
including qualification hidden by a type alias. Const tuple-like elements can
therefore feed value parameters through reference-wrapped callbacks while
preserving pointee qualification and all existing reference-binding checks.
Volatile, restrict and address-space restrictions remain in force.

Each source element and corresponding callback parameter must be admitted
scalars connected by a checked direct scalar conversion, or the same complete
source-owned standard-layout record type that is trivially copyable and
destructible, passed by value; the result may be `void`, an admitted scalar,
or a complete source-owned record value. Exact lvalue- or rvalue-reference
parameters and results are also admitted for supported scalar, object-pointer,
function-pointer, complete fixed-array, authenticated `std::array` and
source-owned record referents. A `const T&` callback parameter also binds an
xvalue with the same unqualified type, including an element of a materialized
tuple, pair or array. Mutable lvalue-reference parameters still require lvalues;
rvalue-reference parameters still require xvalues. These bindings retain the
original element storage and do not extend its lifetime. Volatile referents,
scalar conversions, base adjustments and changes to nested pointer qualifiers
remain excluded for these exact reference bindings.
The by-value scalar boundary includes arithmetic conversions,
`nullptr_t` to object pointers or `bool`, and compatible object-pointer,
pointer-to-void and pointer-to-`bool` conversions. Array elements must satisfy
both the admitted array layout and the composite value/callback rules; admitting
an array carrier alone does not admit every callback signature. Nested array
elements may bind checked array-reference parameters. By-value SDK callback
parameters and results, including nested `std::array` values, remain excluded.

Mutable and const lvalues, xvalues and materialized tuple, pair or array
temporaries are accepted. Reference pair fields preserve their stored bindings:
a const pair does not add const to a mutable referent, and `T&&` fields retain
reference collapsing from the pair's own value category. Value fields and array
elements retain the source object's constness and lvalue/rvalue category. The
callable and source expressions are each evaluated once, elements are converted
and passed in index order, and no libc++ apply helper is emitted. A zero-length
array still evaluates its source expression and calls a nullary callback without
accessing its synthetic carrier. A record parameter receives its own copied
object, while a record result constructs directly in the caller's destination
and keeps its ordinary full-expression cleanup. References preserve the
selected element or callback result storage, qualification and value category.

The proof authenticates the pinned `apply`/invoke helper chain and every selected
`std::get<I>` declaration, template index and element type, forwarding path,
and exact cv/ref result before replacing them with direct projections.
User-defined tuple-like protocols, user SDK specializations and substituted
`get` implementations do not qualify. Existing callable arity, source traversal
and lowering expansion limits still apply.

Exact `std::tuple_cat` calls lower directly when every source is an
authenticated `std::tuple`, `std::pair` or `std::array` containing admitted
scalar or recursively composite elements and the selected result is the exact
concatenated tuple type. Composite elements include source-owned nonempty
trivial standard-layout records, admitted arrays and pairs, and nonempty nested
tuples. Tuple and pair sources may instead contain all-reference or mixed
reference/value elements; the result retains those declared reference types
and copies their bindings while independently copying value elements. Zero
arguments, empty tuple sources and zero-length array sources are included.
Mutable or const lvalues and materialized temporaries are accepted. Each source
expression is evaluated once, all source addresses are captured before any
element is read, and fields are copied in concatenation order without a libc++
tuple-cat helper.

Two-element tuples also accept admitted scalar or composite `std::pair<U, V>`
lvalues and rvalues for construction and assignment under the same per-element
conversion rules. Scalar pair conversion requires `<tuple>` and `<utility>` and
has an authenticated, platform-free 108-file union closure. The composite
array-plus-tuple-plus-utility fixture has a 231-file union closure on all eight
targets.

All six C++17 comparisons accept same-length heterogeneous tuples when each
corresponding value recursively reaches an approved arithmetic or compatible
object-pointer scalar comparison. Authenticated arrays, pairs and nested tuples
retain their field or row-major order; equality also admits `nullptr_t` against
an object pointer. Lexicographic ordering short-circuits at the first unequal
leaf. Generated programs contain no tuple helper calls and do not link libc++.

The standalone empty tuple remains supported, but empty-base-optimized record
or tuple elements do not have the authenticated one-field leaf representation
and remain rejected. Nontrivial records, `long double`, function pointers and
source-record comparisons also remain outside this surface. Apply calls with
unsupported callable objects, nontrivial record parameters, volatile references,
or variadic callbacks
remain rejected. Quoted includes, user shadows,
standard-function addresses and forged declarations remain rejected.

## Fixed value arrays from `<array>`

Core v2 accepts an exact top-level `#include <array>` from the pinned embedded
VFS. Nonempty `std::array<T, N>` objects use libc++'s checked single fixed-array
field and the existing aggregate carrier. The `N == 0` partial specialization
is authenticated separately: its private `std::__empty` byte array, typed
alignment attribute, size and ABI alignment must exactly match one `T` object.
The protocol exposes that inaccessible storage as one synthetic `T` carrier so
the generated record preserves the native size and alignment without exposing
or operating on libc++ internals. `T` may be an admitted integral or enum
scalar up to 64 bits, `float`, `double`, `nullptr_t`, or a non-function object
pointer, a source-owned trivial standard-layout record, or another admitted
`std::array`; `N` is limited to 65536. Nested arrays and record arrays retain
their recursive field layout and ordinary aggregate access.

`size`, `max_size`, `empty`, `data`, `begin`, `end`, `cbegin`, `cend`, indexed
access, `front`, `back`, compile-time in-range `at`, forward and reverse range
access, `fill`, member and free `swap`, all six C++17 comparisons and
index-based `get` lower directly to existing array, pointer, assignment and
control-flow operations. Aggregate initialization, trivial copy/move
construction and copy/move assignment retain ordinary value semantics.
`tuple_size` and `tuple_element` remain checked compile-time metadata.

An array can also supply the element pack to `std::apply` through the
[checked tuple, pair and array callable boundary](#value-tuples-from-tuple).
Mutable and const lvalues, rvalues, zero-length arrays and nested arrays retain
the documented element and callback restrictions; nested SDK array values may
be passed by admitted reference forms, while by-value SDK callback parameters
and results remain excluded.

For `std::array<T, 0>`, capacity is zero, `empty()` is true, and libc++'s
`data()` plus every forward or reverse iterator base is null. `fill` and swap
still evaluate their receiver and arguments but do not read or write the
synthetic carrier. The six comparisons evaluate both operands and return the
empty-range results: equality, `<=` and `>=` are true; inequality, `<` and `>`
are false. This rule composes through nonempty arrays whose nested leaves all
have zero extent. Element access remains rejected because no valid index,
`front`, `back`, `at` or `get` operation exists.

Authenticated array objects also qualify as referents in the checked utility
and functional operations. Direct reference or mixed-reference tuple and pair
construction, `tie`, `forward_as_tuple`, and `make_tuple`/`make_pair` unwrapping of
`ref`/`cref` preserve exact compatible bindings. Index/type `get`, public pair
fields and `tuple_cat` recover or copy those bindings. Assignment and swap write
array values through the bound objects without reseating their references;
copying an array referent into a value tuple or pair retains independent value
storage. A const tuple does not add const to an existing mutable reference,
while a const array referent retains its qualification.

Exact lvalue and rvalue array-reference parameters and results also compose
with the existing checked `apply`, `invoke`, direct `reference_wrapper` calls,
native member-pointer calls and `mem_fn` wrappers. The admitted callable forms
retain their source-owned definition and selected-method checks. Callable,
receiver and argument expressions retain one-time evaluation and the existing
sequencing rules, reference results preserve object identity, and temporary
array referents retain their full-expression lifetime. These reference forms
include authenticated nested arrays and zero-length arrays; zero-length storage
remains inaccessible. By-value SDK callback parameters and results, including
`std::array` values, remain outside the callable boundary. Volatile array objects
or elements, nontrivial element records, and user-defined `std::array`
specializations are rejected even when reached through a reference.

Type queries that need array layout use the authenticated specialization's
shape while retaining the original element and source-expression dependencies.
This allows layout-consuming classifications and reference-binding queries
over already instantiated admitted arrays, and `decltype` queries over
source-owned functions or methods returning array references. Mutable, const
and rvalue references, nested arrays and zero extents retain their identities;
query operands remain unevaluated. Source-record element fields, written
extents, aliases, call arguments and defaults still require their original
source checks, including for zero-length arrays.

Exact admitted `std::get` calls also supply their pinned SDK signature and
body source for queries. Mutable, const and rvalue results preserve reference
identity, including nested arrays and source-record elements; written indices,
types and argument expressions retain their own checks. Constant `get` results
can supply array dimensions. This proof belongs to the selected call and its
callee reference; it does not authorize an independent SDK function address,
other SDK callees or out-of-range element access.

Aggregate-initialized local arrays and array temporaries preserve initializer
and destruction dependencies. Their authenticated implicit trivial destructor
recursively consumes the original element destruction source, even for zero
extents, while ordinary temporary lifetimes remain unchanged. Trivial source
element destructors may have a checked `noexcept(false)` specification; the
array's inferred specification retains that source dependency. SDK construction
and nothrow-destruction query roots still require their separate operation
proofs, and elements such as `std::byte` still need SDK enum-source evidence.

The standalone authenticated closure contains 217 libc++/resource files on all
eight supported targets and contains no platform headers. Generated programs do
not call or link libc++ for these operations. Comparisons recursively preserve
row-major lexicographic order when every leaf element is scalar. Nontrivial
record elements, record comparisons, dynamic or out-of-range `at`, function
addresses, quoted `"array"`, user shadows and forged declarations remain
outside this boundary.

## Initializer-list views from `<initializer_list>`

Core v2 accepts the exact angled `<initializer_list>` entry from the pinned
embedded VFS. Its authenticated 10-file libc++/resource closure is identical on
all eight supported targets and contains no platform headers. Each admitted
`std::initializer_list<T>` specialization must retain libc++'s standard-layout
two-field representation: a private `const T *` followed by the target
`size_t`, with the checked pointer-size layout for that target.

Braced lists materialize their constant backing arrays through the existing
automatic, full-expression and static-storage lifetime machinery. Element
initializers run in source order, nontrivial source-record elements receive
normal destruction, and nested initializer lists retain their respective
backing arrays. A nonempty backing array is limited to 65536 elements. Complete
admitted non-volatile object element types include scalars, enums, object
pointers, source records and nested initializer-list views.

Default construction produces an empty null-and-zero view. Trivial copy/move
construction and assignment preserve the backing-array view. Member `size`,
`begin` and `end`, plus the free `begin` and `end` overloads from
`<initializer_list>`, lower directly. When `<iterator>` is also present, its
authenticated `begin`, `end`, `cbegin`, `cend`, `size`, `empty`, `data`,
`rbegin`, `rend`, `crbegin` and `crend` overloads operate on the same view.
Generated programs do not call or link libc++ for these operations.

Reference, incomplete, volatile, restricted-address-space and function element
types remain outside this boundary. Function addresses, quoted includes, user
shadows and forged declarations remain rejected.

## Value optionals from `<optional>`

Member and free swap prove the exact SDK engagement tests and value projections.
When both objects are engaged, the selected element swap must satisfy the same
recursive SDK operation proof as pairs, arrays and tuples. When only one is
engaged, the proof follows `move`, `__construct`, `__construct_at`, `addressof`,
`forward`, reserved placement construction and reset, requiring the concrete
selected copy/move constructor and destruction to be trivial. Empty/empty swap
performs no element operation. Source ADL swaps, redeclarations and operation
specializations are rejected. Nested wrapper pairs and tuples retain their
bindings through these transfers; both operands are evaluated once and self-swap
preserves engagement and values.

Core v2 accepts the exact angled `<optional>` entry from the pinned embedded
VFS. Its authenticated 136-file libc++/resource closure is identical on all
eight supported targets and contains no platform headers. Each admitted
`std::optional<T>` specialization must match libc++'s exact private base chain,
anonymous value union, engagement flag and target layout. The translator exposes
that representation as a synthetic `{remove_cv_t<T>, bool}` record while
preserving the native size, alignment and field offsets. The composite fixture
that also includes `<array>`, `<tuple>` and `<utility>` has an identical
253-file platform-free union closure on all eight targets.

The current element boundary includes at-most-64-bit integral and enumeration
types, `float`, `double`, object pointers and `nullptr_t`, with optional top-level
`const`. It also includes source-owned trivial standard-layout records and the
admitted array, pair and tuple value domains, recursively. Default and `nullopt`
construction produce an empty value. Direct, zero- or single-value `in_place`,
copy and move construction, converting construction from another optional,
copy and move assignment, converting optional assignment, assignment from
`nullopt`, direct value assignment, `has_value`, contextual `operator bool`,
dereference, implicit or explicit arrow, `reset`, single-value `emplace`,
`value_or`, member and free `swap`, and zero- or single-value `make_optional`
lower directly to aggregate and scalar IR. Composite construction and
conversion require the exact unqualified element type. Composite assignment,
emplacement and swapping additionally require recursive assignability; a
top-level `const` element remains constructible and readable. Scalar operations
retain the directly representable conversion boundary, including the standard
conversion from `nullptr_t` to an object pointer.

All six C++17 comparison forms recurse through authenticated array, pair and
tuple values and preserve their lexicographic leaf order. Composite operands
must have corresponding authenticated shapes; each corresponding scalar leaf
uses the existing comparison boundary. That boundary covers exact scalar
types, heterogeneous arithmetic operands and qualification-compatible object
pointers. Arithmetic leaves receive the C++17 integer promotions and usual
arithmetic conversions before comparison. Pointer leaves receive a common
pointer type by combining pointee `const`; equality also admits an object
pointer with a compatibly qualified `void *`. Ordered pointer comparisons
require a complete object pointee. Exact `nullptr_t` and enumeration leaves
retain their existing scalar path; object pointers additionally compare for
equality with `nullptr_t`. Comparisons with `nullopt` on either side inspect
only engagement and therefore work for every admitted element. Generated
programs do not call or link libc++.

When both compared values are scalar, the selected scalar operator is applied
directly after conversion. Ordered comparisons involving a floating-point NaN
are false, and inequality is true; this also applies to optional-versus-value
comparisons in either order. Pair, tuple and array values retain their C++17
lexicographical definitions, including `<=` and `>=` expressed through `<`.
Engagement and `nullopt` comparisons are unchanged.

Reference, incomplete, volatile, restricted-address-space, nontrivially
destructible, `long double` and function-pointer elements remain outside this
boundary. Source-record value comparisons, throwing `value`, base-adjusting or
otherwise incompatible pointer comparisons, heterogeneous enumeration
comparisons, initializer-list emplacement and the `in_place_type`/
`in_place_index` tags are not admitted. The authenticated `in_place` tag is
erased only as an optional constructor argument. Function addresses, quoted
includes, user shadows and forged declarations remain rejected.

## Iterator metadata from `<iterator>`

Core v2 admits the exact angled `<iterator>` entry and pointer
`std::iterator_traits` metadata. Its authenticated 171-file libc++/resource
closure is identical across all eight targets and contains no platform headers.
The upstream `iterator` header and directly included component headers retain
their original bytes.

Native arrays and admitted `std::array` objects directly lower `begin`, `end`,
`cbegin`, `cend`, `size`, `empty`, `data`, `rbegin`, `rend`, `crbegin` and
`crend`. Raw object pointers and their authenticated `reverse_iterator`
specializations directly lower `advance`, `distance`, `next`, `prev`,
`make_reverse_iterator`, base access, dereference, arrow, increment, decrement,
offset, subscript, difference and comparisons. Generated programs use existing
pointer and control-flow operations and do not link libc++.

The four stream-iterator component headers remain authenticated in the VFS but
their declarations are disabled until the I/O header closure and runtime
operations are admitted. Custom iterator classes, function-pointer iterators,
quoted includes and forged declarations remain rejected.

## Numeric header from `<numeric>`

Core v2 admits the exact angled `<numeric>` entry from the pinned embedded VFS.
Its 126-file libc++/resource dependency closure is identical on all eight
supported targets and contains no platform headers. This revision retains the
upstream public header and all C++17 numeric components byte for byte.

The default `iota`, `accumulate`, `inner_product`, `partial_sum`,
`adjacent_difference`, `reduce`, two-range `transform_reduce`, `inclusive_scan`
and `exclusive_scan` overloads lower to direct pointer loops. Input and output
ranges use admitted non-promoted built-in integers through 64 bits, `float` or
`double`. `partial_sum`, `adjacent_difference`, `inclusive_scan` and
`exclusive_scan` accept a writable scalar output whose element accepts a
checked direct conversion from the input element. Default `accumulate` and
`reduce` additionally admit non-boolean narrow integer input elements and
initial values. Default `partial_sum` and `adjacent_difference` also admit
non-boolean narrow integer inputs; adjacent differences retain integer
promotion before converting to the output element. Default `inclusive_scan`
and `exclusive_scan` likewise admit non-boolean narrow integer inputs, and
`exclusive_scan` accepts a narrow initial accumulator. Default `accumulate`,
`inner_product`, initialized `reduce`, two-range `transform_reduce` and
`exclusive_scan` accept an independent arithmetic initial value, use checked
common arithmetic for their default operations and convert each result back to
the initial value type. Initialized scans additionally accept a writable output
element to which that accumulator converts directly.
`iota` likewise accepts an independent initial arithmetic type, including
non-boolean narrow integers, when its values convert directly to the output
element (which may also be narrow), and increments in that initial type.
Other generated values retain the input type.
Two-argument `reduce` starts from the element type's zero value. The sequential
scan operations preserve empty-range and in-place behavior. Every argument is
captured once before the loop. Default `inner_product` and two-range
`transform_reduce` may use a different numeric second-range element; their
product and sum follow checked arithmetic common types before conversion back
to the accumulator type.
Default `inner_product` and two-range `transform_reduce` additionally admit
non-boolean narrow input elements in either range and a narrow initial
accumulator.
Their six-argument overloads also accept directly empty list-initialized
arithmetic function-object pairs: either operation may be `std::plus`,
`std::minus`, `std::multiplies` or `std::divides`; `std::modulus`, `std::bit_and`,
`std::bit_or` and `std::bit_xor` are admitted when the accumulator and both
input ranges have integer elements. Transparent forms use checked common
arithmetic; typed `T` forms convert admitted arithmetic range elements and
the accumulator to `T` at each invocation, including non-boolean narrow
integers. Each typed transform result converts to its own `T` before reduction,
and each typed reduction result converts to its own `T` before the accumulator.
`std::logical_and` and `std::logical_or` are also admitted as reduction
objects, with transparent or typed arithmetic/`bool` operands and a `bool`
result converted back to the accumulator after each step.
Either may also transform the two input elements into a `bool` term, including
with a distinct typed operand conversion, before the selected reduction.
The two-range forms additionally admit `std::equal_to`, `std::not_equal_to`,
`std::less`, `std::less_equal`, `std::greater` and `std::greater_equal` as
transforms. Transparent forms compare the checked common arithmetic type;
typed forms convert both input elements to their selected arithmetic or `bool`
type before comparison. Each comparison yields a `bool` term.
These six comparisons are also admitted as reduction objects in the two-range
forms and unary `transform_reduce`. Their operands use the same transparent or
typed conversion rules, and each `bool` result converts back to the accumulator
type before the next step.
Stored objects and other
object combinations remain outside this direct lowering.
The five-argument unary `transform_reduce` likewise accepts a directly empty
list-initialized `std::negate` transformation, integral `std::bit_not` or
`std::logical_not`, with
`std::plus`, `std::minus` or
`std::multiplies`, `std::divides` or integral `std::modulus`, `std::bit_and`,
`std::bit_or` or `std::bit_xor` reduction, or a logical `std::logical_and` or
`std::logical_or` reduction. Typed `T` forms convert an admitted
arithmetic input and accumulator to the selected function object's type at
each invocation. Negation and bitwise complement use integer promotion after
the typed input conversion; logical negation yields `bool`, including when its
typed operand is a different arithmetic type. A typed arithmetic transform
result converts to its own `T` before reduction.

The operation-taking overloads of `accumulate`, `inner_product`, `partial_sum`,
`adjacent_difference`, `reduce`, unary and two-range `transform_reduce`,
`inclusive_scan` with or without an initial value, and `exclusive_scan` also
lower directly. The no-init and initialized `transform_inclusive_scan` forms
and initialized `transform_exclusive_scan` are admitted on the same ranges.
These operation forms accept an ordinary function pointer whose return and
by-value parameters are admitted scalars. Uninitialized scans combine in the input
element type. Initialized scans may use an independent arithmetic accumulator:
the binary callback accepts that accumulator and an input or unary-transformed
element, its return converts directly back to the accumulator, and the
accumulator converts directly to the writable output element. Other callbacks
return an input-range element, and each parameter accepts the checked direct
scalar conversion from its corresponding element. Callback values are captured
once, empty ranges make no callback calls, and the sequential scan forms retain
their in-place behavior.
The four-argument `accumulate` and `reduce` function-pointer callbacks also
admit independent arithmetic input and accumulator types, including
non-boolean narrow integers, through a checked common arithmetic type; their
directly convertible scalar results are converted back after each call.
The four-argument `partial_sum` and `adjacent_difference` function-pointer
callbacks admit non-boolean narrow integer inputs and writable scalar outputs;
the adjacent result converts directly to the output element after its callback.
Function-pointer `inclusive_scan` and `exclusive_scan` also admit non-boolean
narrow input elements; initialized forms admit narrow accumulators and convert
each callback result back to that accumulator before storing the output.
The callback forms of `inner_product` and two-range `transform_reduce` accept a
different numeric second-range element when the transform callback accepts the
respective element types and returns a value directly convertible to the
accumulator type.
The six-argument `inner_product` and two-range `transform_reduce` callback
forms admit an independent arithmetic accumulator and different numeric
second-range element, including non-boolean narrow inputs. Their transform
and reduction callbacks return values directly convertible to the accumulator,
and both callback values are retained across the loop.
The five-argument unary `transform_reduce` callback form likewise admits an
independent arithmetic accumulator with non-boolean narrow input, converting
the unary result and each reduction result to that accumulator.
The no-init and initialized `transform_inclusive_scan` and initialized
`transform_exclusive_scan` callback forms also admit non-boolean narrow inputs;
initialized forms accept a narrow accumulator and convert each binary result
back to it before writing the output.
The four-argument `accumulate` and `reduce` overloads additionally accept a source-owned,
standard-layout, trivially copied operation object when its non-template binary
call operator takes directly convertible by-value arithmetic scalars and returns
one directly convertible to the accumulator. The initial value is an admitted
arithmetic type, including non-boolean narrow integers; the input may also be a
non-boolean narrow integer. The pair has a checked common arithmetic type. The pinned C++17 loop
and selected source method are authenticated, including `reduce`'s `std::move`
of the accumulator; one by-value object retains its mutable state
throughout the reduction without changing the caller's object. An empty range
returns the initial value without invoking the operation.
Pinned typed or transparent `<functional>` binary objects, including arithmetic
operations, use the same loop after authenticating their selected SDK method and
matching both operands to the accumulator and input element types. Transparent
SDK objects may use different accumulator and input types; typed SDK objects
also accept a direct scalar conversion of the input element to their template
type, including the checked temporary bound to their const-reference parameter.
The four-argument `partial_sum` and `adjacent_difference` overloads also
accept these SDK binary objects and source-owned trivial binary objects for a
non-promoted arithmetic or non-boolean narrow integer input element and a
directly convertible writable output element. `partial_sum` converts each
operation result back to its input element type before storing it;
`adjacent_difference` converts the operation result directly to the output
element type. Their pinned first-element and subsequent-loop structures are
authenticated, including the empty-range return, in-place output behavior,
and mutable state of the copied source operation. `adjacent_difference`
checks the source's move of each current element into the previous value.
The five-argument initialized `inclusive_scan` overload accepts the same
binary objects when the initial value has the input element type. Source-owned
and transparent SDK objects also admit a different non-promoted arithmetic
initial type with a checked common type. Typed SDK objects admit that pair when
the input converts directly to the object's parameter type, including a
checked scalar temporary bound to its const-reference parameter. The initial
value converts directly to the writable output element. Its pinned sequential
loop retains
the copied object's state and does not invoke it for an empty range.
The four-argument no-init overload authenticates the pinned first-element
branch and its delegation to that initialized overload. The first element is
written without invoking the operation; subsequent elements retain one copied
object's mutable state.
The five-argument `exclusive_scan` overload also accepts these binary objects
on the same input/initial type. Source-owned and transparent SDK objects also
admit a different non-promoted arithmetic initial type with a checked common
type and a direct conversion to the writable output. Typed SDK objects also
admit that pair when the input converts directly to their parameter type;
both checked scalar temporaries bind to the selected const-reference method.
Its pinned loop computes the next
accumulator before writing the old one, and both SDK `move` sites are checked
so empty and in-place ranges preserve their sequential behavior.
The initialized object-taking forms of both scans also admit non-boolean
narrow integer inputs with an admitted non-promoted arithmetic initial value.
The source method or selected typed/transparent SDK operation receives the
input through the same checked scalar conversions.
They may also retain a non-boolean narrow integer initial value, converting
each operation result back to that accumulator type before the output write.
The four-argument no-init `inclusive_scan` authenticates the same narrow
input's first-element seed and delegates the remaining range to the initialized
loop. Its first value is written without invoking the operation.

`gcd` and `lcm` accept any non-boolean built-in integer argument combination
through 64 bits and return libc++'s exact `common_type_t` result. Signed inputs
are converted to unsigned magnitudes before the Euclidean loop, so supported
negative, narrow and mixed-signedness calls retain the standard result and
representability preconditions. Each argument is evaluated once.

Other callable objects, reference callback parameters or results, record callback
results, heterogeneous input ranges in callback-taking forms, heterogeneous
initial values outside the documented operations, promotable range integers,
enums, records, custom iterators and the other range-based
numeric algorithms remain outside the runtime boundary.
Integer arguments wider than 64 bits, quoted includes, shadows, function
addresses for the numeric algorithms themselves and forged declarations remain
rejected.

## New header from `<new>`

Core v2 admits the exact angled `<new>` entry from the pinned embedded VFS. Its
37-file libc++/resource dependency closure is identical on all eight supported
targets and contains no platform headers. The exact upstream C++17 declarations
are authenticated before translation.

`std::nothrow_t` type identity, the scalar `std::align_val_t` enum, and the
positive `hardware_destructive_interference_size` and
`hardware_constructive_interference_size` constants remain compile-time or
scalar metadata. An `align_val_t` value retains its target `size_t` width and
explicit enum conversions. These uses create no standard-library storage and
emit no call.

The exact `std::launder(T *)` template lowers directly for admitted
non-volatile object pointers, including scalar, fixed-array and source-record
pointees. The result has the exact input pointer type and value. Its argument is
captured once, and the portable pointer model then observes the lifetime chosen
by the authenticated C++ call without a libc++ runtime operation.

The exact global `operator new(size_t, void *)` and `operator new[](size_t,
void *)` definitions from the pinned placement component are admitted only when
Clang selects them for a new expression. Single objects and constant-bound
arrays capture the placement argument once and initialize directly in that
storage without emitting an allocation call. Array expressions retain the
checked target cookie layout and advance past any required cookie before
initializing elements, so valid source must provide sufficient aligned storage.

Volatile, void and function pointees, function addresses, quoted includes,
shadows and forged declarations remain rejected. Runtime `std::nothrow` tag
objects, direct calls or addresses of the placement functions, source
redeclarations, default heap allocation, runtime placement-array bounds,
allocation handlers and exception objects remain outside this boundary.

## Memory header from `<memory>`

Core v2 admits the exact angled `<memory>` entry from the pinned embedded VFS.
Its 267-file libc++/resource dependency closure is identical on all eight
supported targets and contains no platform headers. The public C++17 header and
all consumed component headers retain their upstream bytes and are authenticated
before translation. Raw-pointer `std::pointer_traits<T *>` exposes its exact
`pointer`, `element_type`, `difference_type` and `rebind<U>` aliases.

Exact single-object `std::default_delete<T>` and unbounded-array
`std::default_delete<T[]>` specializations are admitted for a non-volatile base
object type. In the array form, `T` may contain complete bounded inner extents,
as in `std::default_delete<int[][3]>`. Each pinned libc++ specialization must be an empty,
standard-layout record with no fields or bases, trivial default construction
and destruction, and an exact one-byte size and alignment. The response
preserves that representation as one synthetic
`nct_default_delete_storage: u8` field at offset zero. Default construction and
implicit trivial copy/move construction lower directly. The pinned converting
constructor is admitted when both specializations have the same scalar or array
form and its pointer conversion keeps the same unqualified element type while
only adding `const`; every source expression is still evaluated.

The exact `const noexcept` call operator must contain the authenticated matching
`delete` or `delete[]` body from `__memory/unique_ptr.h`. Both
`deleter(pointer)` and explicit `deleter.operator()(pointer)` evaluate the
receiver before the pointer, capture the pointer once and preserve the normal
null check. Single-object calls destroy a nontrivial complete source object and
call the exact checked source-defined class delete selected by Clang, or the
checked global sized/unsized delete when lookup remains global. Array calls
read the checked native array cookie, destroy nontrivial elements in reverse
order and call the exact checked source-defined class `delete[]` selected by
Clang, or the checked global sized/unsized `delete[]` when lookup remains
global; sized deallocation receives the original allocation extent. When
libc++ supplies only a standard sized declaration, a checked source-defined
unsized operator is selected instead.
Calls require a complete element within the target's default new alignment,
enable `memory_lifetimes`, and introduce no libc++ runtime call, native-heap
import or ownership object. Multidimensional deletion uses each fixed inner
extent to flatten the native cookie count and destroy base elements in reverse
order.

Exact single-object `std::unique_ptr<T, D>` and unbounded-array
`std::unique_ptr<T[], D>` specializations are admitted for the same
non-volatile base element types, including complete bounded inner extents. `D`
may be the matching `std::default_delete` or a source-owned by-value custom
deleter. A custom deleter must be an empty, standard-layout, trivial one-byte
record with no fields or bases and trivial default/copy/move construction,
assignment and destruction. It must define exactly one ordinary, nonstatic
`void operator()(pointer) noexcept`, optionally `const`, with no ref qualifier;
`pointer` remains the owner's exact raw element or row-pointer type. The pinned
libc++ owner record must contain its exact raw pointer, two exact empty
compressed-pair padding fields and the matching empty deleter, all at offset
zero. The array specialization must also contain the exact empty, trivial
`__unique_ptr_array_bounds_stateless` checker at offset zero. Its total size and
alignment must equal `T *`. The response preserves either ABI as one synthetic
`nct_unique_ptr_pointer: ptr:T` field at offset zero. For a multidimensional
owner this is a pointer to its first bounded row; the empty custom deleter adds
no response field.

Default, `nullptr`, compatible raw-pointer and same-type move construction lower
directly for either deleter. The pointer and `nullptr` constructors also accept
an argument that Clang binds to the exact deleter lvalue or rvalue parameter,
including a supported source conversion to that record. Both arguments and
the conversion are evaluated once; the captured pointer is stored while the
empty trivial deleter value is elided without adding a carrier field.
Const-adding converting moves retain the same scalar or array ownership form
and matching default deleters. A move captures
the source once, transfers its pointer through the checked qualification
conversion and clears the source. `get` and explicit boolean conversion read
the captured pointer once;
single-object owners also admit `operator->` and `operator*`, while array owners
admit `operator[](size_t)` as an lvalue access. The exact mutable and const `get_deleter` overloads
evaluate the owner once and return the correspondingly qualified lvalue
reference to its authenticated deleter subobject. Lowering preserves
the pinned zero-offset empty-subobject address by passing the owner address
through a qualification-preserving `void *` conversion before retyping it as
the one-byte deleter record; it adds no owner field or storage. `release` returns
that pointer and clears the owner without destroying it. Every admitted
nonstatic member accepts either an object receiver or an exact raw pointer to
the owner. A raw-pointer receiver executes once, retains its pointee `const`,
and yields the same owner identity. `reset(pointer())`, array `reset(nullptr)`
and compatible raw-pointer reset evaluate the
receiver before the replacement argument, install the replacement before
destroying the old object. A default deleter uses the matching checked
single-object or reverse-array class/global delete path. A
custom deleter receives the old non-null pointer exactly once through its
source-defined call operator and does not require any global delete definition.
Same-type and admitted const-adding converting move assignment evaluate the
right owner before the left owner,
release the source and then reset the destination; same-type self-move therefore
retains ownership. Explicit member-call assignment instead evaluates its
receiver before its argument. `nullptr` assignment follows the same distinction.
Member `swap` evaluates its receiver
before its argument; free `std::swap` evaluates each owner once. Both exchange
only the captured pointer fields, perform no destruction and retain ownership
during self-swap. All six comparisons capture raw pointers, including either
operand order with `nullptr`. Two admitted owners may use different deleter
specializations when both are scalar or both are arrays and their raw pointers
have a qualification-compatible common type. Ordered forms reuse
the checked flat-address pointer carrier that implements the `std::less` total
order without C relational-pointer undefined behavior.
Automatic and static destruction first clear the owner and then run its
matching deleter on the former pointer. Default array sized delete[] uses the
checked native cookie and original allocation extent. Null pointers skip every
deleter call.

Stateful, reference, non-raw-pointer, nontrivial, overloaded, ref-qualified and
throwing custom deleters remain rejected.

The exact pinned single-object `std::make_unique<T>(args...)` overload and
unbounded-array `std::make_unique<T[]>(count)` overload are also admitted;
array `T` may contain complete bounded inner extents. The
single-object path authenticates the concrete function-template specialization,
`T` and `_Args` pack, non-array `new T(...)`, raw-pointer `unique_ptr<T>`
construction and selected allocation function together. Allocation uses the
exact checked source-defined global or class-specific `operator new` selected
by Clang before evaluating construction arguments. The resulting owner uses
the independently selected scalar class/global delete path. A scalar accepts
value initialization or one admitted direct scalar conversion. A complete
source-owned non-union record calls the exact
source-owned non-template `noexcept` constructor selected by Clang, including
default, copy, move and multi-argument forms. Omitted trailing parameters must
be represented by the exact selected constructor's unrewritten source-owned
default expressions. Supplied arguments and selected defaults are each
evaluated once after allocation.

The array path authenticates its concrete `T[]` specialization, one `size_t`
parameter, `new T[count]()` expression, libc++ private array-owner construction
and selected global or class-specific allocation function together. The
call-site count is the outer extent and must be an integer constant expression
from zero through 65536. Lowering combines it with all fixed inner extents,
computes the checked byte extent, stores the target array cookie when required
and value-initializes every flattened scalar element. A complete source-owned
non-union base record
must select an exact source-owned non-template `noexcept` constructor callable
with zero explicit arguments. Every semantic argument must be the selected
parameter's unrewritten source-owned default expression. The constructor and its
defaults are evaluated independently once for each flattened element, with
temporary cleanup before construction advances to the next element. The
resulting raw pointer is installed in the normal pointer-sized owner, so return
destinations, automatic destruction and the checked scalar or reverse-array
class/global delete path remain shared with direct `unique_ptr`
construction.
Neither factory emits a libc++ runtime call.

Default deletion requires a complete base element within the target's default
new alignment and a matching source-owned selected scalar or array class/global
delete definition. Volatile elements, member-function addresses, const-removing
or base-adjusting converting moves and base-adjusting heterogeneous comparisons
remain rejected at this boundary. Runtime-count `make_unique`, throwing record
construction, over-aligned elements, factory function addresses and other
ownership factories remain rejected. Every admitted operation emits no libc++
runtime call and enables the checked
`memory_lifetimes` policy.

Exact `std::allocator<T>` metadata is admitted when `T` is non-cv `void` or a
non-array object type, including an incomplete object. Its C++17 nested value,
pointer, reference, size, difference and `rebind` aliases remain compile-time
types. Exact `std::allocator_traits<std::allocator<T>>` metadata exposes the
corresponding allocator, value, pointer and rebind aliases plus its integral
trait constants. These identities can appear behind pointer, reference and
fixed-array type wrappers. Exact
`std::uses_allocator<T, std::allocator<U>>` identities expose their inherited
`type` and `value_type` aliases; their `value` and `uses_allocator_v` constants
also fold through the authenticated header. Metadata-only uses create no
allocator storage and emit no call.

Exact runtime `std::allocator<T>` objects use an authenticated stateless
carrier. The pinned libc++ layout must be exactly one byte with one-byte
alignment and no data members; non-void specializations must have only the
exact empty private ABI base from `__memory/allocator.h`. The response preserves
that representation as one synthetic `u8` field at offset zero. Default,
copy/move and same-type assignment are admitted, as is the exact `noexcept`
converting constructor from `std::allocator<U>` into a non-void specialization.
The `allocator<void>` specialization retains its own implicit default,
copy/move and assignment operations, and may be the source of a non-void
converting construction. Every source and receiver expression is still
evaluated; assignment evaluates the right operand before the left.

Exact heterogeneous `operator==` and `operator!=` calls evaluate both allocator
operands and fold to `true` and `false`, respectively. The deprecated C++17
`address` overloads evaluate the allocator receiver and bound object once,
bypass overloaded `operator&`, and return the checked raw address with exact
pointee qualification. The deprecated C++17 `max_size` operation evaluates its
receiver and returns the target `size_t` maximum divided by the positive size of
the complete non-void element. These operations emit no libc++ runtime call.

The deprecated C++17 `allocator<T>::destroy` member accepts the exact matching
raw `T *`. Exact `allocator_traits<allocator<T>>::destroy` additionally accepts
an admitted raw pointer to another scalar or complete source-owned non-union
record, preserving libc++'s selected member-forwarding or `destroy_at` fallback
behavior. Both evaluate the allocator expression before capturing the pointer;
trivial destruction has no body and nontrivial records call the existing
checked destruction helper. Both enable the `memory_lifetimes` alias policy.
The exact traits `max_size` operation evaluates its allocator reference and
uses the same target `size_t` maximum divided by complete `T` size. Exact
`select_on_container_copy_construction` evaluates its source once and returns
the copied stateless allocator carrier. These traits operations emit no libc++
runtime call.

The deprecated C++17 `allocator<T>::construct` member and exact
`allocator_traits<allocator<T>>::construct` forwarding overload authenticate
the instantiated libc++ placement-new body before lowering. Their target may be
a writable admitted scalar or a complete source-owned non-union record,
independent of allocator `T`. Zero-argument scalar construction value-initializes
to zero; one scalar argument uses the checked direct scalar conversion. Record
construction retains Clang's exact selected default, multi-argument, copy or
move constructor and requires a supported source-owned definition with a
resolved `noexcept(true)` specification. Omitted trailing parameters must be
the exact selected constructor's unrewritten source-owned default expressions.
Forwarded reference categories and the allocator, pointer and source argument
evaluations are preserved; every supplied argument and selected default is
evaluated once. The constructed object remains caller-owned and is compatible
with the admitted destruction operations. These calls enable
`memory_lifetimes` and emit no libc++ runtime call. Potentially throwing
constructors, constructor templates, nontrivial by-value record parameters,
user-defined conversions and unsupported target records remain rejected.

Exact `allocator<T>::allocate` and `deallocate` members and their exact
`allocator_traits<allocator<T>>` forwarding operations lower through the
existing source-defined global allocation-function contract. `T` must be a
complete non-void object whose alignment does not exceed the target's default
new alignment. An allocation count must be proven no greater than `max_size`;
this proves the byte multiplication cannot overflow without adding the
unsupported `bad_array_new_length` exception path. When `sizeof(T)` is one,
every count already converted to target `size_t` satisfies that bound, so
runtime counts are admitted. This includes admitted character types, `bool`,
`std::byte` and one-byte source records. Signed inputs retain their ordinary
conversion to `size_t`, including wrapping negative inputs; no pre-conversion
sign check is introduced. Count expressions, user-defined conversions and
temporary cleanup retain their existing source checks and one-time evaluation.
For larger elements, a nonoverflowing integer constant expression remains
valid. Runtime counts also qualify when their final implicit conversion to
`size_t` directly consumes an unsigned builtin type whose entire value range
fits `max_size`. The proof uses that type's target value width, including the
single-bit range of `bool`, and retains the original expression for source
validation, conversion, evaluation and cleanup. For `allocator<int>`,
`unsigned char`, `unsigned short` and `bool` counts qualify on all eight
supported targets; `unsigned int` qualifies on the six 64-bit targets, while
`unsigned long` qualifies only on the two 64-bit Windows targets.

Earlier explicit narrowing is preserved: a conversion to `unsigned char`
has that type's range even if its input is signed. Source-defined conversions
returning a qualifying builtin type retain their checked bodies and temporary
cleanup. The proof does not recover narrower declaration types through later
promotions, explicit casts to `size_t` or full-width locals. Signed runtime
values, unproven full-width values, enums and extended integers do not qualify
through this rule; volatile inputs and hidden unsupported expressions still
fail their original source checks.
Zero is valid and still calls the selected allocator. The deprecated allocation-hint
member and the traits hint overload evaluate the hint once after the allocator
and count, then use the same global allocation function.

Allocation requires the checked source-owned definition of the ordinary global
`operator new(size_t)` and returns its storage as `T *` without creating lexical
ownership. Deallocation evaluates its pointer and count once and multiplies the
runtime count by `sizeof(T)` when a source-defined sized delete is selected. If
the exact pinned libc++ sized-delete declaration has no definition, it may use
the existing standard forward to a checked source-defined unsized delete; any
source-written undefined sized redeclaration blocks that inference. The allocator
receiver or traits allocator reference is evaluated first. Both paths emit
ordinary checked source calls, enable `memory_lifetimes`, and introduce no
libc++ runtime call or allocation opcode. Default heap allocation, unproven or
overflowing allocation counts, extended alignment and exceptions remain rejected.
Ownership objects retain their separate contracts.

The exact `std::addressof(T&)` and raw-pointer
`std::pointer_traits<T *>::pointer_to(T&)` operations directly produce the
address of an otherwise admitted non-volatile object lvalue. They preserve
top-level pointee `const`, accept scalar, pointer, array and source-record
objects, evaluate the bound argument once, and bypass an overloaded
`operator&`. Their result is an ordinary checked raw object pointer; they do not
call libc++ at runtime.

The exact C++17 `std::destroy_at`, `std::destroy` and `std::destroy_n`
templates also lower for raw pointers to admitted scalar objects and complete
source-owned non-union records. Scalar and trivial-record destruction have no
runtime body. Nontrivial records call their existing checked destruction helper:
`destroy_at` destroys one object, while `destroy` and `destroy_n` walk forward
and destroy each selected object in order. Every pointer and count argument is
captured once. `destroy_n` advances and returns the captured pointer once per
positive count; zero and negative signed counts return the original pointer.
All three operations enable the checked `memory_lifetimes` alias policy.

The exact `std::uninitialized_copy`, `std::uninitialized_copy_n`,
`std::uninitialized_fill`, `std::uninitialized_fill_n`,
`std::uninitialized_default_construct`,
`std::uninitialized_default_construct_n`,
`std::uninitialized_value_construct`,
`std::uninitialized_value_construct_n`, `std::uninitialized_move` and
`std::uninitialized_move_n` templates lower for writable raw output pointers to
the same admitted scalar or trivial source-record element type. A record must be
complete, source-owned, non-union, standard-layout and trivial. Copy, move and
fill construct each destination from the captured input or value. Value
construction stores the scalar zero or null value, or recursively zeroes the
record fields. Default construction starts the object lifetime without
inventing a write.

The four default/value-construction forms additionally accept a complete
source-owned non-union record whose selected default constructor is
source-owned, takes exactly zero parameters, has a supported definition and is
resolved `noexcept(true)`. Default construction calls it once per object.
Value construction first recursively zeroes the complete object when the
selected constructor is not user-provided, then calls the constructor. This
preserves zero-initialization of untouched scalar members before a defaulted
nontrivial constructor initializes its record members.

The six copy/fill/move forms additionally accept a complete source-owned
non-union record when their instantiated, authenticated libc++ helper contains
one placement construction and Clang selected a source-owned non-template copy
or move constructor for it. The selected constructor must take exactly one
same-record reference parameter, have a supported definition when nontrivial,
and resolve to `noexcept(true)`. Copy and fill require the selected lvalue
reference constructor. Move uses the constructor Clang actually selected, so a
move constructor receives the source as an rvalue while a valid copy fallback
keeps its lvalue-reference form. Each loop calls that constructor once for each
destination object; explicitly defaulted or implicit nontrivial constructors
are materialized through the normal generated-special-member path.

Every argument is retained once, range forms advance to their end, counted
forms run only while the promoted count is positive, and the exact pointer or
pointer-pair result is preserved. All ten operations enable the checked
`memory_lifetimes` alias policy. Admitted operations cannot throw, so these
direct loops require no exception cleanup.

Volatile objects, rvalues for address utilities, function pointers,
fancy-pointer `pointer_traits`, array-element destruction, custom iterators,
heterogeneous, union or non-source-record uninitialized construction, function
addresses, quoted includes, shadows and forged declarations remain rejected.
Potentially throwing constructors, constructors with extra default arguments,
constructor templates and unsupported source-record definitions remain
rejected. Default-delete function addresses, volatile elements and unsupported
conversions remain rejected. Allocator member or comparison function addresses,
custom allocator types and traits, and other
`allocator_traits` forwarding calls remain rejected. Volatile destruction
pointers remain rejected. Allocation requires a separate default-heap and
exception contract. Other smart pointers and other ownership factories remain
outside this boundary, as do array and unsupported single-object factory
forms.

## Functional header from `<functional>`

Core v2 admits the exact angled `<functional>` entry from the pinned embedded
VFS. The authenticated C++17 closure contains 346 libc++/resource files on every
supported target, has the same dependency set on all eight targets, and
contains no platform headers. The public header and every consumed component
retain their original upstream bytes.

Calls on typed or transparent specializations of `std::plus`,
`std::minus`, `std::multiplies`, `std::divides`, `std::modulus`, `std::negate`,
`std::bit_and`, `std::bit_or`, `std::bit_xor`, `std::bit_not`,
`std::equal_to`, `std::not_equal_to`, `std::less`, `std::greater`,
`std::less_equal`, `std::greater_equal`, `std::logical_and`,
`std::logical_or` and `std::logical_not` lower directly for unqualified
integral types through 64 bits plus `float` and `double`. Transparent `void`
specializations additionally accept heterogeneous admitted operands and use
the built-in operator's Clang-selected promotions and usual arithmetic
conversions. The frontend authenticates the exact libc++ specialization,
member template, `std::forward` calls, concrete signature and single built-in
operator body before emitting scalar IR. Narrow integers use C++ integer
promotion and typed specializations convert the result back to their selected
type. Both function arguments are captured once before a logical result is
formed, preserving the eager argument evaluation of a function call.
The nine comparison and logical objects also accept a top-level `const` on
their typed scalar template argument. Their selected operator still returns
`bool`, with the same input conversion and one-time argument evaluation.

The exact empty specializations may also be stored in local or global objects,
passed by value, and trivially default/copy/move constructed or copy/move
assigned. Their authenticated C++ one-byte size and alignment map to a single
`u8` carrier field at offset zero; calls still lower to the verified built-in
operation without a libc++ runtime dependency.

Exact integral `std::hash` specializations from `bool` through `unsigned long`,
plus `long long` and `unsigned long long`, use the same authenticated one-byte
carrier. The direct specializations use pinned `static_cast<size_t>` semantics.
The wide specializations authenticate libc++'s exact `__scalar_hash` base and
inherited call operator; they preserve the value bits on 64-bit targets and
reproduce libc++'s Murmur2 hash of the eight little-endian value bytes on 32-bit
targets. Temporary, local, global, copied and by-value objects are admitted.
They support same-type copy and move assignment and call directly or through
`std::invoke`. The receiver is evaluated once before the argument. The exact
C++17 `nullptr_t` specialization returns the pinned libc++ constant `662607004`
through the same object, assignment and invocation boundary. Exact `float` and
`double` specializations authenticate the same `__scalar_hash` base and the
outer call operator's zero special case. Positive and negative zero hash to
zero; every other value hashes its IEEE representation. `float` uses its 32-bit
representation. `double` preserves its 64-bit representation on 64-bit targets
and reproduces the same eight-byte Murmur2 algorithm on 32-bit targets. These
objects support the same storage, assignment and direct or `std::invoke` call
forms. The primary `std::hash<E>` specialization for a complete enum also uses
the one-byte carrier and the same object forms. Its exact
`__enum_hash<E, true>` base, underlying-type cast and nested integer hash call
are authenticated; narrow underlying types use the direct size conversion and
64-bit underlying types retain the target-specific path above. Exact
`std::hash<T *>` specializations for non-volatile object or `void` pointers and
fixed-arity ordinary function pointers with the target's object-pointer size
and alignment authenticate the pinned partial specialization, its
pointer/`size_t` union, the pointer
store and the exact `__murmur2_or_cityhash<size_t>` call. The pointer bits use
libc++'s four-byte Murmur2 algorithm on 32-bit targets and its ABI-v1 eight-byte
CityHash algorithm on 64-bit targets. Pointer hashes support the same temporary,
stored, copied, assigned, by-value and direct or `std::invoke` forms.
Both direct calls and `std::invoke` convert `nullptr_t` to the exact function
pointer parameter before hashing.
`long double`, volatile-object, volatile-void, variadic function-pointer and
member-pointer hash specializations remain outside this boundary.

Exact `std::reference_wrapper<T>` and `std::reference_wrapper<const T>` for
non-volatile object types, plus exact function wrappers whose fixed-arity
signature has admitted scalar, object-pointer or fixed-arity ordinary
function-pointer values, exact lvalue- or
rvalue-reference parameters, exact lvalue- or rvalue-reference results, and
complete source-owned record value results, retain the authenticated target ABI layout: one pointer slot under the
Itanium ABI and an empty-base storage slot followed by the pointer under the
Microsoft ABI. Direct construction from a matching lvalue, the direct
`std::ref` and `std::cref` overloads, their wrapper-taking overloads, trivial
copy/move construction, and same-specialization assignment lower without a
libc++ runtime call. Assignment reseats the wrapper by copying its stored
pointer. Wrapper-taking `ref` copies that target pointer; wrapper-taking `cref`
does the same and adds `const` for an object referent. Both accept stored or
temporary admitted wrappers and evaluate the argument once. `get()` returns the
referenced object or function; object forms also lower the implicit `T&`
conversion. These operations preserve object qualification and accept object
or raw-pointer receivers. Volatile referents remain outside this boundary.
Wrappers around an admitted typed or transparent standard function
object, a stored fixed-arity function pointer or an admitted function referent
are callable directly and through `std::invoke`. Fixed-arity function wrappers
also admit exact lvalue or rvalue references to complete source-owned records.
The wrapper and stored referent
are retained before the arguments are evaluated, and each expression is
evaluated once. Exact scalar, object-pointer and function-pointer lvalue- or rvalue-reference
parameters and exact lvalue- or rvalue-reference results preserve their source storage and
qualification. These reference forms include complete fixed arrays with at
most 65536 elements when their recursive element type is otherwise admitted.
Authenticated `std::array` objects, including nested and zero-length arrays,
use the same reference parameter and result boundary throughout the invocation,
member-pointer and `mem_fn` forms below. Their exact array layout and recursive
element types remain checked; by-value SDK callback parameters and results
remain excluded. See the [array contract](#fixed-value-arrays-from-array).
Wrappers around an exact source-owned record callable also call directly or
through `std::invoke` when the selected lvalue or const-lvalue `operator()` is
an admitted defined method with that same parameter and result boundary. The
stored pointer remains the method receiver, so mutations and reference results
retain the original object's identity.

Exact C++17 `std::invoke` calls on an ordinary function or stored function
pointer also lower directly. The target must have fixed arity. Parameters may
be admitted by-value scalars with checked direct argument conversions, exact
fixed-arity ordinary function pointers including function-name decay, or exact
lvalue or rvalue references to those values, complete fixed arrays or complete
source-owned records, plus exact by-value complete source-owned standard-layout
records that are trivially copyable and trivially destructible;
results may be the corresponding
scalar, object-pointer, function-pointer, complete source-owned record value,
exact lvalue or rvalue reference, or `void`. References retain
their source storage and qualification. Const lvalue-reference parameters also
bind xvalues of the same unqualified type, including arguments already
materialized for the SDK forwarding call. The original full-expression cleanup
still applies when a callback returns a reference. This rule is shared by source
function objects, direct reference-wrapper calls and member adapters; it adds no
converted temporary or base adjustment. The callable is retained before the
arguments are evaluated, then the retained function pointer is called once.
The same entry point accepts every admitted typed or transparent standard
function object, including temporary and stored objects, and reuses the exact
authenticated operation body and scalar conversion rules above. Its callable
expression and arguments are each evaluated once. Typed objects convert each
argument to the selected `operator()` parameter type before the scalar
operation, including narrowing integral conversions.
It also accepts an exact source-owned record callable whose selected
nonstatic `operator()` is an admitted defined method. Lvalue, const-lvalue and
rvalue-qualified overload selection follows Clang's checked dispatch. The
method may use the same admitted by-value and exact lvalue- or rvalue-reference
parameter and result boundary as member invocation, including fixed-array and
source-owned-record reference parameters and results and trivial source-record
value parameters, plus complete source-record value results. The callable is retained
before its arguments, reference results preserve storage identity, and a
temporary callable is destroyed at its full-expression boundary.

A direct source-written address of an owned nonstatic member function also
lowers through `std::invoke` when the receiver is an exact-class lvalue,
full-expression temporary, pointer or admitted `std::reference_wrapper` and the method has fixed-arity
admitted scalar, object-pointer or fixed-arity ordinary function-pointer
parameters, either by value with a checked direct conversion or by exact lvalue
or rvalue reference, including trivial source-record value parameters and
references to complete fixed arrays or source-owned records,
with a scalar, object-pointer, function-pointer, exact
lvalue- or rvalue-reference including a complete fixed array or source-owned
record, complete source-owned record value, or `void`
result. Reference parameters and results preserve the selected object's storage
and qualification. The receiver is retained before the arguments are evaluated
and each selected argument conversion is preserved. A direct source-written
address of an admitted non-volatile scalar, object-pointer or fixed-arity
ordinary function-pointer field lowers on the same receiver forms and retains
the qualified field lvalue or xvalue, including assignment to lvalues and reads
of const objects, declared-const fields or temporary receivers. A projected
function pointer may be called or reseated through that result. A local automatic member pointer whose
sole initializer is an exact direct address of an admitted method or data field
may be retained across statements and passed to `std::invoke`; the frontend
authenticates and erases the variable, then emits the same direct method call or
field projection. It may be copied or moved through further exact same-type
local automatic variables, including through authenticated `std::move`,
`std::forward`, `std::move_if_noexcept` and `std::as_const` adapters; every
carrier in that initializer chain is authenticated and erased. The same
adapters may wrap that authenticated pointer at its final `std::invoke`, native
member operation or `std::mem_fn` factory use. Reassigning,
returning or constructing a null member pointer remains rejected. An admitted
direct address or stored data-member pointer may also be applied with native
`.*` or `->*` to an exact-class lvalue, pointer or full-expression temporary,
preserving the field glvalue and the const qualification contributed by either
the field or receiver. A temporary is destroyed at its C++ lifetime boundary;
native `.*` binding to a local reference retains the standard lifetime
extension. An admitted stored member-function pointer may be called
with native `(object.*pointer)(arguments...)` or
`(object_pointer->*pointer)(arguments...)` syntax through the same fixed-arity
method boundary; a direct source-written member-function address is accepted
in the same syntax. Exact full-expression temporary receivers are also
materialized through this method boundary and destroyed after the call. The receiver is retained before the arguments, const methods
accept exact const receivers, `&&`-qualified methods require an exact temporary
receiver, admitted lvalue- or rvalue-reference parameters, and admitted
lvalue- or rvalue-reference results preserve storage identity. Complete
source-owned record results construct through the caller's final destination
and retain their normal full-expression cleanup. Volatile native data-member access remains
rejected. Parameter-sourced, reassigned or null native member
pointers remain rejected. The selected libc++
member-function or member-object dispatcher body, wrapper `get()` body and
parameter flow are authenticated before the direct method call or field
projection is emitted.

An exact `std::mem_fn` wrapper built from either admitted direct named address
or an authenticated local member-pointer initializer chain may be called
immediately, directly or as the callable of `std::invoke`,
through the same receiver, argument and result boundary. A wrapper for an
admitted method or data field may also be retained in a directly initialized
local automatic variable, then called directly or through `std::invoke`. The pinned
`__mem_fn` specialization, stored member field, factory, call operator, both
`__invoke` layers and every forwarding edge are authenticated before the
wrapper and every local carrier are erased. Exact same-type local copy/move
initializer chains are admitted, including authenticated `std::move`,
`std::forward`, `std::move_if_noexcept` and `std::as_const` adapters; copies or
moves from parameters and reassignment remain outside the runtime boundary.
The same adapters may wrap an authenticated local wrapper at its final direct
call or `std::invoke` use.

Other cv-qualified typed template arguments, addresses or pointers to function
objects, user-defined operands, `long double`, `std::function`, binders and searchers do
not yet lower. Reassigned or null member pointers, `mem_fn` copies or moves
from parameters, reassigned `mem_fn` objects, base-adjusting
receivers, volatile
receivers, function referents, references to incomplete or runtime-bound
arrays, nontrivial source-record value parameters,
other unsupported reference signatures and variadic targets remain
outside the `std::invoke` boundary.
Quoted includes, shadows, forged declarations and other runtime uses remain
rejected by the normal source and semantic checks.

## Algorithm header from `<algorithm>`

Core v2 admits the exact angled `<algorithm>` entry from the pinned embedded
VFS. The authenticated closure contains 354 libc++/resource files on every
supported target, has the same dependency set on all eight targets, and contains
no platform headers. The upstream public header and every consumed component
retain their original bytes.

The exact public `std::copy`, `std::copy_n`, `std::move`,
`std::copy_backward`, `std::move_backward`, `std::fill`, `std::fill_n`,
`std::iter_swap`, `std::swap_ranges`, `std::reverse`, `std::reverse_copy`,
`std::rotate` and `std::rotate_copy` templates directly lower for non-volatile
raw object pointer ranges of admitted scalar elements. Copy and move outputs
accept checked direct scalar conversions from their input element. Swap and
in-place reorder operations retain the same unqualified element type. Fill
values may use a different scalar type with a checked direct conversion to the
writable output element. Transfer and fill algorithms require a writable output
element;
`iter_swap` and `swap_ranges` require both mutated ranges to be writable, and
`reverse` and `rotate` require a writable input range. `copy_n` and `fill_n`
accept integral or non-scoped enum counts whose promoted type is at most 64
bits. Floating counts are outside this boundary.

The exact default-equality `std::find`, `std::count`, three- and four-iterator
`std::equal`, `std::adjacent_find`, `std::remove`, `std::remove_copy`,
`std::replace`, `std::replace_copy`, `std::unique` and `std::unique_copy`
templates lower for built-in integer, enum, `float`, `double`, object pointer
or `nullptr_t` values. `equal` may compare two scalar element types with a checked
common equality type. `find` and `count` may compare the range element with a
different scalar value through the same checked common type, as may `remove`
and `remove_copy`. Default `search_n` accepts the same heterogeneous value
boundary. `replace` and `replace_copy` also admit a different scalar old/new
value type when the new value converts directly to the destination.
Algorithms that compact or replace an input range require
it to be writable; copy variants require a writable scalar output whose element
accepts a checked direct conversion from the input.
Value references remain live through the loop, including when they alias an
element that an earlier iteration changes. Enum elements use built-in equality
only when no source `operator==` accepts that enum; such overloads remain
rejected instead of being silently bypassed.

The exact binary-predicate overloads of `std::adjacent_find`, three- and
four-iterator `std::equal`, three- and four-iterator `std::mismatch`, and three-
and four-iterator `std::is_permutation`, plus `std::unique`,
`std::unique_copy`, `std::search`, `std::find_end`, `std::find_first_of` and
`std::search_n`, accept checked ordinary function pointers. Each predicate
takes two admitted by-value scalar parameters reachable through checked direct
conversions from the compared values and returns `bool` exactly. This admits
enums and the other scalar carriers; `equal`, `mismatch` and the two-range
search operations may compare two different element types. `search_n` may
likewise use a different scalar type for its retained `const` search value.
`adjacent_find`, `unique` and `unique_copy` require one common element type
because they compare elements within one range. Predicate `unique`
requires a writable range, and predicate `unique_copy` requires a writable
scalar output whose element accepts a checked direct conversion from the input.
The callback value is evaluated and retained once.
Adjacent and equality scans stop on their first decisive comparison, mismatch
returns its authenticated pointer pair, and bounded permutation checks unequal
lengths before invoking the predicate. Empty patterns and ranges perform no
predicate calls; nonempty unique operations make one call per element after the
first. Non-positive `search_n` counts return the first iterator without a call,
and a positive unsuccessful run applies its predicate at most once per input
element. Reference parameters, variadic functions, non-boolean results and
callable objects stay outside this boundary.

The exact three-argument `std::find_if`, `std::find_if_not`, `std::count_if`,
`std::all_of`, `std::any_of` and `std::none_of` templates accept the same raw
scalar-pointer ranges plus a checked ordinary function-pointer predicate. The
predicate must take one admitted by-value scalar parameter reachable through
the checked direct conversion from the range element and return `bool` exactly.
This admits integer and enum scalars, `float`, `double`, object pointers and
`nullptr_t`. The function-pointer value is evaluated and retained once, then
invoked once per inspected element. Find and boolean queries stop at their
first decisive result; `count_if` visits the whole range and returns the target
`ptrdiff_t`. Empty ranges preserve the standard `all_of`/`none_of` true and
`any_of` false identities without invoking the predicate. Reference parameters,
non-boolean results and callable objects stay outside this boundary.

The exact `std::copy_if`, `std::remove_if`, `std::remove_copy_if`,
`std::replace_if` and `std::replace_copy_if` templates use the same checked
unary predicate boundary. Copy variants require a writable output whose element
accepts a checked direct scalar conversion from the input;
`remove_if` and `replace_if` require a writable input range. `copy_if` and both
remove forms preserve the relative order of retained elements and return the
advanced output or new logical end. The replace forms accept a scalar `const`
value reference with a checked direct conversion to the mutated input or copy
output element, retain that reference through the loop, and therefore observe
changes when a callback mutates an aliased replacement object.
`replace_copy_if` writes exactly one output per input and returns the advanced
output. Every range, output, callback and replacement argument is evaluated
once; empty ranges return their unadvanced iterator and do not invoke the
predicate.

The exact `std::is_partitioned`, `std::partition`, `std::stable_partition`,
`std::partition_copy` and `std::partition_point` templates use that predicate
boundary too.
`is_partitioned` accepts a read-only range and stops when it finds a selected
element after the first rejected element. `partition` requires a writable range,
performs at most one predicate call per element, and returns the boundary between
selected and rejected elements; it does not promise stable ordering.
`stable_partition` requires the same writable range, invokes the predicate
exactly once for every element, and uses in-place scalar shifts to preserve the
relative order of both groups. It returns the first rejected element.
`partition_copy` requires two writable scalar outputs whose elements each
accept a checked direct conversion from the input, preserves the input order
within both output groups, and returns their advanced pointers in an
authenticated `std::pair`. `partition_point` accepts a range already partitioned
by the predicate and uses logarithmic bisection. Empty ranges return their input
or output iterators without invoking the predicate, and all arguments are
evaluated and retained once.

The exact `std::for_each`, `std::for_each_n`, unary and binary
`std::transform`, `std::generate` and `std::generate_n` templates accept checked
ordinary function pointers too. Traversal and transform callbacks take each
input through a directly convertible by-value scalar parameter. `for_each`
permits a `void` or admitted scalar result, ignores that result, and returns the
retained function pointer; `for_each_n` returns the advanced input. Transform
callback results must be directly convertible to the output's scalar element
type. Generators take no arguments and their scalar results must be directly
convertible to the output element.
Output ranges are writable, callback values are retained once, and each visited
or generated element invokes its callback exactly once. The counted forms use
the same promoted integral or non-scoped enum count boundary as `copy_n`; a
non-positive signed count performs no calls and returns the original iterator.
`for_each_n` additionally accepts the same standard-layout, trivially copied
source function objects described for unary predicates below. Its exact pinned
count conversion and loop are authenticated, the selected non-template call
operator may return `void` or an admitted scalar, and that result is discarded.
The algorithm invokes one retained by-value object in input order and returns
the advanced pointer. Pinned typed or transparent `std::logical_not`,
`std::negate` and integral `std::bit_not` objects are accepted under their
checked scalar input conversion rule. Typed objects may convert a distinct
input element to their operand type before each call. Ordinary `for_each` also
accepts those source objects under its authenticated loop and trivial move
return. It copies the modified by-value callable into the result without
changing the caller's object, including on empty ranges. Pinned typed or
transparent `std::logical_not`, `std::negate` and integral `std::bit_not`
objects use the same checked input conversion and return boundary.

`generate` and `generate_n` accept source-owned, standard-layout, trivially
copied generator objects with a defined non-template nullary call operator.
The selected scalar result must convert directly to the writable output
element. Their exact pinned loops are authenticated, and the generator's
by-value state is retained once across visits. Empty ranges and non-positive
counted ranges do not invoke it.

Both `transform` overloads accept these source-owned trivial function objects
when the selected non-template operator returns an admitted scalar that converts
directly to the writable output element. The unary overload reads one scalar
input; the binary overload reads two independently typed scalar inputs. Their
exact pinned loops are authenticated, retaining one by-value operation object
and advancing each pointer once after its corresponding read and store. Empty
and overlapping in-place ranges preserve that order. The binary overload also
accepts admitted typed or transparent `<functional>` binary objects, including
arithmetic, comparison and logical operators, after authenticating the selected
SDK method and checking direct scalar conversions from both input elements to
their operand types. Typed objects convert both inputs before the
binary operation, including narrowing conversions.
The unary overload similarly accepts authenticated typed or transparent
`std::negate`, integral `std::bit_not` and `std::logical_not` objects. Their
selected operand type accepts a checked direct conversion from the input
element, including typed narrowing before the operation; the result converts
directly to the output element. Other unary SDK objects remain excluded.

The exact default-equality four-iterator `std::search`, `std::find_end` and
`std::find_first_of` templates lower nested equality scans over two ranges;
their five-argument forms use the checked binary-predicate boundary above.
Three- and four-iterator `std::mismatch` return an authenticated
`std::pair` of the first unequal pointers; the bounded form also stops when the
second range ends. Exact four-argument default-equality and five-argument
predicate `std::search_n` accept an integral or non-scoped enum count whose
promoted type is at most 64 bits. It returns the first iterator for non-positive
counts. Default-equality operations may compare two scalar element types with a
checked common equality type; the documented predicate forms use checked direct
conversions to their callback parameter types instead.

The exact two-argument `std::min_element`, `std::max_element`, `std::is_sorted`
and `std::is_sorted_until` templates and exact three-argument
`std::lower_bound`, `std::upper_bound`, `std::equal_range` and
`std::binary_search` templates lower for raw pointers to built-in integer,
enum, `float`, `double` or complete non-function object-pointer elements. The
searched value may use a different compatible scalar type when both have a
checked ordered common type. Binary bounds use the target
`ptrdiff_t` and preserve logarithmic bisection; `equal_range` returns an
authenticated pair of the lower and upper pointers. Enum forms require built-in
ordering with no source `operator<` accepting the enum.

The corresponding three-argument `std::min_element`, `std::max_element`,
`std::is_sorted` and `std::is_sorted_until` overloads and four-argument
`std::lower_bound`, `std::upper_bound`, `std::equal_range` and
`std::binary_search` overloads accept a checked ordinary function-pointer
comparator. It takes two admitted by-value scalar parameters reachable through
checked direct conversions from the range element and bound value and returns
`bool` exactly. Lower and upper bounds require their used comparison direction;
`equal_range` and `binary_search` require both directions. This admits
heterogeneous values, enums, object pointers and
the other scalar carriers. The callback is evaluated and retained once.
Extremum scans keep the first equivalent element; sortedness scans stop at the
first inversion; empty and single-element ranges make no calls. Bounds retain
logarithmic bisection. Reference parameters, non-boolean results, variadic
functions, source-owned callable objects, heterogeneous values and record
elements stay outside this function-pointer boundary.

The `std::min_element` and `std::max_element` comparator overloads also accept
authenticated typed or transparent empty standard `<functional>` comparison
objects on arithmetic scalar ranges. The selected instantiated `operator()`
is proved and lowered directly. The object argument is evaluated once, even
for an empty range, and equivalent extrema retain their first position.

The comparator overloads of `std::is_sorted` and `std::is_sorted_until` also
accept authenticated typed or transparent empty standard `<functional>`
comparison objects on arithmetic scalar ranges. The selected instantiated
`operator()` is proved and lowered directly; the object argument is evaluated
once, including for an empty range.

The comparator overloads of `std::lower_bound`, `std::upper_bound`,
`std::binary_search` and `std::equal_range` also accept authenticated typed or
transparent empty standard `<functional>` comparison objects on arithmetic
scalar elements and values. The selected instantiated operation is checked for
the element-to-value direction in lower bounds, the value-to-element direction
in upper bounds, and both directions in searches and equal ranges. The object
argument is evaluated once and comparisons lower directly, including when the
range and value have different admitted arithmetic types.

The exact two-argument `std::min` and `std::max`, three-argument `std::clamp`,
two-argument `std::minmax` and two-iterator `std::minmax_element` templates use
the same built-in arithmetic, enum or complete object-pointer ordering boundary.
`min`, `max` and `clamp`
preserve the selected const-reference identity. `minmax` constructs only its
exact authenticated `std::pair<const T&, const T&>` result; its fields retain
the two argument referents on every pointer width. `minmax_element` returns an
authenticated pointer pair, selects the first minimum and last maximum, and
uses pairwise comparisons after its initial elements.

The corresponding three-argument `std::min`, `std::max` and `std::minmax`,
four-argument `std::clamp`, and three-argument `std::minmax_element` overloads
use the checked comparator boundary above. Scalar reference algorithms retain
the selected argument identity; equivalent inputs select the first argument for
`min` and `max`, while `minmax` keeps the first argument as its minimum and the
second as its maximum. Comparator `minmax_element` retains the first equivalent
minimum and last equivalent maximum, uses the same pairwise comparison bound,
and performs no calls for empty or single-element ranges. Enum and object
pointer values are admitted, while records and unsupported callbacks remain
rejected.

The three-argument `std::minmax_element` overload also accepts authenticated
typed or transparent empty standard `<functional>` comparison objects on
arithmetic scalar ranges. Its selected instantiated `operator()` lowers
directly; the object argument is evaluated once, even for an empty or
single-element range. The first equivalent minimum, last equivalent maximum
and pairwise comparison bound are preserved.

The scalar-reference `min`, `max`, `minmax` and `clamp` comparator overloads
also accept authenticated empty standard `<functional>` objects with a selected
binary `bool` operation on admitted arithmetic scalars. This includes typed
objects such as `std::greater<int>` and transparent objects such as
`std::greater<>`. The exact instantiated libc++ algorithm body must call that
object with its own parameter objects in the documented order, and the selected
`operator()` body must pass the existing functional-operation proof. The object
argument is evaluated once; checked scalar conversions and the selected
reference identity are preserved. Generated programs make no libc++ call for
these comparisons. Source specializations and user-defined comparator objects
remain outside this boundary.

The exact two-iterator `std::is_heap`, `std::is_heap_until`, `std::make_heap`,
`std::push_heap`, `std::pop_heap` and `std::sort_heap` templates use the same
built-in arithmetic, enum or complete object-pointer ordering boundary. Heap queries accept const or writable
raw pointers; heap mutation requires a writable range. `is_heap_until` returns
the first child greater than its parent. `make_heap` builds a max heap,
`push_heap` filters the appended final element upward, `pop_heap` moves the
maximum to the final position and repairs the shortened heap, and `sort_heap`
produces ascending order from a max heap. `make_heap` visits parents bottom-up
and retains the standard linear comparison bound. Empty and single-element
query, construction, push and sort ranges are handled without dereferencing
them; `pop_heap` retains the standard nonempty-range precondition.

The corresponding three-argument heap overloads accept the checked scalar
function-pointer comparator boundary above, including enum and object-pointer
elements. The comparator defines the heap order: for example, a greater-than
callback builds a minimum heap and `sort_heap` produces descending order. Each
call retains the callback once; query ranges may be read-only, mutation ranges
remain writable, and empty or single-element work performs no callback calls.
Reference callback signatures, non-boolean results, variadic functions,
other callable objects and record elements remain rejected. The six heap
overloads also accept authenticated empty standard `<functional>` comparison
objects on arithmetic scalars, including typed and transparent forms. The
selected instantiated `operator()` body is proved before its comparison is
lowered directly. The object argument is evaluated once; a greater-than object
builds a minimum heap and makes `sort_heap` produce descending order.

The exact default-order `std::sort`, `std::partial_sort`,
`std::partial_sort_copy` and `std::nth_element` templates use that arithmetic,
enum or complete object-pointer ordering boundary. `sort`, `partial_sort` and `nth_element` require writable
same-type ranges. `partial_sort_copy` accepts a const or writable input range
and a writable scalar output whose element accepts a checked direct conversion
from the input, returning the advanced output pointer. `sort` uses worst-case
`O(N log N)` heap sorting. Default mixed input/output comparisons use their
checked arithmetic common type. The partial forms
retain a maximum heap of the selected prefix or output capacity for
`O(N log M)` comparison complexity. `nth_element` uses an in-place three-way
partition around a retained scalar pivot, provides average linear comparison
complexity and terminates directly on ranges of equivalent values. An empty
selected prefix leaves `partial_sort` unchanged; an empty output returns the
original output pointer.

The corresponding comparator overloads accept the same checked scalar
function-pointer boundary. `sort`, `partial_sort` and `nth_element` require a
writable same-element range; `partial_sort_copy` accepts a read-only input and
writable directly convertible output. A greater-than callback therefore sorts or
selects in descending order. All callback and iterator arguments are retained
once. Empty selected prefixes and outputs make no callback calls, and the
three-way `nth_element` partition still terminates directly on equivalent
values. Unsupported callbacks and record elements remain rejected.

The three-argument `std::sort` overload also accepts the same authenticated
standard comparison objects as the heap algorithms. Its checked heap lowering
applies the selected scalar `operator()` operation directly, preserving the
object argument's single evaluation and the comparator's ordering.

The comparator overloads of `std::partial_sort`, `std::partial_sort_copy` and
`std::nth_element` accept these authenticated typed or transparent standard
comparison objects on arithmetic scalars. `partial_sort_copy` separately
checks the instantiated operations for input-to-output comparison and
output-heap comparison, including different input and output element types.
Each object argument is evaluated once, and comparisons lower directly.

The exact `std::stable_sort` overloads use the same default arithmetic, enum or
complete object-pointer ordering and
checked function-pointer comparator boundaries on writable scalar ranges. An in-place
bottom-up merge retains the relative order of equivalent elements without a
heap or libc++ runtime dependency and performs `O(N log N)` comparisons. The
iterators and optional callback are retained once; empty and single-element
ranges make no callback calls. Comparator overloads additionally admit enum
and object-pointer elements. The comparator overload also admits authenticated
typed or transparent empty standard `<functional>` comparison objects on
arithmetic scalars. The selected instantiated operator body is proved and
lowered directly, and the object argument is evaluated once. Other callable
objects, unsupported callbacks and record elements remain rejected.

The exact `std::inplace_merge` overloads reuse the same stable in-place merge
for two adjacent, already ordered writable scalar ranges. Equivalent elements
from the first half remain before equivalent elements from the second half,
and at most `N - 1` comparisons are made. An empty half performs no comparison.
The default overload uses built-in arithmetic, enum or complete object-pointer
ordering; the comparator overload
also admits enum and object-pointer elements through the checked callback
boundary. Authenticated typed or transparent empty standard comparison objects
also lower directly on arithmetic scalars, with one-time argument evaluation.

The exact default-order `std::next_permutation` and `std::prev_permutation`
templates use the same writable built-in arithmetic, enum or complete
object-pointer element boundary. They
find the rightmost movable pivot, exchange it with the rightmost qualifying
element, reverse the suffix and return whether a lexicographically adjacent
permutation existed. Empty and single-element ranges return false. A range at
its final or initial permutation is reversed to the opposite endpoint before
returning false.

Their three-argument comparator overloads accept the same checked scalar
function-pointer boundary, including enum and object-pointer elements. The
callback defines the lexicographical order, is retained once, and is not called
for empty or single-element ranges. Repeated values, suffix reversal and
endpoint wraparound retain the same behavior under that order. Unsupported
callbacks, other callable objects and record elements remain rejected. These
two overloads also accept authenticated typed or transparent empty standard
comparison objects on arithmetic scalars, preserving the selected ordering,
wraparound and one-time object evaluation without a libc++ runtime call.

The exact default-equality three- and four-iterator `std::is_permutation`
templates use the equality element boundary, so const ranges, enums and
object-pointer elements are accepted while records remain excluded. The two ranges
may have different scalar element types with a checked common equality type.
Their checked binary-predicate overloads use the scalar predicate boundary above
and also admit heterogeneous scalar ranges, including enum ranges. The
three-iterator form compares a second range of the first range's length. The
four-iterator form checks both lengths before inspecting elements. Distinct
equivalence classes are counted at most once, preserving the standard quadratic
comparison bound and duplicate multiplicities without allocating storage.

The exact four-iterator `std::lexicographical_compare` and `std::includes`
templates and exact five-iterator `std::merge`, `std::set_union`,
`std::set_intersection`, `std::set_difference` and
`std::set_symmetric_difference` templates lower for two ranges with the
arithmetic-or-enum ordering boundary. The ranges may have different scalar element
types with a checked ordered common type. Ordered output algorithms require a
writable scalar destination whose element accepts a checked direct conversion
from both inputs and return its advanced pointer. Merge keeps equivalent
elements from the first range first; set operations preserve their
standard maximum, minimum and excess duplicate counts. Default-order enum ranges
must share the same enum type and have no source `operator<` accepting that
enum. Object-pointer ranges use a checked compatible common pointer type; as
with source C++, relational ordering is guaranteed for pointers into the same
complete object or array.

The corresponding comparator overloads admit heterogeneous scalar ranges with
a checked function pointer whose two by-value scalar parameters are reachable
through direct conversions from the respective element types in both argument
orientations and whose result is exactly `bool`. This includes enum and pointer
elements whose ordering is supplied entirely by the callback. They also accept
authenticated typed or transparent empty standard `<functional>` comparison
objects on arithmetic scalar ranges. Both selected instantiated `operator()`
directions are proved and lowered directly, including when the input ranges
have different element types. Ordered output forms require conversions from
both inputs to the writable destination. The comparator argument is evaluated
once, and the generated loops use both argument orientations when
distinguishing equivalent elements. Source-owned function objects, reference
parameters and converted result types remain rejected.

Each call evaluates and retains its arguments once before entering generated
pointer loops. `find` preserves the bound value reference, `count` uses the
target `ptrdiff_t`, `equal` preserves short-circuit results, `fill_n` applies
the libc++ integer promotion before its positive-count loop, and algorithms
with output iterators return the advanced output pointer. `search_n` applies
the same promotion and preserves a non-positive-count fast path. `mismatch`
constructs its scalar-pointer pair result directly. `remove` and `unique`
return the compacted logical end; copy forms return their advanced destination.
Predicate `unique` and `unique_copy` preserve the first element from each
equivalent run and compare later elements against the last retained element.
`copy_n` also preserves a non-positive-count fast path. `rotate` returns the
new location of the original first element, while `rotate_copy` returns its
advanced destination. Permutation mutation evaluates both bounds once, and
permutation queries evaluate each supplied iterator once. Ordered two-range
algorithms stop at the first decisive comparison and copy only the tails
required by their standard result. Comparator forms preserve the same
first-range merge priority and set multiplicities.
Forward and backward loops preserve their respective standard overlap
direction; `reverse` uses equality-only bidirectional contraction. Minimum and
maximum scans retain the first equivalent element; sortedness scans report the
first descending element. Heap queries scan parent-child relationships; heap
mutation uses index-based upward or downward filtering with target
`ptrdiff_t`. Sorting and partial sorting share those checked heap operations;
selection uses pointer-width partition indexes. Generated programs do not call
or link libc++ for these operations. Heterogeneous value types outside the
documented binary-predicate `equal` and `mismatch` forms, other predicate or
comparator overloads, custom iterators, record elements, callable objects outside
the fourteen unary algorithms below, four scalar-reference extrema operations,
six heap operations and `sort`,
and addresses of standard algorithms remain rejected, as do calls outside a
documented direct lowering.

`find_if`, `find_if_not`, `none_of`, `all_of`, `any_of`, `count_if`,
`replace_if`, `replace_copy_if`, `remove_copy_if`, `copy_if`, `is_partitioned`,
`partition_copy`, `partition_point` and `remove_if`
additionally accept ordinary source-owned
function objects with standard layout, trivial copy construction and destruction,
and one Clang-selected non-template call operator taking an admitted scalar by
value and returning `bool`. The exact pinned algorithm specialization, complete
redeclaration chain, loop, predicate parameter and selected call are authenticated.
The predicate is initialized once in its own by-value parameter storage, so a
prvalue constructor observes the final receiver and a caller lvalue supplies an
independent copy. Mutable state persists between invocations; `const`, non-const
and lvalue-qualified call operators retain the SDK's lvalue receiver selection.
Scalar argument conversions, empty ranges and first-match short circuit follow
the existing pointer algorithm contract. The selected source definition must
complete normal signature/body checks and be emitted before lowering proceeds.
For `all_of`, `any_of` and `count_if`, the public parameter remains the same
object through the reference-taking helper, both internal `__invoke` calls and
the identity projection. The selected helper bodies, exact forwarding casts,
identity method and `forward` template are authenticated independently; no
additional predicate or element copy is introduced. `count_if` retains the
pinned policy, alias and pointer `iterator_traits` chain and uses the target's
signed pointer-difference carrier for its counter and result. It visits the
whole range, while `all_of` and `any_of` preserve their decisive short circuit.
The replacement algorithms authenticate the exact conditional stores and
iterator increments. `replace_if` writes through a mutable scalar input pointer;
`replace_copy_if` accepts a separate writable scalar output with checked direct
conversions from the input and replacement types. The replacement remains a
reference throughout the scan: predicate-side changes and aliases to an input
or output element remain visible to later stores. Empty ranges still initialize
the predicate parameter and preserve full-expression temporary cleanup.
`copy_if` and `remove_copy_if` also accept the same source predicate objects. Their exact
conditional copy loops and both iterator increments are authenticated; output
storage accepts checked direct scalar conversions. Only kept elements advance
the output iterator. The copy reads the input after the predicate returns, so
allowed predicate-side writes through independently held pointers are visible.
Empty ranges still construct the predicate parameter and preserve caller
full-expression temporary cleanup.
For `copy_if`, the public predicate parameter is passed by reference through
`__copy_if`, the internal invoke calls and the identity projection. The helper's
returned pointer pair, `make_pair`, `move`, `forward`, pair construction and the
public `second` projection retain separate exact signature and source checks.
No additional predicate object or user-visible pair is created by lowering.
`is_partitioned` authenticates both direct predicate call sites and the exact
two-scan control flow. Both scans use the same parameter object and selected
method; the first false element is tested once, then skipped before the tail
scan. A later true result stops immediately. Empty, all-true and all-false
ranges preserve the standard result without extra predicate calls.
`partition_point` authenticates the pinned pointer-distance, positive-half and
advance helpers together with the exact binary-search loop. Each iteration calls
the same predicate parameter on the selected middle element, then narrows the
range with target `ptrdiff_t` arithmetic. Predicate state therefore follows the
SDK's middle-element order. Empty, all-true and all-false ranges preserve their
exact call counts and boundary pointers.
`partition_copy` uses the same predicate storage and authenticates both
conditional stores, each output increment and the returned pair construction.
The true and false destinations may have distinct admitted scalar types, with
independent direct conversions from the input. Each store rereads its element
after the predicate returns. The returned pair preserves the two final output
pointers and composes with supported structured bindings and result queries.
`remove_if` authenticates the exact reference-parameter `find_if` specialization,
its selected predicate call, the subsequent scan and the scalar move stores.
Finding the first removable element and compacting later retained elements use
one predicate object with continuing state. Retained prefix elements are not
assigned to themselves; subsequent moves read their input after the predicate.
The returned pointer ends the retained prefix. Empty/all-kept/all-removed ranges
and full-expression cleanup preserve the same source parameter lifetime.
Source-owned class-template predicates additionally support checked concrete
primary, partial and full class specializations with an in-class non-template
call operator. Type and value parameters, defaults, each concrete receiver's
storage and the selected member definition retain the ordinary class-template
source checks. Different instantiations keep distinct method and record identities.
Out-of-line non-template call operators of namespace primary class templates
also qualify when every original declaration and definition writes a direct
`bool` return and a direct builtin or class type-parameter argument. The written
class qualifier must map each fresh outer parameter directly to the primary;
renamed type/value parameters retain their exact identities. Both signatures
must have no exception specification or plain `noexcept`. Aliases, adjusted
array parameters, computed exception expressions, out-of-line partial/member
specializations and nested template owners still require further source proof.
The actual body must finish ordinary source traversal and be emitted; this
algorithm-only proof does not admit out-of-line operation-trait definitions.
Member function templates, lambda objects, nontrivial copying or
destruction, reference parameters, non-boolean results and function objects in
other algorithm overloads remain outside this increment.
These fourteen algorithms also accept pinned `std::logical_not<T>` and
`std::logical_not<>` predicates over admitted scalar elements. A typed predicate
accepts a checked direct scalar conversion from the input element to its
parameter, including a temporary bound to a const-reference parameter. The
selected SDK operator, its template origins and boolean expression are
independently authenticated, then lowered to a scalar logical negation without
emitting an SDK function. The empty predicate object is still initialized once,
preserving factory calls and full-expression cleanup even for empty ranges.
Typed empty-base aggregate initializers retain their checked effects in the
one-byte functional-object carrier.
Other SDK function objects and other algorithm overloads require separate proofs.
Already instantiated calls support the same result queries below; the SDK operator's
complete proof replaces source-operator dependencies, while caller arguments and
written template arguments still require source completion.
For these fourteen source-object overloads, `decltype`, `noexcept` and queries of
an initialized variable's deduced type can consume an already instantiated,
authenticated algorithm specialization. Each exact call and callee reference
retains the selected source operator's signature and exception dependencies;
caller expressions, written template arguments, constructors and defaults still
require ordinary source completion. Queries do not execute the predicate or its
arguments. The pinned SDK `std::ptrdiff_t` alias supplies target pointer-difference
metadata without requiring user traversal of its internal pointer-subtraction
expression. Query-only calls do not manufacture a missing instantiated body,
and taking an algorithm's address does not inherit a checked call's proof.

The `shuffle` and `sample` component headers are authenticated but their
declarations stay disabled until the random-distribution header closure and
direct lowering are implemented. Quoted includes, user shadow headers and
forged declarations remain rejected.

## Standard template parsing across targets

The embedded frontend disables MSVC compatibility extensions and delayed template parsing for `cpp-core-v2` on every supported target, including Windows x64 and ARM64. Definitions use standard C++17 parsing and lookup rules. Duplicate explicit instantiation definitions, late specializations and incompatible exception specifications retain language diagnostics (`TR0202`); parsed source still receives the existing support checks (`TR0201`). Unused dependent bodies remain lazy until instantiation is needed. This setting does not change project/math profile configuration or the target data layout, and requires no external Clang executable. Full C++/STL support remains unfinished.

Explicit function specializations still require their own definition in this source unit. Repeated declarations do not provide a body and remain `TR0203`, even when the primary template has a definition.

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
and enumerator initializers. A cast to `void` cannot hide an unsupported operand
such as a `long double` expression or an unsupported runtime array-new bound. Diagnostic assertion messages
remain separate from runtime string objects. String literals follow the storage
contract below; `std::string` requires further library and lifetime support.

## String literals and constant arrays

Core v2 admits ordinary, UTF-8, UTF-16, UTF-32 and wide C++17 string literals,
including escapes, raw spelling, adjacent concatenation and embedded zero
characters. Embedded Clang decodes the source. The translator preserves its
character type, exact code units, array bound and terminating zero; target
character widths and alignments must match their existing integer carriers.
It does not convert byte strings into NeverC's UTF-8 `string` type.

An evaluated literal lvalue has static, read-only array storage. Pointer decay,
array references, address formation, offsets, subscripts, source-selected calls,
defaults and templates retain that storage across calls and scope exits. A
literal needs no automatic cleanup or allocation. Separate literal occurrences
may have separate objects; equality between different occurrences is not a
portable pooling guarantee. Discarded and unevaluated literals are still checked
but need no emitted storage when their value/address is unused.

```cpp
const char* message() { return "hello"; }
constexpr char fixed[8] = "world";
int main() {
  char local[8] = {"hello"};
  local[0] = 'H';
  return message()[0] != 'h' || local[0] != 'H'
      || local[7] != 0 || fixed[7] != 0;
}
```

Character-array initialization creates the destination's own elements in order,
including its trailing zero fill. This includes ordinary/braced/parenthesized
initializers, nested arrays, record fields, default member initializers,
constructors and existing record copy/return operations. The source destination
retains its mutability and identity. Implicit conversion of a literal to a
mutable character pointer is rejected as invalid C++17, including the extension
that Clang otherwise diagnoses only with a warning. Explicit casts retain the
existing defined-source-execution contract; they do not make writes to literal
objects defined.

Source-owned const/constexpr namespace arrays, and const records containing
arrays, can use fully defined constant initialization. The frontend evaluates
the actual initialized object, retains every nested array element/filler and
emits one canonical static const object per declaration. Ordinary explicit-bound
redeclarations share its owned definition; missing definitions retain `TR0203`.
Supported numeric, null, callback and trivial record elements use their existing
typed constant representations. No global constructor, destructor or runtime
initialization is invented. All written initializer source remains checked.

Namespace and local/member static arrays also follow the
[fixed-array static storage contract](#fixed-array-static-storage).
Object pointers follow their [static address contract](#static-object-pointer-storage).
Static reference bindings follow their [alias contract](#static-reference-bindings).
Dynamic initialization, global destruction, user-defined literal operators,
and `std::string` allocation/operations remain unfinished.
Existing array extent and generated-node budgets apply; no runtime bounds checks
are added. V1 profiles retain their previous accepted inputs.

Paired source/protocol cases, const/array IR verification and O0/O2 fixtures cover
encoding, zero fill, aliases, persistent addresses, source effects, copying,
lifetimes, canonical globals and relocation. Native results require CI of the
implementing revision; this increment does not establish complete C++/STL.

## C string scans from `<cstring>`

Core v2 admits an exact angle include of the pinned `<cstring>` header. The
authenticated `std::strlen(const char *)`, `std::strcmp(const char *, const char *)`
and `std::strncmp(const char *, const char *, std::size_t)` calls lower to direct
byte scans. Each argument is evaluated once. `strlen` stops at the first zero
byte; `strcmp` and `strncmp` compare as unsigned bytes and return a negative,
zero or positive `int`. A zero `strncmp` count reads neither string. No runtime
libc string call is emitted.

Only the `std::` names introduced by the pinned header are admitted. Global
`::strlen` and `::strcmp`, other `<cstring>` functions, quoted or shadow headers,
and function addresses remain outside this boundary. The caller still supplies
zero-terminated strings to `strlen` and `strcmp`, and readable character arrays
through the bytes examined by `strncmp`. This does not add `std::string`.

Eight-target protocol checks and O0/O2 execution cover the direct lowering,
unsigned comparison order, prefix and zero-count behavior, and one-time
argument evaluation.

## String-view header and metadata from `<string_view>`

Core v2 admits an exact angle include of the pinned C++17 `<string_view>`
header. Its 240-file libc++/resource closure is identical on all eight core
targets and uses no platform SDK header. The closure exposes
`std::string_view`'s `size_type`, `npos`, and constant size/alignment queries.
The parsed object has the libc++ two-field pointer-and-size layout.

NeverC's SDK supplies the target C runtime `mbstate_t` declaration required by
the pinned `char_traits` header: 128-byte, 8-aligned on Darwin, and 8-byte,
4-aligned on the supported glibc and UCRT targets. A minimal C `stdio.h` shim
provides `EOF` and a C-linkage `remove(const char*)` declaration for the
`<string>` header's overload set. Neither shim admits a top-level C header or
runtime C I/O call.

The exact `std::basic_string_view<char, std::char_traits<char>>` specialization
admits the pinned pointer-and-size layout. Default, copy, pointer-and-length,
and zero-terminated pointer construction, plus copy assignment, lower directly
to field stores and a byte scan where needed. `size()`, `length()`, `empty()`
and `data()` read the verified fields without an SDK runtime call. The pointer
argument is evaluated once; pointer-and-length construction preserves embedded
zero bytes. `begin()`, `cbegin()`, `end()` and `cend()` expose the verified
`const char*` range, including range-for iteration. `operator[]`, `front()` and
`back()` return read-only character elements. `remove_prefix()`,
`remove_suffix()` and member `swap()` update the view fields directly; each
receiver and argument is evaluated once. `rbegin()`, `crbegin()`, `rend()` and
`crend()` return the authenticated reverse iterator over the same range.
`max_size()` reports the target `size_t` maximum. `compare(string_view)`
compares unsigned character values lexicographically, including embedded zero
bytes. The six free `==`, `!=`, `<`, `>`, `<=` and `>=` operators use the same
ordering and accept the pinned view's ordinary implicit pointer construction.
`find(char, size_t)` and its zero-position default return the first
matching offset or `npos`. `find(string_view, size_t)` searches for the first
matching byte sequence, while `rfind(char, size_t)` and
`rfind(string_view, size_t)` search backward. Their omitted positions use the
pinned zero or `npos` defaults. Empty patterns and out-of-range positions
follow libc++'s view-search results. The caller remains responsible for the
ordinary view lifetime, readable-range and valid-index preconditions.
Custom traits, other character types, throwing `at()`, and other string-view
operations still require separate direct lowerings.

## String header and metadata from `<string>`

Core v2 admits an exact angle include of the pinned C++17 `<string>` header.
With reduced transitive includes, its 286-file libc++/resource closure is
platform-free. The public header and `__ios/fpos.h`,
`__string/extern_template_lists.h`, and `__utility/scope_guard.h` retain their
LLVM 20.1.8 source bytes and catalog hashes. Clang can fold constant
`std::string` size and alignment queries and `npos`, and resolve its
`size_type` alias. Authenticated `std::basic_string<char, std::char_traits<char>,
std::allocator<char>>` objects now have direct lowering for default,
`const char*`, pointer-and-length, copy and move construction; copy and move
assignment; `size`, `length`, `capacity`, `empty`, `data`, `c_str`, subscript
access, mutable and const `front`/`back`, `clear`, `push_back(char)`,
`pop_back()`, `reserve(size_type)`, both `resize` overloads, and destruction.
The `append(const char*, size_type)`, `append(const char*)`, and
`append(size_type, char)` overloads and `append(const std::string&)` also lower
directly and return the receiver reference. Pointer and string append copy
self-referenced source bytes before releasing storage during growth; the
C-string overload scans to the first NUL.
The `operator+=` overloads for `char`, `const char*`, and `const std::string&`
reuse those paths and return the receiver reference.
Member `swap` and `std::swap` exchange the authenticated representation words
without allocating and preserve each long string's buffer ownership.
The `assign(const char*)`, `assign(const char*, size_type)`,
`assign(const std::string&)`, and `assign(size_type, char)` overloads reuse
capacity when possible and return the receiver reference. Pointer assignment
copies self-referenced bytes before changing size or releasing old storage.
Positional `erase(pos, count)` and its default arguments remove bytes in place,
retain capacity, and return the receiver reference.
The frontend checks the pinned libc++ representation before emitting three
storage words, including the alternate short-string layout selected on Apple
arm64. Short strings stay inline, while long strings use the selected
allocation and release functions. Copy construction owns independent storage;
copy assignment reuses existing capacity when possible. Moves transfer the
representation and leave the source empty. Clearing, popping, and shrinking
retain existing capacity and storage. Pushing, resizing, reserving, and
appending grow storage when needed. Host O0/O2 fixtures exercise both
representations, embedded NUL, mutable and const access, copy and move,
push/pop, resize/reserve, fill, pointer and string append with self-reference,
the three `operator+=` overloads, four `assign` overloads, positional erase,
member/free swap, clearing, and lifetime release; all eight supported target
triples pass frontend translation. Other modifiers, character or allocator
types, and throwing length/allocation paths remain unsupported.
Quoted and shadow headers remain rejected.

## Vector header and metadata from `<vector>`

Core v2 admits an exact angle include of the pinned C++17 `<vector>` header.
With reduced transitive includes, its 300-file libc++/resource closure is
identical and platform-free on all eight core targets. The public header and
the `__bit_reference`, `__vector/pmr.h`, and `__vector/vector_bool.h` files
retain their LLVM 20.1.8 source bytes and catalog hashes. For the default
allocator specialization, Clang can fold `std::vector<int>` size and alignment
queries from libc++'s three-pointer layout and resolve its `size_type` alias.
Authenticated `std::vector<T, std::allocator<T>>` objects with non-boolean
integer or floating elements use direct lowering for default, bounded
count/fill/list, copy and move construction; copy and move assignment;
destruction; size, capacity, empty, data, element/front/back access; clear,
push/pop, begin/end and const iteration; and reserve/resize. Growth preserves
element values and the selected allocator calls, and copy construction owns
independent storage. Host O0/O2 fixtures check capacity reuse, reallocation,
aliased fill arguments and eventual release. Record elements, other allocators,
remaining vector methods and throwing allocation or length-error paths remain
unsupported. Quoted and shadow headers remain rejected.

## Dynamic local static initialization

Admitted non-constexpr functions can dynamically initialize local static numeric,
boolean, enum, null, object-pointer, callback, record and fixed-array objects,
including const objects and records/array elements with admitted static destruction.
The original initializer stays in the declaring function and may
use parameters, automatic locals and its actual `this`. Constant initialization
keeps its existing eager data representation; a required destructor adds only
first-passage registration under a guard.

Local static lvalue/rvalue references can also bind existing objects through
parameters, pointer loads, conditional glvalues and owned reference-returning
calls. Their readonly binding carrier is zero-initialized pointer storage;
the first-use region assigns the selected address, not a value into the referent.
Later calls reuse that binding. Const pointees, arrays, callback slots and
template instances retain their normal types and identity. The binding does not
extend the lifetime of an existing object. Temporary call arguments retain
ordinary full-expression cleanup. A temporary whose lifetime extends to that
reference is constructed in permanent storage inside the same first-use region;
see [dynamic temporary lifetimes](#dynamic-local-static-temporaries). A partial
value retained by failed constant evaluation never replaces runtime effects.
The static-initializer check retains proven null-reference, out-of-bounds and
expired-temporary diagnostics before selecting dynamic initialization. Merely
reading runtime state still permits the dynamic path, including a local static
binding to a live parameter. Empty declarations between owned declarations or
class members are accepted and do not create storage or instructions.

```cpp
int constructions = 0;
struct Value {
  int number;
  const Value *self;
  Value(int n) : number(n), self(this) { ++constructions; }
};
const Value &value(int n) {
  static const Value object(n);
  return object;
}
int main() {
  const Value &a = value(7);
  return a.number != 7 || a.self != &a || &value(9) != &a || constructions != 1;
}
```

Storage is zero-initialized before any first-use evaluation. First passage through
the declaration runs initialization in that actual storage and completes its
full-expression temporary cleanup before publication. Later passages skip the
entire initializer, including reads of automatic locals. Concurrent entrants wait
and acquire the completed object's effects. Recursive entry into the same object
while it is initializing has undefined source behavior; calling a function that
initializes a different object is supported. These first-use, concurrency and
recursive-entry rules follow [C++17 declaration statements](https://timsong-cpp.github.io/cppwp/n4659/stmt.dcl#4).

Each canonical declaration has one object and one guard, also across template
aliases and repeated calls; distinct instances retain distinct storage. An
unreachable declaration may retain zero storage without an emitted initialization
site. No automatic shadow or lexical destructor is introduced for the static
itself. Array and record initialization uses existing field/element sequencing,
constructor destination and normal full-expression cleanup rules.

Core-v2 IR marks the zero global with `dynamic_initialization: true`. Explicit
`static_init_begin` and `static_init_end` instructions acquire and publish its
initialization. The consumer proves initialization ownership over the CFG before
checking expression permissions. Only the granted region can directly write or
form a mutable address of a const dynamic global. Physical C23 storage is mutable
during construction; source const accesses and ordinary pointer qualifiers remain
checked. This is a direct-root permission check, not an alias nonescape proof:
constructor `this` may escape, and later mutation of a source-const object through
such an alias retains undefined C++ behavior.

The independently constructed NeverC target must support always-lock-free 32-bit
unsigned int atomics. Generated guards use `__c11_atomic_*` with acquire reads/CAS
and release publication, without headers or foreign runtime functions. On AArch64
the enter helper carries `target("no-outline-atomics")` and `noinline`, preventing
outlined CAS dependencies even when subsequent compilation enables outlining.
Generated assertions check atomic capability and storage layout. Protocol/IR
checks, O0/O2 execution, sixteen-thread visibility tests and assembly checks for
runtime helper dependencies require native validation at the implementing CI head.

TLS and exception propagation/retry still require implementation. Static objects
use the [registered destruction contract](#static-destruction). Nonlocal objects follow the [startup contract](#nonlocal-dynamic-initialization). Unsupported throwing source and
unowned callees remain diagnosed; there is no substitute termination or fake
success path. Actual standard headers and complete C++/STL remain unfinished.

## Static destruction

Core v2 registers destruction of admitted complete static records, fixed arrays
and lifetime-extended static temporaries. This includes const objects, local and
nonlocal definitions, class/variable-template instances and implicit containing
record destruction. The source destructor must pass the same owned-body, type,
layout and selected-template checks as ordinary automatic destruction.

Each complete object registers its internal `void()` cleanup immediately after
construction. A complete root registers before its initializer's ordinary
full-expression cleanup; each permanent child registers separately when that
child finishes. The root guard publishes after ordinary cleanup. This preserves
interleaving when another static completes between a child and its parent, or
inside an argument destructor. Arrays register one cleanup for the complete
array and destroy elements in reverse order; record helpers retain reverse member
cleanup. No static object receives automatic lexical cleanup.

Constant initialization and runtime registration are distinct: a complete constant
root keeps its constant value and uses a once guard only for registration. A local
root registers at first passage; a nonlocal root registers through native startup.
C++17's checked constant-initializer evaluation rejects a static materialized
temporary whose complete type requires destruction, including recursive arrays,
fields and reference paths. Such temporary objects take the dynamic path; retained
partial APValues never authorize constant construction or teardown. A reference
to a named static object does not register another destruction for that referent.

Native Linux and Darwin use `__cxa_atexit` and the module's hidden `__dso_handle`.
MSVC Windows uses `atexit`; x86 declarations, callback types and thunks explicitly
use the C calling convention. Real matching-signature thunks call checked internal
cleanup functions. This preserves native exit-callback order and ordinary module
unload ownership. Registration return values follow pinned Clang's existing
ignored-result behavior; there is no invented retry or exception path.

The consumer independently selects the hosted target ABI. Saved NC checks hosted
compilation and the MSVC target ABI marker independently of compatibility-version
flags. DynCode compilation is explicitly rejected, including a unit containing
only local statics. Manual loaders without the ordinary CRT module lifecycle are
unsupported. Storage requiring a destructor is physically writable, while source
const accesses remain checked. Cleanup obtains a const address and uses the
existing explicit cv pointer cast for the destructor receiver; registration-only
regions grant no new const-write permission.

Paired source/protocol and malformed-IR cases cover registration ownership,
constant data, callback signatures and native ABI selection. O0/O2 fixtures cover
C `atexit` interleaving, ordinary full-expression cleanup, parent/child completion,
reverse arrays/members, conditional and never-called locals, initialization from
a destructor, placement reconstruction and template identity. Separate native
library load/unload and sixteen-thread first-use fixtures cover module ownership
and once-only registration. These fixtures require the implementing CI revision;
no native result is implied by source-only checks. TLS, exception unwinding,
default heap runtime, standard headers and complete C++/STL remain unfinished.

## Nonlocal dynamic initialization

Core v2 initializes admitted nonlocal scalar, pointer, callback, record, fixed-array
and reference objects before `main`, including const objects and materialized
class/member/variable-template instances. Zero and required constant initialization
precede all dynamic initialization. The frontend preserves Clang's native
point-of-definition constant-initialization classification and requires a fully
defined checked value; successful end-of-unit evaluation alone cannot promote a
dynamic initializer. Trivial default construction retains static zero initialization.

A single internal startup function runs checked dynamic initializers in definition
order within the source unit. Original source locations preserve declaration order
inside macro expansions. This selects a permitted order for unordered template
instances and preserves required ordered/partially ordered relationships. Merely
declaring a global earlier does not move its later defining initializer earlier.
Unused nonlocal definitions still initialize; unused template instances remain lazy.
Local static declarations keep their existing first-use behavior, including when
called from startup. Every initialized object keeps its actual static address.

```cpp
int calls;
int seed() { return ++calls; }
const int first = seed();
const int &second = seed();
int main() { return first != 1 || second != 2 || calls != 2; }
```

Each full expression cleans its ordinary temporaries before publishing that
object and starting the next initializer. Lifetime-extended temporaries are built
in their existing permanent child storage. Source-defined single-object allocation
can run in startup; missing default heap functions still require a runtime.
TLS, exceptions/unwind, actual standard headers and
complete C++/STL remain unfinished.

The optional core-v2 module `startup` identifier names an independently checked
internal `void()` definition. Generated C23 gives only that function the native
`constructor` attribute and checks attribute support. Existing NeverC native object
startup executes it when a program starts or an ordinary hosted module is loaded,
including when a separately compiled C client supplies `main`. This adds no public
startup API. Manual loading and DynCode are separate boundaries: the current
DynCode IR stage still rejects `llvm.global_ctors`. Normal source types, definition
ownership, initialization CFG permissions and const rules remain checked.

Paired source/protocol and forged-IR cases, O0/O2 program execution and a separate
C-client link require native validation on the implementing CI revision. The
ordinary NeverC source language remains C23; this is C++17 input translation.

## Fixed-array static storage

Core v2 admits complete fixed arrays with static storage at namespace scope,
inside ordinary supported functions, and as defined class static data members.
Mutable and const arrays retain their source access rules. Definitions may use
zero initialization or fully defined constant initialization, including nested
arrays, character strings, numeric/null/callback values and admitted record
elements that require no destruction. Source qualifiers, record layout, array
extents and initializer operations retain their normal checks.

```cpp
int counters[2];
struct Labels {
  inline static constexpr char title[8] = "counter";
};
template<int N> int* state() {
  static int values[2] = {N, 0};
  return values;
}
int main() {
  ++counters[0];
  state<3>()[1] = counters[0];
  return state<3>()[1] != 1 || state<5>()[1] != 0;
}
```

Each canonical declaration or admitted concrete template instance has one
permanent array. Re-entering a function, branch, loop or switch does not allocate
or initialize it again. Array references and pointers retain that storage after
the declaring function returns. Static class members add no instance fields;
access through a receiver retains its evaluated effects and temporary cleanup.
Destruction of a receiver does not destroy a static array.

Class static arrays and namespace/member variable templates retain the existing
source ownership, specialization, instantiation and lazy-initializer checks.
Equivalent template arguments share storage; distinct instances keep separate
arrays. Ordinary explicit-bound redeclarations and out-of-line definitions must
resolve to the same owned definition. A required missing definition is `TR0203`,
including const array members that cannot use scalar declaration-only values.

The frontend evaluates each actual initialized object once and serializes all
elements, including static zero fill. Mutable arrays use the existing global
`mutable` permission; const arrays and literal objects remain read-only. Local
constant-initialized declarations emit no automatic shadow or repeated element
stores; arrays requiring destruction use a guard only for registration. Arrays cannot be assigned as whole values.

Dynamic local arrays follow the [first-use contract](#dynamic-local-static-initialization).
Nonlocal arrays follow the [startup contract](#nonlocal-dynamic-initialization).
TLS and volatile storage still need further support. Records follow their [static object contract](#static-record-objects). Object-pointer elements follow the
[static address contract](#static-object-pointer-storage). Standard headers and full STL remain
unfinished. Paired source/protocol cases, IR checks and O0/O2 fixtures cover
shared state, initialization, nested elements, aliases, template identity,
receiver effects and relocation; native results require the implementing CI.

## Static object-pointer storage

Core v2 supports zero or fully constant initialization of object pointers at
namespace scope, in function-local statics, and in defined class static members,
including admitted concrete class and variable templates. Pointers may designate
null, source-owned static objects, string literals, array elements or record
fields. Const pointer objects retain their fixed value; mutable pointers can be
reseated. Pointee constness and source-selected conversions remain distinct from
the mutability of the pointer object itself.

```cpp
extern int value;
int* pointer = &value;
int value = 3;
int values[3] = {4, 5, 6};
int* end = values + 3;
const char* message = "ready";
int main() {
  *pointer = 7;
  return value != 7 || end[-1] != 6 || message[4] != 'y';
}
```

The frontend retains the constant evaluator's actual target declaration or
literal, typed field/array path and exact byte offset. Array indices must be in
range, with a final one-past address permitted. The separate one-past state of
a complete object is preserved too. Intermediate one-past subobjects, arbitrary
integer addresses and automatic or thread-local storage are not admitted as
constant targets. Selected definitions must exist in this unit; missing owned
targets or required pointer definitions report `TR0203`.

Constant pointer expressions can refer to later definitions, to the pointer
object itself through an admitted type conversion, or to another static
pointer's address. A constexpr pointer value or constexpr function can supply
the address, while loading a mutable pointer at startup follows the separate nonlocal dynamic
initialization contract. Nested pointer arrays, const record
values and statically initialized arrays of records may contain these symbolic
addresses. The existing source checks cover all written expressions and types,
including folded, unused and instantiated source.

The IR uses checked `address`, `member`, `index`, `array_decay`, pointer `cast`
and `null` nodes. Constant address validation is separate from runtime value
loads. The emitter introduces internal storage declarations when needed for
forward addresses and emits ordinary C23 address constants; no runtime pointer
helper is called from a global initializer. Array and field paths retain their
typed layout instead of becoming unchecked byte offsets. Runtime pointer
operations continue to use their existing sequencing and arithmetic rules.

Canonical static identity, receiver effects, temporary cleanup and separate
template-instance state follow the existing storage contracts. Static reference
bindings follow their [alias contract](#static-reference-bindings).
Dynamic local pointers follow the [first-use contract](#dynamic-local-static-initialization).
Nonlocal pointers follow the [startup contract](#nonlocal-dynamic-initialization).
TLS, default heap runtime,
standard-library headers/runtime still require further work. Mutable records
follow their [static object contract](#static-record-objects). Paired source/protocol cases, malformed-address IR cases,
relocation and O0/O2 fixtures require native validation in implementing CI.
This increment does not establish complete C++/STL.

## Static reference bindings

Core v2 admits constant bindings of namespace references, function-local static
references and defined class static references, including admitted concrete
class/variable templates. Lvalue references and rvalue references can alias
existing source-owned static objects, their array/record subobjects, and string
literal storage. Chained references and constexpr calls/conversions retain the
actual target; no copy of the referred object is introduced.

```cpp
int value = 3;
int& alias = value;
int values[2] = {4, 5};
int (&array)[2] = values;
const char (&text)[4] = "cat";
int& local() {
  static int& binding = value;
  return binding;
}
int main() {
  alias = 7;
  local() = 8;
  array[1] = 9;
  return value != 8 || values[1] != 9 || text[1] != 'a';
}
```

The initializer is evaluated in the context of its actual reference definition.
Its canonical declaration, owning scope, type and source initializer must agree.
The resulting address must designate permanent storage, with the same typed
subobject/offset checks as static pointers. Null and one-past addresses cannot
bind references. Missing required reference or target definitions retain
`TR0203`; source qualification errors retain `TR0202`.

Each reference uses one immutable internal pointer carrier. Reading, assigning,
taking an address, returning an alias or selecting a member dereferences that
carrier and accesses the original object. Assigning through a reference cannot
reseat its binding. Referred-to constness, array extents, pointer-object constness
and template-instance identity remain intact. Class static receivers still
produce their source effects and temporary cleanup; the reference owns no
referred object and introduces no destruction or block-entry initializer.

Automatic objects, runtime pointer loads/calls, TLS, null/one-past addresses,
integer-derived pointers and unsupported source types cannot provide these
constant bindings. Constant-initialized static temporaries follow the contract
below. Nonstatic reference members follow their [binding contract](#reference-members).
Function references and reference non-type template arguments still require further work.
Nonlocal dynamic bindings follow the [startup contract](#nonlocal-dynamic-initialization).
Local runtime bindings and their lifetime-extended temporaries use the
[first-use contract](#dynamic-local-static-initialization). Actual standard-library
headers/runtime remain unfinished. Native validation of the paired
source/protocol cases, const-carrier IR checks, relocation and O0/O2 fixtures
requires implementing CI. Complete C++/STL remains an active goal.

## Static reference temporary lifetime extension

Core v2 supports temporary objects whose lifetimes C++17 extends to a namespace,
function-local static or class static reference, including admitted concrete
class and variable templates. Namespace and class references support checked
constant initialization or [dynamic startup](#nonlocal-dynamic-initialization);
local static references support the dynamic initialization below. The temporary must have trivial destruction. Scalars, fixed
arrays and admitted records retain their complete storage, including when the
reference names only a field, array element or multidimensional row.

```cpp
struct State {
  int value;
  int* address;
  constexpr State() : value(3), address(&value) {}
};
State&& state = State();
const int (&values)[3] = {4, 5};
int& local() {
  static int&& value = 6;
  return value;
}
int main() {
  *state.address = 7;
  local() = 8;
  return state.value != 7 || local() != 8 || values[2] != 0;
}
```

The materialization descriptor must identify the exact extending declaration,
initializer operand and static storage duration. Constant evaluation occurs in
that owner's initialization context; the translator reads Clang's retained
complete-object value instead of reevaluating its operand independently. Each
materialization receives one internal static object. Its identity is registered
before its fields are emitted so self pointers name the same object. Copying a
record keeps stored pointer values; it does not retarget them to the copy.

The temporary's actual type determines mutability independently of the reference
carrier, which remains immutable. Rvalue-reference objects can remain writable;
const temporary objects remain read-only. Distinct materializations and distinct
template instances have distinct addresses. Aliases share the same temporary,
and function reentry does not reinitialize local static storage. Pointer and
reference paths retain the existing subobject, const and byte-offset validation.

Constant initialization introduces no runtime initializer or guard.
Nontrivial static temporaries use [dynamic registration](#static-destruction); TLS
still requires further work.
Binding through a function call or pointer arithmetic does not
invent lifetime extension; unsupported source remains checked even when folded
or unused. Paired source/protocol tests cover admission and rejection, retained
self addresses, const permissions and relocation. O0/O2 tests cover persistent
state and object identity; native acceptance requires CI of this implementation.
Complete C++/STL remains unfinished.

### Dynamic local static temporaries

A local declaration such as `static const R &r = make(argument);` constructs
its complete temporary in permanent storage under the reference's first-use
guard. Scalar, pointer, callback, null, record and fixed-array temporaries retain
their admitted source types and use [registered destruction](#static-destruction)
when required. The exact
Clang materialization descriptor, canonical extending declaration and static
storage duration establish ownership. References to fields and array elements
retain the complete temporary, following [C++17 temporary lifetime rules](https://timsong-cpp.github.io/cppwp/n4659/class.temporary).

The binding and its temporary children start with semantic zero. Runtime lowering
initializes each actual child from its original operand; a failed constant
evaluation's retained partial APValue is never consumed. Constructors receive the
final destination, preserving self pointers. One outer materialization around a
prvalue conditional shares one destination; materializations inside glvalue arms
have separate destinations and initialize only on their selected branch. An
unselected child may remain zero, including when the binding selects an existing
object. Each lowered runtime occurrence receives distinct permanent storage,
including shared array-filler ASTs evaluated for different elements.

IR children name their independent dynamic owner through `initialization_owner`: a
readonly reference-binding pointer, or an admitted record/fixed-array object
containing reference members. Nested temporary children name the same root owner.
One guard publishes the whole group after binding and ordinary full-expression
cleanup. The verifier grants direct-root construction access only within that
owner's proven region; pointer aliases retain their ordinary const checks. Static
children never acquire lexical or full-expression destruction. Binding through
a reference-returning call does not extend its argument temporary's lifetime.

Constant temporary serialization separately requires successful evaluation of
the exact owner, including Clang's positive constant-initialization evidence for
later static member definitions. Merely retaining an APValue grants no access.
Nontrivial constant temporary allocation remains rejected by the C++17 constant
initializer proof; dynamic temporaries register their own destruction when completed.
Unsupported destructor source remains diagnosed in dead or unselected source.
Paired source/protocol, malformed IR, relocation, O0/O2 and concurrent-reader
fixtures require native validation at the implementing CI revision.
Exceptions/retry, TLS, default heap runtime and real
standard-library support remain unfinished parts of the full C++/STL goal.

## Static record objects

Core v2 admits source-owned static records with checked constant initialization
and [registered destruction](#static-destruction), at namespace scope, in function-local statics and in
defined class static members, including admitted class/variable templates.
Mutable records support updates to their existing object; const records and
const fields retain the source language's access rules. Nested records, fixed
arrays, strings, object pointers and callback fields use their existing typed
storage contracts.

```cpp
struct State {
  int value;
  int* address;
  constexpr State() : value(3), address(&value) {}
};
State state;
State& local() {
  static State value;
  return value;
}
int main() {
  *state.address = 4;
  local().value = 5;
  return state.value != 4 || local().address != &local().value;
}
```

Initialization evaluates the actual canonical definition, preserving constexpr
constructor/default-member semantics and addresses of the object itself or its
fields. Copy initialization retains the source pointer values: copying a record
with a self pointer does not retarget that pointer to the new copy. Pure trivial
default construction at static storage receives the language-required zero
initialization, including nested scalar, pointer and array fields; this rule
also applies to statically stored arrays of trivial records. It never supplies
values to uninitialized automatic objects.

Namespace redeclarations must resolve to one owned definition with the same
type. Equivalent template instances share storage; different instances retain
separate state. Static declarations create no automatic shadow or repeated
initializer. Receivers of class static members still execute their source effects
and temporary cleanup. Existing references, pointers and assignment operations
continue to access the same object. IR layout, field types, folded leaves and
mutability are checked before emitting internal C23 record objects and any
forward declarations needed for self addresses.

Local nonconstant constructor calls and initializer reads follow the
[first-use contract](#dynamic-local-static-initialization). Nonlocal objects use
[dynamic startup](#nonlocal-dynamic-initialization). TLS, volatile objects,
unsupported layouts/fields and default heap runtime remain
outside this increment. Exception unwinding and actual
standard-library headers/runtime still require
further work. Paired source/protocol cases, mutable/self-address IR cases,
relocation and O0/O2 fixtures require native validation in implementing CI;
complete C++/STL remains unfinished.

## Binary floating-point values

Core v2 admits IEEE binary32 `float` and binary64 `double` in values, aliases,
parameters/results, locals, references, object pointers, fixed arrays, record
fields and supported callback signatures. Clang selects source overloads and
conversions before lowering. Arithmetic `+`, `-`, `*`, `/`, comparisons, boolean
conversion, increments and compound assignments preserve those selected types
and source effects. Mixed operations explicitly convert their operands; a
compound assignment converts its computed result back to the stored type.
Supported integer/floating conversions follow C++ rules, including rounding and
truncation. Out-of-range floating-to-integer conversions gain no invented
saturation behavior.

```cpp
double total = 0.5;
float add(float value) {
  static float calls = 0.0f;
  ++calls;
  total += value;
  return calls;
}
int main() {
  return add(1.25f) != 1.0f || total != 1.75;
}
```

Zero or checked constant initialization supports floating globals, scalar static
locals, defined static data members and admitted concrete variable templates.
Their addresses and values persist through the existing scalar storage model.
Automatic scalar locals keep their source initialization semantics. Source
expressions in defaults, assertions, size queries and materialized template
arguments remain inspected even when their result is folded or discarded.

Literals and folded constants carry exact 32/64-bit hexadecimal representations,
including signed zero, subnormals, infinities and NaN sign/payload/quiet bits.
Finite NC literals use exact hexadecimal spelling; special values use typed
compiler constants. Carrier widths, ABI alignments and record offsets are
checked independently against NeverC and asserted in generated code.

The execution contract uses round-to-nearest, masked floating traps and gradual
underflow, without FTZ/DAZ. Arithmetic evaluates in its declared type with no
implicit fused multiply-add contraction. The embedded source frontend disables
fast math and contraction; generated code disables contraction and rejects
fast-math/finite-math-only or excess-precision settings. Source floating-
environment pragmas/APIs, `long double`, complex types and standard headers remain
outside this increment. Dynamic static values follow the first-use and startup contracts. The existing
math v1 mapping contract is unchanged; core v2 does not inherit its SDK or calls.

Paired source/protocol fixtures and independent IR cases cover representations,
conversions, layout, malformed payloads and retained source checks. O0/O2 fixtures
exercise rounding, subnormals, signed zero, noncontracted arithmetic, aliases,
static persistence, callbacks and record lifetimes. Native results require CI
of the implementing revision; complete C++/STL remains unfinished.

## Null pointer values

Core v2 admits the distinct C++ `decltype(nullptr)` type in aliases, deduced
values, parameters/results, locals, references, object pointers, fixed arrays,
record fields and supported callback signatures. Equality, source-selected
overloads, contextual or explicit boolean conversion, and conversion to admitted
object/function pointer types follow embedded Clang's C++17 rules. A null value
always compares equal to another null value; separate objects still have their
own storage and addresses.

```cpp
using Null = decltype(nullptr);
int calls = 0;
Null make_null() { ++calls; return nullptr; }

int main() {
  Null first{}, second = nullptr;
  int *pointer = make_null();
  return calls != 1 || pointer != nullptr || &first == &second;
}
```

Null-to-pointer conversions evaluate the source expression before producing the
typed null pointer. Calls, indirect calls, selected user conversions, comma and
conditional expressions, receiver effects and temporary cleanup are retained.
The lvalue-to-rvalue conversion of a null object evaluates the object expression
and yields the null value, without loading its stored representation.

Zero or checked constant initialization is supported for null globals, scalar
static locals, static data members and admitted concrete variable templates.
Equivalent template instances share their object; other instances remain distinct.
Dynamic local null objects follow the [first-use contract](#dynamic-local-static-initialization).
Nonlocal null objects follow the [startup contract](#nonlocal-dynamic-initialization).
Thread-local/volatile storage remains unsupported. `nullptr_t` non-type arguments
follow the [template value contract](#nullptr-template-values). All written types, initializers,
defaults and unevaluated expressions retain source checks; folding to null does
not hide unsupported operations. Standard headers, including `<cstddef>`, are
still outside this single-source profile; `using Null = decltype(nullptr)` needs
no header. Complete C++/STL support remains unfinished.

The IR uses the distinct scalar spelling `nullptr` and a payload-free `null`
expression. Generated C23 uses `typeof(nullptr)` declarations and `nullptr`
values. Source size/alignment are checked against the independent default-pointer
carrier layout, including record field offsets, and generated code asserts the
C23 carrier size/alignment. Paired source/protocol cases, forged-IR checks and
O0/O2 execution fixtures require native CI from the implementing revision.

## Nullptr template values

Core v2 admits concrete `decltype(nullptr)` non-type template arguments, including
an alias for that type, C++17 `auto`, dependent `T N`, selected scalar defaults and
bounded empty or nonempty packs. Existing function, class, alias, variable, member,
partial/full specialization and explicit-instantiation source rules still apply.

```cpp
using Null = decltype(nullptr);
template<auto N> int &slot() { static int value = 3; return value; }
template<class T, T N = nullptr> T selected() { return N; }
int main() {
  return selected<Null>() != nullptr ||
         &slot<nullptr>() != &slot<(sizeof(int), nullptr)>() ||
         &slot<nullptr>() == &slot<0>();
}
```

The selected Clang argument must be `NullPtr` with concrete `nullptr_t` type;
pointer-typed null arguments, function pointers, references and class values remain
unsupported, including through `auto` and dependent parameter types. A replacement
must match the selected argument's exact type and evaluate to an APValue null,
never an integer zero. Equal null arguments share canonical instances and storage;
integer `0`, boolean `false` and `nullptr` have distinct deduced template types.

Every retained written or converted argument, default and parameter type keeps
its source checks, including ignored alias arguments, repeated canonical uses,
copied members and partial patterns. Authenticated SDK calls, including
`get`, allocator member templates and `to_integer`, also inspect their written
template arguments before using the selected library operation. Unsupported
expressions and declaration-valued alias arguments cannot disappear through
constant folding or type deduction, including in unevaluated calls.
Null arguments explicitly traverse their
original expression because pinned Clang's default visitor skips that argument
kind. A missing retained expression is rejected; the producer does not invent
source evidence from the canonical value. Folded `sizeof(long double)` syntax
remains rejected. Existing pack/depth and expansion limits remain unchanged.

The existing typed `null` expression and `nullptr` IR type carry these values;
no runtime template argument or additional wire operation is introduced. Paired
source/protocol tests cover 33 accepted, 20 source-boundary rejection and 5 invalid-C++ cases, with saved-NC O0/O2
execution, canonical storage identity and relocation checks. Native validation
requires CI of the implementing revision. Standard headers and complete C++/STL
remain unfinished.

## Integer widths, characters and size queries

Core v2 admits signed and unsigned 8-, 16-, 32- and 64-bit integer storage.
This covers `char`, `signed char`, `unsigned char`, `short`, `int`, `long`,
`long long`, their unsigned forms, `wchar_t`, `char16_t` and `char32_t`.
`bool` keeps its distinct boolean representation. Enums may use any admitted
integral underlying type, including `bool`; opaque fixed-underlying enums are
checked before erasure. Ordinary and wide/UTF character literals use the values
resolved by the pinned Clang frontend. String literals use the permanent
constant-array storage described in [String literals and constant arrays](#string-literals-and-constant-arrays).

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
expression-form alignment are rejected. Resolved `sizeof...` follows the
[parameter-pack contract](#concrete-parameter-packs).

## Builtin type classification

The pinned source frontend's resolved boolean classification queries are admitted
for supported operand types. They preserve C++ type identity before translation
to C carriers: an enum differs from its underlying integer, `long` differs from
`long long` even at equal width, references differ from pointers, and `noexcept`
function types differ from potentially throwing function types.

| Classification | Builtin spellings |
| --- | --- |
| Arithmetic | `__is_arithmetic`, `__is_floating_point`, `__is_integral`, `__is_signed`, `__is_unsigned` |
| Type categories | `__is_void`, `__is_fundamental`, `__is_object`, `__is_scalar`, `__is_compound`, `__is_enum`, `__is_class`, `__is_union` |
| Arrays, pointers and functions | `__is_array`, `__is_pointer`, `__is_function`, `__is_member_pointer`, `__is_member_object_pointer`, `__is_member_function_pointer` |
| References and qualifications | `__is_reference`, `__is_lvalue_reference`, `__is_rvalue_reference`, `__is_const`, `__is_volatile` |
| Exact source type identity | `__is_same`, including its Clang alias `__is_same_as` |
| Record and object properties | `__is_aggregate`, `__is_empty`, `__is_standard_layout`, `__is_trivial`, `__is_trivially_copyable`, `__is_pod`, `__is_polymorphic`, `__is_abstract`, `__is_final`, `__is_literal`, `__has_unique_object_representations` |
| Class relationship | `__is_base_of` |
| Destructibility | `__is_destructible`, `__is_trivially_destructible` |

Each operand retains its written type source and must satisfy the existing type
contract, including `void` for queries. Bare function types and function aliases
use the admitted callback signature contract; normal parameter adjustments are
preserved. Function-type template arguments and references to functions retain
their existing restrictions. Required class completeness follows the source
language, while unused dependent member bodies remain lazy. No constructor or
destructor runs as a result of a query. Record properties describe the C++ type:
a reference-bearing record is not standard-layout even when its C carrier is;
a user-provided default constructor can coexist with trivial copying.
`__is_base_of` includes supported indirect and private empty bases, compares the
same class independently of cv-qualification, and does not require a pointer
conversion to be accessible.

Unknown-bound arrays such as `int[]` and `int[][3]` also have checked type-only
metadata, including pointer/reference wrappers, aliases and template arguments.
Their known inner dimensions and admitted element types remain checked;
no unknown count or runtime carrier is invented. Direct array queries retain
element layout-source dependencies; pointers/references keep the existing policy
of checking type/bound source without consuming pointee record layout. Ordinary
and trivial destructibility preserve the incomplete-value false and reference
true results without selecting an element destructor. Nothrow destruction and
other operation queries use the checked type-only operation contract below.

Core v2 accepts the standard class `final` keyword on otherwise admitted ordinary,
nested and template record definitions, including partial/full specializations,
copied nested types and final leaves of admitted empty-base chains. Clang checks
prohibited derivation; finality adds no C storage or virtual-call mechanism.
Generic source-shape checks permit only this record attribute and keep all
non-record attribute rules. Other class attributes, the MS `sealed` spelling,
virtual/final methods and unsupported layouts remain excluded. V1 remains unchanged.

`__is_literal` uses C++17 literal-type rules, including cv void and references;
a constexpr constructor can make an admitted record literal, while a nontrivial
destructor prevents it. `__has_unique_object_representations` uses source object
representation, padding and trivial-copy metadata. A two-byte character record
has unique representations on admitted targets; a padded character/integer
record and an empty class do not. The empty class's generated byte carrier does
not change this C++ answer. These queries perform no hypothetical constructor,
assignment or conversion call and instantiate no otherwise unused dependent body.
Paired source checks, 33 saved-NC O0/O2 runtime checkpoints, eight-target property
and layout assertions, and relocation cover the classification boundary; native
validation requires the implementing revision's CI.

`__is_destructible` and `__is_trivially_destructible` also cover admitted records,
references and fixed arrays. They inspect the exact C++ destructor's deletion and
access, including implicit deletion caused by a field's destructor. A private or
deleted destructor makes the object query false, while a reference to that type
remains destructible and trivially destructible. A reference member does not own
or destroy its referent. User-provided destruction can be valid without being
trivial; array queries retain the element's behavior.

These two predicates neither resolve a destructor's `noexcept` nor instantiate
its body. In C++17, implicit destructor declaration only performs the relevant
field/base deletion and access checks for admitted nonvirtual layouts. A generic
destructor's dependent exception specification and unused body stay lazy, even
when they would fail if instantiated. Existing ordinary function-definition and
record source restrictions still apply. `__is_nothrow_destructible` on record
values and arrays uses the separate retained destructor and exception-source
proof below. Record references use the unconditional reference result below.
The shared classification fixture has 46 O0/O2 checkpoints and thirty-three exported
boolean checks, including a real destruction outside the query; native results
require the implementing revision's CI.

`decltype` operands, array bounds, exception specifications, template arguments
and selected defaults are inspected even when the query is folded, discarded or
inside `noexcept`. Their unevaluated side effects, construction and destruction
are not executed. Existing template substitution sources and lazy uninstantiated
patterns remain authoritative. Within a written template parameter's type,
metadata classification queries can name direct type parameters from that same
list (including cv spelling) and concrete admitted operands. Pointer/alias-shaped
dependent operands need a separate source proof. This declaration-only check
never reads an unresolved boolean; every selected substitution is checked again.
Concrete queries can feed static assertions,
constant initializers, `if constexpr`, defaults and bounded parameter packs.

Every supported concrete classification query also closes its exact type/expression
source graph before lowering. Alias bounds, enum values needed for layout, consumed
record field types, selected defaults and materialized constexpr/generated value
sources cannot disappear behind folded metadata. True and false results use the
same source gate. This proof reuses the checked source rules below, without turning
metadata classification into a hypothetical construction or destruction query.
Pointer/reference spelling keeps unconsumed pointee layout lazy; declaration-only
dependent parameter queries retain their existing path. The paired classification
corpus has 184 accepted, 96 unsupported-source and seventeen invalid-C++ cases.

The builtin spelling does not expand the operand domain. Volatile types, member
pointers, union types and `long double` remain rejected, even for a
query that would return false. Unknown-bound arrays use the type-only metadata
contract above. Direct unknown-bound arrays
are distinct from array parameters adjusted within an admitted function type.
Construction, assignment, conversion and nothrow destruction follow the narrower
[non-record operation contract](#non-record-operation-traits) below. Other builtin
trait kinds remain rejected. V1 admission
is unchanged. Paired source/protocol tests, O0/O2 saved-NC execution, unevaluated
effects and relocation require the implementing revision's CI; this does not
establish standard-header or complete C++/STL support.

### Incomplete record type metadata

Owned, named non-union forward declarations and uninstantiated class types can
supply identity to type-only aliases, template arguments, classification and
array rank/extent queries. For example, `struct R;` permits `__is_class(R)`,
`__is_same(R, const R)` (false), `__is_base_of(R, const R)` (true), and
`__array_extent(R[][3], 1)` (three). Pointer/reference and cv spelling remain
distinct. Ordinary/trivial destruction of `R[]` is false, while a reference is
destructible without inspecting a missing destructor. Traits whose semantics
require a complete class still receive the original C++ diagnostic.

Completeness is determined from the actual record, never its primary template's
body. Querying `__is_class(R<int>)` need not instantiate fields or methods.
Each concrete template use still requires its exact written arguments, selected
defaults and retained substitution frames. Copied nested types retain their
ordinary source/origin rules. The ordinary forward-declaration gate adds no
permission for namespace explicit-specialization forward declarations. A real
definition, when present, retains the existing whole-source and layout checks.

This metadata creates no IR record, storage, helper or invented size. Bounds,
qualifiers, source expressions and expansion budgets remain checked, including
unsupported source hidden behind a constant result. Actual objects, fields,
parameters, results and callback signatures keep their complete-carrier checks.
Operation queries use the [separate retained-operation proof](#incomplete-record-operation-types).
The eight-ABI identity fixture asserts seven constant
boolean exports with no records, globals, parameters or additional functions;
the rank/extent fixture also checks incomplete elements with native `size_t`.
O0/O2 and relocation verification use the implementing revision's CI.

## Non-record operation traits

Core v2 also admits the pinned frontend's resolved operation queries for types
that cannot invoke user-defined operations:

| Operation | Builtin spellings |
| --- | --- |
| Construction | `__is_constructible`, `__is_nothrow_constructible`, `__is_trivially_constructible` |
| Assignment | `__is_assignable`, `__is_nothrow_assignable`, `__is_trivially_assignable` |
| Implicit conversion | `__is_convertible`, `__is_convertible_to`, `__is_nothrow_convertible` |
| Nothrow destruction | `__is_nothrow_destructible` |

For the non-record subset, each operand satisfies the existing queried-type
contract. After removing a reference and every array extent, it is not a record. Admitted scalar types,
ordinary pointers, references and fixed arrays of those types are covered; `void`
and ordinary function types keep their actual C++ query results. Pointers to
admitted records, including arrays of such pointers, qualify because constructing,
assigning or destroying the pointer does not invoke an operation on the pointee.
All written type sources still pass the existing checks. An incomplete pointee
can use its checked record identity without forcing a class-template definition;
an available actual definition retains its normal source and layout checks.
An inaccessible base-pointer conversion returns false without inventing access.

Construction takes one destination type and zero to 64 argument types. Assignment
and conversion take exactly two types; destruction takes one. The result is the
pinned Clang boolean, including false results and the distinction between explicit
construction and implicit conversion. For example, construction of `bool` from
`decltype(nullptr)` succeeds, while implicit conversion does not. A scalar rvalue
is not an assignable lvalue; reference identity cannot be inferred from its C
pointer carrier. Queries create no runtime calls, objects or destruction.

```cpp
static_assert(__is_constructible(int));
static_assert(__is_constructible(const int&, int));
static_assert(!__is_constructible(int&, int));
static_assert(__is_trivially_assignable(int&, int));
static_assert(!__is_assignable(int&&, int));
static_assert(__is_convertible(int*, const void*));
static_assert(__is_nothrow_destructible(int[3]));
```

The private source frontend retains hypothetical initialization and assignment
roots and their exact synthetic operands for an opt-in NeverC consumer. One local
state is passed through the actual Sema helpers and associated with the resulting
query node; nested queries cannot overwrite it. Synthetic operands use ASTContext
storage after a bounded reservation, so the trees survive Sema's stack and local
allocator lifetimes. Other consumers keep their original operand allocation.
The adapter checks query identity, operand types/categories and completion status.
These private nodes are never serialized or executed.

An attempted operation that fails can already have selected an overload or
processed a default argument. Its incomplete status is explicit; a missing root
does not prove absence of source operations. A complete operation can still give
a false nothrow/trivial result. Full checking of those selected sources, partial
failure paths and exception dependencies remains necessary. The implicit trivial
and completed ordinary-definition subsets below have independent source proofs.
Nothrow construction, assignment and conversion also require the checked resolved
exception source described below. Record-value and array nothrow destruction use
retained destructor selection and the resolved prototype snapshot described below.
Nothrow destruction of record references takes the separate no-selection path. This is
separate from the metadata record queries above. Actual standard-header and complete C++/STL support remain
unfinished.

Written `decltype` expressions, adjusted function parameters, array bounds,
`noexcept`, template arguments and selected defaults remain checked before their
results can be erased. Concrete queries work in defaults, SFINAE, `if constexpr`,
variable templates and bounded packs without evaluating operand side effects.
A dependent operation query in a non-type template parameter declaration type
can use direct type parameters from that exact written parameter list and concrete
admitted operands. It shares the supported trait arities with concrete queries,
but never reads a dependent boolean or invents a semantic operation event. Nested
dependent pointer/alias shapes remain excluded;
each selected substitution still needs its own concrete type and retained source proof.
The paired 687 accepted, 381 unsupported-source, seventeen missing-definition and thirty-two invalid-C++ cases cover
these boundaries. Twenty-nine scalar saved-NC O0/O2 runtime checkpoints, relocation and twenty-eight
boolean results across eight native ABIs require the implementing revision's CI.
V1 and the transport format are unchanged; no opaque source or LLVM fallback is
introduced.

### Unknown-bound array operation types

Supported operation queries can inspect `int[]`, `R[][3]`, aliases and their
pointer/reference wrappers through bounded type-only validation. The element must
be admitted, including a checked incomplete record identity. Incomplete array destinations preserve the
construction query's early false result; nothrow destruction preserves false for
an incomplete array and true for a reference without looking up or resolving the
element destructor. A retained selection event must show no destructor, prototype
or attempted lookup on those paths.

Exact reference bindings and array-to-element-pointer decay retain the normal
complete hypothetical operation, original operand identities and source graphs.
Failed record-array assignment or reference initialization still needs complete
source evidence and remains rejected when the producer retained only a failed
attempt. Inner bounds, aliases, selected defaults and consumed element layout stay
checked even for false results. Unselected element constructors/destructors stay
lazy. Actual function parameters, callback signatures, expressions and object
storage still require ordinary supported runtime carriers; these queries do not
add an unknown-array storage representation or adopt C++20 conversions.

### Incomplete record operation types

Owned incomplete non-union class identities also qualify for operation queries
where the retained semantic event supplies the necessary proof. Constructing or
assigning `R*`, converting it to `void*`, binding `R&` from the same reference and
destroying a reference require no referent layout. Exact array reference binding
and unknown-array decay follow the same checked root and synthetic operand rules.
Nothrow destruction of `R[]` returns false with an explicit no-lookup event.

Pinned conversion checks can return false before creating any hypothetical
operand: examples include `int` to incomplete `R`, `R` to `R`, or `R` to `void`.
These results keep their empty unattempted events. Failed record-reference
assignment/conversion cannot use that shortcut after an attempt has begun.
Unrelated pointer types keep the existing scalar false-result path. Direct record
construction, assignment and destruction, and fixed-array arguments needing
complete elements, retain Clang's original completeness diagnostics.

Each retained expression still passes exact type/source, cast, selection and
owning-destruction checks. A selected lazy conversion or assignment additionally
validates every actual redeclaration's return carrier: a completed type source
does not grant an incomplete return signature. Parameter/default and exception
source checks are unchanged. No incomplete object, callback, runtime parameter
or result gains storage or an emitted helper. The eight-ABI operation fixture
checks twenty-eight constant boolean exports and only its original complete
record; O0/O2 tests preserve unevaluated effects and relocation.

### Implicit trivial record operations

The construction, assignment and conversion predicates additionally
accept an admitted record operand when its retained operation is complete and
uses only implicit trivial constructors or assignment operators. Direct reference
binding and array decay can also use the exact synthetic operands without
invoking a record operation. A pre-operation result is admitted only when Sema
has not attempted initialization and retained neither a root nor operands.

```cpp
struct Value { int n; };
static_assert(__is_constructible(Value));
static_assert(__is_trivially_constructible(Value, const Value&));
static_assert(__is_trivially_assignable(Value&, Value));
static_assert(__is_convertible(Value&, const Value&));
```

The independent checker visits each retained operation node with the existing
type, depth and expansion limits. A selected generated operation must remain
implicit through every owning field, array element and base in that operation
family. Explicitly defaulted subobject operations can carry written exception
source despite being trivial, so they still require further selection evidence.
Reference members do not own their referents. Construction separately requires
the shared owning destruction proof below: completed ordinary user destruction,
checked explicit defaulting and implicit nontrivial owners can accompany a
checked implicit trivial constructor. A direct reference binding does not destroy
the referent and can therefore bind an admitted record with a nontrivial or
deleted destructor.

The implicit subset rejects selected user or explicitly defaulted constructors/assignments, incomplete
failed initialization and unsupported derived-to-base reference adjustments.
Record-value/array nothrow destruction uses its separate proof below. Nothrow construction, assignment and
conversion use the additional resolved-exception check below. Failed selection cannot be accepted just because the
boolean is false. Unused user default-constructor bodies stay lazy when the query
selects only an implicit copy/move operation. Written query types, aliases, bounds
and defaults retain their ordinary source checks.

The source checker does not populate runtime construction/default caches or queue
runtime helpers. A separate eighteen-checkpoint O0/O2 fixture exercises query values,
zero query effects, actual copy/assignment/reference identity and real destruction.
Thirteen exported boolean functions check that no hypothetical calls or record locals
enter the protocol; relocation remains deterministic. Native validation requires
the implementing revision's CI.

### Checked defaulted record operations

Retained construction and assignment roots can also select checked ordinary or
single-stage inline template defaulted operations. A trivial written operation
requires every actual declaration and concrete signature to have completed source
checks, plus the implicit owning-subobject family proof. A nontrivial operation
requires its exact existing generated definition and completed initializer, body
and layout source. The query does not cause a missing body to be instantiated.

```cpp
struct Value {
  int n;
  Value() = default;
  Value &operator=(const Value&) = default;
};
static_assert(__is_trivially_constructible(Value));
static_assert(__is_nothrow_assignable(Value&, const Value&));
```

The original synthetic operands, complete-object construction kind, argument
count, receiver/reference identity, selected exception specifications and separate
owning destruction proof remain required. Assignment keeps its exact direct method
reference. These checks run after source completion; the provisional implicit fast
path is unchanged. True, false, trivial and nothrow results retain Clang's values.
Written child operation families, template split defaulting, unmaterialized
nontrivial operations and unsupported signature/body source still require further
proof. Selected checked constructor defaults use the same completed generated
source proof while retaining their independent initializer and lifetime checks.

### Operations with completed ordinary definitions

Record construction, assignment and conversion queries also accept
selected ordinary user operations when the exact body-owning definitions have
completed normal source traversal. This includes explicit constructors, user
copy/move constructors, assignment operators, conversion functions, their admitted
standard conversions and user destructors. Built-in scalar assignment can use a
checked record conversion, such as assigning an `operator int()` result to `double`.
False triviality results preserve the same selected-source checks as true results.

```cpp
struct Value {
  int n;
  Value(int v) : n(v) {}
  operator int() const { return n; }
};
static_assert(__is_constructible(Value, int));
static_assert(!__is_trivially_constructible(Value, int));
static_assert(__is_convertible(Value, double));
static_assert(__is_assignable(int&, Value));
```

The source phase defers complete roots that need these definitions. After all
ordinary and selected generated source traversal succeeds, an independent pass
checks every pending root. Deferral closes before this pass and before any
serialization or runtime lowering. A query inside its own selected function body,
mutual query dependencies and later out-of-line definitions therefore need no
recursive body traversal. A function queued for emission is not a completed proof.
Any failed traversal or pending query prevents artifact publication.

Source-owned, user-provided ordinary definitions and the checked concrete template
definitions below qualify here. Each root checks its exact synthetic operands,
selected declaration's actual definition,
converted arguments, result type and supported full-expression temporary shape.
Constructed objects and record-valued results also require checked destruction of
every owning base, field and array element. A user destructor body alone cannot
prove implicit subobject destruction. Reference and pointer members do not own
their referents. Query checking never requests runtime construction/default caches
or queues hypothetical destruction helpers.

Already materialized concrete template operations also qualify when the exact
body has completed the same normal source traversal. The completion set excludes
implicit and defaulted functions explicitly: an out-of-line defaulted function
can be user-provided in Clang's classification while RAV still skips its body.
Both the selected declaration and the actual body-owning definition require their
original completed type-source nodes and current exception-expression graphs.
An already emitted or referenced function does not substitute for that proof.

The template subset requires an original inline definition. Its raw
member-specialization origin or proven primary-origin declaration must
itself own the body and match Clang's actual instantiation pattern exactly. The
origin cannot itself have member-specialization or primary-template instantiation
metadata. Both selected and body-owning declarations pass this identity gate.
Class-template operations and member templates in ordinary or instantiated classes
can qualify; copied member-template primaries require the bounded raw origin proof
described below. Explicit specializations in this category and separate template
declaration/definition pairs still need further evidence.
Body instantiation can retain an earlier declaration's TypeSourceInfo, so a body
alone cannot prove a later template definition's independently written signature.

The query never materializes an unused template body. A later real source use or
explicit instantiation can supply the already checked definition before final
validation; queries within that body use the same deferred completion rule.
Unmaterialized bodies outside the lazy class-constructor signature subset below,
and selected defaults lacking exact completed source, remain unsupported.
These source restrictions do not change ordinary runtime template support.

Selected ordinary and checked concrete template constructor defaults also qualify
when the exact parameter and unchanged initializer have completed normal source
checking. A template default must already be instantiated on that actual parameter;
its pattern or another specialization's initializer cannot supply completion.
Normal parameter traversal retains the matching class or function template context,
including dependent scalar defaults and member constructor templates. The root
constructor still requires its exact completed definition and strict inline-origin
source proof; collecting a default does not materialize a missing body. Unselected
uninstantiated defaults stay lazy, including bad defaults bypassed by explicit arguments.
The actual selected parameter owner and argument slot must match; inherited and
out-of-line defaults retain their own parameter identity. An owned declaration
alone cannot prove an initializer inherited from unowned source. Clang may strip
one full-expression wrapper from a parameter default, so its type, value category,
exact operand and cleanup objects are checked independently. Per-use rewritten
initializers cannot borrow the original expression's proof.

An additional bounded scan checks generated operations and destruction in
query-only defaults. Implicit/defaulted constructors and assignments require the
same implicit trivial family or completed generated-operation source proof as
outer query operations. The latter path runs only with all final source tables;
actual written declarations, signatures, owning families and existing nontrivial
generated bodies remain required. Marking a selected function referenced can infer
its exception specification even for a non-nothrow query, while a trivial body
remains ungenerated. Clang's normal selected-default processing can materialize
a generated body; the checker itself never asks for one. Nested unevaluated
expressions still need their own source proof. The scan covers
record prvalues, bound temporaries, nested defaults and semantic array fillers,
explicit destructor calls and delete all retain their owning-subobject proofs.
During normal parameter traversal, a scoped collector records implicit/defaulted
constructor and assignment selections and record-prvalue destruction dependencies.
Selection may point to an earlier declaration whose later definition spells
`= default`. Both the collector and evaluated scan classify the complete
declaration family, then check the actual selected signature and generated body
independently; the defaulting flag on one declaration is not a source certificate.
It also covers unevaluated operands and written decltype reached through TypeLoc;
Clang can resolve inferred specifications there and can omit trivial destructor
binding nodes. Each exact parameter/initializer retains its own completion proof. Parameters
inheriting the same exact initializer share its collected dependency union.
Each semantic initializer also retains its collected dependencies and edges to
other semantic initializers, including cache hits. A nested default can therefore
reuse source first checked in another parameter or member initializer. Every
reachable node must have completed checking; an in-progress cache entry supplies
no completion proof. Nodes, edges and active frames use the shared expansion
budget, with a 64-frame depth limit. Dependencies are checked after source
completion. This is separate from the evaluated
expression scan and never requests runtime cleanup or instantiates a body.

The proof conservatively requires destruction evidence for collected record
prvalues, including constructor expressions below new and unevaluated aggregates.
The exact terminal call of a written `decltype` is exempt from result destruction,
including only its parentheses and built-in comma-right wrappers, because Clang
does not introduce that result temporary. Callee/type source, arguments, comma-left
temporaries and actual binding/destruction expressions remain checked. The exemption
does not extend to a separate hypothetical operation on the resulting alias.
Implicit destruction, ordinary explicitly defaulted destructors with checked
written source, or checked ordinary/inline template user destructors qualify;
lazy template destructor prvalues need a separate completed-signature proof and
remain rejected when destruction is consumed. Unused bodies that contribute no such dependency remain lazy. Ordinary default expressions can call already admitted
functions, including nested template expressions under existing source rules;
uninstantiated or incompletely checked default parameters need further source evidence.

Declaration-only and other lazy template user operations, unproven defaulted
construction/assignment, incomplete selection and
unproven exception dependencies still require further source evidence.
Unused templates remain lazy, and actual runtime use retains all ordinary source
and lowering checks.

Paired tests cover source order, recursive queries, conversions, missing bodies,
hidden unsupported source and these remaining restrictions. A separate saved-NC
fixture has one hundred and sixteen O0/O2 checkpoints for zero hypothetical effects, real user
construction/copy/move/assignment/conversion, field-array destruction and repeated
default evaluation with full-expression temporary cleanup. One hundred and fifty-two exported query
functions must contain only boolean value flow; relocation must
preserve the protocol exactly. Native results require implementing CI.

### Lazy class-template constructor signatures

Construction, assignment and conversion predicates can use a checked signature for an ordinary
constructor of a concrete class-template instance whose body remains uninstantiated.
The constructor must be referenced but unused, with an exact checked inline
definition origin and already resolved standard exception specifications on every
actual redeclaration. This permits dependent or unsupported source in that unused
body; actual runtime use still instantiates and checks the body normally.

Each exact construction consumed inside the current complete retained operation
is eligible, including constructor conversions and nested argument construction.
The completed proof belongs to that expression, not every use of the constructor.
The bounded traversal follows only supported operation expressions and checked
temporary/conversion envelopes; it never scans unvisited template-body queries.
Copy/move constructor signatures and complete false trivial/nothrow results use
the same checks. Member function-template constructors additionally need the exact
selection-source proof below. Separate template definitions and missing ordinary
definitions retain their existing requirements.

A first signature check visits existing actual TypeSourceInfo and parameters in
the concrete method context, including selected defaults and original/resolved
exception expressions. Existing incomplete nodes are never replayed. Constructor
signatures, owning destructor signatures and generated bodies all finish before
final validation, including work discovered by another signature. The final proof
still checks every actual declaration's signature source, selected argument/default
identity, conversions and full owning destruction. It neither requests a missing
body nor adds a runtime constructor helper. A query-only template specialization
with a poisoned body and a different real specialization exercise this separation
in the shared runtime fixture.

### Lazy class-template destructor signatures

An ordinary destructor of a concrete class-template instance can also use its
checked signature while its inline body remains uninstantiated. It must be unused,
have no actual body, and retain the exact uncopied inline origin. Every actual
redeclaration needs complete type source and an already resolved standard exception
specification. Unary nothrow destruction does not mark the destructor referenced,
so its existing exact lookup/resolution event supplies selection evidence.

Only the existing authorized signature queue can register this proof: an actually
visited valid unary destruction query or a complete retained operation's consumed
record-prvalue construction/call/binding and its owning base/field/array graph.
This includes a temporary inside an operation with a scalar or reference result.
Completing TypeSourceInfo through another
ordinary traversal alone does not register a lazy destructor. Once registered,
that exact destructor's signature may supply another query's source dependency,
including an earlier or later selected default containing `sizeof` of a temporary.
No body is instantiated and no runtime cleanup helper is created by the proof.

Every use still checks the full owning destruction graph and current signatures,
including false results and inferred `noexcept(false)`. Unresolved children,
unsupported written exception source, separate template definitions and missing
ordinary definitions retain their restrictions. Defaulted destructors retain their
separate proof; runtime destruction still requires and checks actual user bodies.
Poisoned query-only bodies, another real specialization's independent storage,
reverse destruction order and an actual potentially-throwing destructor are
covered by the shared runtime fixture.

### Lazy class-template assignment and conversion signatures

Construction, assignment and conversion predicates can check an
ordinary operator of a concrete class-template instance while its inline body
remains uninstantiated. Assignment requires an exact retained `operator=`
call, including copy/move and other admitted assignment signatures. Conversion
requires an exact retained conversion-function call. Supported standard
conversion, temporary binding, materialization and cleanup envelopes retain their
type and value category checks. Checked constructor arguments, assignment operands
and conversion objects can contain further selected operations, each requiring its
own exact expression proof. Thus a scalar assignment or construction may consume
a lazy template conversion, and a class construction may consume one in an argument.

The actual selected method must be referenced, unused and have no actual body,
with an exact checked inline origin and already resolved standard specifications.
A first check traverses only its existing concrete signature in the matching
method context. Completed signatures register the exact call expression; final
validation reaches that same expression from the current query's root and checks every
actual redeclaration's source and current prototype. Member-template assignments and conversions
additionally need the exact selection-source proof below. Separate template definitions
and missing ordinary definitions retain their restrictions.

The proof is limited to that exact selected expression. Synthetic operands are
leaves, and selected default initializers keep their separate parameter/source
proof instead of being traversed through this queue. Ordinary source calls cannot
use these expression completions to bypass their body requirements. False
nothrow/trivial results retain all source checks, including owning destruction of
nested record temporaries. Unvisited query events cannot start signature
work. All signature and generated-body queues finish before final checking, including
queries discovered in another method's exception specification. Real runtime calls
still instantiate and check bodies normally. The shared runtime fixture checks
query-only poisoned specializations, real assignment reference identity, move
effects, reference conversions, nested conversion counts, temporary destruction,
standard post-conversions and zero query effects.

### Lazy member-template operation signatures

A constructor, assignment or conversion function-template specialization can use the
same exact expression proof while its inline body remains uninstantiated. Its
actual selected expression must also have retained successful member-template
selection evidence at its original deduction location. Constructors and conversions
use their exact retained selection event. An ordinary `operator=` uses its exact
direct function reference and matching operator location to find the successful
deduction source. Both the selected declaration and source location must match;
canonical function identity alone is insufficient. Each matching event checks the
actual canonical template arguments, parameter types and consumed defaults.
The first signature traversal uses the exact function/primary/argument context,
fenced from the caller's template argument frames. Existing incomplete signature
nodes cannot be replayed.

For a primary copied with an enclosing class instance, a bounded chain of raw
`getInstantiatedFromMemberTemplate` edges must reach the original inline body.
Each edge retains owned valid declarations, matching method kinds and parameter
counts, the exact selected class pattern and the previously checked source ordinal.
Canonical declarations only identify repeated nodes and source ordinals; they do
not replace the raw declaration used to find the body. Every member-specialization
stop is rejected, and the final declaration must itself own the body and equal
Clang's definition instantiation pattern. Split definitions cannot supply a body
through a later declaration. Inner signature frames retain the actual copied
primary and its arguments; outer substitutions use their actual class instance
and selected pattern. A different outer instance cannot supply either proof.

Each expression keeps a separate completed selection-source dependency graph.
A consumed template default can disappear from the final function signature, so
signature completion alone is insufficient. The final operation checker closes
these exact selection dependencies alongside the declaration/signature proof.
The same requirement applies when the selected body already exists: a real call
may deduce an argument that the query instead obtains from a default. Every
underlying source-check failure prevents selection completion, including failures
that return without adding a diagnostic.

Selected function defaults and bounded packs retain their existing source checks;
unused defaults and poisoned uninstantiated bodies remain lazy. Unproven copied
origins, split template definitions and explicit specializations retain their
restrictions. Other operator categories do not enter this signature queue. The proof
does not register a body or runtime helper. Paired tests cover erased default
source and existing-body selections; runtime checks use query-only integer
specializations and real unsigned calls to verify defaults, conversions, assignment
reference identity, forwarding arguments, ordinary overload selection and independent
object storage. A real explicit template call can select the same assignment
specialization without its default; a later query still checks that consumed default.
Copied-template runtime checks use distinct outer types with the same inner type,
including independent static counters, defaults and instance storage.

### Resolved exception source for operation queries

`__is_nothrow_constructible`, `__is_nothrow_assignable` and
`__is_nothrow_convertible` use the same retained roots and source proofs above.
Each selected constructor, assignment operator, conversion function and bound
temporary destructor must additionally have an already-resolved standard exception
specification. The adapter reads these specifications without resolving new ones,
instantiating bodies or recomputing Clang's result. Both true and false results
must pass the source checks. A `noexcept` constructor with a throwing argument
conversion or bound destructor still gives the original false result.

Implicit constructor/assignment families exclude written/defaulted subobject
operations of that family; implicit trivial default construction also excludes
default member initializers. Ordinary user definitions include their written
signature checks; inferred user-destructor specifications also retain recursive
source checks for all owning subobject destructors. An unselected destructor does
not need new exception resolution solely because its object is constructed.

Clang represents an inferred potentially-throwing specification with a generated
locationless `false` literal. Only that exact current prototype of an admitted
special member or ordinary destructor, with no written exception specification
on any redeclaration, uses its owner's complete operation/destruction source in
place of written-expression evidence. This also covers selected defaults and
type-query dependencies. Written `noexcept(false)` expressions keep their normal
source checks, and every owning dependency remains checked after a false result.
C++17 aggregate initialization such as `F<int>{}` does not consume an unused
inline-defaulted constructor's exception specification; `F<int>()` does.

Each consumed written noexcept expression has its own completed source node in
the bounded dependency graph. Normal Type, TypeLoc, concrete-template and friend
signature traversal collect its implicit operations, destruction and further
exception expressions. Calls, function references and address-taking can consume
callee specifications; nested selected defaults and cached semantic initializers
retain their edges. The graph is checked for ordinary operations as well, because
referencing a selected function can resolve its specification independently of a
nothrow predicate. Completed function definitions cannot substitute for completed
expression source. Independent function declarations use separate collection
frames, so their unused defaults and bodies do not become caller dependencies.
Instantiation may also rebuild a nondependent exception expression when its
selected default-argument context changes. The original written node and the
distinct already-resolved node both receive normal source checks; a dependent
written expression uses its resolved replacement. No new resolution is requested.
Different concrete specializations can share the exact written TypeSourceInfo
while retaining distinct resolved exception expressions. A completed shared type
node proves only that written source. Each consumed actual expression still
receives its first check in its own method and template argument context; existing
incomplete type or expression nodes are never replayed or repaired. The final
prototype check requires that actual node and its full dependency closure.
Referenced unused inline friends can keep their body lazy only with exact paired
written/selected/granting-class evidence and a concrete standard prototype. Their
actual signature still completes in its own friend context, with unselected
defaults isolated. Runtime-used friends continue to require a materialized body.
Missing or unfinished graph nodes remain unsupported; the adapter does not replay
source traversal or request resolution to fill them. A selected inline defaulted
destructor can receive its first concrete signature traversal as described below.

Constant values also retain source dependencies: variable and enum initializers,
cached variable-template initializers, and already materialized user-provided
constexpr bodies and constructor initializers have independent completion nodes.
An implicit enum value follows the nearest preceding explicit initializer without
recomputing its value. Parameter references do not select their defaults; actual
default-argument expressions retain that selection. The proof conservatively
checks an already materialized constexpr body even for an unevaluated reference,
but never materializes a missing body. Collected generated-operation dependencies
can use ordinary explicitly defaulted trivial methods after exact original
redeclaration/type/specification checks, while owning subobject families remain
implicit. Nontrivial generated methods require a separate completion node from
their existing successful semantic initializer/body traversal, including the
owning layout source. An emission queue entry alone is insufficient.
Concrete inline template defaulting can use the same proof when every actual
declaration is defaulted and has completed type/specification source. Its raw
member-instantiation origin must be the exact source-owned inline `= default`
declaration of the same method kind, with no further instantiated or function-template
origin. The origin proves category only; it supplies neither missing concrete
defaulting nor completion. Separate template definitions, explicit specializations,
copied origin chains and unmaterialized nontrivial bodies need further evidence.
Generated array assignment may replace selected element calls with `memcpy`;
record elements therefore retain a separate implicit assignment-family proof.
The same completed generated proof also supplies the defaulted operation roots
above; their evaluated-default scan remains unchanged. Semantic visit-once state is separate from graph-node
creation, so value initializers cannot hide cached semantic children.
Consumed type metadata also has exact completion nodes, keyed by the original
qualified type and `TypeLoc` source identity. Normal type traversal captures array
bounds, `decltype` operands and retained template substitutions. Typedefs link to
their written underlying source, and declaration references retain their actual
type source rather than borrowing a canonical type's folded array size. Record
layout uses link to existing base and field type source; pointer and reference
spelling alone does not request the pointee's layout. Function signatures retain
return and parameter type source with unused defaults isolated. These internal
nodes share the expression graph's completion and budget checks; no type is
instantiated to fill a missing source node. This remains the bounded operation
source proof, not complete C++ constant-expression coverage.
Every resolved operation query consumes its completed type-source graph after
ordinary source traversal, including successful implicit assignment checks,
scalar operands and false results reached before selecting an operation.
Assignment references retain the operated record's layout dependencies; the
nothrow-destruction reference shortcut keeps its no-selection behavior. Enum
layout consumes existing written underlying-type declarations, including opaque
enums, and nonfixed enum values retain their initializer source.

Clang's expression exception check can stop after a throwing callee. If a later
callee remains unresolved, this bounded proof rejects the query conservatively.
Destructor inference differs: it can still resolve later subobject destructors
after an earlier one makes the result throwing, so every owning subobject remains
part of the source proof. Lazy template operations and failed initialization
still require further evidence.

Unchanged checked defaults also support nothrow construction when every actual
call, constructor, bound destructor, allocation and deallocation dependency has
an already-resolved standard exception specification. Calls inspect the actual
callee expression prototype as well as any selected direct declaration. A function
pointer conversion can erase noexcept, so checking the declaration alone would
not describe the call. The frontend preserves Clang's computed true or false value;
it neither recomputes that value nor resolves a dependency skipped by an earlier
throwing expression. The same independent implicit-family and destruction checks
continue to apply to all defaults, including non-nothrow queries.

### Nothrow destruction of record references

`__is_nothrow_destructible` accepts references to admitted records and record
arrays, including records with throwing, deleted or nonpublic destructors. Pinned
Clang returns true for a reference before destructor lookup or exception-specification
resolution: destroying a reference does not destroy its referent. A dependent
unused destructor specification or body remains lazy on this path.

The full referred type and every written type source still require admission.
Hidden unsupported types in `decltype`, array bounds or template arguments remain
rejected. Actual destruction elsewhere retains ordinary body and lifetime checks.
This path requires its own retained query event with no destructor lookup or
exception resolution. Values and arrays use the distinct proof below; other
nothrow record operations require the retained operation proof above.

### Nothrow destruction of record values and arrays

`__is_nothrow_destructible` also accepts admitted record values and fixed arrays
when the actual selected destructor has checked source. The private frontend
retains the exact destructor from its original lookup and the exact prototype
returned by its original exception-specification resolution. Per-query attempt
flags distinguish selection and resolution from earlier returns; nested queries
retain independent evidence. The adapter never repeats either Sema operation or
recomputes the boolean.

Public, nondeleted destructors require a resolved standard prototype snapshot
that still matches the selected declaration's current prototype. The consumed
noexcept expression and its transitive source dependencies must have completed
normal checking. The shared owning-subobject proof admits implicit destruction,
explicitly defaulted destructors with checked written declarations, and
user destructors with completed ordinary or checked inline template definitions,
including throwing and inferred specifications. Every owning base and by-value
field remains part of that proof, even after an earlier destructor makes the
result false. Pointer and reference fields do not destroy their referents.
Each written defaulted destructor requires an actual nonimplicit defaulting
declaration in its own redeclaration chain. Every written redeclaration retains
its completed original type source and current exception-expression dependencies;
a class-template pattern cannot supply missing concrete defaulting evidence.
Inline template defaulting also requires the exact single-stage origin proof above.
Generated bodies
need not be instantiated for an unevaluated query. An unwritten inferred
specification may remain lazy in the shared source proof only as `EST_Unevaluated`
owned by the same destructor declaration family; the direct nothrow query still
requires its retained resolved snapshot. Implicit nontrivial owners use the same
recursive subobject proof, including implicit class-template owners. A standalone
nothrow destruction lookup does not mark a template destructor referenced. For an
actually visited query with valid retained lookup/resolution evidence, an admitted
inline defaulted class-template destructor can receive its first concrete signature
check after translation-unit traversal. Existing exact TSI nodes are never replayed;
all completed-source and prototype snapshot checks still apply. The pass restores
the actual method context and checks only existing written/resolved type source,
without instantiating a body, requesting resolution or scanning unused template
body events. Nested visited queries can add signatures, and signature/generated
body worklists both drain before final validation. The consumed owning graph
also supplies exact existing signatures of by-value fields, bases and array
elements for a first check when each signature is already resolved and satisfies
the same concrete inline-defaulting category. Record identities are deduplicated
and traversal is bounded to 64 levels; pointer/reference referents stay lazy.
Child source checks neither supply a new query event nor recompute the root
boolean. An explicit parent specification need not resolve child specifications;
missing completion of such unresolved template children still fails the final
source check. Existing admitted implicit/ordinary inferred specifications keep
their separate source rules. This pass does not invent missing local-class context.
Deleted/access and
reference short circuits do not trigger this pass. Separate template
defaulting and user destructors without an exact completed definition or the
authorized lazy class-destructor signature proof still require further source evidence.
Construction and assignment retain their separate operation source checks.
An actually visited construction, conversion or assignment query can also supply
this first owning-signature check when its retained operation is attempted,
complete and has an exact concrete record-prvalue root. The adapter uses that
root's already declared destructor without selecting or resolving another one.
References, xvalues, pointers and incomplete operations do not trigger the queue.
The unchanged final operation proof still checks root shape, operands, definitions,
exceptions and all lifetime dependencies, including complete roots whose trait
result is false. Nested argument/default temporaries require their existing proof.
For retained implicit constructors, destruction is verified independently for the
exact record prvalue. The same composition applies inside query-only defaults;
the constructor expression must have the same base-element record type as its
selected constructor's parent. Neither path skips the shared owning proof.

Pinned Clang can omit a binding node for a trivial destructor even when its
written specification is `noexcept(false)`. As a result, its nothrow construction
query can be true while its nothrow destruction query is false. Both source
paths remain checked and their original booleans are preserved independently;
the adapter does not synthesize one trait's answer from another.

Deleted and, when access control is enabled, nonpublic destructors preserve the
original false result before exception resolution. Their retained event must
contain the exact selected destructor and no resolution attempt or prototype;
an unconsumed lazy body or specification is not required. Written source and
query-type dependencies still receive their ordinary checks. Missing required
ordinary definitions retain `TR0203`; unsupported source retains `TR0201`.

Paired cases cover fixed arrays, owning subobjects, deleted/access short circuits,
lazy template controls, queries before later ordinary definitions and nested
queries. The shared ninety-six-checkpoint user-operation fixture checks
boolean-only output, zero query effects, real implicit/template copies with
independent storage, template operations, constexpr generated source values and
user/generated array destruction at O0/O2. Query-only nested owning signatures
use a different template specialization from the real copied object; reverse
destruction order and independent member storage are checked;
relocation must preserve the protocol. Native results require implementing CI.
Standard headers and complete C++/STL support remain unfinished.

## Bare function type metadata

Core v2 permits ordinary bare function types and direct lvalue/rvalue references
to them in type-only template arguments, selected defaults, packs, aliases,
queries and transforms. For example,
`template<class T> using D = __decay(T);` accepts `D<int(int)>` and preserves the
existing callback pointer type; `D<int(&)(int)>` produces the same pointer type.
No function declaration, reference carrier or body is invented by a type-only
use. Actual callback targets, stored pointers and invocations retain their
existing definition, lifetime and ABI checks.

Each function type uses the same bounded default-ABI callback signature check
as a direct function query. A direct function reference is unwrapped only for
this check; lvalue/rvalue identity remains available to classification and
transform builtins. Noexcept type identity is preserved. Variadic,
cv/ref-qualified, unsupported calling-convention and wide signatures remain
excluded, as do array values in callback parameters or results. Complete
source-owned record values use the callback parameter/result destination
contract above. Supported complete-record pointers/references keep their existing carrier contract;
incomplete record signatures do not gain a carrier. Runtime function-reference
variables, fields, parameters and results remain outside the carrier contract.

Actual written function-type template arguments, retained alias underlying
sources and ordinary typedef/using sources register exact source roots. Normal
traversal also retains each owned concrete function prototype as a root. This
covers function types deduced from pointer arguments and conversion destinations,
which need not have written template arguments. Original adjusted parameter
bounds and noexcept dependencies remain checked even when an alias erases the
function type. Registration never synthesizes a type source, traverses a source
a second time, or instantiates a body. Dependent pattern signatures remain lazy;
selected resolved prototypes retain their separate existing checks. Function
bodies and unselected defaults do not become signature dependencies.

Paired controls cover free/member/constructor deduction, conversion targets,
aliases/defaults/partials/packs, erased signatures and direct function
references. Eight ABI protocol checks verify fourteen boolean exports, including
zero reference rank, plus native pointer size without extra storage. A 23-checkpoint
runtime fixture verifies metadata identities, callback invocation, noexcept
conversion, deduction, stored callbacks and zero source expression effects.
O0/O2 and relocation require the implementing revision's CI. This work adds no
runtime type, IR operation or helper. Standard headers and complete C++/STL
remain unfinished.

## Unary type transforms

Core v2 checks the sixteen unary type transforms in the pinned frontend:
`__add_lvalue_reference`, `__add_pointer`, `__add_rvalue_reference`, `__decay`,
`__make_signed`, `__make_unsigned`, `__remove_all_extents`, `__remove_const`,
`__remove_cv`, `__remove_cvref`, `__remove_extent`, `__remove_pointer`,
`__remove_reference_t`, `__remove_restrict`, `__remove_volatile`, and
`__underlying_type`. Both the original input and transformed result must be
admitted type metadata. Removing qualifiers does not make a volatile/restrict
input admissible; the corresponding transforms can still be used as no-ops over
admitted inputs. C++ itself diagnoses invalid transforms, such as making `bool`
signed or obtaining the underlying type of `int`.

Type-only unknown-bound arrays and incomplete non-union record identities use
their existing contracts. Ordinary bare function types and direct function
references can decay to admitted callback pointers or retain reference identity
through the reference transforms. Their template arguments follow the
[function metadata contract](#bare-function-type-metadata). Runtime objects and
signatures still require actual admitted storage; a transformed function
reference or pointer to an incomplete record does not create a runtime carrier.

The private `TreeTransform.h` repair retains the actual transformed operand's
`TypeSourceInfo` and also substitutes inputs that are instantiation-dependent
but already have a nondependent result, such as `Erased<T>` where `Erased` aliases
`int`. The original and repaired pinned states are checked exactly, with
idempotence and drift rejection. The adapter compares the operand's exact
`QualType` identity with the semantic base, traverses its real source in the
existing template frame, and checks input and result without recomputing Sema's
answer. Dependent non-type template parameter declarations remain lazy; their
actual substitutions require concrete source. Generic unused bodies remain lazy.

Each consumed written transform is a final source-check root, including a plain
alias without a surrounding query. Original bounds, defaults and `decltype`
expressions cannot disappear behind a supported result. Signedness and enum
underlying transforms retain the consumed enum layout source; forming a pointer
does not newly consume the pointee's field layout. Raw semantic traversal never
manufactures written source evidence.

Paired cases cover all sixteen kinds, erased aliases, composition, template
parameters/defaults/partials/packs and rejected hidden source. The runtime fixture
contains twenty boolean exports, two native-size exports and thirty checkpoints
for values, reference/array storage, copies and unevaluated effects. Eight ABI
protocol checks and relocated output compare exact identities and native widths;
O0/O2 execution requires the implementing revision's CI. This work adds no IR
operation or runtime helper. Standard headers and complete C++/STL remain unfinished.

Pinned source references: [transform kinds](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/clang/include/clang/Basic/TransformTypeTraits.def),
[operand substitution](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/clang/lib/Sema/TreeTransform.h),
and [type source layout](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/clang/include/clang/AST/TypeLoc.h).

## Array type queries

`__array_rank(T)` returns the number of array dimensions and
`__array_extent(T, I)` returns the bound of dimension `I`, starting at zero.
Both use the pinned Clang result type, native `size_t`. Extent returns zero when
the dimension has unknown bound or is outside the rank; both queries return zero for a supported
non-array type. References and pointers to arrays remain non-array types for
these queries. All queried types satisfy the same admitted operand domain as
[builtin classification](#builtin-type-classification), including unknown-bound
arrays and [incomplete record identities](#incomplete-record-type-metadata). Known inner bounds remain exact:
`__array_rank(int[][3])` is two, and extents zero and one are zero and three.
Volatile, union and other unsupported types remain rejected.

Type-only validation covers written and concrete template arguments, selected
type defaults, alias underlying types and retained replacements. Each original
TypeLoc and substitution source remains checked. Runtime declarations, signatures
and expressions still require an admitted carrier; array parameter adjustment
can produce an already supported pointer. `sizeof`/`alignof` cannot obtain an
invented size for an unknown-bound array. Fixed dimensions keep the existing
extent/storage limits, and metadata recursion has the same bounded depth.

An extent dimension must be a resolved nonnegative constant of admitted integer,
boolean or unscoped enum type. Scoped enum indices need an explicit admitted
integer conversion. Implicit class conversions as dimension operands require a
separate source contract and remain rejected. Large unsigned indices simply
produce zero. Rank has no dimension operand.

The written type and dimension expression are checked before the query becomes
an integer literal, including nested queries, `sizeof`/`decltype` expressions,
template arguments and selected defaults. Their unevaluated effects are never
executed, even when a non-array operand or an out-of-range index yields zero.
The private frontend defers dependent dimension evaluation and substitutes the
index even when its array type is unchanged. Both parsing and substitution use
a constant-evaluated dimension context so required constexpr bodies are available
inside `sizeof` and `noexcept`. Parameter-pack collection explicitly includes the
dimension; the shared runtime fixture has twenty-seven checkpoints and fourteen exported
native-size integer checks. Ordinary and pack substitutions,
constant assertions, variable/alias/class defaults and `if constexpr` use the
existing template source rules; unused dependent patterns remain lazy.

The final source graph covers both the original type and the exact dimension
expression, including cached constant/default children and generated value source.
Its synchronous dimension root completes independently of the folded index. Rank,
non-array and out-of-range results cannot bypass applicable source dependencies.
The paired corpus has 77 accepted, 48 unsupported-source and ten invalid-C++ cases;
no new array query kind or implicit conversion is admitted by this source proof.

V1 remains unchanged. Paired source/protocol, fixed-type template-index regression,
O0/O2 saved-NC and relocation fixtures require the implementing revision's CI.
An unpatched upstream baseline can reproduce the original dimension substitution
failure and is not the translated-program oracle. Standard headers and complete
C++/STL remain unfinished.

## Object pointers and lvalue references

- Local variables and supported function parameters/results may use object pointers
  and lvalue references. Supported pointees are the admitted integer and character types, `bool`,
  the admitted enums, complete admitted records and admitted fixed arrays; `void *` is supported without
  dereferencing `void`. Function and member pointers remain unsupported.
- Address, dereference, `->`, pointer equality/inequality, conversion to `bool`,
  null initialization (`nullptr`, zero and value initialization), qualification
  conversions and pointer/`void *` round trips preserve the admitted source
  behavior. `const_cast` may adjust qualifications on supported pointer and
  reference types. Unary object-pointer plus and pointer/integer reinterpretation
  remain rejected. Ordering, offsets and differences follow the rules below.
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

Automatic local reference lifetime extension, including converted values and
record subobjects, follows the dedicated contract below. Static pointers and
references follow their storage contracts above. Nonstatic reference fields
follow their [binding and lifetime contract](#reference-members).
Unused/dead bindings are checked too. Standalone
`nullptr_t` variables follow the [null-value contract](#null-pointer-values). The contract covers accesses to live
objects; it does not define dangling/invalid pointer behavior or remove C++
undefined behavior. In particular, casting away `const` does not make an
originally const object writable. Generated `.nc` source targets NeverC's
pointer aliasing behavior, not an arbitrary C compiler's alias rules.

## Reference members

A `const` record is not automatically usable as a `constexpr` source. Copying
`S::b` into `S::a` can require dynamic startup even when `S::b` receives a constant
initializer. The copy now runs through [startup](#nonlocal-dynamic-initialization),
after required constant initialization of `S::b`, preserving the original referent.
A checked `constexpr` source can retain constant initialization; the translator
does not silently promote a dynamic copy or change definition order.


Core v2 supports nonstatic lvalue/rvalue reference members whose referents have
admitted object types, including scalars, records, fixed arrays, pointer objects
and function-pointer objects. Each reference field has one checked pointer
carrier in the record layout. Source records may be non-standard-layout because
of reference fields or nested such records; they still require uniform field
access, no bases, virtual dispatch, union storage, bitfields, mutable fields or
unsupported attributes. The native Clang layout must match the carrier layout.

Construction initializes the binding directly. Member reads, writes, address
formation and reference returns access the referent. A const containing record
does not make a mutable referent const. References to arrays preserve the full
extent; references to pointers and callbacks permit reseating the referred-to
pointer object. A reference binding itself is not reseated by assignment through
the member. Ordinary source access and qualification errors remain Clang errors.

```cpp
struct Ref { int& value; };
struct Owned { const int& value; };
const Owned& local(int value) {
  static const Owned object{value + 1};
  return object;
}
int main() {
  int value = 3;
  const Ref first{value};
  Ref copy = first;
  copy.value = 7;
  return value != 7 || &copy.value != &value || local(8).value != 9;
}
```

Implicit or explicitly defaulted copy/move construction preserves reference
bindings, including when other fields need selected nontrivial operations.
Self-references remain directed at the source object after copying. Source-deleted
assignment or copy operations remain deleted; an admitted user assignment can
write through its reference members. These rules follow [C++17 special-member
semantics](https://timsong-cpp.github.io/cppwp/n4659/class.copy).

A reference member does not own an existing referent. Accessing it through a
temporary containing object does not inherit the containing object's lifetime.
Returning `Ref{existing}.value` retains the existing object's address and cleans
up the containing temporary normally. This is not a general dangling-reference
analysis.

When C++17 aggregate initialization extends a temporary to an automatic record
or array variable, its exact materialization descriptor must name that variable.
The temporary registers lexical cleanup before its containing object, so the
container is destroyed first and the extended temporaries follow in reverse
construction order. Default member initializers use their selected construction
receiver and rebuilt source expression. Each repeated array element gets its own
temporary. A source FieldDecl-owned default expression authorizes source checking
only; runtime storage still requires the actual full-expression or variable owner.
Clang's constructor-initializer lifetime errors remain errors. See [C++17 temporary
lifetimes](https://timsong-cpp.github.io/cppwp/n4659/class.temporary).

Static aggregates with reference members can have constant initialization, or
synchronized first-use local initialization under the existing guard contract.
Constant bindings serialize their actual referent address; a reference-field
layout offset cannot stand in for the referent. Dynamic aggregates and their
extended temporary children use one flat initialization group and one guard.
Children initialize at their final addresses and register their own required static
destruction immediately upon completion.
Materializing default elements retain distinct constant and runtime identities.

Materializing default array elements receive separate semantic initializers in
the private Clang frontend. Each omitted element runs the existing initialization
sequence with its actual element index, preserving independent static APValues,
self references and runtime objects. Even a single omitted element has an explicit
semantic slot, so a later outer reference-to-array lifetime extension reaches its
inner temporaries. The original written initializer list remains intact.

The frontend preserves the exact identity of every generated omitted-element
initializer. When that initializer directly invokes the element's default
constructor, its temporary default arguments are destroyed before the next element
initializes. Explicit clauses and constructor calls initializing fields inside an
aggregate element keep the enclosing initializer's full-expression. Nested arrays
establish their own qualifying element-constructor boundaries. An aggregate's
reference-field temporary can live until the complete array is destroyed. These
boundaries follow the [C++17 temporary rules](https://timsong-cpp.github.io/cppwp/n4659/class.temporary#5)
and [full-expression definition](https://timsong-cpp.github.io/cppwp/n4659/intro.execution#12),
including when the unmodified upstream filler code emits earlier cleanup.

A default-neutral AST consumer hook requests this source representation only for
core v2. Nonmaterializing fillers, verification-only checks, vectors, NoInit
updates and ordinary upstream consumers retain their previous paths. Before a
semantic list grows, the consumer reserves a global allowance of at most 200000
weighted source nodes and bounds each array dimension to 65536. Scanning is bounded
in depth and work, and nested reconstruction shares the same reservation. Exceeding
these source limits receives `TR0201` without output artifacts. Unexpected retained
sharing of static materializations remains a checked invariant rather than silently
merging objects or forcing constant initialization into runtime initialization.

Paired source/protocol fixtures cover binding, copied aliases, nested layouts,
source diagnostics, zero children, owner graphs and relocation. O0/O2 and
concurrent-reader fixtures cover identity, first-use state and destruction order;
native acceptance requires the implementing CI revision. Actual standard-library
headers/runtime, remaining templates, default heap runtime, exceptions,
inheritance/virtual dispatch and multi-TU v2 remain unfinished.

## Live-object rvalue references

Core v2 admits `T&&` local aliases, parameters and results for the supported
scalar, pointer, record and fixed-array types when the reference designates an
already live object. Nested pointee `const`, type aliases and reference collapsing
retain their source meaning. The same checked pointer representation carries
both reference categories; Clang resolves overloads before normalization, so
functions that differ by `T&`/`T&&` still have distinct canonical identities.

`static_cast<T&&>(live)` changes value category while retaining the object's
address. A named rvalue-reference variable remains an lvalue. An xvalue member,
array element, comma expression, selected conditional arm or reference-result
call likewise retains its actual storage. Conditional glvalues join addresses,
not record copies, and evaluate only the chosen arm. Ordinary `&&` and `const &&`
methods can use live xvalue receivers; their receiver is captured before explicit
arguments, using the same hidden `this` signature and no receiver copy.

```cpp
int &&asRvalue(int &value) {
  return static_cast<int&&>(value);
}
int main() {
  int value = 7;
  int &&alias = asRvalue(value);
  alias = 9; // The named reference expression is an lvalue.
  return value - 9;
}
```

Reference declarations, arguments and results add no complete-object cleanup
owner. Passing a live xvalue by value or returning it as a value follows Clang's
selected admitted copy/move or existing implicit trivial operation. User and generated moves
follow their separate contracts below. Copy
assignment declarations retain their own receiver restrictions; an unqualified
admitted assignment can operate on a live xvalue receiver.

The provenance check still follows materialization and binding wrappers, nested
member/array access, casts, conditionals and comma expressions. Call-site
bindings and receivers may use full-expression temporaries under the contract
below. Automatic local extension follows its exact-owner contract below;
fresh-reference returns remain rejected, including converted values and subobjects
in dead code. Reference fields and static lifetime extension follow their separate
contracts. Function references, volatile types and arbitrary dangling-reference
analysis are not enabled. Invalid C++ category/qualification uses retain source diagnostics.
V1 profiles continue to reject rvalue references and methods.

Fixtures exercise alias mutation, selected overloads, named-reference categories,
reference results, conditional addresses, arrays, source-required copies, normal
cleanup counts and receiver/argument effects at O0/O2 with inlining disabled.
Protocol checks assert complete pointer signatures, original object/subobject
identity, absence of extra record owners and deterministic relocation. Native
evidence must come from the implementing revision's CI.

## Ordinary record methods

Core v2 admits named nonvirtual member functions of the supported records: nonstatic methods with no cv qualifier or with `const`, optional
lvalue or rvalue ref qualification (`&`/`&&`), and static methods. Clang resolves access,
overloads and out-of-line definitions before normalization. Explicit access
specifiers are compile-time syntax only; data fields must still satisfy the
field and layout restrictions. Private helper methods do not change object layout.

A nonstatic method becomes an ordinary generated function with a pointer to the
original object (`const` for a const method), after a hidden record-result pointer
when present and before explicit source parameters. `this`, implicit
field access, nested calls, recursion, returning `this`, and returning `*this`
or a field by a supported reference preserve storage identity. Calling a method does
not copy the receiver or read unrelated uninitialized fields. Methods have no
C export ABI; canonical identities distinguish declaring types, overloads and
cv/ref qualifications.

The receiver expression is evaluated and its pointer captured before explicit
arguments, including when an argument reseats a pointer used as the receiver.
Static calls through an object evaluate/discard that object expression before
arguments and have no receiver parameter; `make_record().static_method()`
is allowed for an otherwise admitted record result. A class-qualified static
call has no object expression. Methods are direct named call targets only;
function values and member pointers remain rejected even in folded source.

```cpp
struct Counter {
  int value;
  Counter &add(int n) { value += n; return *this; }
  int get() const { return value; }
};
int main() {
  Counter counter{3};
  Counter &same = counter.add(4);
  return &same == &counter && counter.get() == 7 ? 0 : 1;
}
```

Nonstatic dot calls support live receivers and full-expression temporary
receivers. Arrow calls follow object identity through array decay, pointer
offsets, comma and conditional expressions, including subobjects of temporary
records. A pointer field of a temporary container may also point to a separate
live object. Static/instance reference arguments share the full-expression
temporary-call contract below; they introduce no reference lifetime extension.

Virtual methods, inheritance,
volatile/restrict methods remain unsupported; member function templates follow
their separate contract below. Defined scalar
static data follows its separate storage contract below. Ordinary
constructors and destructors follow the separate rules below. The existing
implicit trivial copy paths are unchanged.
These methods do not establish STL container or iterator support. V1 profiles
retain their rejected-method boundary.

## Default arguments

Core v2 supports resolved default arguments on admitted free functions, including concrete function-template instances, and on methods,
call operators and user constructors. User-provided copy and
move constructors can have trailing default parameters after their required
source reference. Implicit and explicitly defaulted special members retain their
separate signature restrictions. Clang checks declaration visibility, inherited
or added defaults across redeclarations, overload resolution and source access.
Names in a default refer to declarations at its declaration point, not similarly
named locals at the call site.

Each omitted argument evaluates its selected semantic default expression anew
at that call. Explicit supplied arguments suppress runtime evaluation of the
corresponding defaults. Defaults do not become callee prologue code, optional
wire parameters or stored AST values. Every emitted call has the full checked
parameter signature. Existing receiver and argument sequencing rules apply.

Admitted scalar, enum, pointer and reference defaults use their normal typed
representations. Reference defaults can alias live objects or bind admitted
full-expression temporaries and their subobjects. Record values construct in
actual parameter destinations with the selected copy/move and callee parameter
cleanup. Reference arguments do not add a second owner or extend lifetimes past
the enclosing call's full expression. Defaults can contain calls, conversions,
comma expressions and supported record/array initialization.

Array default initialization has a distinct cleanup boundary. When an element
has no corresponding initializer, temporaries created in its constructor defaults
are destroyed before the next element is constructed. Generated whole-array
copying also cleans each element's constructor-default temporaries before the
next element. Reusing the semantic filler never reuses a runtime object. Explicit
array initializer clauses retain the outer full-expression lifetime, including
explicit empty braces. Thus `R a[2] = {};` and `R b[2] = {{}, {}};` can observe
different counts of simultaneously live default-argument temporaries. The array
continues to own its elements and destroys them in reverse order.

In non-template declarations, both declared defaults and selected call-site
semantic expressions are fully inspected, even when unused, explicitly overridden, folded in constexpr code or
inside a noexcept query. Queries create no runtime default effects. Unsupported
operand types, volatile objects, member pointers, unsupported template forms,
Unsupported allocation and throwing remain diagnosed under their existing boundaries.
Invalid C++ defaults or calls remain Clang diagnostics; missing required owned
definitions remain definition errors. Only core v2 gains this support.

O0/O2 no-inline fixtures check repeated evaluation, declaration lookup, explicit
overrides, references, actual value destinations, copy/move defaults, omitted
versus explicit array elements, generated array copying/moving and query purity.
Protocol fixtures check complete calls, source effects, cleanup order and
relocation. Native evidence requires implementing CI. Complete C++/STL remains
unfinished, and translation currently supports C++ only.

## Void and discarded-value expressions

Core v2 admits C-style, static and functional casts to void, `void()`, `void{}`
and supported non-template void aliases. They compose with calls, returns, comma
expressions and void conditional branches. A cast evaluates its operand for its
discarded effects; it does not invoke unrelated user conversions merely because
the source record offers them. An explicit inner conversion still executes.

Direct discarded nonvolatile lvalues do not load their stored values. Thus
`(void)uninitialized_int` does not invent a value read, while receiver, pointer
and index expressions retain their effects. Existing array and unambiguous free
function designators can be discarded without a decay or call; this does not
admit unresolved overload sets or nonstatic method values. Ordinary callback
values follow the function-pointer contract below. Discarded
`nullptr` needs no general nullptr_t value carrier. Unsupported declared types
and volatile objects remain rejected, including behind a void cast.

Record and array prvalues still use actual storage and selected constructors,
copies or moves. Destruction stays at the existing complete full-expression
boundary. For example, `(void)R(1), (void)R(2);` destroys both temporaries after
the comma expression, in reverse construction order. A void conditional executes
only its selected branch. A void return evaluates its expression, destroys its
full-expression temporaries, cleans locals and by-value parameters, then returns
without a value. No cast introduces an extra cleanup boundary or owner for an alias.

`void()` performs no initialization. Clang's typeless empty list under `void{}` is
accepted only through its checked functional-cast parent; arbitrary untyped
expressions and nonempty/alternate lists are not accepted. No void local, literal,
aggregate or value operand is emitted. Existing call/branch/return instructions
represent the observable effects without a new protocol opcode or version.

Constant expressions, enum initializers, static assertions and noexcept operands
remain fully inspected. Noexcept/sizeof queries do not execute operand effects.
Invalid C++ remains a Clang diagnostic; unsupported types or source constructs
remain profile errors in dead or discarded code. Other profiles keep their prior
boundaries. Fixtures check source effects, absence of lvalue reads, actual temporary
storage, cleanup order, typed protocol results and relocation. Native evidence
requires the implementing revision's CI; full C++/STL remains unfinished.

## Empty record storage and operations

Core v2 admits ordinary empty standard-layout records, including stateless
functors and conversion objects, with the same method, constructor, destructor,
source ownership and type checks as other records. The separate empty
base-chain contract below extends the base-free storage case. No unions, nesting,
virtual dispatch, unrestricted class templates, packing or custom alignment are added. Other
profiles retain their nonempty-record boundary.

The source field list and layout offset list stay empty. The consumer independently
requires size 8 bits and ABI alignment 8 bits. Generated NC declares one internal
`unsigned char nct_emit_empty_storage` only for a fieldless record because a native
NC empty struct has size zero. This byte provides actual object size and identity;
it is absent from source fields, protocol member lookup and field-offset assertions.
Size/alignment assertions remain. Empty array elements have a one-byte stride and
contribute at least one unit each to existing storage and expansion budgets.
Containing-record fields keep their independently checked natural offsets.

Construction uses the actual local, subobject, parameter or caller result storage.
Selected user constructors, copies, moves, assignments, operators and destructors
still execute. Trivial empty copying evaluates the source expression but performs
no data-field store. Trivial assignment captures both operands in the existing
C++17 order and returns the receiver alias without a carrier copy. Direct empty
aggregate initialization can emit no field stores; preserved zero-field constant
or aggregate values retain their normal `{}` initialization. Trivial containing
record copies can still copy object representation with NeverC's aggregate memcpy.

Local objects, reference-extended temporaries, full-expression temporaries and
arrays retain their existing complete-object cleanup ownership. Aliases create
no extra owner. Empty constexpr values and admitted trivial constant globals keep
normal definition checking. Nontrivial global destruction follows the
[registered static lifetime contract](#static-destruction).

The internal byte is not a semantic source field. Existing pointer conversions
can still reach object representation through byte pointers; absence of a named
field does not make that memory inaccessible. Exact padding/representation probe
results remain outside this profile's contract. Generated field operations do not
introduce scalar reads or comparisons of the internal byte.

Fixtures cover source effects, distinct addresses, array stride, nested layouts,
by-value parameters, returned values, selected conversions, cleanup, protocol
signatures and forged layout evidence. Successful manifests use an empty
`field_offsets_bits` array as allowed by the manifest schema. Native verification
requires CI for the implementing revision. Full C++/STL remains unfinished.

## Empty base chains

Core v2 admits source-owned standard-layout empty single-base chains. Every
concrete node has no nonstatic fields, a one-byte size/alignment, at most one
nonvirtual base at offset zero. Checked ordinary and concrete template constructors,
generated copy/move construction and assignment, and nonvirtual destructors can
have effects. Every selected operation retains its own definition, signature and
source checks. Ordinary methods, call operators, conversion functions and static
members retain their existing source and effect checks.

Each derived carrier contains its actual base carrier as a synthetic first
member, recursively. This is real C subobject storage. The frontend checks the
source base offset; the consumer independently computes the carrier layout and
emits size, alignment and member-offset assertions. The innermost empty carrier
owns its existing internal storage byte. Source code cannot name the synthetic
member. Base dependencies are ordered before derived definitions and count
against declaration, storage and expansion limits.

```cpp
template<bool V> struct Boolean {
  static constexpr bool value = V;
  constexpr operator bool() const { return V; }
};
template<class T, class U> struct Same : Boolean<__is_same(T, U)> {};
static_assert(Same<int, int>::value && !Same<int, unsigned>::value);
```

Ordinary, primary, partial, full and supported nested templates retain their
actual base TypeLoc and selected substitution evidence. Nondependent written
base types are checked even in unused patterns; concrete instances undergo the
full layout/lifecycle check. Unsupported fields, erased template arguments,
virtual/multiple bases, inherited constructors, custom alignment and packing remain
rejected. Nonempty derived objects need a separate representation for overlapping
base and member storage and remain outside this contract.

Derived-to-base references designate real nested members. Pointer conversions
capture source effects once and retain null without taking a member address
through it. Downcasts require Clang's exact checked nonvirtual base path and
retain source access and cv rules. Constant APValues preserve every base value
and actual nonvirtual base address path, including array element and temporary
owners. Existing offset, one-past and lifetime checks continue to apply.

Base aggregate initialization and selected trivial copies retain source effects
without storing the synthetic byte or adding a complete-object owner. Nontrivial
construction calls an internal base entry for the actual checked definition. This
entry preserves base-object initialization through delegation; ordinary complete
objects, including locals inside either entry, retain complete-object zeroing.
Both entries share the original local static storage, initialization guard and
destructor registration. Generated implicit base initializers carry locationless
type information: their actual type must match the checked direct base, whose
original base-specifier source supplies the separate written-type proof.

Base initialization finishes its default-argument temporaries before the derived
constructor body. Copy/move construction and assignment retain the selected base
operation and actual source/destination aliases. Destruction runs the derived
body and its locals before recursively destroying the direct base, including
after an early return. Static objects and lifetime-extended base references retain
complete-object cleanup. The fixtures include these lifecycle effects, inherited
members, temporary references, static pointers, null/effectful conversions, arrays
and heap elements. Paired source/protocol, eight-target layout, native exit order
and saved-NC O0/O2 checks require implementing-revision CI.
Standard headers and complete C++/STL remain unfinished.

## Ordinary record constructors

Core v2 admits ordinary user-provided default, converting and multi-argument
constructors, including `explicit`, `constexpr` and out-of-line definitions.
User-constructed records must be standard-layout with no bases or an admitted
empty base chain; named non-template nested
records follow the scope contract below. Empty records
follow the storage contract above. Each
selected construction, copy or assignment must follow its admitted operation
contract, including the user moves described below.
Destruction follows the separate lifetime contract below. Fields remain non-mutable, non-reference and non-bitfield. Const members
follow their initialization and access contract below. Default member initializers follow the contract below. Ordinary
methods may use these records. Allowing a constructor does not admit arbitrary
class layouts or foreign C++ ABI interchange.

A constructor becomes a void function with a first mutable pointer to its actual
destination. Field initialization runs in declaration order, regardless of the
written mem-initializer order, followed by the constructor body. `this`, field
access and ordinary method calls observe that same destination. Early `return;`
returns from the constructor body after field initialization. Written and implicit
member initializers, including selected array filler constructors, are checked
before lowering; unsupported source cannot disappear behind constant folding.

Locals, record fields and array elements construct directly in their final
storage through parentheses, braces, converting initialization and supported
prvalue conditional/comma expressions. Arrays default-construct each element;
braced-list fillers run separately for every omitted element, including nested
arrays. Existing extent and expansion limits apply. Constructors that store
`this` therefore retain the destination's address. A source-requested trivial
copy still copies the stored pointer value; it does not repair self pointers.

```cpp
struct Item {
  int value;
  Item *self;
  Item() : value(7), self(this) {}
  explicit Item(int n) : value(n), self(this) {}
};
int main() {
  Item items[2] = {Item(3)};
  return items[0].self == &items[0] && items[1].self == &items[1]
      && items[1].value == 7 ? 0 : 1;
}
```

Zero-initialization follows the selected C++ initialization. User-provided default
constructors receive no invented zero pass; an omitted scalar member remains
uninitialized. Supported value initialization still zeroes storage when required. Missing record
members use Clang's semantic initializer; generated/defaulted default constructors
follow the next section. Aggregate initialization remains available where C++
selects it.

A materialized record temporary and its field/array views share one destination
for that evaluation. No extra record copy is inserted during materialization.
Temporary field reads and by-value function arguments/results are supported for
the admitted records, using the explicit call storage described below. C++17
permits implementation copies of eligible trivial class function arguments/results;
NeverC selects direct destinations for prvalues and retains source-required copies. Temporary reference arguments and receivers use the full-expression call
contract below; automatic local extension follows its own contract, while fresh
reference returns remain excluded. Const
local destinations are constructed once; subsequent accesses keep source const
qualifications.

Deleted, inherited and variadic constructors remain rejected. Delegating
constructors and constructor templates follow their contracts below. Dynamic
local statics follow their first-use contract above. Exception unwinding,
default heap runtime, inheritance, virtual dispatch and STL are still outside this increment.
Compile-time const scalar/record globals may use an admitted constexpr
constructor after source inspection; nonlocal dynamic construction follows the
startup contract. Records use the [static destruction contract](#static-destruction). Static pointer fields and arrays follow their
storage contract above. V1 profiles continue to reject user constructors.

## Deleted function declarations

Core v2 accepts supported source-owned function declarations defined with
`= delete`, and explicitly defaulted special members that Clang determines are
deleted. This includes ordinary functions, constructors, destructors, named and
static methods, conversions, admitted operators, friends and their admitted
template forms. These declarations remain available to Clang for overload
resolution and access checking. They require no executable definition and emit
no function, callback, cleanup or assignment helper of their own.

```cpp
struct Item {
  int value;
  Item(int n) : value(n) {}
  Item(const Item&) = delete;
  Item(Item&& other) : value(other.value) { other.value = 0; }
};

int main() {
  Item first(7);
  Item second(static_cast<Item&&>(first));
  return second.value - 7;
}
```

The frontend preserves the distinction between an explicitly deleted move,
which can make an expression ill-formed, and a defaulted move defined as deleted,
which C++ excludes from overload resolution. A containing class can therefore
copy from an rvalue when its deleted defaulted move is ignored. C++17 guaranteed
copy elision can construct a noncopyable, nonmovable result directly in its final
storage. These behaviors use the constructors/assignments selected by Clang and
the existing destination and lifetime lowering.

Calling or taking the address of a selected deleted function, constructing an
object through a deleted constructor or requiring a deleted destructor retains
`TR0202`. Ordinary nondeleted functions still require their actual definitions
(`TR0203`). All eagerly visited signatures, written defaults and `noexcept`
expressions retain their source checks; deletion cannot hide unsupported numeric
types or operations. Template patterns and unused dependent defaults keep their
existing instantiation laziness. Virtual/variadic functions, unsupported operators
and unsupported signature types retain the selected profile's restrictions.
V1 profiles retain their deletion rejection.

Paired source/protocol regressions cover deletion, overload selection, retained
source diagnostics, explicit instantiation, specialization and emitted-symbol
absence. O0/O2 fixtures exercise move-only objects, copied rvalue fallbacks,
nonmovable elision, const keys and destruction counts. Native behavior requires
CI of the implementing revision. Standard headers and complete STL remain
unfinished.

## Const data members

Core v2 admits const-qualified nonstatic data members with supported scalar,
record, pointer, callback or fixed-array types, including fields in concrete
class templates, partial/full specializations and named nested classes. Aggregate,
zero, member-default and constructor initialization use the actual destination.
Source-selected copy/move construction remains valid even when assignment is
implicitly deleted. A const record member can select its copy constructor when
the containing object is moved; the frontend preserves that selected operation.

```cpp
template<class Key, class Value>
struct Pair { Key first; Value second; };

int main() {
  Pair<const int, int> item{3, 4};
  item.second = 5;
  const int &key = item.first;
  return key + item.second - 8;
}
```

Clang rejects writes, increments, mutable-reference binding, qualification loss,
non-const member calls and deleted assignments at their source locations.
A const pointer member cannot be reseated, but its non-const pointee can still
be modified. The original object and member addresses remain distinct across
copies. Missing required constructors/destructors retain definition checks.

As with const locals, translated field storage uses the unqualified carrier so
initialization and selected construction can write it. This internal representation
does not perform a source assignment to a const member. Evaluated addresses,
references and array decay preserve source constness through typed `cptr:`
carriers, and source access is checked before lowering. Record layout and field
offsets are independently verified using the same carriers. User-written assignment
operators can modify permitted non-const members while leaving const keys intact.

Volatile, mutable, reference and bitfield members remain outside this increment.
Deleted declarations follow their separate contract below. Restrictions on
unsupported element types remain. V1 profiles retain
their const-field rejection. Paired source/protocol tests, const-pointer signature
and relocation checks, and O0/O2 fixtures require native CI of the implementing
revision. Standard headers and complete C++/STL remain unfinished.

## Delegating constructors

Core v2 supports user-provided delegating constructors in admitted ordinary and
concrete generic classes, including chains, braced targets, aliases, out-of-line
definitions, copy/move delegation and selected member constructor templates.
The target must be an admitted constructor of the same canonical class with a
checked definition. Clang resolves overloads, access, defaults and template
selection. A single written type initializer in a dependent no-base class remains
lazy until substitution determines the target; its written type and arguments
retain the existing source checks.

```cpp
struct Value {
  int number;
  int *self;
  Value(int n) : number(n), self(&number) {}
  Value() : Value(3) { ++number; }
};
```

The target initializes the final object through the existing receiver pointer.
Delegation creates no intermediate object and performs no additional member
initialization. After the target returns, temporaries in the delegation's full
expression are destroyed, then the delegating constructor's body executes.
Member defaults, parameter defaults, reference arguments, member/array lifetimes
and Clang-selected zero initialization retain their ordinary behavior. The
completed object receives its usual single destruction at the owning scope.

Cycles, multiple initializers with delegation and invalid target selection retain
C++ diagnostics. Missing selected definitions remain `TR0203`; unsupported types
or operations remain `TR0201`, including folded arguments and checked dead bodies.
Inheritance, variadics and exception unwinding
remain separate work. Old profiles continue to reject user-provided constructors.
Paired source/protocol cases, receiver/call/relocation assertions and O0/O2 runtime
fixtures require implementing-revision native CI. Complete C++/STL is unfinished.

## Named nested records

Core v2 admits named non-template nested classes and structs with the supported
standard layout and fields. Public, private and protected nested types preserve
C++ source access, including legal aliases, factories, nested member definitions
and out-of-line definitions. A nested member can access its enclosing class's
private fields through an explicit enclosing object. The enclosing class does
not gain access to the nested class's private members without a valid grant.
A friend function defined inside a nested class does not inherit access to
its enclosing classes. The embedded frontend corrects the pinned Clang access
context for this C++17 rule, covering fields, types, defaults and selected
constructors/destructors. Explicit function friendship from the nested or
enclosing class still applies; granting one overload does not grant another.
Local named nested records follow the existing local-class rules.

Each declaration retains its canonical scope identity: two `Owner::Item` types
with the same spelling and layout remain distinct. A nested object has only its
own fields and the ordinary receiver for its member functions. There is no
implicit pointer to an enclosing object. Source aliases, enums and legal
friend declarations remain checked before erasure.

The producer emits record definitions in a stable order with by-value field
and fixed-array element dependencies first. The protocol verifier and emitted
source continue to require complete by-value dependencies. Self and mutual
pointer references use forward declarations and add no sorting edge. Definitions
are visited once, with a 64-level by-value depth bound and the existing expanded
node budget; cached dependencies retain their full depth. Source-invalid cyclic
or incomplete by-value fields produce C++ diagnostics.

Construction, generated copy/move, assignment and destruction use actual nested
field or array-element storage. Construction follows field and element order;
cleanup reverses it. Named nested empty records keep their storage identity.
Private nested iterator types work with the ordinary selected range operations.
A record-typed data member and a nested type declaration are separate concepts;
both are admitted under these contracts.

Anonymous nested structs, including typedef-named anonymous nested definitions,
remain rejected; a named tag with a typedef alias is supported. Existing
non-nested anonymous-record behavior is unchanged. Unions, unsupported member class templates, inheritance,
virtual dispatch and unsupported field/body operations retain their restrictions,
including ordinary nongeneric nested definitions. Generic ordinary member bodies
follow the separate lazy [member-body contract](#ordinary-nested-classes-in-generic-owners).
Older profiles continue to reject nested
records. Full C++ and STL remain unfinished.

Native O0/O2 fixtures cover access, scope identity, layouts, aliases, factories,
local and out-of-line definitions, pointer cycles, generated operations, cleanup
and nested iterators. Protocol fixtures check dependency order, receiver types,
actual destinations, reverse destruction and relocation. Native results require
CI from the implementing revision.

## Non-template friends

Core v2 admits resolved non-template friend functions and friend type declarations
in supported source-owned classes. This includes inline hidden friends found by
ADL, friend operators and defaults, friend declarations with a definition in the
same source unit, repeated declarations, friend classes/type aliases, and
specified existing member functions. A supported nonclass friend type has its
C++17 no-grant meaning and is still checked before erasure.

Embedded Clang resolves names, overloads and access. Friendship is neither
reciprocal nor transitive, and granting one overload or member does not grant
the others. A hidden-only friend cannot be found by qualified lookup; ADL still
requires an associated class argument. Invalid access or lookup produces a C++
diagnostic. Source-defined friend functions retain the ordinary requirement for
a definition in this unit, including unused declarations.

Friend declarations introduce no runtime operation or IR access flag. Free
friends have their source parameters, without an extra receiver; existing
member friends keep their usual receiver. Field access, references, default
arguments, factory results and private construction/destruction use the same
typed storage and selected calls as other authorized operations. Repeated
declarations retain one canonical function identity.

The allowlist still traverses each written friend type, owned forward tag,
function signature, default and body. Unused, dead and folded operations are
not exempt. Friend template/type-pack forms and any friend marked unsupported
by the embedded frontend are rejected. Existing class, field, storage and type
restrictions remain, as do older profiles' friend rejection. Other template forms and full
STL support require further work.

Native O0/O2 fixtures cover lookup, overloads, repeated declarations, private
access, aliases, factories, defaults, ADL ranges and cleanup. Protocol checks
cover signatures, canonical identities, actual field/result storage and
relocation. Native results require CI from the implementing revision.

## Nonpublic data members

Private and protected non-static data members are admitted in otherwise
supported standard-layout classes. This includes the default private access of
`class`, explicit access sections, member definitions outside the class,
ordinary constructors and factories, defaults resolved in class context,
reference and ordinary pointer getters, private arrays, and selected generated
copy/move and destruction operations. All non-static data members must still
meet the standard-layout requirements, including the same access level.

Embedded Clang checks access after source overload/name resolution and before
translation. A public method can access its class's private state, and a public
factory can invoke a private constructor where C++ permits it. An illegal
external field, method, constructor, copy or destructor access still produces
`TR0202`, including unevaluated source uses. Access checks for a default argument
belong to its declaration context. Admission does not change which source
declarations are accessible.

Access labels have no runtime storage or secrecy semantics. The producer emits
all supported fields with their existing canonical identities, types and target
layout, and authorized operations use those same fields. No access flag, new IR
instruction or runtime wrapper is needed. Generated source remains reviewable.

Mixed-access non-standard-layout classes, unsupported dependent friend class-template forms, inheritance,
anonymous nested records and unsupported member class templates remain outside current support. Bitfields and
mutable, reference or unsupported numeric fields retain their existing
restrictions; const members follow their separate contract. A getter returning a field's ordinary `T*` address does not admit
pointer-to-member types such as `T C::*`. Older profiles keep their contracts.
Native O0/O2 fixtures cover state, aliasing, defaulted array copying/moving and
nontrivial member cleanup; protocol fixtures cover storage, signatures, layout
and relocation. Native results require CI from the implementing revision.

## Local record structured bindings

Core v2 admits automatic local C++17 structured bindings of source-owned,
non-volatile trivial standard-layout aggregates with no bases. Every direct
field must be public, non-mutable, non-reference and non-bit-field, with an
existing supported scalar or ordinary object-pointer type, including admitted
`void*` carriers. Scalar widths, pointee types and qualifiers keep their existing restrictions; function
and member pointers, nested record fields and array fields are not added.
Const fields and const owners preserve the binding's actual qualification.

`auto [a, b] = source` copies the record or directly constructs a prvalue in one
hidden value object. Writing a binding changes that hidden object's field;
pointer fields retain ordinary shallow-copy semantics. `auto&`, `const auto&`
and `auto&&` forms alias the selected fields, including reference collapsing
when `auto&&` receives an lvalue. A binding name denotes an lvalue even when its
hidden owner is an rvalue reference. The initializer executes once, and later
binding reads, writes and address-taking reuse that field storage.

A reference decomposition extends a temporary only when Clang identifies its
exact hidden declaration as the extending owner with automatic storage duration.
That temporary survives the initializer's full-expression and belongs to the
owner's lexical scope. Aliasing an existing object does not extend its lifetime;
nested argument temporaries keep their own full-expression lifetimes. Written
initializer and type sources, member projections and queries using a binding
retain their ordinary source checks.

These declarations may appear in blocks, C++17 `if`/`switch` init-statements and
ordinary `for` initialization. A structured binding used as the condition
variable of `if`, `while`, `for` or `switch` is rejected as outside C++17, even
when embedded Clang would accept it with an extension warning. This restriction
does not reject a valid init-statement followed by a separate condition.

Native fixed arrays follow the separate [array contract](#local-array-structured-bindings).
SDK pair/tuple/array objects follow the [SDK binding contract](#local-sdk-structured-bindings).
User `tuple_size`/`get` protocols remain unsupported. So do
unions, bases, private/protected fields, reference/mutable/bit-field members,
volatile or nontrivial records and static/thread-local/global decomposition.
Admitted range-for declarations follow the [range contract](#range-based-for).
Ordinary supported bindings inside a loop body keep their block scope. Other profiles retain their existing
restrictions; this increment adds no protocol operation or C23 source feature.

## Local array structured bindings

Core v2 admits automatic local C++17 structured bindings of native fixed arrays,
including multidimensional arrays. The binding count equals the outer extent;
for `T[2][3]`, each of the two bindings denotes a whole `T[3]` row. The existing
complete-array type, qualification, extent and expansion limits apply to every
dimension and element, including unused bindings.

`auto [a, b] = source` initializes one hidden array. Lvalue and xvalue sources
use the selected element copy or move operations in increasing index order.
These operations retain their existing source and lifetime checks, including
supported nontrivial source-record constructors, default arguments and
destructors. A prvalue array initializes the destination directly without an
extra copy or move. Equal, parenthesized and braced declaration initializers
retain their checked written source and selected initialization semantics.
The source expression runs once, including for multidimensional copies.

`auto&`, `const auto&` and `auto&&` bind to the original elements or rows, with
ordinary reference collapsing and preserved cv qualification. Binding names
remain lvalues. Value bindings refer to the independent hidden copy; pointer
elements keep ordinary shallow-copy behavior. Each projection uses the exact
hidden owner and corresponding constant index, retaining array extent and
element type sources in queries and runtime operations.

Default-argument temporaries for an element's copy or move constructor are
cleaned up at the end of that element's initialization. Temporaries belonging
to the source expression retain its outer full-expression lifetime. The whole
hidden array owns cleanup once, in reverse element order across dimensions;
individual binding names add no cleanup. A reference decomposition extends an
array temporary or the complete object containing an array subobject only when
Clang identifies that exact declaration as the extending owner. Ordinary aliases
and references returned through calls do not create an additional extension.

Blocks, C++17 `if`/`switch` init-statements and ordinary `for` initialization
follow the same scope rules as [record bindings](#local-record-structured-bindings).
Static/thread-local/namespace declarations and structured condition variables
remain excluded, as do user tuple-like decomposition,
variable or unknown extents, zero-length arrays, unsupported qualifiers or
element operations, and exception unwinding. This feature uses existing array,
reference and cleanup operations without changing the protocol or C23 frontend.

## Local SDK structured bindings

Core v2 admits automatic local C++17 structured bindings of authenticated
`std::pair`, `std::tuple` and nonempty `std::array` specializations. Their existing
storage, element, constructor and destructor contracts apply unchanged. This
includes pair/tuple value, reference and mixed elements, supported composite
values and array elements. Binding count equals the authenticated container size.

`auto [a, b] = source` creates one hidden value owner; reference forms retain the
original storage. The hidden owner is passed to `get<I>` as an lvalue only when
its declared type is an lvalue reference, and as an xvalue otherwise. Each
binding name remains an lvalue. `decltype(name)` retains the exact
`tuple_element` type (including `T&` or `T&&`); `decltype((name))` describes its
lvalue use. Const owners qualify value elements, while stored references still
alias the original objects with their original qualification.

Admission proves the actual `tuple_size` and `tuple_element` instances retained
at the owner and binding locations, their selected SDK partial specializations,
const forwarding chains and resulting size/types. Every selected `get` must be
an exact pinned SDK instantiation with the correct index, container parameter
and result reference. A user specialization that returns the same number or
type is still excluded, as are user ADL `get` implementations. These semantic
checks do not erase the original owner initializer, written type arguments,
extents, selected operations or source dependencies.

The owner initializer executes once and completes its full-expression cleanup
before the hidden binding references initialize in declaration order. Each
binding holds a typed pointer alias and adds no element lifetime. Exact C++17
temporary extension applies to reference owners, including SDK subobjects of a
complete source object; a reference returned through a function does not extend
the referred argument temporary. Blocks, `if`/`switch` init-statements, ordinary
`for` initialization and abrupt exits use the existing owner cleanup rules.

Custom tuple-like protocols, member `get`, source trait/get specializations,
volatile owners, unsupported container elements, empty binding lists,
static/thread-local/namespace storage and structured condition variables remain
excluded. Range-for binding declarations follow the [range contract](#range-based-for).
There is no new protocol operation, runtime library dependency or C23 source-language feature.

## Range-based for

Core v2 admits resolved C++17 range-based `for` statements over supported fixed
arrays and source-defined records. It preserves the embedded frontend's selected
member or ADL `begin/end` calls, including default arguments, overloaded iterator
comparison/dereference/increment and a different C++17 sentinel type. Every
selected declaration, initializer, operation and body must satisfy the existing
type, definition and source-ownership checks. This includes unused or folded
source operations. The frontend remains built in; no external Clang executable
is used.

The loop declaration may use the admitted local record, native fixed-array or
SDK pair/tuple/array structured-binding forms. One hidden owner is initialized
from the selected iterator dereference per iteration, preserving its lvalue,
xvalue or prvalue semantics. Value owners copy their elements independently;
reference owners retain aliases and qualification. Native-array copies retain
selected element constructors and each element's default-argument cleanup.
The owner's initializer completes before SDK binding references initialize.
Neither the binding names nor those references acquire independent lifetimes.

The original range expression, selected iterator operations, loop declaration
and binding initializer all retain their source checks. Synthetic hidden
iterator type locations are not treated as user-written type sources; their
actual initializers lead back to the original range and selected calls.

The range initializer runs once, followed by `begin` and then `end`, also once.
The condition precedes each iteration. A value loop variable uses its selected
copy or direct prvalue destination; references alias the selected element.
The loop variable and any temporary whose lifetime it extends are destroyed
after the body and before increment. `continue` performs that cleanup before
increment, `break` skips increment and leaves the range scope, and `return`
captures its result before cleaning up the body, iteration and range scopes.
The end iterator is destroyed before the begin iterator and then the range.
Nested loops and switches retain their own control targets.

Only the exact three hidden declarations and the loop declaration belonging to
a checked range statement are registered. Their names do not grant access to
implicit declarations generally.
The exact hidden range reference may extend its C++17 temporary to the loop's
scope; supported member/array subobjects retain the complete temporary owner.
Per-iteration references can extend their own temporary only to that iteration.
This does not extend temporaries returned through dangling references or add
C++23 lifetime rules.

The lowering uses existing typed storage, calls, branches and cleanup guards.
No new instruction or opaque range representation is introduced. Fixed arrays,
array temporaries, const ranges, ADL lookup, record iterators, distinct sentinels,
copies, aliases, per-iteration prvalues and abrupt exits have native O0/O2
fixtures. Protocol checks cover actual storage identities, complete signatures,
one-time initialization, cleanup paths and relocation. Native results require
CI from the implementing revision.

Unsupported class-template forms, standard headers and STL containers, initializer-list ranges,
C++20 range init-statements and coroutine range loops
remain outside this increment. Existing extent and expansion budgets still
apply, and older profiles retain their previous range-loop rejection.

## Defined scalar static data members

Core v2 admits source-owned definitions of non-volatile integer, boolean, enum,
float and double static data members in supported non-template classes. This includes C++17
inline and constexpr definitions and ordinary out-of-line definitions. Mutable
members use constant or static zero initialization. Const members require a
fully defined constant initializer. For a non-inline const integral member, the
initializer may reside in the in-class declaration while the definition appears
out of line. Redundant constexpr redeclarations retain the original definition.
All written initializers and selected operations remain checked before folding.

A defined static member has one canonical global storage object, shared by every instance;
it contributes no field or size to its class. A static-only class therefore
retains the empty-record layout. Mutable members use the existing scalar global
write permission; const members remain read-only with const-qualified addresses.
Private/protected access and class scopes remain embedded Clang source rules.
Nested classes and equal-spelled members preserve distinct canonical identities.

Access through `object.member` or `pointer->member` evaluates the explicit object
expression once for its effects, then uses the static storage. It does not read
the receiver's fields or dereference the resulting pointer to obtain the static
member. An uninitialized ordinary receiver field remains unread, and a const
receiver does not make a mutable static member const. Discarded accesses still
preserve receiver effects. Unevaluated and skipped operands retain their normal
semantics.

A temporary receiver is destroyed at its ordinary full-expression boundary.
References or addresses to a static member designate separate static storage and
remain valid after that receiver is destroyed; no receiver lifetime extension
is introduced. Default arguments and default member initializers access the same
global object, including later changes to mutable values.

Members requiring storage must have a definition in the same source unit;
otherwise translation reports `TR0203`. Non-inline const integral or enum
members used only for their checked values follow the
[declaration-only constant contract](#declaration-only-static-constant-values).
They need no invented storage definition.

Fixed arrays follow the [static array contract](#fixed-array-static-storage).
Static references follow their [alias contract](#static-reference-bindings).
Records follow their [static object contract](#static-record-objects).
Dynamic initialization and static destruction use the linked contracts; TLS remains
unsupported. Older profiles retain their
existing behavior. Native O0/O2 fixtures and protocol checks cover shared state,
canonical definitions, initializer ownership, receiver effects, actual addresses,
const permissions, temporary cleanup, default arguments and relocation. Native
validation requires the implementing revision's CI; full C++/STL remains unfinished.

## Declaration-only static constant values

Core v2 admits non-inline const integer, boolean and enum static members with
source-owned in-class constant initializers, even without an out-of-line
definition, when their uses do not require an object identity. The embedded
Clang library resolves each use and initializer. The producer checks the entire
owned source, including unused declarations, folded initializers and constexpr
function bodies, before emitting any artifact. Unsupported operations cannot be
hidden inside a folded constant.

Direct value reads become exact typed literals. Values used by a glvalue
conditional or comma expression may use fresh internal scalar storage to pass
through the existing typed control flow; those objects are value carriers, not
definitions of the source static member. No global, record field, initializer
function, opaque payload or external compiler invocation is introduced. If the
source provides a real definition, accesses keep that canonical global object
and its address identity instead.

Constant values work in ordinary arithmetic, parameter defaults, default member
initializers, static assertions, enum initializers and fixed array extents.
Private/protected access, nested class scopes and a member function referring
to a later-declared constant retain the source language rules. Unevaluated
`sizeof` and `noexcept` operands create neither storage nor effects.

Discarded expressions also need no constant object. This includes bare expression
statements, explicit void casts, the left side of a comma, discarded conditional
results and loop expressions. Only the potential results of the discarded
expression receive this treatment; it does not propagate into arbitrary
children, receiver arguments, address operands or explicit reference casts.
Through `object.member` or `pointer->member`, the receiver still executes once
and any temporary receiver is destroyed at its ordinary full-expression
boundary. A skipped conditional arm produces no effects or cleanup.

Taking an address, returning or binding a reference, or passing the member to a
reference parameter requires a definition and reports `TR0203` if it is absent.
This also applies in discarded or statically skipped expressions, and to
reference defaults. Creating a new prvalue, such as `+R::value`, can instead
bind its own temporary to a const reference. A mutable member or const member
without a usable constant initializer retains the definition requirement.
TLS and unsupported template forms retain their separate restrictions. Admitted
static storage follows the first-use and nonlocal startup contracts. V1 profiles retain their previous behavior.

Paired native and protocol fixtures cover value widths, constant-expression
contexts, selected branches, missing-definition diagnostics, receiver effects,
cleanup timing, actual storage identity and deterministic relocation. Native
O0/O2 results require CI from the implementing revision. Full C++/STL support
remains unfinished.

## Concrete aggregate class templates

Core v2 admits concrete namespace-scope standard-layout class-template instances with
one to 64 type or scalar integer/bool/enum/nullptr_t parameters. Scalar `auto`
and dependent scalar parameters follow the same argument rules as free function
templates. Type defaults, explicit instantiation and explicit specialization,
including forward declarations followed by definitions, are supported.

Patterns and explicit specializations may declare fields, ordinary type aliases,
enums, static assertions, access labels, ordinary named methods and user-provided
constructors, destructors, operators and conversions as described below. Aggregates and constructed instances must be
complete standard-layout records whose fields satisfy the ordinary type,
array, storage and lifetime rules. Existing implicit special-member operations
remain checked when selected. Nested records/templates and instantiated free
friends and empty base chains follow their separate contracts; other bases remain unsupported.
Namespace partial specializations follow their separate contract below. Template-template parameters and non-scalar value arguments remain excluded.

The producer traverses materialized records without enabling unrestricted
implicit AST traversal. Dependent field types and unused field defaults stay
lazy until Clang instantiates them; selected field initializers and instantiated
aliases/assertions receive ordinary checks. Written parameter types, type
defaults and explicit argument expressions are checked before erasure, including
explicit instantiation/specialization arguments. Direct dependent type metadata
can remain lazy; expression-bearing dependent default types must still pass the
source-expression checks. Unsupported `long double` expressions cannot be hidden by
an integral argument or a `decltype` result. Ordinary non-template declarations
retain their existing checks.

Each instance becomes an ordinary record with explicit fields and native layout
evidence. Equivalent arguments and aliases share the canonical type; different
values, actual types and primaries retain distinct record/field identities.
By-value dependencies are emitted before their containing records. Fixed arrays,
reference mutation, parameter/return storage, copy and field/array destruction
use the existing typed operations, with return capture before cleanup. There is
no runtime template parameter, opaque source or template interpreter.

Native O0/O2 fixtures exercise storage, deduction, default field initialization,
copying, returns and destruction. Protocol fixtures check identities, fields,
extents/layouts, dependency order, scalar values, signatures, cleanup order and
relocation. Native validation requires the implementing revision's CI. Full
C++/STL, standard headers and library containers remain unfinished.

## Ordinary class-template member functions

Admitted standard-layout class templates may contain ordinary named instance and static
member functions with no own template parameters. Const and lvalue/rvalue
reference-qualified overloads, resolved defaults and noexcept, constexpr methods,
static factories, member begin/end ranges and scalar static locals use the
existing method and lifetime rules. Explicit class/method instantiation, explicit
method specialization and in-unit out-of-line definitions are supported. Explicit
full class specializations keep the eager checks of ordinary non-template methods.

Primary method bodies and unused defaults remain lazy. Only concrete methods with
materialized definitions are translated; every such body is checked, including
those forced by explicit instantiation. An unused implicit member declaration may
remain without a body or a deduced auto result. A selected call, including a call
under sizeof/noexcept, still requires a definition. Explicit member instantiation
or specialization declarations retain the in-unit definition requirement. Explicit
class instantiation forces only the member definitions visible at that point.

Each method declaration's shape is checked even when its body remains lazy.
Written outer template parameter types on separate out-of-line definitions receive
the same source checks as the primary parameter list before metadata is erased.
Selected default/noexcept expressions and materialized signatures/bodies must pass
the ordinary profile checks. Deleted declarations use the separate declaration
contract; attributes, virtual/variadic methods and volatile/restrict qualifiers
are excluded. The class
must remain an admitted standard-layout record. Static data, friend,
Member class templates follow the ordinary-owner contract below; bases require the empty-chain contract. Namespace partial
specializations follow their separate contract below. Operators and
conversions follow their separate class-template contract below.

Methods use ordinary typed functions: instance receivers keep their cv-qualified
pointer, record results use existing hidden result storage, and static functions
have no receiver parameter. Equivalent class arguments share method and local
storage identity; different values, types or primaries remain distinct. Local
records and their fields also retain per-instance identities. There is no runtime
template parameter or opaque fallback. Member ranges preserve one-time range
initialization and reference mutation; return values are captured before cleanup.

Native O0/O2 fixtures cover these calls, identities, storage and lifetimes. Protocol
fixtures inspect signatures, selected callees, default values, local record/static
identities, range calls, cleanup and relocation. Native results require CI of the
implementing revision. Complete C++/STL and standard-library containers remain
unfinished.

## Class-template constructors

Concrete namespace class-template instances support user-provided ordinary,
explicit/converting, copy and move constructors. This includes default arguments,
constexpr/resolved noexcept, out-of-line definitions and explicit instantiation or
specialization. Instances may be non-aggregates but must remain standard-layout
records with the existing field and no-base restrictions. Existing named methods
also work on these constructed instances.

Unused constructor bodies, initializers and defaults stay lazy. Selected
construction requires an in-unit materialized definition, including constructions
under sizeof/noexcept; explicit constructor declarations retain the definition
requirement. Every instantiated body and initializer is checked, including
non-written semantic member initializers. Written outer parameter types on
out-of-line definitions are checked before dependent metadata is discarded.
Extra copy-constructor defaults may stay uninstantiated when all arguments are
explicit; omitted defaults still undergo ordinary per-use source and type checks.

Constructors return void and receive destination storage before their runtime
parameters. Member initialization follows declaration order, even when the source
initializer list is ordered differently. Copy and move source references preserve
the existing binding rules; destinations have their own storage and source
mutation is retained. Ordinary member/array construction and destruction,
temporary lifetime extension, by-value arguments and return storage use existing
operations. Constructor-local records and scalar static locals retain concrete
class-instance identities. No template argument becomes a runtime parameter.

Inherited constructors are not included. Delegating constructors follow their
contract below. Member function templates
follow their separate contract below. Operators and conversions follow their separate contract below. Defaulted special members follow the rules below. User-provided template destructors and
member/array cleanup follow the destructor rules below.
Native O0/O2 and protocol fixtures cover calls, defaults, copy/move storage,
initialization order, member/array lifetimes, identities and relocation. Native
results require the implementing revision's CI; complete C++/STL remains unfinished.

## Class-template destructors

Admitted concrete standard-layout class templates support user-provided nonvirtual
destructors, including out-of-line definitions, explicit instantiation and explicit
member/full-class specialization. The existing standard exception-specification,
source-ownership, field, layout and expansion limits apply. Every materialized
owned body is checked, even without a call, and dependent bodies stay lazy until
the embedded frontend instantiates them.

Definition demand is independent of emitted cleanup. A destructor marked used by
Clang must have an in-unit definition (`TR0203` if missing), including a function
that returns an object directly into caller storage. A type-only query does not
force the body. An unevaluated sizeof/noexcept reference checks a resolved written
exception specification, including dependent substitutions and this/member uses,
without requiring an unused body. Unsupported expressions cannot disappear behind
a folded exception value. Explicit member instantiation/specialization declarations
retain the existing definition rule; explicit class instantiation only instantiates
visible definitions.

Each destruction helper is an internal void function with one pointer to the
concrete record. The user body runs before reverse member and array-element
cleanup, including on early return. Existing lexical/full-expression cleanups,
reference lifetime extension, by-value parameters and returned-object ownership
are preserved. Return values are captured before cleanup can change their source.
Template arguments affect identity, not the runtime parameter list; aliases share
helpers and differing instances retain distinct local records and static storage.

The producer queues helpers for materialized user destructor bodies and actual
lowered cleanup. Member cleanup adds dependencies to the same bounded deduplicated
queue. Unused implicit/defaulted wrappers do not force an unused template member's
destructor body. Ordinary materialized user bodies remain checked and emitted.
There is no opaque source or binary fallback and no external Clang process.

Explicit destructor calls follow their separate contract below. Lifetime restart, virtual dispatch/bases,
the other unsupported
template forms remain unfinished. Native O0/O2 fixtures check body/member/array
order, control-flow exits, temporary/reference/parameter/result storage, copy/move,
specializations and local identities. Protocol checks cover exact signatures,
callee closure, pointer identity, unused bodies and relocation. Native results
require CI of the implementing revision; complete C++17/STL is still unfinished.

## Defaulted class-template special members

Concrete instances of admitted namespace standard-layout class templates support
`= default` for the default constructor, copy/move constructors, copy/move
assignment and nonvirtual destructor. Both in-class and out-of-line definitions
use the existing concrete special-member signature checks. Explicit instantiation
and specialization retain their source and definition requirements. Copy sources
may be const or mutable lvalue references as selected by Clang; move sources are
unqualified rvalue references. Copy assignment permits unqualified or `&`
receivers, and move assignment also permits `&&`. Bases, unsupported field types and layouts remain
outside this increment.

Defaulting evidence comes from the concrete declarations or their exact direct
primary member definition. This handles out-of-line defaulting before a lazy
instance has copied Clang's defaulted flag. An explicit specialization cannot
inherit that evidence from its primary. Classification never invents a runtime
body, alters AST flags or bypasses source ownership. Merely referenced operations
can remain without a generated body, but their resolved written exception
specifications and concrete source types still receive normal checks, including
substituted expressions and `this` contexts. Unused dependent initializers and
implicitly deleted instances remain lazy; invalid actual use is a C++ diagnostic.

Used nontrivial operations require owned generated definitions (`TR0203` if
missing). Every materialized constructor or assignment definition is checked,
including bodies generated by explicit instantiation without a caller. First-
declaration defaulting can remain bodyless even after explicit class instantiation;
source validation follows Clang's actual semantic demand instead of forcing a
body. Defaulted destructor bodies must be empty and use the existing demand queue
for reverse member/array cleanup. Unused wrappers do not force field destruction.

Generated constructors initialize the final destination in member order. Value
initialization preserves zeroing for eligible first-declaration defaulting and
never invents zeroing for a user-provided out-of-line default constructor. Copying
and moving preserve selected member operations, scalar and array storage, source
mutation and one-time evaluation; field defaults are not rerun. Assignment updates
existing members, returns the receiver reference and neither reinitializes nor
destroys the receiver. Generated trivial array copies retain bounded typed stores,
without an external memory-copy call. Existing temporary, reference, by-value and
result lifetimes, expansion limits and per-instance identities remain applicable.

Paired source/protocol regressions cover the six defaulted forms, lazy out-of-line
queries, forced definitions, exact helper ABI, field/array order, source and
receiver identity, zeroing, complete call-target closure and relocation. Native
O0/O2 fixtures check values, side effects and lifetimes; results require CI of the
implementing revision. Full C++17/STL, hosted standard headers and other language
frontends remain unfinished. Translation uses embedded Clang libraries and never
requires launching an external Clang executable.

## Class-template operators and conversions

Concrete instances of admitted namespace standard-layout class templates support
user-provided member operators and conversion functions. The operator kinds and
concrete signatures follow the ordinary operator contract: arithmetic, bitwise,
comparison, logical, comma, shifts, assignment, increment/decrement, dereference,
subscript, arrow/arrow-star and call. Copy/move assignment and other ordinary
assignment signatures preserve their selected result and source types. Integral,
bool, pointer, reference and record conversions retain explicit/contextual and
const/ref-qualified selection. Resolved `auto` and `decltype(auto)` conversion
results use their deduced admitted types.

Out-of-line definitions, explicit class/member instantiation and explicit
specialization follow the existing ownership and definition rules. These are
members of a class template; a member's own function-template parameter list
follows the separate member function template contract below. Namespace operator templates follow their separate contract. Allocation/deallocation follows its contract below;
virtual dispatch, unsupported qualifiers, fields and signatures retain their
restrictions. C++20 conditional `explicit` remains outside C++17.

Unused dependent bodies, undeduced conversion returns and lazy defaults are not
forced. Selected declarations, concrete source signatures, instantiated defaults,
written exception specifications and every materialized body are checked before
folding or erasure. This includes constexpr results and unevaluated queries.
Selected user-operator/conversion declarations still require a definition in this
unit, including under `sizeof`/`noexcept` (`TR0203` if absent). Invalid C++17
remains a source diagnostic; valid but unsupported profile forms are rejected.
Out-of-line outer parameter expressions cannot bypass the normal source checks.

Calls use existing typed functions, with a cv-qualified receiver pointer and an
initial hidden destination for record results. Template arguments become concrete
types or constants, never runtime parameters. References retain the actual source
storage. Record prvalues initialize the final destination without an extra copy;
temporary, reference-extension, parameter and result cleanup follow existing
lifetime rules. Operator assignment captures the RHS before the receiver; explicit
member-call syntax captures the receiver first. Overloaded logical operators
evaluate both operands; contextual bool conversions retain builtin short circuiting.

Equivalent arguments and aliases share canonical members and scalar static local
storage. Different class arguments, deduced value types and overloads keep their
own function, local record, field and static identities. Paired source regressions,
native O0/O2 fixtures and protocol assertions cover iterator operations, overloads,
defaults, sequencing, actual storage, lifetime, full call signatures and relocation.
Native results require CI of the implementing revision. Standard headers, hosted
libraries, remaining class-template forms and complete C++17/STL are unfinished.
Only C++ input translation is implemented; E Language, Python and other frontends
remain planned. The C++ frontend uses embedded Clang libraries and does not launch
an external Clang executable.

## Namespace alias templates

Core v2 admits owned namespace alias templates with up to 64 type or scalar
parameters and at most 64 elements per concrete pack. Supported aliases include
scalar and enum types, void, pointers, references, fixed arrays and admitted
class-template instances. Alias chains, namespace imports, dependent member
names, type/scalar/auto defaults and concrete pack substitutions preserve the
underlying canonical type. An alias creates no runtime function, record identity,
static object or wire opcode. Equivalent underlying class instances and function
arguments continue to share their canonical records and static storage.

Each reached concrete template type retains its exact sugared Type identity,
raw template-name location and written argument-source identities. Namespace
aliases also retain the substituted underlying TypeLoc. Explicit arguments,
selected defaults and underlying source are checked even when the resulting
canonical type ignores an argument or folds away an expression. Type source is
never reconstructed from a canonical type or an approximate line number.

The same selected-use evidence covers existing function and class templates.
Successful deduction events own their original and converted defaults; only the
selected callee consumes the matching event. Explicit class specialization and
instantiation declarations, and explicit function-specialization declarations,
retain their own source evidence even when unused or repeated with no effect.
Failed substitution and unselected overload candidates retain ordinary C++
behavior. A successful but unselected candidate does not trigger default-source
validation merely because another use has the same canonical arguments.

Selected non-type arguments also retain the actual substituted parameter
TypeLoc, so a dependent alias used as the parameter type cannot hide unsupported
source behind a folded scalar type. Preliminary explicit function arguments
carry their evidence through the same deduction candidate; an extended pack uses
its fresh complete conversion. Parameter type source is checked in declaration
order before that parameter's selected default. Plain unconstrained auto and
decltype(auto) leaves remain written metadata beside the checked scalar result.
Inherited parameter declarations retain their actual source identities.

An empty deduced function parameter pack still substitutes its parameter type in
Clang. Its separate source record has no argument element and is checked at the
selected function use. An unresolved direct type-pack expansion requires the
matching checked empty type pack; other unresolved shapes remain unsupported.
Class/alias argument-list checking does not substitute an unused empty pack's
parameter type, so that dependent source stays lazy. Per-use temporary parameter
type evidence is bounded to 4096 records and persistent copies share the source
budget. Missing or conflicting source coverage is diagnosed.

Nondependent parameter defaults and alias underlying types are checked at their
primary declaration. Dependent defaults and underlying types remain lazy until
selected. Uninstantiated generic bodies and discarded dependent if-constexpr
branches keep their existing traversal rules. Instantiation-dependence is checked
in addition to canonical type dependence: Ignore<T> can have canonical type int
while its written argument still requires substitution. Evidence collected while
parsing such a body is retained for a later reached source use.

Explicit arguments are visited in the caller's existing source context. The
selected template frame is then installed for ordered defaults and the alias
underlying source. Retained Subst nodes resolve to previously checked parameter
slots, with guarded owner, index, argument kind and reverse pack index. They do
not manufacture a new alias use at a parameter name. Ordinary method, const
method, instantiated member and field-initializer contexts remain active, so
Identity<decltype(this)> and Count<sizeof(this->n)> retain normal this checks.
A separate active-use stack enforces cycle/depth checks throughout argument
traversal, including recursive calls to the same primary. All source collection
shares the 200000-unit budget before copying; selected-use depth is bounded to 64.

Nondependent type defaults receive the same concrete type admission as selected
type arguments, including in unused template declarations. Written type expressions
remain checked after that admission. Dependent defaults retain their normal lazy
instantiation. Qualified function-template calls match successful deduction source
at the exact selected declaration and the written name or qualifier-begin location;
equivalent template values alone do not replace source evidence.

The built-in frontend opts into a private six-file source-preservation patch in
ASTConsumer.h, Sema.h, TemplateDeduction.h, SemaTemplate.cpp,
SemaTemplateDeduction.cpp and SemaTemplateInstantiateDecl.cpp. It substitutes
an alias TypeSourceInfo once and retains substitution wrappers for alias/default
and non-type parameter source. Other profiles and consumers keep their original substitution path and
finality. No external Clang process is invoked. Independent literal archive
fixtures verify complete, idempotent patch states and rejection of missing,
duplicate, partial, mixed, drifted and orphan-marker states before any group file
is rewritten.

Paired fixtures cover source boundaries, default selection, recursion, this
contexts, SFINAE, explicit declarations and 64/65-element limits. Native O0/O2
fixtures verify array extents, reference writes, shared/distinct static objects,
record results, copies, moves and full-expression destruction. Protocol fixtures
check exact types, record reuse, result destinations, reference roots, selected
calls and relocation. Native results require the implementing revision's CI.
Member alias templates follow their separate contract below. Template-template parameters, standard headers and
complete C++17/STL remain unfinished. Namespace class partial specializations
follow the contract below.
Only C++ input is implemented; E Language, Python and other frontends remain
planned.

## Partial declaration source checks

Written class and variable partial specializations retain the successful primary
argument check separately from later partial-selection deduction. A member
variable partial copied into a concrete outer class retains that copy's exact
original declaration, actual written arguments and converted parameter type
sources. Already resolved parameter type syntax is checked even if no inner
specialization is selected. For example, a type alias used by a primary non-type
parameter cannot hide unsupported source solely because the partial's value
argument remains dependent. Original generic class-scope full member variable
declarations also retain pending arguments for their existing source checks.

An unknown-length pack expansion can end Clang's argument-to-parameter matching
before all primary slots are known. The checked prefix and all actual source are
retained; only completely checked primary slots can provide substitution edges.
For an already expanded primary pack, each checked parameter type keeps its
actual forward pack index. A partially checked pack does not become a concrete
source slot. Later selected specializations still require complete concrete
argument and deduction evidence. Checking never guesses missing arguments,
reorders the successful conversion result or requests another instantiation.

Successful argument conversion does not establish a valid partial specialization.
Clang still checks specialization ordering and parameter deducibility; invalid
fixed-arity or expanded-pack patterns retain C++ diagnostics (`TR0202`). An extern
instantiation declaration after an instantiation definition also remains invalid.

Paired native and protocol cases cover direct declarations, unused and selected
copies, valid trailing packs, resolved type sources, pending full declarations
and invalid fixed/expanded-pack language diagnostics. Seven O0/O2 runtime checkpoints check values and static
storage identity. All native results require the implementing CI revision.
Template-owner restrictions remain unchanged by these source checks; full
C++/STL remains unfinished. Only C++ input is implemented. E Language, Python
and other frontends remain planned, and translation uses embedded Clang libraries
without installing or invoking an external Clang executable.

## Ordinary-owner member class templates

Named member class templates are supported inside admitted ordinary record
scopes, including multiple ordinary nested owners. Ordinary owners add lexical
scope identity without adding template parameter levels. Primaries, in-class or
out-of-line partial/full specializations, defaults, bounded type/scalar packs,
forward declarations, definitions and explicit instantiation retain their
existing source and materialization checks. For example:

```cpp
struct Scope {
  template<class T> struct Item { T value; };
  template<class T> struct Item<T*> { T value[2]; };
  template<> struct Item<int*> { int value[3]; };
};
int selected() {
  Scope::Item<int*> item{{1, 2, 3}};
  return item.value[2];
}
```

The full specialization above takes precedence over the partial. Equivalent
instantiations share canonical types and scalar static storage; distinct owners
or arguments retain distinct identities. Records are emitted after by-value
field dependencies, including an enclosing ordinary record that contains its
member class instance. There is no implicit enclosing-object pointer. Ordinary
methods and selected constructors, copies, moves and destructors keep their
existing receiver and lifetime rules. Member function, alias and scalar variable
templates can be used within these classes under their separate contracts.

Every nondependent declaration qualifier remains source-checked before erasure,
including unused out-of-line primaries and partials. Full declarations retain
their own exact successful argument check and written argument source. Pack
owners include the member primary and each direct partial. Limits remain 64
parameters/elements and the existing source-depth and total expansion budgets.
Unsupported generic member bodies retain normal laziness until selected.

Ordinary named record owners follow this contract. Templated outer owners follow
[the dependent-owner contract](#dependent-outer-member-class-templates) below.
Generic nested ordinary records inside a class template, local/union owners,
inheritance, virtual dispatch, unsupported field types and default heap runtime retain
separate restrictions. Full C++/STL remains unfinished. Eighteen O0/O2 runtime
checkpoints and paired protocol fixtures cover layouts, calls, storage identity,
lifetimes and relocation; native results require the implementing CI revision.
Only C++ input is implemented; E Language, Python and other input frontends remain
planned. Translation uses embedded Clang libraries without an external Clang
executable.

## Dependent outer member class templates

Member class primary and partial templates can be nested inside admitted class
primary/partial templates and concrete specializations, including multiple
template levels. Directly written full specializations in concrete outer scopes
and explicit own member-primary/partial specializations retain their own source
and definition. For example:

```cpp
template<class T> struct Outer {
  template<class U> struct Inner { T first; U second; };
  template<class U> struct Inner<U*> { T first; U second[2]; };
};
template<> template<> struct Outer<int>::Inner<bool> { int result; };
int read() {
  Outer<int>::Inner<int*> value{1, {2, 3}};
  return value.first + value.second[1];
}
```

Parameter depth follows actual enclosing template contexts. An original inner
parameter can have depth one or more; a copied primary loses substituted outer
levels. Ordinary record scopes add no template level. Actual primaries and
partials retain semantic argument ownership even when their instantiated body
comes from an earlier member pattern. An explicit own member specialization
requires a written declaration before it can stop that body-origin chain.

Lexical declarations are indexed before hidden specialization lists. Newly found
copied member primaries contribute their own hidden instances. Every written or
copied partial retains its exact successful primary-argument source checks,
including resolved non-type parameter types when values or pack lengths remain
dependent. Unselected hidden copies are checked too; they cannot discard already
resolved source. This reuses the partial-declaration contract without changing
its actual argument slots, pack frontier rules or wire protocol.

Nondependent defaults and qualifiers remain checked before erasure; dependent
source is checked after substitution. Retained out-of-line headers, including
renamed parameter packs and empty specialization headers, match the actual
owner sequence. A partial specialization retains the enclosing template levels
even though Clang marks it as explicitly specialized; only a full specialization
stops that walk. Class tags retain their own header storage. Definitions keep
existing field layout, scalar static identity, method receiver and selected
copy/move/assignment/destruction rules. Member function, alias and scalar
variable templates can refer to these distinct outer and inner levels. Different
outer instances do not share a static object merely because the final field or
function types happen to agree.

Clang can retain dependent type sugar in an instantiated out-of-line member's
qualifier even when its semantic class owner is concrete. That exact declaration
qualifier is checked against the actual enclosing record chain, selected template
origins and every written type/scalar argument, including expanded packs. This
also covers qualified namespaces and selected partials. The exception does not
apply to parameter, body, initializer or unrelated type source. Those sources
retain ordinary checks, including unsupported types hidden by constant folding.

Copied class-scope full declarations follow the separate
[full declaration contract](#copied-class-scope-full-specializations) below.
Ordinary nested records follow the [ordinary member contract](#ordinary-nested-classes-in-generic-owners).
Local/union owners, inheritance, virtual
dispatch, unsupported fields and default heap runtime retain separate restrictions. Limits
remain 64 parameters/elements, bounded declaration/owner depth and the total
expansion budget. Full C++/STL remains unfinished. Paired source/protocol fixtures
and seventeen O0/O2 runtime checkpoints require native CI from the implementing
revision. Only C++ input is implemented; E Language, Python and other frontends
remain planned. Translation uses embedded Clang libraries without invoking an
external Clang executable.

## Copied class-scope full specializations

A full member class specialization written inside an admitted dependent outer
class retains its own body when that outer class is instantiated. For example:

```cpp
template<class T> struct Outer {
  template<class U> struct Inner { int primary; };
  template<> struct Inner<int> {
    T value;
    template<class V> T get(V) const { return value; }
    inline static int count = sizeof(T);
  };
};
int read() {
  Outer<int>::Inner<int> value{3};
  return value.get(0);
}
```

The original full declaration and its concrete copy have separate exact source
events. The embedded frontend retains the successful argument conversion at
both creation points, including defaults, non-type parameter types and the
actual copied declaration's written arguments and original full object. This
also checks copies that are not subsequently selected as an inner object.
Resolved source is checked immediately; original dependent values stay lazy.
An original unknown-length expansion retains the same checked prefix and actual
frontier ordering as the partial-declaration contract. A concrete copy requires
complete arguments. No missing arguments or source edges are reconstructed.

The specialized primary owns the inner arguments. The full record owns its body,
and actual outer instances own substitutions in that body. A full record adds
no template parameter level: in `Outer<T>::Inner<int>`, a member template's own
parameter has depth one before substitution and depth zero in the concrete copy.
Its fields, ordinary methods, constructors, conversions, defaulted special
members, scalar statics and admitted member templates retain the existing
layout, receiver, lifetime and storage rules. Equal inner arguments in different
outer instances do not merge their scalar static objects. Missing required
bodies/storage produce `TR0203`; invalid C++ produces `TR0202`; unsupported
materialized source produces `TR0201` without output artifacts.

Clang eagerly instantiates the full class declaration when copying it. NeverC
checks that actual class definition, while ordinary uninstantiated method bodies
retain their existing laziness. Default member initializers are substituted when
needed, using the copied full's actual member-class origin and enclosing template
arguments. A narrow embedded Clang correction recognizes this origin even though
the copy retains an explicit-specialization kind. It preserves field order and
per-object initialization; explicitly supplied aggregate initializers and copies
do not rerun omitted defaults. Ordinary full declarations and nonrequesting
consumers keep their existing behavior. Cyclic or unavailable initializers retain
their diagnostics, and reached initializer expressions retain source checks. Source checks do not supply a fake parameter
owner, substitute twice, call an external Clang executable or emit an opaque
body. Limits remain 64 parameters/elements and bounded owner/source traversal.
Ordinary nested records follow the [ordinary member contract](#ordinary-nested-classes-in-generic-owners).
Inheritance, unsupported fields,
default heap runtime and complete C++/STL remain unfinished. Only C++ input is implemented;
E Language and Python remain planned. Paired source/protocol fixtures and
seventeen O0/O2 runtime checkpoints require native CI of the implementing
revision; protocol counts remain subject to that verification.

## Ordinary nested classes in generic owners

Named ordinary nested classes in admitted generic classes use their actual
concrete member copies. An own explicit member specialization supplies its own
body, including a different layout or member set:

```cpp
template<class T> struct Outer {
  struct Inner {
    T value;
    T get() const { return value; }
    inline static int count = sizeof(T);
  };
};
template<> struct Outer<bool>::Inner {
  long long value;
  long long get() const { return value + 1; }
};
long long read() {
  Outer<bool>::Inner object{3};
  return object.get();
}
```

Ordinary nested classes add no template argument level. Existing function,
alias, scalar variable and class member templates retain their actual enclosing
parameter owners and substituted source. Primary, partial, copied class-scope
full and ordinary nested scopes can be combined. Forward declarations,
out-of-line definitions with renamed headers, own member specializations and
bounded type/value/empty packs preserve exact declaration and body origins.
Canonical specialization flags alone never establish a written own body.

Nonlocal ordinary nested definitions are lazy. An unused concrete forward copy
requires no invented definition; materialized records use only their actual
fields and members. Uninstantiated ordinary method bodies stay lazy. Copied
class-scope full declarations retain their separate eager instantiation rule.
Already resolved declaration headers and concrete qualifiers are checked before
erasure even when no ordinary nested object is used. Selected constructors,
copy/move/assignment/destruction, receivers, by-value field layout and scalar
static storage retain the existing typed representation. Equal scalar values
in distinct outer instances do not merge their static objects; equivalent aliases
share storage. An own specialization does not reuse the generic body's members.

Explicit ordinary member-class instantiation directives have no separate Clang
AST node. The embedded frontend retains the actual target and origin, qualifier,
name/template/extern locations and parsed attribute presence at each successful
Sema exit. This includes redundant extern and no-effect directives. Each written
qualifier is checked independently even when directives reuse a canonical record.
This event is internal; no public wire object or extra substitution is introduced.
Windows' existing extern-template rules for enclosing-class instantiations remain
those of the embedded frontend.

Named nonlocal standard-layout records and existing field restrictions apply.
Unsupported dependent friend class-template forms and friend-type expansions, unions, anonymous records, inheritance, default heap runtime and complete
C++/STL remain unfinished. Invalid C++ keeps `TR0202`; unsupported materialized
source keeps `TR0201`; missing required function/storage definitions keep `TR0203`,
without output artifacts. Source owner/redeclaration walks and packs remain
bounded. Only C++ input is implemented; E Language and Python remain planned.
Translation uses embedded Clang libraries without an external Clang executable.
Paired source/protocol regressions and seventeen O0/O2 runtime checkpoints require
native CI of the implementing revision; protocol counts remain provisional until
that validation.

## Instantiated non-template friend functions

Non-template free friend functions can be instantiated from admitted class
primary/partial templates, member class templates, copied class-scope full
specializations and ordinary nested class bodies. Named hidden friends and
supported free operators preserve ADL and embedded Sema access checks:

```cpp
template<int N> class Item {
  int value = N;
  friend int read(const Item &item) { return item.value; }
};
int result() { Item<3> item; return read(item); }
```

A friend keeps its actual explicit parameters, without an implicit receiver or
an invented template level. Namespace redeclarations retain one canonical
function identity. Different instance overloads and scalar static locals retain
separate storage. Selected record arguments/results, local classes, copies,
moves and cleanup use the existing typed storage rules. Friendship does not
become transitive or change qualified lookup visibility.

Two internal source events retain the written FriendDecl and the exact incoming,
selected and actual function declarations plus the granting class. Selected
signature/body source may differ from the original friend spelling. Canonical
namespace merging does not replace actual declaration or member-origin checks.
Owner and redeclaration walks are bounded; class packs keep their real owners.
Every selected definition is inspected. Unused generic bodies, auto results and
unselected dependent defaults retain ordinary instantiation laziness.

When signature normalization chooses a different declaration, the incoming
signature needs complete nondependent original source evidence. This checks its
written return and parameter types, including adjusted-away array bounds,
qualifier, its own parsed/instantiated/non-inherited defaults, and both written
and resolved standard exception specifications. Each original default/noexcept
operand is inspected directly. Pending or dependent original evidence yields
`TR0201`; a selected declaration's defaults or resolved noexcept cannot stand in
for that source. Ordinary source fixtures currently establish namespace merging,
not AST-merge normalization. The latter boundary has static source review and
producer identity checks, without a claim of native normalization coverage.

Declaration-only friends retain the same source-unit definition policy as
ordinary declarations. Missing required definitions yield `TR0203`. Invalid
C++ lookup, access or conflicting definitions yield `TR0202`. Unsupported source
produces `TR0201` without output artifacts. Unsupported dependent friend class-template forms, friend
type expansions, inheritance, default heap runtime and complete C++/STL
remain unfinished; existing nondependent friend type/member rules are preserved.
No wire schema or lowering ABI is added. Paired source/protocol fixtures and
seventeen O0/O2 runtime checkpoints require implementing-revision native CI;
protocol cardinalities remain provisional. Only C++ input is implemented;
E Language, Python and other frontends remain planned. Translation uses embedded
Clang libraries without an external Clang executable.

## Instantiated non-template friend types

Admitted generic class bodies support resolved type-form friends, including
`friend T;`, type aliases, `friend typename T::Reader;` and concrete class
template specializations. Primary/partial and member class templates, copied
full specializations and ordinary nested class bodies retain their actual grants:

```cpp
class Reader;
template<class T> class Cell {
  int value = 3;
  friend T;
};
class Reader {
public:
  static int read(const Cell<Reader> &cell) { return cell.value; }
};
```

Embedded Sema decides access, including private types, construction and
destruction. Friendship does not become reciprocal or transitive. A different
class-template argument grants access to that actual type only. Legal nonclass
friends, such as an integer or pointer alias, are ignored for access while their
types still undergo the ordinary support and source checks.

The successful ordinary type-substitution path retains the exact actual/written
FriendDecl pair. The original declaration must be a lexical source root belonging
to the actual granting class's selected original or own body. Missing pairs and
unproved intermediate copy chains fail `TR0201`; matching names, canonical types
or source locations alone cannot supply a lost origin. No second substitution
or access lookup is performed. Original nondependent source and the complete
actual substituted TypeLoc are checked, including elaborated owned tags. A still
dependent type is deferred only in a dependent class context after source identity
checks; a concrete granting class needs a resolved type. Folded aliases and type
expressions retain their source checks.

Friend type grants add no functions, implicit receivers, template levels or
runtime storage. Existing concrete record and method/call lowering remains in
use. Source and protocol fixtures, root relocation, and ten O0/O2 noinline runtime
checkpoints require native CI of the implementing revision; exact protocol counts
remain provisional. Invalid C++ or access yields `TR0202`; ordinary missing
required function definitions remain `TR0203`.

Unsupported dependent friend class-template forms, friend-type pack expansions, unsupported friend template
headers and unproved chained copies remain separate work. Complete C++/STL is
unfinished. Only C++ input is implemented; E Language, Python and other languages
remain planned. Clang libraries are embedded with no external Clang executable.

## Friend function templates

Free friend function templates in admitted ordinary and generic classes support
named functions and the existing free operators. ADL and access remain governed
by embedded Sema. Outer class arguments and inner function deduction, defaults
and concrete type/scalar packs retain their actual owners:

```cpp
template<int N> class Box {
  int value = 3;
  template<class T> friend int read(const Box &box, T extra) {
    return box.value + int(extra) + N;
  }
};
int main() { Box<2> box; return read(box, 1) - 6; }
```

The same rules apply to admitted partial/full/member class templates, copied
full specializations and ordinary nested records. Calls use existing concrete
free-function lowering, without an implicit receiver. References, record results,
construction/destruction, local classes and scalar static locals keep their
ordinary storage and lifetime rules. Equivalent canonical calls share one
instance; distinct outer instances, inner arguments and actual primaries retain
separate identities. C++17 name visibility still applies: hidden friends are
found through ADL, and explicit template syntax requires ordinary lookup rules.

The embedded source bridge retains both the exact copied/written friend pair
and the actual copied function-template primary with its incoming declaration,
selected declaration and granting class. Original declarations must be lexical
source roots. A normalized different original signature needs complete independent
nondependent source evidence; unresolved normalization and unproved copy chains
remain `TR0201`. No second substitution or lookup supplies missing proof.

Template-origin metadata is shared across redeclarations. A declaration-only
friend can therefore acquire an origin when a later definition joins its chain.
Per-declaration events remain authoritative. Inner parameters and stable identity
belong to the actual primary; the function body uses the compatible declaration
and lexical context selected by Sema at its existing instantiation boundary.
Those contexts may come from a different granting class or outer parameter depth.

A copied canonical primary with no independent written namespace/friend identity
includes its proven actual granting-class identity in the existing template key.
A real canonical merge into an independently indexed declaration preserves that
identity. Original source checks remain separate from canonical deduplication.
No absolute root, pointer address or traversal allocation order is used as a key.

Generic bodies remain lazy. Original nondependent signature/default source,
actual substituted signatures, selected defaults and every materialized body
are checked, including folded source and local method packs. Existing source,
parameter and pack budgets remain in force. Invalid C++ and access yield `TR0202`;
missing required definitions retain `TR0203`. Referenced unused inline friends
with exact retained source and a concrete resolved signature can remain lazy in
unevaluated queries; their selected defaults still require source checks.
Unsupported syntax yields `TR0201` without artifacts.

Paired source/protocol fixtures cover ADL, access, owner variants, independent
inner/outer arguments, declaration-only/namespace merges and compatible bodies
from different classes/depths. Ten O0/O2 noinline runtime checkpoints exercise
values, receivers, lifetimes, local classes, packs and static identity. Protocol
fixtures check typed free calls, canonical reuse, separate storage, reference
closure and root relocation. All native behavior and exact output counts require
CI of the implementing revision; source fixtures alone do not prove every Sema
normalization path.

Unsupported dependent friend class-template forms, unsupported friend headers/copy chains, function
pointers and complete C++/STL remain unfinished. Only C++ input is implemented;
E Language and Python remain planned. Clang libraries are embedded and no
external Clang executable is installed or called for translation.

## Friend class templates

Ordinary friend class-template declarations in admitted ordinary and generic
classes support namespace introductions, existing targets and supported qualified
namespace/member targets. Friendship grants access to the target template's
specializations under the existing C++ rules; it is neither reciprocal nor
transitive. The target's ordinary class definitions, partial/full specializations,
fields, methods, constructors, destructors and static storage retain their normal
profile checks.

```cpp
template<class T> class Vault {
  int value = 3;
  template<class U> friend struct Reader;
};
template<class U> struct Reader {
  static int get(const Vault<int> &v) { return v.value; }
};
int main() { Vault<int> v; return Reader<bool>::get(v) - 3; }
```

Repeated grants from different outer instances share the same semantic target
class and static storage. They do not create extra target instances, runtime
access functions or a granting-class suffix in the target identity. A forward
friend declaration never supplies a class body: the actual namespace or admitted
member definition remains authoritative.

The embedded source bridge records each successful copied class-template target,
original declaration, actual granting record, resolved lookup context and selected
previous target. The actual/written FriendDecl pair is retained independently.
Dependent original friends are deliberately absent from the ordinary lookup and
redeclaration chains in pinned Sema. Their lexical header is therefore separate
from the copied target, the parameter/default header selected by lookup, and the
actual body definition. Existing retained parameter pointers must belong to the
real target chain and supported slots. No additional substitution or lookup is
used to fabricate missing evidence.

Qualified friend lookup can resolve an earlier namespace block while its previous
target was declared in a reopened block. These contexts must identify the same
primary namespace; the exact lookup result and preceding target chain are still
checked independently.

Each original and actual header checks supported type/integer/boolean/enum/nullptr_t
parameters, packs, nondependent type source and qualifiers. Pending dependent
syntax stays lazy until a successful copy or use. Written defaults on the original
friend class-template declaration produce `TR0202`, even for an unused generic
granting class: pinned Sema omits this language diagnostic in dependent contexts.
Defaults actually inherited from a previous target remain allowed and source
checked. Hidden folded source remains subject to `TR0201`; invalid access and
declarations yield `TR0202`; required missing definitions yield `TR0203`.

Paired source/protocol fixtures exercise introductions before and after generic
granting instances, existing and qualified targets, different header depths and
names, inherited defaults, partial/full/member/nested granting bodies and shared
target identities. Ten O0/O2 noinline runtime checkpoints cover private aliases,
construction/destruction, references, repeated grants and static storage.
Protocol fixtures check record fields, typed concrete calls, canonical reuse,
distinct actual class specializations, closed references and root relocation.
Native language validity and output counts require CI of the implementing revision.

This increment admits exact ClassTemplateDecl friend targets. Dependent friend
forms represented as unsupported FriendTemplateDecl or unsupported type/header
nodes, template-template parameters and complete C++/STL remain unfinished. Only
C++ input is implemented; E Language and Python are planned. Clang libraries are
embedded, with no external Clang executable installed or called for translation.

## Ordinary function pointers

Standalone `cpp-core-v2` supports typed pointers to ordinary source-owned free
functions and static member functions with definitions in the selected unit.
Unambiguous decay, explicit address-taking, unary plus, selected ordinary overloads,
parenthesized/dereferenced designators, null values, boolean conversion,
same-signature equality, copying, assignment, comma and conditional expressions
are supported. The source-selected callback is saved before argument evaluation,
including compound postfix expressions whose final target Clang can determine.
Static method access evaluates its object/pointer base exactly once. Discarded
function designators without address-taking or decay retain their previous rules.

```cpp
using Callback = int (*)(int);
int plus_one(int n) { return n + 1; }
int apply(Callback callback, int n) { return callback(n); }
int main() { return apply(plus_one, 2) - 3; }
```

Signatures have a result and at most 64 parameters, within the existing recursive
source and protocol budgets. Results and parameters use admitted integer/boolean
scalars, object pointers, nested function pointers or existing reference carriers;
only results may be void. Parameters and results additionally admit complete
source-owned records by value. The caller constructs an independent parameter
object, passes its hidden pointer through the callable signature and retains the
selected copy/move construction and callee-owned destruction. A record result
uses the direct-function ABI's leading caller-owned destination pointer, so a
prvalue constructs in its final destination without an extra copy. References
to records or fixed arrays retain binding, constness and alias behavior. Function references
require separate lowering and remain unsupported. Function-type aliases
are accepted as source spellings for these pointer signatures, without adding a
bare function-value IR type. Nested factory callbacks may return callbacks. Record declarations are ordered
before any callback signature that needs their complete array element types;
plain references or pointers to records retain forward declarations.

Callbacks may occupy parameters/results, local arrays, record fields and pointers
or references to callback storage. This extends the earlier integer/boolean/enum
static-storage contract with nonvolatile callback globals, static locals and
ordinary static data members, including non-template members of admitted concrete
class instances. Constant initializers must be null or checked symbolic function addresses;
zero initialization and mutable reseating are supported. Dynamic callback
initializers follow the first-use and startup contracts. Thread-local storage
retains its separate restrictions. Constant
record aggregates may contain callback fields under their existing rules.
Object-pointer storage follows the [static address contract](#static-object-pointer-storage).

The actual Clang FunctionProtoType must use default ExtInfo and parameter ABI
metadata, method/ref qualifiers, SME attributes, function effects and address
spaces. Object pointer/reference components also retain default address spaces;
source address-space qualifiers are not erased from callback signatures. Resolved ordinary exception specifications are checked separately. C++17
noexcept-to-potentially-throwing pointer conversion preserves the same normalized
signature; Clang still rejects the reverse conversion and checks noexcept queries.
Code-pointer size and alignment are compared with the independently specified
NeverC default pointer carrier. Each emitted function-pointer signature also gets
its own sizeof/alignof guards. A source layout mismatch is `TR0204`.

Emission uses actual typed C declarations, named function addresses and typed
indirect calls. Target prototypes precede callback global initializers. There are
no integer address tables, source-text wrappers or external compiler processes.
Function definitions and call arguments/results must close under exact normalized
signatures. Missing owned definitions produce `TR0203`, unsupported source forms
produce `TR0201`, and invalid C++ remains `TR0202`.

Exact direct calls, including parenthesized direct calls, retain existing record
ownership and template-function behavior. Concrete template-function addresses
follow the source-selection contract below. Nonstatic member pointers,
lambda conversions, variadics, nondefault ABI metadata, function/object/integer
pointer conversions, casts between different signatures, pointer arithmetic and
ordering remain unsupported. Null or uninitialized callback invocation has no
portable result and is not used as a defined-behavior test case.

Paired native/protocol fixtures cover typed values, signature closure, storage,
postfix order and root relocation. O0/O2 runtime checks with inlining disabled
exercise callback reseating during argument evaluation, static-base effects,
references, arrays, copied fields, nested factory results, temporary cleanup and
static identity.
Synthetic IR tests independently check canonical type grammar, address definitions,
indirect operands and rejection before output. Native fixture validity, protocol
counts and runtime behavior require CI of the implementing revision.

This increment does not widen `cpp-core-v1`, `cpp-project-v1` or `cpp-math-v1`.
Cross-translation-unit callbacks, project callback symbol remapping and owning-TU
inline callback addresses remain separate work. Complete C++/STL is unfinished.
Only C++ input is implemented; E Language and Python are planned. Clang libraries
are embedded in NeverC; translation neither installs nor invokes external Clang.

## Concrete function-template pointers

Core v2 also admits callback values selecting concrete free-function and static
member function-template instances. Explicit template-ids (`&get<int>` or
`get<int>`), deduction from an expected pointer signature, supported overloads,
qualified names, defaults, bounded packs, explicit specializations and owned
instantiation definitions use the same target identity as direct calls. Admitted
ordinary, generic and nested class owners and namespace-visible friend templates
retain their existing access, lookup and source-origin requirements.

A selected declaration is not sufficient by itself. The embedded frontend retains
Clang's successful complete selection event, including the actual specialization,
canonical and source arguments, selected defaults and parameter types. Each use
checks its exact selected function and source location. Written arguments, alias
sources and expressions under constant folding or `noexcept` still pass the normal
bounded source checks. There is no second deduction, guessed source or fabricated
function definition. The selected primary, owner and actual body must be supported;
a named callback without an owned definition produces `TR0203`.

The existing `fnptr` signature, `function_address` and `indirect_call` representation
is reused. Repeated addresses and direct calls of the same canonical specialization
share one emitted definition and its static-local objects. Different template
instances remain distinct even when their normalized signatures are identical.
Friend outer/inner instance identity follows the same source-owner rules as direct
calls. The postfix callback is captured before argument evaluation.

Pinned Clang can attach a visible friend-template specialization to a namespace
declaration even though its body originates in an instantiated granting class.
The private declaration-instantiation walk follows the compatible definition's
concrete lexical class when resolving original fields, methods and static members.
It preserves the actual declaration's lexical context and the independently
retained body-source evidence. The upstream CI experiment isolates this context
loss; native translator acceptance still requires the implementing revision's CI.

Braced callback arrays retain the written clause alongside Clang's selected
semantic initializer. A stale unresolved name is admitted only when the same
explicit clause proves the selected lookup declaration, name, qualifier,
template-argument source objects and source locations. Parentheses and address
operators must also match their selected wrappers. The semantic expression checks
the function body, signature and complete template source before the written
overload pseudo-type is skipped. Unsupported syntax in a second clause remains
diagnosed even when it selects the same canonical specialization.

The ordinary callback contract still excludes function references, nonstatic
member pointers, constructor/conversion-template addresses,
variadics, lambda conversions and nondefault ABI metadata. Function-pointer
non-type template arguments and cross-unit callbacks remain unsupported.
Variable-template callback storage follows its own contract below. Existing direct calls and discarded function
designators keep their previous rules. No project profile is expanded.

Paired source and protocol fixtures cover selection, source rejection, signature
closure, canonical identity and relocation. O0/O2 runtime fixtures with inlining
disabled check shared and distinct static storage, callback capture, references,
arrays, copied fields, member/friend instances and temporary cleanup. Native
acceptance requires CI for the implementing revision. Full C++/STL is unfinished;
only C++ input is implemented, with E Language and Python planned. Translation uses
embedded Clang libraries and does not invoke an external Clang executable.

## Callback variable templates

Core v2 supports callback storage in concrete namespace and static-member variable
templates, with the existing admitted primary, partial/full specialization and
owner forms. Fixed and supported dependent function-pointer types, directly
written `auto`/`const auto`, defaults, bounded packs and explicit instantiation
retain the variable-template source rules. Supported pointer/reference declarators
around the actual written `auto` token, including `auto *` and `auto (*)()`, retain
that token and traversal of the complete written type. The concrete deduced type
must still satisfy the static-storage and callback-signature rules.

Each materialized instance retains its own actual type substitution, first/previous
and completion declaration relation, selected argument/default source and reached
initializer. Final callback type equality cannot replace those source checks.
Function-template address initializers retain the selected target's source and
owned definition checks. Folding does not skip unsupported initializer syntax.
Unused generic initializers remain lazy; auto deduction checks the initializer it
needs. An evaluated use, storage address or reference requires a definition.

Initializers must normalize to a checked symbolic defined function address or
null; mutable definitions without an initializer are zero-initialized. `constexpr`
callback variables preserve const storage. Equivalent template arguments and
redeclarations share one global; different instances have separate storage even
when initialized with the same function address. Reseating one instance does not
change another. References/pointers to the stored callback and static-member
receiver effects use ordinary typed lowering. Declaration-only integral constant
metadata remains separate and does not invent callback storage.

Dynamic initialization, thread-local/volatile storage and record variable
results, function-pointer non-type template arguments, unsupported callback
signatures and cross-unit callback linking remain unfinished. The existing
`fnptr`/`function_address`/`indirect_call` protocol is unchanged. Previous profiles
retain their restrictions. Paired source/protocol, relocation and O0/O2 noinline
runtime fixtures require native CI for the implementing revision. This does not
complete C++/STL. Input remains C++ only; E Language and Python are planned. Clang
libraries are embedded and no external Clang executable is invoked.

## Concrete member variable templates

Admitted ordinary records, including nested records, and concrete class-template primary, partial
and full instances support scalar static member variable templates. Results are
integer, boolean, enum, float or double values, including scalar deduced `auto`, with zero or
constant initialization. Callback results additionally follow the
[callback variable-template contract](#callback-variable-templates), and fixed
arrays follow their [static storage contract](#fixed-array-static-storage).
Object-pointer results follow the [static address contract](#static-object-pointer-storage).
Inner type/scalar arguments, defaults, partial/full
specializations and bounded packs retain the existing 64-entry limits. Ordinary
C++ access, specialization ordering and required definitions still apply.

```cpp
template<int N> struct Values {
  template<class T> inline static int item = N;
  template<class T> static const int constant = N + 1;
  template<class T> static int late;
};
template<int N> template<class T> int Values<N>::late = N + 2;
int main() {
  Values<3>::item<int> = 5;
  return Values<3>::item<int> + Values<4>::item<int>
       + Values<3>::constant<bool> + Values<3>::late<int>;
}
```

Equivalent arguments share a canonical global; different inner or outer
arguments keep separate objects. Out-of-line definitions retain their exact
previous concrete declaration, selected primary/partial and type substitution.
Both first declaration and later definition source are checked. Specialized
member primaries and partials use their own definitions; copied patterns retain
the selected outer class origin. No use is recovered by matching argument values
or approximate source positions, and no second Sema substitution is performed.
Nondependent written outer qualifiers are checked even for unused templates.

Non-inline const values with an already checked in-class constant initializer
may be read or discarded without emitting a storage definition. Evaluated
addresses and references require a definition. Unused fixed-type queries do not
force an initializer or create storage; deduced auto still requires its actual
initializer. Object and pointer access evaluate the receiver once and preserve
full-expression cleanup, using the existing typed storage and lifetime paths.

Five former rejection sources are promoted without changing their spelling.
Paired source/protocol fixtures and twenty-two O0/O2 runtime checkpoints cover
storage identity, values, mutations, defaults, partial/full selection, late
initializers, receiver effects and temporary destruction. Protocol fixtures
check canonical globals, omitted declaration-only storage, calls and relocation.
Native validation requires the implementing CI revision. Invalid C++ reports
`TR0202`, unsupported source reports `TR0201`, and required missing definitions
report `TR0203`; failed translation emits no artifacts.

A full member variable specialization written inside a dependent outer class
retains its original declaration and the exact copied declaration in each actual
outer instance. Already resolved written arguments, selected defaults and
non-type parameter types are checked even when the outer class is unused.
Remaining outer-dependent source is checked after instantiation. The full
initializer has no inner generic slots and cannot borrow a caller's arguments.
The copied declaration is joined to its exact original pattern and successful
argument/type source without repeating substitution. For example:

```cpp
template<int N> struct Full {
  template<class T> inline static int value = 1;
  template<> inline int value<int> = N;
};
int& first() { return Full<3>::value<int>; }
int& other() { return Full<4>::value<int>; }
```

The objects above remain distinct. Concrete namespace-written full
specializations and those in an ordinary or fully specialized outer class keep
their own declaration source. Nested member accesses check argument source once
and still evaluate each receiver once, with the existing 64-level source bound
and total expansion budget. Other result types,
thread-local storage, local/union owners, unsupported template owner chains, template-template parameters, inheritance, default heap runtime and full
C++/STL remain unfinished. Only C++ input is implemented; E Language, Python and
other frontends are planned. Translation uses embedded Clang libraries and
launches no external Clang executable.

## Concrete member alias templates

Admitted ordinary records, including nested records, and concrete class-template primary,
partial and full specializations support member alias templates. Their type,
scalar, enum and scalar `auto` arguments, defaults and packs follow the existing
64-entry parameter/pack limits. Results may use admitted scalar, void, pointer,
reference, fixed-array and record types. C++ access and lookup remain authoritative.

```cpp
template<class T> struct Types {
  template<class U = T> using Value = U;
  template<int N> using Array = T[N];
};
struct Aliases {
  template<class T> using Reference = T&;
};
int main() {
  Types<int>::Array<2> values{3, 4};
  Aliases::Reference<int> first = values[0];
  first = 5;
  return values[0] + values[1];
}
```

Outer class arguments and inner alias arguments retain separate source contexts.
Copied member aliases must identify their exact written origin and selected
class pattern, including reordered partial-specialization parameters. An alias
written in a full class specialization has its own declaration. Equal underlying
types keep their existing identities; using aliases does not create runtime
objects, distinct overloads or copies of the referenced storage.

Every concrete use checks written arguments, selected defaults, non-type
parameter types and the actual substituted underlying TypeLoc. Canonical type
folding cannot hide unsupported source. A nondependent declaration is checked
even when unused, including a member alias made nondependent by outer class
substitution. Still-dependent underlying types retain template laziness until
substitution. Unselected class partials do not force their dependent aliases.
A member alias can retain a dependent non-type parameter type such as
`decltype(T{})` until its actual use, including in an ordinary class. The selected
substitution checks the complete type source, so expressions such as
`decltype((sizeof(long double), T{}))` cannot conceal an unsupported operand.
The embedded Clang frontend retains the original two-stage substitution source;
it performs no second source-only substitution and launches no external compiler.

Three former member-alias rejection cases are promoted without changing their
source. Paired source/protocol fixtures, nine O0/O2 runtime checkpoints, concrete
signature/storage behavior and relocation checks require validation on the
implementing CI revision. Source-depth and shared source-budget guards still
apply. Qualified-name wrappers and terminal type locations retain their complete
source checks without consuming another recursion level: 64 nested member-alias
uses remain admissible, while 65 exceed the limit.
Unsupported source reports `TR0201`, invalid C++ reports `TR0202`, and
required missing definitions report `TR0203`.

Local/union owners, unsupported template owner chains, template-template
parameters, inheritance, default heap runtime and hosted standard-library headers remain
outside this increment. C++17 does not allow explicit or partial specialization
of an alias template. Full C++/STL remains unfinished. C++ is the only implemented
input frontend; E Language, Python and other frontends are planned.

## Concrete member function templates

Admitted standard-layout ordinary records, including nested records, and concrete
class-template instances in namespaces or ordinary record scopes support named and static member function templates, ordinary operator templates,
constructor templates and conversion-function templates. Their concrete parameter
and result types use the existing scalar, pointer, reference, array and record
rules. Type/scalar parameters, C++17 scalar `auto`, selected defaults, concrete
packs, out-of-line definitions, explicit instantiation and specialization are
included. Each parameter list and pack has at most 64 entries.

```cpp
template<int N> struct Counter {
  template<class T> static int& state() {
    static int value = N;
    return value;
  }
  template<class T> int add(T value) { return N + int(value); }
};
struct Number {
  int value;
  template<class T> Number(T n) : value(int(n)) {}
  template<class T> operator T() const { return T(value); }
};
int main() {
  Number number = 3;
  int value = number;
  Counter<4> counter;
  return counter.add(value);
}
```

A member's own template arguments remain separate from its enclosing class
arguments, including selected class partials and member-template specializations.
Written member primaries receive deterministic identities before hidden concrete
copies are indexed. Equivalent calls share functions and scalar static locals;
different inner arguments, outer instances or written primaries remain distinct.
No template arguments or source-check records enter the runtime protocol.
Concrete outer class qualifiers on member-template declarations are checked
even without an instantiated body. A folded argument or erased alias inside
the qualifier cannot bypass the existing source checks.

Selected calls retain the actual function and its written arguments, selected
defaults and non-type parameter types. Constructor and implicit-conversion
expressions also retain their exact successful selection location before lifetime
or conversion wrappers are added. This matters when an equals token or implicit
member name differs from deduction's location. The embedded frontend records
these already-computed results; it does not repeat deduction or instantiate a
body just to recover source evidence.

Materialized function definitions and selected function defaults have their own
exact argument contexts. Nested instances cannot borrow another specialization's
slots. A specialization of the member template itself uses its own body while
retaining the actual inherited function-default source selected by C++. Unused
bodies, dependent defaults and unselected candidates remain lazy. Every
materialized body, selected exception specification and explicit directive still
receives source checks, including repeated and no-effect directives.

Calls reuse existing receiver/result storage, C++17 evaluation order, reference
aliases and construction/destruction rules. Static calls through an object retain
receiver effects. Constructors cover direct/copy initialization, argument/return
conversions and member initialization. Conversion templates preserve the selected
scalar, pointer, reference or record destination and subsequent standard
conversions. Access and invalid C++ diagnostics remain authoritative.

Unsupported template owner chains,
template-template parameters, unsupported dependent friend class-template forms, inheritance, virtual dispatch, unsupported layouts, default heap runtime and
standard-library headers remain outside this increment. A constructor template
cannot be explicitly defaulted under C++17; that remains a language error.
Source depth is bounded at 64 with the shared 200000-unit budget. Unsupported
materialized source reports `TR0201`, invalid C++ reports `TR0202`, and required
missing definitions report `TR0203`. Legacy core v1 keeps rejecting templates.

Paired source/protocol cases, 20 O0/O2 runtime checkpoints, canonical call/storage
identity, reference closure and relocation fixtures are included. Native results
require the implementing revision's CI. Full C++/STL remains unfinished. C++ is
the only implemented input frontend; E Language, Python and others remain
planned. Translation uses embedded Clang libraries and requires no external
Clang executable.

## Namespace scalar variable templates

Owned namespace variable templates support non-volatile integer, boolean, enum,
float and double results with zero or fully checked scalar constant initialization. This includes
plain or constexpr variables, C++17 inline variables, deduced `auto` and
`decltype(auto)`, primary templates, selected partial specializations, explicit
full specializations, type/scalar defaults and concrete packs. Each parameter
list and pack has at most 64 entries. Member variable templates follow their
separate contract above. Callback results follow the
[callback storage contract](#callback-variable-templates). Fixed arrays follow
their [static storage contract](#fixed-array-static-storage), and object pointers
follow the [static address contract](#static-object-pointer-storage). Nonlocal dynamic
initialization follows the startup contract; other result types and TLS remain excluded.

```cpp
template<int N> int counter = N;
template<class A, class B> inline constexpr bool same_v = false;
template<class T> inline constexpr bool same_v<T, T> = true;
template<class T> constexpr int depth_v = 0;
template<class T> constexpr int depth_v<T*> = 1 + depth_v<T>;
static_assert(same_v<int, int>);
static_assert(depth_v<int**> == 2);
int& first() { return counter<3>; }
int& again() { return counter<1 + 2>; }
```

These are source-defined C++17 traits; standard-library headers are not implied.
Trait values can supply `static_assert`, `if constexpr`, scalar defaults and
arguments to admitted function, class and alias templates. Template selection,
partial ordering, substitution failures and diagnostics follow embedded Clang.
Equivalent primary/argument identities share one typed global object. Distinct
templates or arguments retain separate storage, including mutable zero state,
addresses and references. No runtime template parameters or new wire opcodes are
introduced.

Fixed-type queries such as `sizeof(v<int>)` inspect the actual variable type
without forcing its unused initializer or creating a global definition. A
resolved-auto type query can require the initializer for type deduction. Value,
address and required explicit-definition uses must have a definition in this
source unit. `extern template` alone does not provide one. A non-const namespace
full specialization without an initializer is a zero-initialized definition;
a second full definition is a language error. A full specialization cannot carry
an explicit storage-class specifier such as `extern` or `static`.

Every reached variable reference and explicit directive retains its actual
written argument source, selected defaults and non-type parameter type source.
Selected partials validate their own deduced slots and substituted primary
pattern. Their exact successful sugared deduction list identifies the chosen
candidate; class partials use a different canonical list. Argument-value equality
alone never identifies either selection. Failed or unselected candidates retain
ordinary template laziness.

The already-performed first declaration and later definition type substitutions
are both checked, including alias and `decltype` spellings that canonical types
would erase. Direct written `auto` tokens remain source metadata only when the
final type is an admitted scalar. Materialized initializers are always traversed,
even if their values fold to constants. Each initializer uses its exact selected
primary/partial argument frame; nested instances cannot borrow a caller's slots.
A full specialization checks its own type and initializer. No additional
instantiation or type substitution is performed just to obtain source evidence.

Source depth remains bounded by 64 and shares the 200000-unit expansion budget.
Unsupported materialized source reports `TR0201`, invalid C++ reports `TR0202`,
and required missing definitions report `TR0203`. Legacy core v1 keeps rejecting
variable templates. Paired source/protocol fixtures, 20 O0/O2 runtime checkpoints,
canonical storage, reference closure and relocation checks are included; native
results require the implementing revision's CI. Complete C++/STL remains
unfinished. Only C++ input is implemented; E Language, Python and other input
frontends remain planned.

## Class-template partial specializations

Owned namespace class-template partial specializations support the same admitted
concrete type and integer/bool/enum/nullptr_t/auto arguments, fields, ordinary members,
scalar static data, constructors, destructors and defaulted operations as primary
class templates. Each parameter list and concrete pack has at most 64 entries.
Records still require supported standard-layout storage with no bases or an
admitted empty base chain; member
class templates, template-template parameters and full standard-library
headers remain outside this increment.

```cpp
template<class T> struct View;
template<class T, int N> struct View<T[N]> {
  T data[N];
  int count() { return N; }
};
int main() {
  View<int[3]> v{{2, 4, 6}};
  return v.count() == 3 && v.data[2] == 6 ? 0 : 1;
}
```

Embedded Clang selects the primary, the uniquely best matching partial, or an
explicit full specialization under normal C++17 rules. Successful deduction,
partial ordering, non-deduced contexts, namespace lookup and SFINAE keep their
language semantics. Ambiguous or otherwise ill-formed required uses retain
`TR0202`. Uninstantiated dependent member bodies and unselected candidates remain
lazy. A partial declaration's ordinary structural constraints and nondependent
written source are still checked even if that partial is never selected.

Primary arguments identify the concrete record. The selected partial has its
own separately checked deduced parameters: for `View<int[3]>`, the primary has
one type argument while the partial has the type `int` and scalar `3`. Parameter
order may also differ. Types, scalar substitutions and pack counts use the exact
selected owner, slot and pack element, preserving Clang's reverse substitution
index convention. They never reinterpret a partial slot as a primary slot.
Equivalent aliases and redeclarations share the same concrete record, methods
and static objects; distinct concrete primary arguments retain distinct identity.
Out-of-line definitions, explicit class/member/static instantiations and admitted
member specializations use their real declarations and selected definitions.

The private source hook retains two completed records: the partial deduction,
including its selected non-type parameter type source, and the actual substituted
written pattern checked against the primary arguments. The exact deduced AST
argument-list object is transferred by Clang to the selected instance; that
object joins the two records to the class. A canonical argument value alone does
not identify the candidate. Collection uses the existing shared source budget;
selected-source recursion has a depth bound and cycle checks. No extra deduction,
instantiation, external compiler process or runtime protocol operation is added.

The written pattern is traversed in its deduced-parameter context before the
primary argument frame is installed. Alias expansion, folded scalar expressions,
selected primary defaults, non-type parameter types and deduced empty-pack type
substitutions keep their source checks. Unsupported source erased to an otherwise
supported canonical type still receives `TR0201`; neither a losing candidate nor
an uninstantiated dependent body is forced just to inspect its source. Actual
class-associated substitutions use that class's exact selected argument list,
preventing a recursive instance from borrowing another instance's source frame.

Paired native/protocol fixtures cover selection, different parameter layouts,
primary/full-specialization precedence, aliases, forward and out-of-line
declarations, remove-reference/conditional/is-same/enable-if-style traits, empty/nonempty and 64/65 packs, source erasure, diagnostics and
missing selected definitions. The native O0/O2 fixture checks 18 runtime
checkpoints for values, field arrays, reference writes, shared/distinct static
storage, recursive instances, copy/move effects and destruction. Protocol fixtures
check selected call targets, exact record/field types, result destinations,
reference identity, destruction, closure and relocation. Native validation
requires the implementing revision's CI; these fixtures do not establish full
C++17/STL coverage. Only C++ input is implemented; E Language, Python and other
language frontends remain planned.

## Scalar template parameter defaults

Admitted namespace function, operator and class templates support scalar non-type
parameter defaults, including integer, boolean, enum and nullptr_t values, scalar C++17 auto,
and types or expressions depending on earlier parameters. Deduction takes
precedence where applicable; otherwise embedded Clang substitutes the selected
default. Partial explicit argument lists, inherited defaults and ordinary explicit
instantiation/specialization retain C++ lookup and selection. Equivalent omitted
and explicit arguments share canonical functions, records and static storage;
different values or deduced types retain independent identities.

The frontend checks every nondependent written default through the existing source
visitor, including unused or overridden declarations. Unsupported `long double` source
cannot disappear through constant folding. Unused dependent defaults stay lazy;
an explicit value can bypass them. Once a successful default belongs to a reached
selected use or explicit declaration, its original and converted source is checked
together with the final scalar argument type. This preserves dependent arithmetic, sizeof/noexcept,
constexpr calls and conversion-added operations. An allowed constexpr record
conversion can produce an integer argument; this does not admit record-valued
non-type parameters. Selected calls retain ordinary source-definition checks.

Private callbacks attach successful defaults to their own completed template-use
or deduction event. Original and converted ArgLocs and canonical results survive
erasure. Failed substitution/conversion retains normal SFINAE overload fallback;
unselected candidates stay lazy. Reached type uses, selected callees and every
explicit declaration/directive consume their matching source evidence. Actual parameter identity is
matched at its index across at most 64 redeclarations of the same template, so
inherited defaults retain their real declaration provenance. Evidence collection
shares the 200000-unit template source budget. Core v2 retains substitution
wrappers on the private source-bearing paths; deduction results and canonical
identities stay unchanged. No runtime parameters, global initialization or opaque
representations are added.

Final defaults must be supported scalar constants. Pointer/reference/record-valued
arguments, unsupported dependent friend class-template forms, standard headers and remaining
C++17/STL features are still unfinished. Invalid C++ retains TR0202; unsupported
profile source uses TR0201, and selected definitions missing from this source unit
use TR0203 where ordinary rules require them.

Paired source fixtures cover lazy and inherited defaults, both conversion paths,
source checks, overload fallback and diagnostics. Native O0/O2 fixtures cover
actual arrays, object lifetime, canonical static objects, aliases, operator defaults
and typed auto identities. Protocol fixtures check signatures, values, extents,
call closure and relocation, including no runtime default-expression calls.
Native validation requires the implementing revision's CI. Only C++ input is
implemented; E Language, Python and other frontends remain planned. Translation
uses built-in Clang libraries and does not launch an external Clang executable.

## Class-template scalar static data

Admitted concrete class templates support integer, boolean, enum, float and double static data
members with checked zero or constant initialization. C++17 inline/constexpr,
out-of-line definitions, scalar auto/decltype(auto), private/protected access,
explicit member instantiation/specialization and full class specialization retain
ordinary C++ source semantics. A dependent static type must resolve to a supported
scalar, a [fixed array](#fixed-array-static-storage) or an
[object pointer](#static-object-pointer-storage) or a
[static reference](#static-reference-bindings), or a
[static record](#static-record-objects). Dynamic initialization follows the startup
contract; volatile/thread-local data remain excluded.

Each actual definition becomes one ordinary typed global. Equivalent class
arguments, aliases and repeated instantiations share it; distinct values, types
and primary templates have independent objects. Static members add no record
fields or per-object storage. Explicit member specializations use their own
initializer and source location; hidden out-of-line definitions retain their
actual source evidence. Mutable objects allow shared updates even through const
receivers. Receiver calls and temporary construction/destruction still execute;
references to static data survive destruction of temporary receivers. Member
default arguments and field initializers access the same static object.

An unused implicit member with no materialized definition or initializer stays
lazy. In particular, merely instantiating a class need not instantiate an unused
inline initializer or require a missing static definition. Every materialized
initializer is checked, including eagerly instantiated non-inline constants and
hidden namespace definitions. Constant folding, discarded values and unevaluated
queries cannot conceal unsupported source expressions.

The existing declaration-only constant rules apply. A non-inline const member
with a checked in-class constant initializer can supply a value without a global
object. An unevaluated sizeof(&member) or noexcept query does not itself require
storage. Potentially evaluated address/reference uses, including discarded or
folded uses, require a real definition in this source unit (TR0203). Selected
mutable members and explicit instantiation declarations retain the existing
source-definition requirement. Inline/constexpr definitions provide real storage
when materialized. The frontend neither fabricates a constant object nor emits
startup initialization code.

The private embedded Clang callback preserves each explicit static-member
instantiation's selected variable, written type, qualifier, location and parsed
attribute presence before no-effect handling. Extern declarations, repeated
spellings and directives following implicit use or specialization are each
checked. The written scalar type must agree with the selected member type;
unsupported spelling, attributes or disagreement produce TR0201. Clang-diagnosed
invalid C++ still produces TR0202. Written outer parameter types and expressions
inside qualifiers or decltype cannot disappear behind semantic canonicalization.
Static and function directives share the bounded source-evidence budget described
below. No semantic flags, runtime template arguments or new wire types are added.

Paired source fixtures cover definitions, lazy use, constant-only values,
specialization and complete directive evidence. Native O0/O2 fixtures cover real
storage, aliases, independent instances, zero/constant values and receiver cleanup.
Protocol fixtures check canonical globals, mutability, values, definition locations,
empty/static-free record layouts, full typed call closure and relocation. Native
validation requires the implementing revision's CI. Complete C++17/STL is still
unfinished. C++ is the only implemented input frontend; E Language, Python and
others remain planned. Translation uses embedded Clang libraries without an
external Clang executable.

## Instantiated local classes and written directives

Local classes inside admitted concrete free-function or class-template member
instances retain ordinary named methods, operators, conversions, constructors,
destructors and selected defaulted operations. The frontend distinguishes their
member-specialization metadata from direct class-template instances. Each member
must have matching method/record instantiation origins and a resolved concrete
signature. Enclosing local-function chains are bounded to 64 levels. Existing
ownership, access, qualifier, layout, field, definition and type rules apply.

Clang eagerly instantiates local-class members with their enclosing entity; every
materialized body is checked, including unused local methods. Uninstantiated
outer function bodies remain lazy. Ordinary source and selected exception/default
checks, receiver/result ABI, references, construction, copy/move, cleanup and
per-instance scalar static locals use the existing implementation. Local class
membership does not authorize own member function templates or virtual dispatch.

The private embedded frontend preserves every explicit function-instantiation
directive before Clang reuses a declaration or returns for a no-effect directive.
The evidence retains written template arguments, the function type, declaration
name (including a conversion-type-id), qualifiers, source location and parsed
attribute presence. `extern template`, directives after implicit use or explicit
specialization, and repeated equivalent arguments receive their own source checks.
An earlier valid spelling cannot conceal a later unsupported expression, nor can
a later valid spelling erase an earlier one. Parsed attributes remain unsupported
even when a directive has no semantic effect. This metadata neither changes Clang
specialization selection nor creates a runtime declaration or extra parameter.

Function directives, static-member directives and converted scalar defaults share
a limit of 200000 source units. Each directive/default charges one unit plus each
copied explicit function template argument before retention; ordinary source
expansion limits still apply during validation. All
written type and expression checks run before emission through the same visitor.
Dependent constructor patterns in supported no-base class templates may have
written member initializers or one type initializer that resolves to delegation
in a concrete instance. Packs and mixed type/member initialization remain
unsupported. Initializer expressions retain their normal lazy behavior. Source-invalid C++ retains Clang diagnostics; unsupported admitted
source is rejected without publishing partial artifacts.

Paired regressions cover these source paths, canonical instance identities,
receiver/field storage, local static isolation, complete typed call signatures and
relocation. Native O0/O2 fixtures cover actual mutation, results, references,
copy/move, cleanup and imported array-reference defaults. Native results require
CI of the implementing revision. Complete C++17/STL remains unfinished. The
translator uses statically embedded Clang libraries, with no external Clang
executable required at translation time; other input languages remain planned.

## Namespace operator function templates

Core v2 admits source-owned namespace operator function templates for the existing
ordinary C++17 operator set. This includes arithmetic, comparisons, logical/comma,
shifts, compound assignment, increment/decrement, dereference, address and
arrow-star forms that C++ permits as non-members. Embedded Clang enforces arity,
operand types, access and overload resolution. Allocation/deallocation follows the separate source-defined contract below. Literal
operators, unsupported dependent friend class-template forms and later-standard operators remain excluded.
Member function templates follow their separate contract above.

The existing function-template limits apply: up to 64 type or supported
scalar parameters, deduction, type defaults, scalar C++17 auto and dependent
scalar parameter types. ADL, namespace imports, explicit calls, specialization,
instantiation and recursion retain selected concrete identities. Unused primary
bodies stay lazy. Selected types, definitions, exception specifications, folded
source and every materialized body remain checked. Explicit directives keep each
written argument/type/name/qualifier and attribute check, including extern and
no-effect repetition. An unused implicit free-function specialization can retain
only its signature when its owned primary has a definition, or when it is an
owned non-friend namespace template with no definition. Its actual signature,
resolved exception specification and selected template/default arguments are
checked; existing primary bodies stay lazy. Unused non-friend namespace calls
retain a final dependency proof for their complete selection and argument source,
with or without an owned primary definition.
Used instances and explicit instantiations still require their concrete definitions.
This does not change member-template or callback definition boundaries.

Free operator calls contain only explicit source parameters plus the ordinary
hidden result pointer for record returns. There is no implicit receiver or runtime
template parameter. References preserve actual object/field addresses; record
results use their destination directly. Operator notation keeps C++17 sequencing:
assignment forms evaluate the right side first, while shifts, comma and overloaded
logical operations evaluate the left side first. Both operands of overloaded
logical operators are evaluated. Explicit function notation uses an existing
permitted function-call order. Temporary, by-value, returned and lifetime-extended
objects keep normal construction and cleanup rules.

Canonical primary/argument identities also separate local classes, fields and
scalar static locals. Equivalent instances and repeated directives reuse one
function; distinct primary templates, values and deduced types retain their own
state. Iterator dereference, comparison and increment can therefore be free
function templates within an otherwise admitted range.

Native O0/O2 fixtures cover mutation, aliases, sequencing, iterator ranges, record
results, copies and lifetime cleanup. Protocol fixtures check complete signatures,
selected calls, canonical reuse, distinct local/static identities and relocation.
Native validation requires CI of this implementation. Complete C++17/STL remains
unfinished. C++ is the only implemented input frontend; other languages remain
planned. Translation uses embedded Clang libraries without an external Clang
executable.

## Concrete parameter packs

Admitted namespace function/operator templates and standard-layout class
templates support resolved type and scalar value parameter packs. A primary has
at most 64 parameters; each concrete pack has at most 64 elements, including zero.
Every packed type or integer/bool/enum/nullptr_t value is checked, even if the body uses only
the count. Scalar auto packs may contain different admitted deduced scalar types.
Pointer/reference/class-valued non-type arguments, template-template parameters,
unsupported member class templates, unsupported dependent friend class-template forms and bases retain their
existing exclusions. Namespace class partial specializations use the contract above. C ellipsis varargs are separate and unsupported.

Embedded Clang performs deduction, reference collapsing, explicit prefix handling
and parameter expansion. Each concrete function parameter uses separate ordinary
storage even when expanded parameters share a source name and location. Fixed
parameters, array references, outer class-template method/constructor expansions,
out-of-line definitions and ordinary instantiation/specialization follow the
existing type, signature, source and lifetime checks. Template argument values
become typed constants; equivalent packs share canonical instances while different
types, values or primaries retain their own types and static scalar objects.

Resolved `sizeof...(Pack)` becomes a typed size count. The producer validates the
exact original pack declaration and its admitted owned primary, including written
outer lists on out-of-line members and packs in instantiated local-class methods.
No dependent or partially substituted count can reach lowering. Only while
checking a written non-type parameter's TypeLoc may a dependent count from that
same template parameter list remain lazy metadata, as in
`template<class... T, decltype(sizeof...(T)) N=0>`. This exception never obtains a
pack length and does not extend to defaults, bodies or arbitrary dependent source.
Selected defaults still supply concrete original and converted evidence.

Concrete fold expressions become ordinary builtin or selected overloaded
operations. Unary/binary, left/right folds retain sequencing, builtin logical
short circuit, reference writes, record result storage and existing cleanup.
Empty logical/comma identities and binary seeds follow C++17. Empty expansions
do not instantiate their pattern: `(0 + ... + (sizeof(long double), Ns))` is admitted
for an empty pack, but a nonempty materialization is rejected for its `long double`
source. Seeds and every materialized element are checked. Unresolved folds or
pack-expansion AST nodes are not accepted as runtime representations.

Malformed C++17 pack declarations, mismatched simultaneous expansion lengths and
empty unary arithmetic folds retain TR0202. Unsupported elements/materialized
source and resource overflow use TR0201; selected missing definitions retain
TR0203. Pack indexing and expansion work share the existing source budget.
Paired regressions and O0/O2 native fixtures cover parameter identity, ordering,
references, counts, folds, static storage and lifetime. Protocol checks cover exact
calls, parameter/receiver/result types, constants, cleanup and relocation. Native
results require CI of the implementing revision. Complete C++17/STL remains
unfinished. Only C++ input is implemented; E Language, Python and other frontends
are planned. Translation uses embedded Clang libraries without launching an
external Clang executable.

A direct type-form `sizeof(T)` or `alignof(T)` in a non-type parameter's
written type can remain declaration metadata while that type is dependent.
The written, optionally cv-qualified operand must be the exact non-pack type
parameter from the same template parameter list. This does not permit arbitrary
dependent expressions, alias-shaped operands, foreign owners or runtime query
values. Normal source traversal still inspects neighboring expressions and all
selected substituted parameter types before erasure; no extra Sema substitution
is performed. For example, `template<class T, decltype(sizeof(T)) N=3>` and
source-defined enable-if traits using a direct `sizeof(T)` are admitted when
their selected concrete types satisfy the profile. Bare `extern template`
declarations do not supply the concrete definitions required by the existing
closed-source-unit function declaration contract. Ambiguous partial
specializations remain C++ errors, including candidates whose alias patterns
hide unsupported source.

## Concrete free function templates

Core v2 admits source-owned namespace/free function templates with one to 64
unconstrained parameters, mixing supported types with integer, boolean
and enum values. Embedded Clang performs deduction, overload ordering,
substitution and explicit specialization/instantiation. Type-parameter defaults,
namespace imports, recursion and nested calls use ordinary typed functions.
Unsupported dependent friend class-template forms, template-template parameters,
abbreviated/constrained templates and standard headers remain
outside this stage.

Scalar non-type parameters include C++17 `template<auto N>` and dependent scalar
types such as `template<class T, T N>`. Every materialized value argument must
resolve to an integral argument with a supported concrete integer/bool/enum type
or a null argument whose type is exactly nullptr_t.
Pointers, references, pointer-typed nulls, function/member pointers and class values
are not admitted, even through `auto` or a dependent parameter type. Scalar
non-type defaults, including inherited defaults, follow their source-evidence
contract above. Existing type and function defaults retain their own rules.

The substituted scalar becomes an ordinary typed constant; `N` does not become
a runtime function parameter or mutable local. Array-bound deduction, fixed
local extents, loop and switch constants, finite recursion using `if constexpr`,
function default arguments and constant static-local initialization use existing
lowering. Equivalent constant arguments share a specialization; different values
or deduced argument types preserve distinct instance identities and storage.

The producer checks written non-type parameter types and explicit argument
expressions before erasure, including `sizeof`/`decltype` source expressions and
explicit instantiation/specialization arguments. Unsupported `long double` operations
cannot disappear behind an integer or nullptr_t result. Direct dependent `T` and `auto` type
metadata stay lazy; other expression-bearing dependent parameter types must pass
the ordinary source checks and are not generally admitted by this increment.

Generic patterns do not become runtime functions. The producer checks template
metadata and every materialized concrete body, including unused explicit
instantiation definitions and explicit specializations. It does not force bodies
for unselected overload candidates. Uninstantiated bodies and lazy function
parameter defaults may contain operations outside the runtime subset; when
instantiated, their concrete types and operations receive ordinary checks.
Selected defaults execute anew at each omitted argument. An explicitly supplied
argument does not instantiate a still-lazy default. This template-specific rule
does not weaken checking of ordinary non-template functions or their defaults.

Template-dependent `if constexpr` uses Clang's selected instantiated branch;
the discarded dependent branch is not instantiated or lowered. Initializers,
selected object storage, return capture and scope cleanup retain ordinary rules.
Non-template `if constexpr` still checks both original source branches.

Every concrete function has one identity including its primary template and
actual arguments. A bounded declaration pre-index assigns deterministic primary
ordinals before any type checking, so forward references to a later template's
local record work. Ordinals distinguish even macro-generated primary templates
whose Clang USRs and expansion locations collide. Instance-local records,
fields, parameters and scalar static variables inherit that identity. Repeated
calls and redeclarations share storage; distinct instances or primaries do not.
No generic source blob, opaque IR or new runtime dispatch is emitted.

The current single-unit definition restriction remains: selected calls need a
body, explicit declarations need an in-unit definition, and a signature-only
call still fails under `sizeof` or `noexcept`. Generic uninstantiated declarations
and unselected implicit overload candidates alone do not require a body.
These remaining closure limits mean full C++ template support is unfinished.
Core v2 enables the pinned post-C++17 extension diagnostic groups and rejects
those warnings as TR0201 even in uninstantiated patterns or discarded branches.
Actual C++ source errors remain TR0202. Other profiles are unchanged.

Native O0/O2 fixtures cover type/value deduction, integer widths, array bounds,
defaults, static storage, cleanup, recursion, specialization and explicit
instantiation. Protocol checks assert complete call signatures, scalar constants
without runtime template parameters, equivalent and distinct argument identities,
recursive call closure, selected cleanup, distinct primary/local identities,
macro collisions, forward local-record returns and relocation stability. Native
validation requires the implementing revision's CI. Full C++/STL remains
unfinished; this stage does not add standard headers or containers.

## Resolved constexpr-if

Core v2 admits C++17 `if constexpr` in ordinary functions and admitted concrete
free function-template instances with a resolved constant bool condition,
including supported integer/enum conversions, size/alignment and
noexcept queries, constexpr function/member calls and constexpr conversions.
Only the selected substatement emits runtime operations. A false condition
without an else selects an empty body. Nested and else-if forms retain this
selection, including non-template auto return deduction that ignores discarded
return types.

The init-statement still executes once, and a condition-variable declaration
still creates and initializes its object. Their scope covers the selected body.
Evaluating the constant condition itself generates no runtime call, conversion,
temporary object or conditional branch. In particular, a declared literal-record
condition variable retains storage while a temporary used only to compute the
constant condition does not.

Selected-body and initializer objects retain reverse-order destruction on normal
exit, return, break and continue. Return values are captured before cleanup.
The constexpr-if introduces no loop control target. An outer switch cannot
dispatch into a constexpr-if substatement, so its storage pre-registration stops
at that statement; normal selected lowering allocates the live declarations.
Discarded automatic arrays and records are not allocated, including in that
switch combination. Nested switches retain their own case registration.

In non-template functions, both owned source branches and the complete condition
source are still checked, even when discarded or folded. In concrete template
instances, Clang does not instantiate a discarded dependent branch. Unsupported source operations and declarations
retain their diagnostics, and invalid non-template discarded code remains a
source error. This stage retains the profile's stricter complete-definition
requirement for declared functions and globals, even when their only uses are
discarded. The standard's full discarded ODR-use exemption and remaining template
forms need further closure work; this is not complete constexpr-if or template
support. See the concrete free function-template rules above. Static scalar
declarations in discarded source
may remain canonical globals but add no runtime body or initialization effects.

C++23 `if consteval` and its negated spellings produce C++17 parse errors
(`TR0202`) before ordinary-if lowering; `consteval` remains a valid C++17
identifier. Recognized later-language extensions retain `TR0201`. Old profiles retain their constexpr-if boundary. Native
O0/O2 fixtures and protocol assertions cover selection, runtime initializers,
condition variables, return deduction, cleanup, switch storage and relocation.
Native validation requires the implementing revision's CI; full C++/STL remains
unfinished.

## Inline namespaces

Core v2 admits source-owned C++17 named and anonymous inline namespaces,
including transitive nesting, reopening with or without the inline keyword,
and out-of-namespace member definitions. Parent-qualified lookup includes the
inline namespace's declarations even when the parent has same-named overloads.
Argument-dependent lookup includes both the inline namespace and its parent.
Embedded Clang resolves these rules before translation.

Implicit visibility, explicit qualification, namespace aliases and using imports
refer to the original canonical declaration. They share global storage and const
permissions, record types/layouts, and selected function definitions. Distinct
version namespaces retain separate symbols and types. Namespace declarations
create no runtime objects, wrappers, initialization or cleanup; ordinary class
lifetimes and source closure remain unchanged.

The C++17 spelling `namespace A::V {}` can reopen an existing inline `V`.
The C++20 spelling `namespace A::inline V {}` remains rejected, including when
the inline token comes from a macro. The pinned AST marks both declarations as
inline; the producer distinguishes the written start token from inherited inline
status. Macro-spelled C++17 inline declarations and namespace separators remain
admitted. A namespace first declared non-inline cannot later become inline;
Clang's source diagnostic is preserved.

Namespace attributes, unsupported template forms/types/bodies, foreign includes and
missing required definitions retain their restrictions, including unused and
statically skipped code. Old profiles retain their existing inline namespace
boundary. No header, SDK or standard-library support is implied by namespace
admission.

Native O0/O2 fixtures cover qualified overloads, bidirectional ADL, versioned
object/type identity, transitive visibility, reopening, macros and cleanup.
Protocol checks verify canonical objects, exact signatures/layouts, declaration
erasure and deterministic relocation. Native validation requires the implementing
revision's CI; full C++/STL remains unfinished.

## Resolved namespace imports

Core v2 admits namespace aliases, using-directives and ordinary resolved
using-declarations in namespace and block scopes. Aliases and directives may
refer to source-owned nested, inline, anonymous or reopened namespaces, including
chains of namespace aliases. Imported functions, variables, typedefs, record
and enum types, and unscoped enumerators keep their original canonical source
declarations. This includes unscoped enumerators local to an ordinary function
or method and imported into a nested block. Repeated imports and C++17
comma-separated using-declarators create no duplicate definitions.

Embedded Clang resolves lookup at each use: a using-declaration imports the
overloads visible at its declaration, a using-directive can see later namespace
additions, and later valid default arguments remain available. Local hiding,
qualified lookup, access control, argument-dependent lookup and hidden friends
retain those resolved meanings. The producer never repeats lookup against the
final namespace contents or creates alias wrapper functions.

Lookup declarations add no runtime storage, initialization or cleanup. Imported
and qualified names address the same object, preserve const permissions, and
use the same record layout and typed call signatures. Block imports preserve
surrounding effects and object destruction, including loop, if and switch
bodies. Namespace aliases and using-declarations/directives are not valid
C++17 for/if/switch init-statements; source errors remain source errors.

Alias target walks and using-shadow chains are bounded to 64 links, cycle
checked, source-ownership checked and charged against the existing expansion
budget. Importing a name never exempts its original type, initializer, defaults,
body or definition from its applicable source validation. Ordinary non-template
source remains checked even when unused or statically skipped. Source errors retain TR0202 and missing required definitions TR0203.

Admitted free function templates may also be imported; their lazy patterns and
materialized bodies follow the separate template rules above. Class-member
imports, inherited constructors, unsupported class-template forms and dependent/pack import forms,
foreign targets and unsupported source types or bodies remain
excluded. C++20 using-enum and scoped-enumerator imports remain rejected under
the C++17 contract even when the embedded library only issues an extension
warning. Old profiles and header/library restrictions are unchanged.

Native O0/O2 fixtures cover lookup differences, canonical addresses, mutation,
type identity and cleanup. Protocol checks verify original IDs, exact overload
signatures, defaults, layouts, declaration erasure and deterministic relocation.
Native validation requires CI for the implementing revision. Full C++/STL
remains unfinished.

## Statically initialized scalar locals

Core v2 admits source-owned static integer, boolean, enum, float and double local variables in
ordinary non-constexpr functions, admitted concrete free function-template
instances and methods, including constructors and
destructors. The type must be non-volatile and non-thread-local. Mutable scalars
without an initializer are statically zero-initialized; explicit initializers
must be fully defined constant expressions. Const and constexpr locals require
constant initializers. An ordinary function may contain `static constexpr`; a
static local inside its actual owning constexpr function is rejected under the
C++17 contract even if the embedded Clang library only warns about an extension.
A normal local-class method nested inside a constexpr function is checked as
its own function.

Each canonical declaration has one typed static object, shared across calls,
recursive invocations, receiver instances and lexical exits. Distinct functions,
overloads and sibling scopes retain separate objects even when names match.
References and pointers to these objects remain valid after the function
returns. Source access and const rules remain enforced; an automatic local
that shadows a static variable remains an independent automatic object.

The producer reuses scalar global storage and its existing optional `mutable`
permission. A local declaration emits no allocation, initializer store, guard
or cleanup at block entry. Switch case pre-registration uses the same global
object, including permitted entry past a static declaration. Loop-body static
variables and for/if/switch initializers retain values when control re-enters
their scopes. Constant initialization may use checked constexpr functions,
earlier constant locals, static class constants and unevaluated size queries;
it creates no runtime call. The entire owned initializer and function source
is still inspected, including unused declarations and statically skipped code.

Dynamic initialization follows the [first-use contract](#dynamic-local-static-initialization). TLS,
volatile storage, local extern declarations, static types outside scalars and
[fixed arrays](#fixed-array-static-storage), and
other template forms retain separate restrictions. This stage adds no static record
destruction or exception machinery. V1 profiles retain their previous behavior.
The static storage rule does not change automatic uninitialized scalar locals.

Native O0/O2 fixtures cover persistent state, shared and distinct addresses,
recursion, loop re-entry, case jumps, receiver independence and escaped aliases.
Protocol checks cover exact global values/types/locations, read/write permissions,
absence of automatic shadows or repeated initializers, call signatures and
deterministic relocation. Native results require CI from the implementing
revision; full C++/STL remains unfinished.

## Mutable scalar globals

Core v2 admits mutable namespace/file-scope globals of the supported integer,
boolean, enum, float and double types. Null and callback objects follow their
separate scalar contracts. Definitions without an initializer receive C++ static
zero initialization. Explicit initializers must be fully defined constant
expressions; an admitted constexpr function or conversion may supply that value.
All source initializer operations are still inspected, including unused and
folded code. This does not initialize uninitialized automatic local variables.

Each global has one stable storage object. Reads observe intervening writes;
references, pointers, default arguments and source-defined constructors or
destructors access that same object. Ordinary `extern` redeclarations must
resolve to a source-owned definition of the same canonical variable in this
single translation unit; it is emitted once. Namespace, internal-linkage and
C++17 inline definitions retain their source identity. Globals are emitted with
internal generated names, without adding a public C data-export ABI.

Const globals retain their existing read-only representation. Fixed arrays
follow their [static storage contract](#fixed-array-static-storage), and references
follow their [alias contract](#static-reference-bindings). Records follow their
[static object contract](#static-record-objects). Dynamic initialization follows the
startup contract. Volatile/atomic globals and TLS remain outside current support.
Statically initialized scalar locals and defined scalar static data members
follow their separate contracts above. Nontrivial global object destruction
uses the [registered lifetime contract](#static-destruction). Other profiles retain their prior
constant-global contract.

The optional global IR field `mutable` defaults to `false`. Only core v2 accepts
this field, and supported numeric, boolean, null, pointer, callback and fixed-array carriers can
set it to `true`.
The consumer checks the folded scalar initializer and grants writes or mutable
addresses only to that exact global. Emission uses `static` for mutable storage
and `static const` for constant storage. No startup function or new instruction
is needed. Fixtures cover state across calls, zero/constant initialization,
redeclared aliases, address identity, narrow/wide values, default arguments,
cleanup effects, malformed IR and relocation. Native validation requires CI
from the implementing revision.

## Default construction and defaulted destruction

Core v2 admits generated/defaulted default constructors for the same checked
records, including implicit nontrivial construction of nested record and array
members. In-class `= default`, `explicit` default constructors and out-of-line
`= default` definitions are supported. Default member initializers follow their
separate contract; field types, layout, access and base-class rules still apply.

A nontrivial generated constructor becomes a checked `void(ptr:Record)` function.
Its semantic field initializers use Clang's selected constructors in declaration
order, with each array element constructed in its own destination. Selected
definitions are discovered transitively and emitted once; unselected implicit
copy/move functions are not emitted. There is no replacement source blob or
external compiler invocation.

Initialization keeps the source distinction between default initialization and
value initialization. Omitted scalar fields receive no invented stores during
default initialization. Value initialization first zeroes the object only when
Clang's selected C++ initialization requires it, then runs member constructors.
Out-of-line defaulting is user-provided and retains its different zero-initialization
rules. An admitted trivial default constructor needs no emitted function body,
even when explicitly defaulted. An unused constructor or a constructor used only
in an unevaluated expression such as `sizeof(R{})` does not require Clang to
materialize a body; such expressions introduce no runtime construction or cleanup.

```cpp
struct Inner {
  int value;
  Inner *self;
  Inner() : value(7), self(this) {}
};
struct Outer {
  int zero;
  Inner values[2];
  explicit Outer() = default;
  ~Outer() = default;
};
int main() {
  Outer value{};
  return value.zero == 0 && value.values[0].value == 7
      && value.values[1].self == &value.values[1] ? 0 : 1;
}
```

Defaulted nonvirtual destructors, both in-class and out-of-line, use the normal
reverse member/array cleanup without a user body. Trivial defaulted destructors
need no call. Deleted declarations need no emitted definition; selecting a
deleted operation retains the C++ diagnostic. Exception unwinding and STL remain
later milestones. Actual execution evidence must come from the implementing revision's CI.

## Default member initializers

Core v2 admits brace-or-equal initializers on the supported non-mutable,
non-reference and non-bitfield record fields. Const fields follow their
initialization contract above. Scalar and pointer
initializers, earlier-field references, ordinary calls, nested records and bounded
arrays use the same checked expression and layout rules as explicit initialization.
Written defaults are checked even when unused or always overridden. A selected
Clang default-initializer wrapper is checked explicitly, including its semantic
expression; an empty AST child list cannot hide unsupported source or calls.

Initialization follows member declaration and array element order. A selected
initializer executes once for each actual destination, without an intermediate
record copy. Its `this` points to the object that owns that field, including while
a const complete object is being constructed. Explicit constructor initializers
or aggregate clauses suppress only the corresponding field's default. Omitted
array elements retain their selected initialization, including nested defaults.

The initialization destination and the enclosing expression's `this` are separate.
An explicit aggregate clause in a caller method keeps the caller's `this`. A nested
default temporarily uses the inner object's `this`, then restores the outer one.
This also preserves an explicit outer-object pointer passed from one default into
an inner aggregate that has its own self-pointer default:

```cpp
struct Outer;
struct Inner { Outer *outer; Inner *self = this; };
struct Outer { Inner inner = {this}; Outer *self = this; };
int main() {
  Outer value{};
  return value.inner.outer == &value && value.inner.self == &value.inner
      && value.self == &value ? 0 : 1;
}
```

Implicit/defaulted copy construction and assignment use their selected member
operations and do not rerun default member initializers. Trivial copies preserve
stored pointer values, including a source self pointer. An ordinary user-defined
copy constructor can select a default for a field omitted from its own initializer
list, just like other user constructors.

Default initialization adds no independent lifetime boundary. Each constructor
member initializer retains its full-expression cleanup; temporary objects from
aggregate clauses survive through the complete aggregate initialization. The
existing complete-object cleanup owner remains responsible for normal destruction.
Reference-field lifetime extension, static initialization
outside the current contract, exceptions, other template forms and STL are not enabled
by admitting field defaults. Unevaluated construction introduces no runtime default
calls or invented generated body. V1 profiles continue to reject field defaults.

Regression fixtures cover defaults and overrides, nested receiver identities,
partial arrays, generated and user copying, declaration order and distinct
constructor/aggregate temporary cleanup order at O0 and O2 with inlining disabled.
Protocol assertions check destination identity, helper signatures, lazy definitions
and deterministic relocation. Native results require the implementing revision's CI.

## Record arguments and results

In core v2, a source by-value record parameter denotes a distinct object prepared
by the caller. The generated function receives a mutable pointer to that object;
source constness still controls permitted access in the function body. An lvalue
argument initializes a separate copy, even when the same source object supplies
two parameters. Reference parameters retain their original aliases. Ordinary
constructors, static methods and instance methods use the same argument rules.

A record-returning function has a wire result of `void` and a first mutable
result pointer. An instance receiver, when present, follows that result pointer;
explicit source parameters come afterward. Constructors only have their existing
destination pointer and do not gain a second result pointer. Source declarations
and overload identities are resolved before this internal convention is applied;
it is not a foreign C++ binary ABI. Scalar, pointer, reference and v1 calls retain
their existing conventions.

Direct prvalue calls construct in the selected local, field, array element,
parameter or materialized temporary. A direct returned call forwards the same
result destination. Conditional/comma initialization and nested calls preserve
source effects; receiver capture still precedes explicit arguments. Every
invocation has distinct parameter storage, including recursion. Temporary places
and generated calls count against the existing expansion budget.

```cpp
struct Item {
  int value;
  Item *self;
  explicit Item(int n) : value(n), self(this) {}
};
Item make(int n) { return Item(n); }
int main() {
  Item direct = make(7);
  Item copy = direct;
  return direct.self == &direct && copy.self == &direct ? 0 : 1;
}
```

The self-address check selects NeverC's direct-storage behavior for the admitted
trivial records. C++17 permits other implementations to introduce eligible
trivial function argument/result copies; this is not a universal language address
guarantee. A trivial copy retains stored pointers and does not repair them;
user-defined copying instead executes its selected source body.
Named-local or named-parameter returns keep Clang's selected copy/move operation;
NeverC does not infer NRVO by aliasing the source to the destination. Normal
destruction and parameter cleanup follow the contract below. User-defined copy
and generated copy/move operations follow the next sections. Exception unwinding
still requires further support before broader C++/STL admission.

## User-defined copy operations

Core v2 supports ordinary user-provided copy constructors whose first source
parameter is `R&` or `const R&` for the same canonical record. Additional parameters
may have admitted default arguments under the default argument rules. They may be
`explicit`, `constexpr` or defined out of line. Field initialization follows the
constructor rules, and the selected source function runs directly on the actual
destination. Passing the source reference does not first read or snapshot the
whole object; a copy body can read only its initialized fields. Source overload
resolution distinguishes mutable and const source-reference overloads before
normalization to the protocol's pointer carriers.

The conventional user copy assignment form has one `R&` or `const R&` source
parameter, a mutable receiver with no ref qualifier or `&`, and result `R&`.
It executes on the actual receiver and preserves the returned alias, even when
the source body returns a different live object. Assignment does not implicitly
destroy and reconstruct the target. Self-assignment follows the user body.
General ordinary assignment signatures, including by-value parameters, other
results and const/rvalue-qualified receivers, follow the overloaded-operator
contract below. Volatile/restrict receivers and sources remain unsupported.

Operator notation `left() = right()` evaluates and captures the source reference
before evaluating the receiver. Explicit member notation
`left().operator=(right())` captures the receiver first, then evaluates the
source argument. Both use one checked ordinary call with the receiver first in
the wire signature, followed by the source pointer. This preserves C++17's
syntax-dependent sequencing when either expression reseats an aliased pointer.

```cpp
struct Item {
  int value;
  Item *self;
  Item(int n) : value(n), self(this) {}
  Item(const Item &other) : value(other.value + 1), self(this) {}
  Item &operator=(const Item &other) {
    value = other.value + 2;
    return *this;
  }
};
int main() {
  Item source(3);
  Item copied = source;
  if (copied.value != 4 || copied.self != &copied) return 1;
  copied = source;
  return copied.value == 5 && copied.self == &copied ? 0 : 2;
}
```

A source-required user copy is a constructor/assignment call, never a raw record
assignment. Lvalue arguments initialize separate by-value parameter objects and
retain their existing callee-owned cleanup. Direct prvalue arguments and results
continue to construct in their actual destinations without extra copies. A
named/lvalue return keeps the selected admitted copy and the normal result,
temporary and local destruction order; no NRVO heuristic aliases the local to
its result. Reference source parameters do not own or destroy their referents.

A containing aggregate may be initialized with a member having user-defined
copy operations without selecting a copy of the containing object. Generated
copy construction and assignment follow the next sections. Deleted special
members, unsupported variadic/default arguments,
allocation and exception unwinding are not added. Temporary source-reference
arguments and receivers follow the full-expression call contract below. Missing definitions and
invalid source const/access operations remain diagnostics. V1 admission and
trivial value-copy representation are unchanged.

Regression fixtures cover side effects, self-addresses, reference aliases,
source overloads, selected calls, field/array initialization, partially initialized
objects, parameter and result lifetimes, return copies, operand ordering and
relocation. Runtime validation uses the implementing revision's O0/O2 no-inline
and full native CI results.

## User-defined move operations

Core v2 supports ordinary user-provided move constructors whose first source
parameter is `R&&` or `const R&&` of the same canonical record. Additional parameters
may have admitted default arguments under the default argument rules. `explicit`,
`constexpr` and out-of-line definitions retain their C++ rules. A conventional move
assignment takes the same source reference and returns mutable `R&`; its mutable
receiver may be unqualified, `&`-qualified or `&&`-qualified. Source references
must designate live objects or admitted full-expression temporaries under the
reference-provenance contract.

Clang selects the operation before lowering. A named rvalue-reference expression
remains an lvalue and can therefore select copying. A cast to `R&&` does not
itself guarantee a move: copy fallback and explicit-constructor rules still
apply. For example, copy initialization excludes an explicit move constructor;
direct initialization can select it. Distinct copy/move and const-source overloads
retain distinct canonical identities even when their pointer signatures match.

Construction calls the selected function with the actual destination, followed
by the live source address. Field initialization and selected field defaults
observe that destination. No intermediate record copy or self-pointer repair is
inserted. By-value arguments use separate caller-prepared objects; selected
return moves construct into the hidden result destination. Direct C++17 prvalue
forwarding still introduces no extra copy or move. NeverC does not infer NRVO.

```cpp
struct Item {
  int value;
  Item *self = this;
  Item(int n) : value(n) {}
  explicit Item(Item &&source) : value(source.value + 1) { source.value = -1; }
  Item &operator=(Item &&source) {
    value = source.value + 2;
    source.value = -2;
    return *this;
  }
};
int main() {
  Item source(3);
  Item target(static_cast<Item&&>(source));
  if (target.value != 4 || target.self != &target || source.value != -1) return 1;
  source = static_cast<Item&&>(target);
  return source.value == 6 && target.value == -2 ? 0 : 2;
}
```

Assignment executes the selected body once and preserves its returned reference,
including a reference to an object other than the receiver. Self-assignment and
source mutation follow that body. In a chain, the returned reference determines
the next operation's source category. Operator syntax captures the source before
the receiver; explicit member syntax captures the receiver first. Source values
are read by the selected body after those operand effects.

A move does not end the source lifetime or transfer cleanup responsibility.
Source and destination complete objects retain their normal owners and destruction
order; references and member initialization add no separate complete-object owner.
The callee still destroys by-value parameters. Assignment adds neither an implicit
destruction nor a replacement construction.

Deleted functions, volatile/restrict sources
or receivers, variadic parameters, inheritance,
other template forms and STL remain outside this increment. Temporary source-reference
arguments and receivers follow the full-expression call contract below. Invalid
C++ overload, cv/ref or deleted-copy uses retain source diagnostics; missing user
definitions retain the missing-definition diagnostic. V1 profiles reject user moves.

O0/O2 no-inline fixtures cover const and explicit moves, copy fallback, named
rvalue references, operand effects, returned aliases, member/array destinations,
out-of-line definitions, ref-qualified assignment and source/target destruction.
Protocol checks assert full signatures, selected identities, final storage,
absence of extra copying/owners and deterministic relocation. Native execution
evidence must come from the implementing revision's CI.

## Generated move construction and assignment

Core v2 supports implicit and explicitly defaulted move constructors and move
assignment for the admitted records. Their sole source parameter is mutable
`R&&` of the same canonical type; assignment returns mutable `R&` and admits
unqualified, `&` and `&&` receivers. In-class, out-of-line and explicit defaulted
move constructors retain their C++ initialization rules. Standard resolved
exception specifications follow the noexcept rules below, including across
redeclarations. Invalid C++17
defaulted signatures, such as `const R&&` sources or const receivers, retain
source diagnostics; this differs from the admitted user-defined const-source moves.

Selected generated definitions are discovered transitively and emitted once.
Unused, unevaluated and trivial functions need no invented body. Nontrivial
construction uses the actual destination and initializes members in declaration
order; nested array sources are captured once per enclosing row, with elements
initialized in increasing index order. Each member retains the copy or move that
Clang selected. A member with no move operation may therefore execute its copy
constructor or copy assignment. Generated moves never rerun default member
initializers or repair stored self pointers.

```cpp
struct Leaf {
  int value;
  Leaf *self = this;
  Leaf(int n) : value(n) {}
  Leaf(Leaf &&source) : value(source.value + 1) { source.value = -1; }
};
struct Box {
  Leaf items[2];
  Box(Box &&) = default;
};
int main() {
  Box source{{Leaf(3), Leaf(5)}};
  Box target(static_cast<Box&&>(source));
  return target.items[0].value == 4 && source.items[0].value == -1
      && target.items[1].self == &target.items[1] ? 0 : 1;
}
```

Generated nontrivial assignment executes selected member functions and nested
array loops, then returns its own receiver. A member's different returned alias
does not change that enclosing result. Scalar arrays and arrays with a selected
trivial assignment use bounded typed stores, including trivial copy fallback and
records whose constructors or destructors are nontrivial. Only Clang's checked
generated member-array copy shape is recognized; no runtime memory-copy import
or ordinary source builtin is admitted.

Trivial moves retain stored field and pointer values. Assignment captures source
and receiver references in the syntax-required order, then reads source values
after operand effects. The old inline implicit trivial move operation also uses
this sequencing. Its previously admitted temporary sources and operator receivers
remain supported through ordinary full-expression materialization and inline
stores, without a helper call or lifetime extension. The analogous implicit
trivial move-construction path is preserved. Ordinary/defaulted/user move calls
and explicit member calls also admit full-expression temporary sources and
receivers under the contract below. Automatic local reference extension is
handled separately and is never inferred from a copy or move call.

Move construction and assignment leave complete-object cleanup ownership intact.
Both moved-from sources and destinations are destroyed normally. By-value
parameters still own separate caller-prepared storage and are destroyed by the
callee; selected return moves initialize the caller's result. Direct prvalue
forwarding adds no extra operation and no NRVO heuristic aliases named objects.
Array extents, storage and expanded-node budgets remain enforced. Unsupported
layouts, deleted operations, broader temporary lifetimes, exceptions,
other template forms and STL still require further work; v1 profiles are unchanged.

O0/O2 no-inline fixtures cover selected copy fallback, member/array order, self
and chained assignment, operand effects, pointer values, defaults, by-value and
result storage, cleanup and existing inline temporary behavior. Protocol fixtures
check canonical functions, complete signatures, array source/destination indices,
typed stores, lazy definitions, ownership and deterministic relocation. Native
execution evidence must come from the implementing revision's CI.

## Generated copy construction

Core v2 supports implicit and explicitly defaulted copy constructors for the same
admitted record layouts, including nested record members and multidimensional
arrays. A source parameter must be exactly one `R&` or `const R&`; mutable-only
member copying therefore retains its mutable source. In-class, out-of-line and
`explicit` defaulted copies are supported. Each selected nontrivial definition is
discovered once, including transitive member copies, and translated to a checked
`void(ptr:Record, ptr:Record)` or `void(ptr:Record, cptr:Record)` function.

Copy construction initializes the actual destination in member declaration order.
The source array address is captured once before copying elements in increasing
index order. Nested arrays use the enclosing element index when identifying their
source row. Each nontrivial member invokes its selected copy constructor; the
containing object is never replaced by a raw value assignment. Trivial copied
members keep ordinary value copying, including stored self pointers: their values
continue to refer to the source object when that is what the source stored.

```cpp
struct Leaf {
  int value;
  Leaf *self;
  Leaf(int n) : value(n), self(this) {}
  Leaf(const Leaf &source) : value(source.value + 1), self(this) {}
};
struct Box {
  Leaf values[2];
  Box(const Box &) = default;
};
int main() {
  Box source{{Leaf(3), Leaf(5)}};
  Box copied = source;
  return copied.values[0].value == 4 && copied.values[1].value == 6
      && copied.values[1].self == &copied.values[1] ? 0 : 1;
}
```

By-value parameters use distinct caller-prepared storage, source returns retain
selected member copies, and direct prvalue forwarding introduces no additional
copy. The existing normal cleanup convention still owns complete objects;
copying a field or array element introduces no separate cleanup owner. Unused
or purely unevaluated defaulted copies do not need a materialized Clang body.
An admitted trivial copy also needs no function body. Runtime nontrivial copies
must have a checked materialized definition.

Unsupported layouts, static reference lifetime extension, exceptions,
other template forms and STL headers remain outside this increment. Array extent, object
storage and expanded-node limits still apply. Regression fixtures cover selected
calls, nested source/destination indices, one source-array capture, mutable source
overloads, pointer identity, side effects, value parameters, return copies,
destruction counts, lazy definitions, negative source and deterministic relocation.
Native validation requires the implementing revision's CI.

## Generated copy assignment

Core v2 admits implicit and explicitly defaulted copy assignment for the same
checked records. The source parameter is exactly one `R&` or `const R&`, and the
result is mutable `R&`. The receiver may be unqualified or lvalue-qualified with
`&`. In-class and out-of-line defaulting preserve their selected operations;
deleted declarations are retained for source checking without an executable
assignment definition. Standard resolved exception specifications
follow the noexcept rules below.

A nontrivial generated assignment becomes a checked
`ptr:Record(ptr:Record, ptr/cptr:Record)` function. Its synthesized body assigns
members in declaration order and arrays in increasing element order. Nested
nontrivial members invoke the selected assignment functions. A member function
may return another live reference; the containing generated assignment ignores
that member result and returns its own receiver. Assignment does not construct
or destroy an additional complete object.

Generated copies of scalar or trivially assigned record arrays become bounded
typed element assignments. A member type may have nontrivial constructors or a
destructor while its selected assignment remains trivial. Such assignment copies
stored values, including self pointers, without invoking those lifecycle functions
or repairing pointers. The source and destination array addresses are captured
once. This also preserves defined self-assignment without a runtime memory-copy
import. The implementation recognizes only Clang's exact generated member-array
copy shape; ordinary source calls to `__builtin_memcpy` are not admitted.

```cpp
struct Leaf {
  int value;
  Leaf &operator=(const Leaf &source) {
    value = source.value + 1;
    return *this;
  }
};
struct Box {
  int scalar[2];
  Leaf values[2];
  Box &operator=(const Box &) = default;
};
int main() {
  Box source{{1, 2}, {{3}, {4}}};
  Box target{{0, 0}, {{0}, {0}}};
  Box &result = (target = source);
  return &result == &target && target.scalar[1] == 2
      && target.values[0].value == 4 && target.values[1].value == 5 ? 0 : 1;
}
```

An admitted trivial assignment requires no materialized function body. Both
operator syntax and explicit `.operator=` / `->operator=` calls capture their
references in source order, perform one typed value assignment and return the
actual receiver reference. Operator syntax captures the RHS reference before
receiver evaluation; explicit member syntax captures the receiver first. Source
values are read after those effects. If receiver evaluation mutates source fields,
those updated values are copied; if it reseats a pointer used to identify the
source, the previously captured source identity stays unchanged. No whole-source
snapshot is inserted merely to bind a reference.

Unused or unevaluated defaulted assignment can remain without a lazy Clang body;
`sizeof(target = source)` emits no runtime assignment or helper. Runtime nontrivial
assignment requires an owned materialized definition. Existing array extent,
storage, expansion, layout and live-reference checks remain in force. Regression
fixtures cover nested member calls and loops, optimized typed array stores,
mutable sources, returned aliases, self/chained assignment, pointer capture and
value-read sequencing, destruction counts, lazy definitions and deterministic
relocation. Native results require the implementing revision's CI.

## Single-object allocation and placement reuse

Core v2 admits C++17 single-object `new` and `delete` when their selected
allocation and deallocation functions have checked definitions in this source
unit, including the bounded default sized-delete forwarding rule below. It also
admits the exact standard placement allocation selected from the embedded
`<new>` header. Global replacement functions, class-specific static functions,
custom placement overloads and existing concrete function/class/member templates
compose with supported scalar and complete record types. Allocation operator
declarations and direct calls, including `operator new[]`/`operator delete[]`,
follow ordinary checked function rules; array expressions follow the separate
contract below.

The selected allocator must return `void*` and start with the target `size_t`.
Clang supplies allocation selection and converted placement arguments. An exact
private source event records the final new expression, selected template function
and overload-resolution location, preserving template arguments, deduction,
defaults and written source. Synthetic size/alignment arguments are not retained
as source AST pointers. The checked object layout determines allocation size and,
when Clang selects it, the additional alignment argument. The existing bounded
object size and type restrictions still apply.

Placement arguments are evaluated and captured before the allocation call. Their
full-expression temporaries remain live through initialization. Initialization
executes directly at the returned address, preserving constructor `this`, record
self-pointers, reference bindings and source cv qualifiers. Scalar default
initialization performs no store; value initialization follows the existing zero
initialization rules. A failed nonthrowing allocation skips the initializer as
required by Clang's semantic null-check flag, while retaining argument cleanup.
A newly allocated object receives no automatic scope cleanup registration.

Delete captures its pointer operand once. Null skips destruction and deallocation.
For nonnull pointers it runs the existing destructor helper first, then calls the
selected usual deallocator with that captured address. Optional size and alignment
parameters use the source object layout; aligned deletion uses the target's
preferred object alignment, matching pinned Clang. A destructor that changes the
variable originally holding the pointer cannot change the deallocation address.
Global qualification and class-specific selection remain Clang's decisions.

When a delete/delete[] expression selects Clang's untouched implicit global
`operator delete` or `operator delete[]` with signature `void(void*, size_t)`,
core v2 implements the C++17 default forwarding rule by calling the matching
source-defined unsized `void(void*)` operator. The selected sized declaration and
every redeclaration must be implicit, have no written type, location or body, use
the ordinary default ABI and noexcept signature, and carry only Clang's exact
implicit default visibility attribute, if any. Lookup must find a unique ordinary
namespace-scope non-template definition in this source unit; its declarations,
attributes, parameter types and body retain their ordinary source checks. A
written sized definition takes priority. A written sized declaration without a
body, including one after the delete expression, cannot borrow this default.

Scalar and array operator names never substitute for each other. Class-specific,
aligned and destroying deletion, direct calls and operator addresses do not gain
a default definition from this rule. Template friend bodies are not instantiated
or borrowed by fallback lookup. The generated call targets the actual checked
unsized function; there is no runtime C++ ABI dependency or synthesized sized
function. Array cookie decisions still use the original expression's usual
array-delete metadata, even for trivial elements with a class-specific sized
delete[] and a global-qualified `::delete[]`. The raw allocation pointer must be
recovered before calling the unsized body. This follows C++17's default
[single-object](https://timsong-cpp.github.io/cppwp/n4659/new.delete.single#16) and
[array](https://timsong-cpp.github.io/cppwp/n4659/new.delete.array#15) deallocation
semantics. Paired admission/diagnostic cases, eight-ABI cookie protocol checks,
relocation and a saved-NC C client with 15 O0/O2 runtime checkpoints cover this
boundary; native validation requires the implementing revision's CI.

Explicit destruction followed by placement construction can reuse supported
storage and satisfy an automatic object's later cleanup obligation. Both source
operations enable the independently verified `memory_lifetimes` alias policy
below. The translator does not prove runtime ownership, storage capacity, address
alignment or transparent replacement of every old alias. Defined C++ source must
supply suitable storage and obey const, reference-member and lifetime rules.

The self-contained source profile supplies no default heap. A selected function
without an owned definition, the exact embedded standard placement definition or
the default forwarding case reports `TR0203`; it never becomes an unchecked host
allocation call. Reserved global `operator new(size_t, void*)` and its matching
delete cannot be defined or redeclared by user source. The standard functions are
valid only as Clang-selected new-expression machinery; direct calls and addresses
remain rejected. Class-specific placement and custom global overloads with a
distinct parameter list retain the source-owned path. Only exact allocation
attributes synthesized by pinned Clang are admitted. Inherited default visibility
must trace to the exact prior implicit global allocation declaration:
pinned Sema's merge loses its implicit bit, but keeps the inherited flag and absent
source range. Written or unrelated attributes remain rejected. No exception-throwing path, construction
rollback or foreign ABI is newly admitted.

Paired source/diagnostic fixtures check function/template selection, missing
runtime, unsupported source, type aliases, default arguments and operator
addresses. A source-only enum stand-in tests aligned-only and sized/aligned
deallocator selection; it supplies no SDK or implicit extended-alignment new.
Protocol checks cover call closure, size/alignment, construction/destruction
order, null branching, lack of lexical new ownership and relocation. Twenty-seven
runtime checkpoints at O0/O2 with strict aliasing and inlining disabled cover
storage identity, failed allocation, temporary cleanup, placement reuse and saved
deallocation addresses. Native validation requires CI of this implementation.
Default/standard heap runtime, throwing/placement runtime array-new bounds, extended-alignment source
support, exceptions/unwinding, standard headers, inheritance and full C++/STL
remain unfinished.

## Native C heap calls

Core v2 admits direct native `malloc`, `calloc`, `realloc` and `free` calls declared in
source with these exact global C signatures. `Size` denotes the target's native
unsigned `size_t` type; parameter top-level const and compatible redeclarations
retain their ordinary C++ meaning.

```cpp
using Size = decltype(sizeof(0));
extern "C" void *malloc(Size);
extern "C" void *calloc(Size, Size);
extern "C" void *realloc(void *, Size);
extern "C" void free(void *);
```

Source-defined C++ allocation functions can call these operations, including
admitted class `operator new[]`/`delete[]` using the real native heap and native
array cookies. This does not yet supply default throwing C++ allocation,
`new_handler`, `bad_alloc`, exceptions or general standard-library headers.
NeverC's ordinary source frontend remains C23.

Only noninline, non-template, nonvariadic global external-C declarations with
ordinary C calling convention, exact parameter/result types and no default
arguments qualify. Every redeclaration must be source-owned and checked. Pinned
Clang's exact implicit `BuiltinAttr` and `AllocSizeAttr` are admitted for these
signatures; explicit attributes and unrelated implicit attributes remain rejected.
Written types, default arguments, qualifiers and expressions remain checked even
in discarded or unevaluated source. Only the exact direct callee reference is
exempt from function-value conversion; addresses, decay, discarded names and
composite callees remain unsupported. A source definition anywhere in the
redeclaration chain takes priority and is translated as an ordinary function.

Lowering captures each argument before evaluating the next and emits a checked
`native_heap_call`. Independent driver evidence requires a hosted Linux/macOS or
explicit Windows MSVC/GNU target and unsigned pointer-width, pointer-aligned
size_t. The typed IR checks each operation's arity, local void-pointer result or
void free result, and rejects collisions with source C exports. Saved NC declares
only used operations, uses the native CRT, retains Win32 cdecl and recorded
Windows ABI guards, and rejects DynCode/non-hosted compilation. The process's
normal allocator remains authoritative, including legitimate allocator overrides.
`realloc` uses a private volatile function-pointer bridge initialized from the
native CRT symbol. The indirect call survives O0/O2, so the generated C23
compilation cannot replace a zero-size call with its own direct builtin inference.

Zero-size results, null free, resize failure and ownership of the original block
retain the process allocator's actual contract; there is no extra allocation,
private heap, invented C++ exception, or promise about unobservable allocator
calls or additional interposer behavior. Standard headers and complete C++/STL
remain unfinished.

Paired accepted/rejected source cases, raw protocol relocation, malformed IR,
eight-target width/calling-convention checks and an independent C client at O0/O2
cover the boundary. The C client allocates, grows and releases translated
allocations in both directions while checking the preserved prefix. Both the
native CRT and the default program-entry-owned allocator are tested; a helper
translation unit never chooses its caller's allocator. Source-defined class allocators exercise object/array
construction and destruction on the native heap. Native
compilation and execution require CI of the implementing revision.

## Constant array allocation

Core v2 admits source-owned C++17 `new[]` with a nonnegative integer constant
outer extent, including zero, and matching `delete[]` of valid escaped pointers.
Scalar, pointer, callback, const and admitted record elements can have fixed inner
array dimensions. Selected global/class/template allocation functions and custom
placement arguments retain the single-object source-definition contract. The
ordinary NeverC frontend remains C23. This is one step toward full C++/STL.

The frontend uses the same strict integer constant-expression check as pinned
Clang's semantic array initializer construction. It also checks a negative value
before the final implicit size_t conversion. Written casts retain their source
semantics. Outer extents are at most 65536, total initialization storage units at
most 200000, and allocation bytes plus padding must fit target size_t. Runtime
bounds have the separate [nonthrowing class-allocation contract](#runtime-array-allocation).
Throwing runtime bounds still require `bad_array_new_length` support. No SIZE_MAX
allocation request substitutes for the specified invalid-length behavior.

Only exact array-initializer wrappers belonging to the checked new-expression
can bypass serialization as value types. In particular zero length emits no
`array:0` type or dummy object. Bound source, constructors, destructors, defaults,
explicit clauses and written types remain checked even when no element executes.
Allocation still occurs for zero length. Nullable allocation branches before
cookie stores, pointer adjustment or initialization; placement argument effects
and their ordinary cleanup remain observable.

Allocation size is outer-count times the complete allocated inner type size,
plus the native cookie where required. Both new and delete use Clang's resolved
`doesUsualArrayDeleteWantSize()` fact, which can differ from the signature of the
finally selected function, including explicitly qualified global deletion.

| Native family | Cookie condition | Cookie layout |
| --- | --- | --- |
| Generic Itanium: x86/x64 Linux/macOS, AArch64 Linux, admitted Windows GNU | Nontrivial element destruction or usual sized array deletion | max(sizeof(size_t), preferred element alignment) bytes; flattened count right-justified |
| Apple ARM64 macOS | Same condition as Itanium | max(2*sizeof(size_t), element ABI alignment) bytes; base-element size at offset zero and flattened count at offset sizeof(size_t) |
| Explicit MSVC Windows | Nontrivial element destruction | max(sizeof(size_t), element ABI alignment) bytes; flattened count at offset zero |

Module `array_cookie_abi` metadata and independent native target/size_t capability
must agree. Saved NC asserts size_t layout and rejects a mismatched Windows C++
ABI; existing architecture/OS guards distinguish Apple ARM64. This checks the
ABI contract, not the correctness of every producer-generated address expression.
The typed IR, carrier/record layout and `memory_lifetimes` rules still apply.
MSVC's cookie-free trivial elements cannot supply a count to a selected sized
array delete in this profile; that delete expression reports `TR0201`. New-only
uses and unsized deletion remain admitted. No private incompatible cookie is added.

Initialization expands into actual element destinations. Direct omitted default
constructor argument temporaries end before the next element; explicit clauses
and aggregate field defaults keep their enclosing full-expression lifetime.
Each occurrence gets its own temporary storage. String initialization writes its
code units and zero-fills the remainder. Source result pointers retain cv and
inner dimensions. Allocated arrays acquire no lexical cleanup owner.

Delete captures its operand once and skips everything for null. Before invoking
a destructor it captures the raw allocation pointer, cookie count and allocation
bytes. A runtime loop visits base elements in reverse order through byte offsets
from the raw storage, avoiding a C pointer stride across inner arrays. It then
calls the selected deallocator once with the original allocation address and any
selected size/alignment values. Destructors reseating the source pointer cannot
change these saved values; reference-member temporaries already destroyed at the
new-expression's end are not destroyed again by delete.

Paired source/protocol cases, eight-target cookie/layout checks, forged metadata,
relocation and a separate C client at O0/O2 with strict aliasing cover these rules.
The native and pinned-upstream runtime checks run only in CI. Throwing runtime
new bounds, default heap/standard placement, extended-alignment source types, exception
unwinding, standard headers and complete C++/STL remain unfinished. Language
requirements follow [C++17 array new](https://timsong-cpp.github.io/cppwp/n4659/expr.new)
and [C++17 delete](https://timsong-cpp.github.io/cppwp/n4659/expr.delete); native cookie
layouts follow the pinned Clang20.1.8 ABI implementations.

## Runtime array allocation

Core v2 also admits runtime outer lengths for source-defined class `operator new[]`
with a nonthrowing exception specification and no placement arguments. The
ordinary NeverC frontend remains C23. This path uses the same independently
checked native array cookie families, allocation operators and reverse `delete[]`
as constant allocation. It does not provide the default heap or standard library.

The bound is evaluated once. A negative signed result is detected before Sema's
implicit conversion to size_t. Positive wide integers retain normal unsigned
conversion semantics, including truncation on 32-bit targets; explicit source
casts retain their own semantics. The converted count must fit
`(SIZE_MAX-cookieBytes)/sizeof(allocatedInnerType)` and be at least the number of
explicit outer initializer clauses. Invalid lengths return a typed null pointer
without calling the allocator or running any element initializer. Zero valid
lengths still call the allocator. An allocator returning null also skips cookie
writes, pointer adjustment and all element initialization.

Only the exact runtime new-expression's incomplete or prefix-sized initializer
wrappers bypass array value serialization. The private pinned Sema patch retains
the unknown-bound sentinel as the loop filler, never as an explicit initializer.
Fixed inner arrays keep their bounded separate materializations. All source,
selected constructors/defaults, types and semantic fillers are checked before
erasure, including a whole new-expression inside sizeof, noexcept or dead code.

Explicit clauses use bounded expansion and fresh temporary storage. Remaining
elements use a runtime loop and accept direct omitted default construction,
implicit zero initialization, absent initialization, or the checked aggregate
fillers described below. A whole-array
default-constructor wrapper can initialize fixed inner arrays recursively.
Default-constructor argument temporaries end after each element; explicit-prefix
temporaries and bound-conversion temporaries remain alive until the enclosing
full-expression ends. This also applies on invalid-length and allocator-null
paths. ExprWithCleanups alone never creates a per-element lifetime boundary.

Repeated semantic aggregate/list fillers, including fixed inner arrays, are
admitted when their initialization writes directly into the current array element
without separate temporary storage. The proof distinguishes actual initialization
destinations from evaluated, bound or discarded expressions. It follows selected
default member/default argument expressions and all semantic clauses and fillers,
with the existing depth and expansion budget. Scalar calls, live object references,
self pointers, scalar defaults and nested plain aggregate lists are supported.
Each field is initialized before the next field is evaluated. These aggregate
iterations keep the enclosing new-expression's full-expression boundary.

Materialized/bound temporaries, opaque values, evaluated record/array prvalues,
constructed subobjects, record-return calls and by-value record arguments remain
outside this conservative proof and report `TR0201`. Even a trivial scalar
temporary can have observable identity, and a discarded aggregate can need
enclosing-expression destruction. Arbitrary repeated temporary storage requires
further lifetime support. The existing direct default-constructor category keeps
its separate per-element cleanup behavior. Throwing allocators require actual exception runtime support;
runtime placement allocation needs a separately checked argument-evaluation
contract. Runtime primitive/string allocation through the default heap remains
outside this source-owned class path. Constant-length cases retain their broader
existing initialization and placement support.

Paired source/protocol tests, eight-target 32/64-bit bound checks, relocation and
an O0/O2 separate C client cover signed negative values, positive wide conversion,
byte overflow, too-short prefixes, zero, null, receiver identity, escaped deletion,
per-element cleanup and enclosing bound/prefix temporary cleanup. Native checks
also cover 17 accepted and 12 rejected aggregate cases and 13 O0/O2 runtime
checkpoints for self/member-reference identity, nested arrays, effects, prefixes,
null/invalid suppression and reverse destruction using native heap allocation.
Upstream Clang O0/O2 baselines run in the diagnostic workflow. These checks
run only in implementing CI. The semantics follow
[C++17 array new](https://timsong-cpp.github.io/cppwp/n4659/expr.new) and
[CWG 1992](https://cplusplus.github.io/CWG/issues/1992.html). Full C++/STL remains unfinished.

## Explicit destruction

Core v2 admits direct nonvirtual destructor calls on the complete owned records
already supported by this profile. Dot, arrow, qualified names and aliases retain
the selected destructor, including admitted class and function template instances.
A nontrivial call evaluates its receiver once, passes its actual address to the
existing destruction helper, executes the body and then destroys members in
reverse order. Const receivers use the destructor's unqualified internal `this`.
Trivial implicit/defaulted destructors evaluate the receiver without requiring a
body or reading the complete object.

Scalar pseudo-destructor calls require an exact resolved scalar type and an empty
argument list. Dot operands designate storage without loading an uninitialized
scalar; arrow operands evaluate the pointer. Original bases, qualifiers, scope
types and destroyed-type spellings remain inspected. These operations follow
[C++17 pseudo-destructor semantics](https://timsong-cpp.github.io/cppwp/n4659/expr.pseudo).
The pinned producer includes receiver evaluation when calculating `noexcept`:
a potentially throwing pointer-producing call keeps the whole expression
potentially throwing, although the pseudo-destructor operation itself cannot throw.

An explicit call does not cancel a registered automatic or temporary cleanup.
Defined source must satisfy the original object's later lifetime obligations;
the translator does not detect every lifetime violation or reconstruct an object
implicitly. `noexcept` and other unevaluated uses emit no destruction and do not
force an otherwise unused template destructor body. Written specifications and
materialized bodies retain source checks. See
[C++17 object lifetime](https://timsong-cpp.github.io/cppwp/n4659/basic.life).

The module records `memory_lifetimes: true` when these source operations are
admitted, including unevaluated occurrences. The C23 emitter then marks every
source object carrier with `may_alias`: scalar typedefs, pointer objects and
pointees, nested arrays, callback signatures, and record tags/fields. Functions,
globals, locals, casts and helper pointer signatures use these types consistently
across calls. Target/layout and callback guards remain mandatory. This policy
preserves permissive memory accesses under ordinary optimization; it grants no
new typed IR operations, const writes or ownership authority. Private pure
arithmetic helpers and atomic initialization guards expose no source storage and
retain their existing declarations. Unflagged modules keep their existing output.

Paired source/diagnostic cases, lazy-body and relocation protocol checks, and
O0/O2 receiver/cleanup fixtures accompany this change. Independent generated-NC
fixtures cover scalar, pointer-object, array, record and callback alias accesses
at O0, O2 and explicit strict aliasing, with inlining disabled. Native validation
requires implementing CI. Single-object allocation and placement restart follow
the contract above. Default heap runtime,
array cookies, exceptions/unwinding, standard headers and full C++/STL remain
unfinished.

## Record destruction and normal lifetimes

Core v2 admits ordinary user-provided destructors of the records described
above, including out-of-line definitions and implicit destruction of containing
records. Copy construction and assignment must be admitted independently; a user
destructor does not by itself require nontrivial copying. Bases follow the empty-chain
contract; virtual dispatch, unions and unsupported field layouts remain excluded. Reference members retain
their bindings and do not cause their referents to be destroyed.
Selecting a deleted destructor remains a C++ diagnostic, including in dead code.
Explicit calls follow the contract below. Ordinary and defaulted destructors accept implicit exception
specifications and the resolved standard written forms described below. Every ordinary
user destructor needs an owned body; a supported `= default` destructor uses the
member cleanup described below without requiring a materialized body.

Each record needing destruction has one internal ordinary void function with a
mutable pointer to its object. It runs the user body, destroys its body locals,
and then destroys members in reverse declaration order, followed by its admitted
direct empty base. Arrays recurse in
reverse index order. Early `return;` in a destructor still reaches member
cleanup. Implicit containing-record destruction is synthesized from the checked
record fields, independent of whether Clang has instantiated an implicit body.
These functions and calls retain the existing typed protocol verification.

Automatic objects become live only after initialization completes. Normal scope
exit, `return`, `break` and `continue` destroy the live objects in exited scopes
in reverse completion order. Implicit scopes around unbraced statement bodies
are included. A for-init object lives through the loop; condition-variable scope
follows its statement, including the for increment. Branches and repeated
iterations never destroy skipped or already cleaned objects. Storage declarations
at function entry do not start source object lifetimes.

Temporaries without automatic local-reference lifetime extension are owned by
their enclosing full-expression. Cleanup runs
in reverse order of completed initialization at declaration initializers,
expression statements, conditions, return operands, for increments and each
constructor member initializer. Temporaries in an aggregate initializer remain
alive through all clauses of that initializer. Nested call arguments do not
prematurely clean enclosing temporaries. Short-circuit and conditional expressions
clean only the temporaries actually initialized on the selected path. Conditions
capture their converted bool value, switch selectors their promoted value, and
return operands their value or destination before temporary cleanup can change
an aliased source object. The final false loop condition also performs cleanup.

A by-value record parameter is owned by the callee. NeverC evaluates its arguments
left to right and destroys parameter objects in reverse order after body locals,
at callee exit. C++17 permits implementations to choose callee-exit or enclosing
full-expression destruction for parameters; this profile consistently selects
callee-exit destruction. Caller argument temporaries other than the parameter
objects keep their enclosing full-expression lifetime. The caller owns a record
result and destroys it according to its destination's lifetime. An intentional
source copy has its own lifetime. Trivial copies retain stored pointer values;
user copies execute their selected bodies.

```cpp
struct AddOnExit {
  int *value;
  int amount;
  ~AddOnExit() { *value += amount; }
};
int saved(int &value) {
  AddOnExit local{&value, 2};
  return value;
}
int main() {
  int value = 3;
  int before = saved(value);
  return before == 3 && value == 5 ? 0 : 1;
}
```

This increment covers normal completion only. Throw/catch, stack unwinding,
partial construction rollback and default heap runtime remain rejected.
Static/global object destruction uses its registered lifetime contract. Temporary calls and automatic local reference
extension follow their separate contracts below. Existing expansion/storage limits also bound emitted
cleanup instructions and recursive array destruction. V1 profiles retain their
original trivial-lifetime boundary. Regression fixtures cover O0/O2 execution,
protocol ownership and signatures, return capture, member/array order and
relocation; native success must be established for the implementing revision.

## Ordinary overloaded operators

Core v2 admits source-owned ordinary member and non-member operator functions
with supported parameter/result types and checked bodies. Supported kinds are
arithmetic, bitwise and comparison operators, logical `!`/`&&`/`||`, comma,
prefix/postfix `++`/`--`, dereference/address, subscript, function call, arrow,
arrow-star, assignment and every compound assignment. Clang selects the overload
and enforces C++17 arity and declaration rules before emission. Member operators
may be const, unqualified, `&`-qualified or `&&`-qualified where legal. Standard
resolved exception specifications and queries follow the next section.

Each selected overload becomes an ordinary typed call. Non-member operators have
only their explicit parameters; member operators also receive the actual object
pointer. Record results use the existing hidden destination before receiver and
parameters. Reference results preserve their aliases, including subscript and
increment results. Addresses of ordinary free operators follow the callback signature contract;
nonstatic member addresses and record-by-value callback signatures remain unsupported. Conversion functions follow their separate
contract below. The operators of admitted class-template instances follow the contract above.
Unsupported dependent friend class-template forms and friend-type expansions,
virtual dispatch remains excluded. Allocation/deallocation operators follow their separate contract. Temporary call operands
follow the separate full-expression contract below.

Operator notation preserves the required C++17 operand sequencing. Assignment and
compound assignment capture the RHS before the LHS, including any source
reference before receiver-side alias changes. Shift, subscript, comma and
logical overloads evaluate the left operand first. Overloaded `&&` and `||`
evaluate both operands and call the selected function; they do not inherit
builtin short-circuit behavior. Other unspecified choices use a permitted
order. Explicit `.operatorX(...)` calls capture the receiver first and then their
arguments. Prefix/postfix selection retains the actual chosen function and the
postfix signature's int dummy parameter.

Non-member assignment operators can have two destructible by-value parameters.
NeverC consistently initializes these RHS-first, for both operator notation and
an explicit `operatorX(a, b)` call. That is a permitted ordinary function-call
choice and gives every call to that function the same parameter initialization
order. The callee destroys those parameters in reverse order, LHS before RHS,
including early returns. A directly constructed record result remains owned by
the caller and is destroyed after parameter cleanup. Other ordinary parameters
retain their existing initialization and reverse-destruction convention.

General user-provided `operator=` signatures can take value parameters and return
void, scalar, record or supported reference types; const and ref-qualified
receivers keep their source restrictions. These ordinary functions follow their
bodies rather than the stricter rules for generated/defaulted special members.
Assignment itself does not implicitly destroy or reconstruct the receiver;
by-value parameters and record results do have their own normal lifetimes.
Containing generated assignments preserve selected member calls and clean up any
discarded member-assignment record results, including within arrays.

```cpp
struct Cursor {
  int *value;
  int &operator*() const { return *value; }
  Cursor &operator++() { ++value; return *this; }
};
int main() {
  int values[2] = {3, 4};
  Cursor cursor{values};
  ++cursor;
  *cursor = 7;
  return values[1] == 7 ? 0 : 1;
}
```

O0/O2 no-inline fixtures cover operator categories, sequencing and alias changes,
logical operand effects, custom assignment signatures, record results and exact
lifetime counts. Protocol fixtures check selected identities and full signatures,
free/member argument offsets, parameter capture versus destruction order, direct
result storage, cleanup and deterministic relocation. Unsupported code is still
inspected inside unused functions and noexcept queries. V1 admission is unchanged;
library headers and complete STL remain in development. Native evidence
must come from the implementing revision's CI.

## Deduced reference conversions

Admitted ordinary classes and class-template instances support conversion
functions whose deduced result is a reference. `operator decltype(auto)` can
return an lvalue or rvalue reference; `operator auto&&` can collapse to an lvalue
reference when its return expression is an lvalue. Const receivers and results,
explicit calls/casts, out-of-line definitions and reference arguments retain
normal C++17 selection and storage identity.

The embedded frontend deduces only return forms that can become lvalue
references before applying its reference-only candidate filter. Bare `auto`,
`auto*`, `const auto&&` and `auto*&&` do not gain unnecessary instantiations on
that path. `auto&` and conversions permitting rvalues use their existing
deduction paths. Access, explicit/deleted selection, ambiguity and failed
deduction still receive normal C++ diagnostics. Every materialized body remains
subject to the profile's source and definition checks.

Selected conversions emit ordinary typed calls with a receiver pointer and a
`ptr:` or `cptr:` reference result. They preserve the original object or field;
no owning copy or runtime type-deduction operation is introduced. Protocol
fixtures check exact signatures, aliases, call closure and relocated identities.
Native O0/O2 fixtures check mutation and single evaluation. Native results require
CI from this implementation; full C++17/STL remains unfinished. C++ is currently
the only input language, with other frontends planned. Clang libraries are
embedded, and no external Clang executable is needed at translation time.

Fresh function arguments use the same reference rules without a preceding local
binding or explicit conversion call to trigger deduction. The private frontend
resolves necessary placeholder results before reference compatibility checks in
both initialization and overload argument analysis. Direct reference-candidate
search excludes a written non-function lvalue result from direct rvalue-reference
binding. Later initialization can still require its result type to assess an
indirect standard conversion, such as int& to a temporary bound to const long long&.
For an rvalue-reference argument, overload analysis rejects a non-function
lvalue-reference conversion result even on that indirect path. A competing
auto& body whose result cannot be deduced retains its ordinary C++ diagnostic;
it is not guaranteed to remain uninstantiated. Explicit conversions, access,
constness, result category and overload
ranking retain ordinary C++ diagnostics. Paired fixtures keep the conversion
source fresh; O0/O2 cases verify the selected call, alias, mutation and constness.

Temporary reference arguments remain alive through the enclosing full expression.
A check within that expression observes the live object; a following statement
observes cleanup. By-value parameter cleanup is checked after the full expression
without assuming whether parameter destruction happens on function return or at
the caller's expression boundary. Unevaluated template member calls still require
a materialized definition under this profile's existing source-closure rule;
separate fixtures cover evaluated calls and signature-only diagnostics.

Concrete conversion-function definitions retain the substituted type source in
their conversion name, including out-of-line definitions that spell the same type
through an alias. The embedded frontend replaces the generic name metadata copied
from the definition with that definition's concrete substitution. Its original
type expressions still undergo source checks; unused dependent definitions remain
lazy. Native CI covers conversion results and object lifetime at O0 and O2.

## User-defined conversion functions

Core v2 admits ordinary source-owned conversion functions on live objects and
admitted full-expression temporary receivers,
including implicit and explicit integral, enum and pointer conversions,
contextual explicit `operator bool`, and supported reference or record results.
Const, unqualified, `&` and `&&` receivers retain C++17 overload selection.
Explicit calls such as `value.operator int()` use the same selected method.
Constexpr, out-of-line definitions and resolved noexcept specifications follow
the existing declaration and constant-expression rules. Clang enforces access,
explicit selection and valid conversion signatures before emission.

The checked AST conversion wrapper must contain the selected direct conversion
call with its exact result type and value category. It becomes an ordinary typed
call, executed once, with the receiver captured once. Following standard
promotions, enum conversions and pointer-to-bool conversions remain separate
typed operations. Contextual bool conversions retain builtin short circuiting;
an unevaluated noexcept query inspects the conversion but does not execute it.

Reference results retain the actual aliased storage and constness without
creating an owning object. This includes scalar/pointer references and live
rvalue references. Initializing a record value from a record-reference conversion
retains the subsequent copy or move selected by Clang. A record prvalue result
instead initializes its actual destination directly: a local, field, array
element, returned object or by-value argument. Its hidden result destination
precedes the receiver. Self-addresses and normal destruction follow that
destination. Discarded object results are destroyed once at the full-expression
boundary; references never acquire a result-object destructor.

```cpp
struct Value {
  int n;
  operator int() const { return n; }
  explicit operator bool() const noexcept { return n != 0; }
};
int main() {
  Value value{7};
  int number = value;
  return value && number == 7 ? 0 : 1;
}
```

Conversion functions have no explicit parameters. Admitted class-template
instances and member conversion templates follow the contracts above. Virtual conversions,
volatile/restrict receivers, unsupported result types and function/member
pointers remain excluded. Full-expression temporary receivers and converted
reference arguments follow the next section. Automatic local reference extension
has its own contract below. Static lifetime extension, allocation,
exception execution, remaining template forms and complete STL still require further work.
Unsupported bodies and operands remain checked even in unused declarations,
constexpr initializers, static assertions and noexcept queries. V1 is unchanged.

O0/O2 no-inline fixtures check overload selection, receiver effects, bool short
circuiting, pointer/reference aliases, actual record destinations and exact
copy/move/destructor counts. Protocol fixtures check selected identities and full
signatures, result/receiver ordering, reference returns, selected copying, cleanup,
unevaluated queries and relocation. Native results require the implementing CI.

## Full-expression temporary calls

Core v2 admits temporary receivers and temporary reference arguments for ordinary
functions, constructors, methods, operators and conversion functions. Materialized
scalar, enum, pointer, record and bounded fixed-array values must have Clang's
`SD_FullExpression`
duration, no extending declaration, and a matching prvalue initializer. Every
materialization is checked during recursive source inspection, including erased
noexcept/sizeof/static-assert paths, and again during lowering. Local references
require the separate automatic-owner proof below; fresh reference returns remain
rejected.

Each evaluation initializes real addressable storage once. A scalar reference
argument receives that storage's address; const/reference qualifiers retain their
typed carriers. Temporary objects keep their actual receiver identity and may
use selected const/ref-qualified methods. Subobjects, array decay, offsets,
arrow access and returned reference aliases remain views of the same owner.
Standalone fixed-array temporaries use their own complete array destination
and cleanup under the array contract below. Array subobjects of an admitted
record temporary share that record's storage and cleanup.

Temporary arguments and receivers remain alive throughout argument evaluation,
the selected call, result construction and callee parameter destruction. They
are then destroyed at the enclosing full-expression boundary in reverse order
of completed construction. A constructor member initializer has its own boundary;
aggregate clauses retain their enclosing aggregate boundary. By-value parameter
objects still belong to the callee. Returned object prvalues use their actual
caller destination, while reference results add no owner. Conditions and return
values are captured before cleanup. Conditional paths destroy only constructed
objects, and each loop evaluation constructs and destroys its temporaries again.

```cpp
int read(const int &value) { return value; }
struct Value {
  int n;
  int add(const int &value) const { return n + value; }
};
int main() {
  int result = Value{4}.add(2) + read(3);
  return result == 9 ? 0 : 1;
}
```

Reference parameters do not extend a temporary's lifetime. A reference returned
through a call may be used while its temporary remains alive within that same
full-expression; saving the alias does not create a new owner or lifetime
extension. The defined-execution source contract does not promise general
interprocedural dangling-reference diagnosis. Automatic local extension follows
the next section. Static lifetime extension, fresh reference returns, unsupported
types, exception execution, remaining template forms and complete STL still require further
work. This stage adds no wire opcode or external runtime and leaves v1 unchanged.

O0/O2 no-inline fixtures check real storage, scalar/reference conversion, temporary
receivers and subobjects, user/generated moves, call sequencing, parameter versus
caller destruction, result destinations, default/member initializers, conditions
and loops. Protocol fixtures check full signatures, addresses, cleanup guards,
reverse destruction and relocation. Native evidence requires the implementing CI.

## Automatic local reference lifetime extension

Core v2 preserves C++17 lifetime extension for ordinary automatic local lvalue
and rvalue references to admitted scalar, enum, pointer, record and bounded
fixed-array temporaries.
The materialized object must have Clang's `SD_Automatic` duration and name that
exact ordinary local reference variable as its extending declaration. The
materialized type must match its prvalue initializer; arrays must have complete
fixed bounds within the array/storage limits. This proof
is checked during full source inspection and again when binding the variable.
Const qualification and reference value categories retain their existing rules.

Direct braces (`const R &r{R{1}}`) and equal braces (`R &&r = {R{1}}`) preserve the
same actual storage. Only a semantic single-element transparent glvalue list with
the same type and value category is unwrapped. Semantic materializations hidden
behind the written list still undergo the full source checks, including dead,
constexpr and unevaluated source. A reference-list wrapper never constructs a
second aggregate or bypasses the lifetime-owner checks.

```cpp
struct Value { int n; };
int main() {
  const Value &record{Value{4}};
  int &&element = Value{5}.n;
  ++element;
  return record.n + element == 10 ? 0 : 1;
}
```

The extended object is constructed in one actual destination and survives the
initializer's full-expression. Its destructor belongs to the reference's lexical
scope, with reverse construction order alongside ordinary local objects. Member
and direct array-subobject references keep the complete record alive when Clang
records the extension. Nested call temporaries in the same initializer retain
their shorter full-expression lifetime; they are not adopted by the reference.
Conditional branches clean only their constructed owners. Loop conditions and
bodies recreate storage lifetimes each iteration; for-init references live through
the loop, for-condition references through its increment, and the final false
condition also cleans its object. Return, break and continue perform the same
scope cleanup. A returned value is constructed or captured before that cleanup.

Rebinding a reference to an existing alias adds no owner and does not re-extend
its source. Subsequent copies/moves preserve their selected constructor and
separate destination. Reference-returning calls, pointer arithmetic, dereference
and arrow access do not acquire extension merely by eventually reaching a
temporary: Clang must identify the exact extending variable. Known direct local
bindings through non-extended temporary subobject paths remain rejected. An alias
returned through a call has no general interprocedural dangling-use guarantee;
storing it does not extend the temporary passed to that call.

Static reference lifetimes and reference fields follow their separate contracts.
Checked local record decomposition follows its
[structured-binding contract](#local-record-structured-bindings).
Thread-local storage, unsupported types, exception unwinding,
remaining template forms and complete STL remain outside this increment. V1 is unchanged. O0/O2
no-inline fixtures cover storage identity, braces, subobjects, nested lifetimes,
copy/move/return ordering, conditional owners and loop exits. Protocol fixtures
check complete signatures, actual destinations, guarded cleanup and relocation;
native execution claims require CI at the implementing revision.

## Standalone fixed-array temporary lifetimes

Core v2 admits standalone temporaries of bounded fixed-array types with supported
scalar, enum, pointer or record elements, including multidimensional arrays.
Array extents remain 1..65536; complete storage and generated expansion each
remain bounded by the existing 200000-unit limits. Materialization charges the
actual array storage as well as emitted initialization. Array types and hidden
semantic initializers remain checked even in dead, constexpr or unevaluated code.

Each evaluation allocates one actual complete array. Elements initialize directly
at their final indices in source order. Omitted/default fillers are evaluated
separately for each destination, so record self-addresses and selected calls are
preserved. Direct element initialization creates no independent element owner.
Discarded array prvalues also use real storage, even when Clang omits an explicit
materialization node. Typed array brace wrappers and comma expressions preserve
the same destination; this does not add array assignment or a by-value array ABI.

```cpp
using Values = int[3];
int sum(const Values &values) { return values[0] + values[1] + values[2]; }
int main() {
  const Values &local = {1, 2, 3};
  int &&element = Values{4, 5, 6}[1];
  ++element;
  return sum(local) + sum(Values{2, 3, 4}) + element == 21 ? 0 : 1;
}
```

Arrays passed by reference, decayed to pointers, indexed or used as temporary
element receivers retain their complete owner until the enclosing full-expression
ends. Automatic array references, or permitted direct row/element references,
keep the complete array alive to their lexical scope's end only when Clang names
that exact reference as its extending declaration. Braced bindings use the same
checked semantic reference-list rules. References and pointer views retain their
typed addresses and introduce no owner. Nested argument temporaries keep their
separate full-expression lifetime; alias rebinding never extends it further.

A destructible array registers one complete owner after initialization and cleans
its elements in reverse dimension/element order. Conditional branches destroy
only constructed arrays, loop evaluations recreate their lifetimes, and return,
break and continue clean the appropriate scopes. Array construction needs no
external helper or memory-copy call. Static lifetimes and reference fields follow
their separate contracts. Thread-local storage, fresh reference returns,
non-extended pointer-derived bindings,
unsupported element types, throwing/placement runtime array-new bounds, unwinding, other template forms
and complete STL remain outside this increment. V1 and protocol major 1 are unchanged.

O0/O2 no-inline fixtures cover real element addresses, reference calls, decay,
subscripts, default fillers, discarded arrays, nested temporaries, multidimensional
ownership and early exits. Protocol assertions inspect complete typed signatures,
actual array/element destinations, one cleanup guard per array, reverse destruction,
query purity and relocation. Native execution evidence requires the implementing CI.

## Unevaluated declaration-only template signatures

Unused implicit instances of source-owned non-friend namespace function templates
can provide a resolved signature without a primary definition. This covers the
reference-overload pattern used by `declval`, including reference collapse, arrays,
function references and the substitution-failure fallback for `void`:

```cpp
template<class T> T&& probe(int);
template<class T> T probe(long);
template<class T> decltype(probe<T>(0)) value() noexcept {
  static_assert(!__is_same(T, T));
}
static_assert(__is_same(decltype(value<int>()), int&&));
static_assert(__is_same(decltype(value<void>()), void));
```

The selected namespace primary and actual declaration must retain their original
owned file contexts, without friend, copied-member or member-specialization
metadata. Actual calls and free operator calls collect their complete source
synchronously, including deduction evidence, type/value defaults erased from the
signature, selected function defaults, resolved exceptions and argument lifetimes.
Every collected dependency must pass final source validation even when the query
result is false. The same synchronous source proof applies to unused non-friend
namespace instances whose primary has an owned definition, including separate
namespace declarations and definitions. A generic body is never the actual
instance's body; it remains uninstantiated. Unselected defaults retain normal
C++ laziness; no missing body is fabricated or emitted. This extra source proof
does not change existing friend, member or already materialized body boundaries.

Runtime calls and function values still require definitions. Explicit directives,
ordinary non-template declarations and member/friend templates keep their existing
boundaries. Protocol fixtures check that declaration-only helpers and the poisoned
`value` body emit no functions or calls. Native O0/O2 fixtures check query results,
zero default-argument effects and independent calls to a defined specialization;
these results require the implementing revision's CI. Standard-header enablement
and complete C++/STL remain unfinished.

## Noexcept declarations and queries

Core v2 admits standard resolved exception specifications on its supported free
functions, methods, constructors, copy/move operations and destructors. `noexcept`
and `noexcept(true)` are nonthrowing; `noexcept(false)` is potentially throwing.
C++17 `throw()` has the nonthrowing meaning. Computed specifications may use
admitted constant expressions and nested `noexcept` queries. Clang checks source
compatibility across redeclarations and out-of-line definitions. Unwritten lazy
specifications on generated special members retain their normal resolution rules.

A resolved `noexcept(expression)` becomes a typed bool literal with its source
location. Clang determines the value from the selected functions, implicit or
explicit specifications and any required temporary destruction. For example, a
nonthrowing constructor with a potentially throwing destructor makes the complete
temporary construction query false. A declaration explicitly marked
`noexcept(false)` remains potentially throwing even if its current body has no
throwing operation. The query emits no operand calls, mutations, construction,
materialization or cleanup. Queries work in returns, conditionals, compile-time
initializers, assertions, field defaults and other exception specifications.

```cpp
int safe(int &n) noexcept { return ++n; }
int possible(int &n) noexcept(false) { return ++n; }
int main() {
  int n = 0;
  bool a = noexcept(safe(n));
  bool b = noexcept(possible(n));
  return a && !b && n == 0 ? 0 : 1;
}
```

Every written specification and query operand is still inspected, including
unused, nested and short-circuited expressions. Unsupported types and operations
remain rejected. Unevaluated calls to implicit free-function specializations may
keep the body lazy when the owned primary has a definition; unused non-friend
namespace template declarations can also supply checked signatures without a body.
Their actual signature, resolved specification and selected source remain checked. Referenced unused
inline friends use the same signature-only boundary with their exact paired
written/selected/owning source evidence. Missing required definitions,
unsupported callback forms and unsupported lifetime extension remain rejected. Explicit destructor queries inspect the selected
signature and written source without forcing an otherwise unused template body. Missing ordinary owned
definitions remain diagnostics. Dependent/unresolved written specifications,
vendor forms and C++17-invalid typed dynamic specifications are not accepted.
V1 profiles retain their original specification/query boundaries.

This stage adds declaration and query semantics within the existing source
subset. Throwing, catching, termination on an escaping exception and stack
unwinding require the later exception runtime work; throw/try/catch and foreign
throwing execution paths remain rejected. No exception behavior is silently
removed from an accepted throwing body. The frontend is built into NeverC, and
uses no external Clang executable or opaque source/IR fallback.

O0/O2 no-inline fixtures check query values, zero operand effects, actual calls,
copy/move and destruction counters, defaulted and out-of-line specifications,
field defaults and redeclarations. Protocol fixtures check bool literals, no
query-created owners or calls, selected signatures and deterministic relocation.
Native execution evidence must come from the implementing revision's CI.

## Pointer offsets and differences

Core v2 admits `p + n`, `n + p`, `p - n`, `p - q`, pointer `++`/`--` and
`+=`/`-=` for pointers to admitted complete object types. Offset operands include
explicit source integer promotions. Pointer-to-array operations use the entire
row size; record steps use the independently verified record layout. Offsets
preserve the pointer type and pointee qualifications. Difference operands must
point to cv-qualified versions of the same complete type, preserving nested
pointee types, integer widths and array extents.

The difference result uses the source target's signed `ptrdiff_t`, normalized to
an admitted 32- or 64-bit carrier. NeverC independently checks its native signed
ptrdiff width against the pointer width before accepting this operation; emitted
width/signedness assertions check the compilation context again. Casting the
result after a narrower native subtraction is not accepted as a substitute.

Private typed emission helpers select `p` for zero offsets and signed zero for
equal-pointer differences. This preserves the C++17 null-plus/minus-zero and
null-minus-null rules without unconditionally executing C pointer arithmetic.
Each argument expression is emitted once; helpers are reused by signature to
keep deeply nested IR from expanding the output exponentially. Source effects
are captured before helper calls. Compound assignment evaluates its RHS before
its LHS, and increment/decrement evaluates its target once.

Direct address formation `&p[n]` uses the same guarded offset; direct `&*p`
cancels the address/dereference pair. The syntactic left operand remains first
for `n[p]`. Null `&p[0]`/`&*p` cancellation is the selected implementation behavior,
not a claim that C++17's null arithmetic rule itself defines null indirection.
No load, store or member access through null or past-the-end pointers is thereby
permitted. Array bounds, object lifetimes and representable same-array differences
retain the defined-source-execution contract.

Unary object-pointer `+`, source pointer/integer casts,
void/function/member-pointer arithmetic, and general iterator/container/STL
implementations remain outside this increment. Pointer comparisons follow the
contract below.

## Object pointer ordering

Core v2 admits `<`, `<=`, `>`, and `>=` between equally typed pointers to supported
complete objects after Clang's composite-pointer and qualification conversions.
This includes array ends, rows, record objects and subobjects, nested pointer
elements, concrete template functions and overloaded iterator comparisons.
The result is `bool`. The written types, template source and discarded branches
still pass the existing source checks.

[C++17 relational rules](https://timsong-cpp.github.io/cppwp/n4659/expr.rel)
define ordering within arrays and appropriately ordered record subobjects, and
give equality-specific results for all four operations. Results for other unequal
object pointers can be unspecified. Direct C23 pointer comparison could introduce
undefined behavior in that last case. Emission therefore compares the native
unsigned address representations. On the admitted flat-address targets this
preserves the required object order and chooses a consistent order for unrelated
objects, including null versus nonnull pointers. Each operand is emitted once.

The consumer independently verifies the unsigned pointer-sized carrier on native
x86 or AArch64 targets. The frontend cannot provide this expectation through the
protocol. Generated width and unsignedness assertions check the NC compilation
context again. Unsupported target representations fail validation. These private
emission casts do not admit source or wire pointer/integer conversion expressions.
Void, incomplete, volatile, function and member pointees retain their restrictions.

Paired positive/negative source cases and forged-IR tests check the operator/type
contract and independent carrier requirement. O0/O2 runtime fixtures with inlining
disabled cover arrays, rows, member order, one-past and equal/null pointers,
operand effects, iterator loops and the selected unrelated-object order. Native
acceptance requires CI for the implementing revision. Full C++/STL is unfinished.

## Fixed arrays and initialization

- Local arrays and record array fields may have nonzero constant extents. Each
  dimension is limited to 65536 elements, with at most 200000 expanded storage
  units and a separate 200000-node initializer/assignment expansion budget.
  Nested arrays and records count toward these limits. Initializing a large
  array can exceed the work budget even when its extent alone is permitted.
- Support includes array-to-pointer decay, `a[i]` and `i[a]`, multidimensional
  arrays, adjusted array parameters, and pointers/references to arrays
  as parameters and results. The syntactic left operand is evaluated before
  the right operand, as required by C++17; aliases keep the original storage.
- List/value initialization stores each element in source order. Omitted scalar
  elements are zero-initialized; record elements follow their selected aggregate
  or constructor initialization. Default initialization does
  not initialize scalar elements. Accessing one initialized element never copies
  unrelated uninitialized elements or record fields. Trivial record copies
  include their array fields; direct array assignment is not permitted.
- Array element qualification is preserved through decay and pointers/references
  to arrays. A const record's array cannot be used to obtain a mutable element
  pointer. Reads such as `make_record().values[0]` materialize an admitted record temporary
  for the full expression. Call-site references to temporary record subobjects
  share that lifetime. An automatic local reference extends the complete record
  only when Clang identifies that reference as its extending declaration.

```cpp
using Row = int[3];
Row &row(Row &value) { return value; }
int main() {
  Row values{5, values[0] + 2};
  row(values)[2] = 11;
  return values[0] == 5 && values[1] == 7 && values[2] == 11 ? 0 : 1;
}
```

Const global arrays and const records containing arrays follow the static storage
contract above. Mutable globals and static local/member arrays follow the
[static storage contract](#fixed-array-static-storage). Zero-length and variable-length
arrays and unsupported element types remain rejected, including in unused or
dead code. Admitted record elements follow the normal destruction rules above. Indexing requires the same
valid storage and in-bounds accesses as the source program; the translator does
not add a runtime bounds-check guarantee. Constant `sizeof` and type-form
`alignof` follow the integral-query rules above; pointer offsets and differences
follow their dedicated contract above.

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
entries named `bool`, `i8`, `u8`, `i16`, `u16`, `int`, `uint`, `i64`, `u64`,
`default-pointer`, `float` and `double`. These name the native emission carriers for the admitted source types.

The driver independently constructs NeverC's target model for its recorded
C23 validation options. The verifier compares all source carrier evidence
against this model; a matching triple alone is insufficient. Missing, unknown
or mismatching layout fields are rejected. Source sizes and ABI alignments
must agree, with supported byte sizes and power-of-two alignment.

Each response record also requires `layout` containing `size_bits`,
`abi_align_bits` and `field_offsets_bits` in declaration order. The verifier
reconstructs the natural layout of the currently supported standard-layout records
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

128-bit and extended integers, `long double` and complex types,
exception unwinding, other template forms,
exceptions, STL headers
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
local declarations, ordinary methods/this and receiver sequencing, static calls,
const overloads, reference results, direct constructor destinations and field order,
array filler calls, generated/defaulted lifecycle and lazy definitions,
source-selected user copying and assignment sequencing, normal destruction,
single-place materialization, pointer/reference aliasing with
inlining disabled, nested
const, nulls, reference-return assignment, array initialization and indexing order,
multidimensional arrays, temporary array reads, switch dispatch/fallthrough,
nested case entry and loop control, constant selectors, resource limits, unsupported
bindings, malformed IR, independent carrier/record layout evidence,
compiled layout assertions,
old-profile rejection and consumer profile/version boundaries. CI evidence must
be recorded against the
revision that runs these cases; earlier core v1 CI results do not establish
core v2 execution support.

### Concrete template exception source

For materialized free function templates and admitted class-template functions,
the frontend checks the resolved semantic `noexcept` expression when Clang's
written function type retains the primary's dependent expression. The match is
limited to that declaration's own function TypeLoc, including parenthesized
declarators. Written return and parameter types, and non-dependent exception
expressions, remain checked. Folding a `long double` expression inside an
exception specification does not admit it into core v2. Queries retain their
compile-time boolean result and do not execute the queried call.

### Later namespace defaults through imports

A namespace function's later default arguments are available through an earlier
`using` declaration, including reexports, inline namespaces and qualified
out-of-line definitions. The function's semantic namespace determines its default
set. A later block declaration or a distinct namespace cannot contribute defaults
to that set; a later overload is not added to the earlier import. Calls appearing
before the default declaration still require explicit arguments. Omitted arguments
retain declaration-site binding, evaluate once per call, and preserve the existing
reference temporary lifetime rules.

### Nested friend declaration access

Access to a nested friend function's return type, parameter types and default
arguments uses that function's own privileges. Its friendship with the nested
class does not implicitly grant access to private or protected names of an outer
class. Explicit grants and accessible inner aliases remain valid. Nominated names
are also checked in the befriending class before redeclaration merging. Ordinary
nested member access and non-function friend declaration checks retain their
existing behavior, including supported concrete friend types.
The embedded parser preserves lexical lookup while checking late default arguments
in the function context. Omitted defaults keep their call-site evaluation and
cleanup behavior.
