# Builtin C++ frontend profiles

NeverC includes the full C++17 frontend for its experimental `cpp-core-v1`,
owned-project `cpp-project-v1`, and bounded `cpp-math-v1` translation profiles.
Only C++ translation is implemented. The [support matrix](../docs/support-matrix.md)
defines the language subset, approved targets and measured runtime coverage.

## Build and installation

Build and install NeverC through the repository's normal CMake configuration.
The build pins upstream LLVM/Clang **20.1.8**, builds its required frontend
libraries statically, isolates their symbols from NeverC's own LLVM, and links
that frontend into the NeverC executable. Source and build dependencies are
needed while building NeverC; installed C++ translation does not launch a
separate helper or load an external Clang/LLVM library.

Before linking, the build checks the private frontend's definitions and references
against all host-library definitions. Clang builds use their matching `llvm-nm`
for host LTO archives; GCC builds use `gcc-nm`. MSVC builds read the COFF linker
indexes, including LTCG objects. AppleClang can use the private reader for ordinary
Mach-O archives; an incompatible LTO format fails the check. The build-only
`NEVERC_CPP_HOST_NM` CMake option selects a compatible host reader when needed.

The implementation lives in
[`neverc/lib/Translate/Cpp/Frontend`](../../../neverc/lib/Translate/Cpp/Frontend).
Its C ABI entry runs only in a private child invocation of the same NeverC
executable. This preserves request timeouts, crash isolation and bounded protocol
files without a second executable. `NEVERC_CPP_FRONTEND` has no effect.
The [internal protocol](../docs/protocol.md) reads a request file and creates a
new response file; existing response files are never overwritten. This is an
implementation interface, separate from public manifest/report/map schemas.
Requests must be regular files of at most 1 MiB, with JSON nesting at most 32;
main source files are limited to 8 MiB. Reads are bounded on the opened file
descriptor, and unknown or profile-inapplicable request fields are rejected.

The `frontend.build` identity includes the pinned version and implementation,
build-isolation and embedded-SDK inputs. CMake updates it when those inputs
change. The installed compiler retains NeverC's ordinary configured runtime,
including bundled Python when enabled. Generated user programs contain no
Clang/LLVM or C++ standard-library dependency.

## Scope and semantic handling

Core v2 admits [local record structured bindings](../docs/cpp-core-v2.md#local-record-structured-bindings)
for automatic source-owned trivial standard-layout aggregates with supported
non-volatile scalar or object-pointer direct public fields. Value bindings copy
or directly construct one hidden object; `&`, `const&` and `&&` forms alias the
selected storage with its original qualification. Initialization occurs once,
and exact temporary owners retain their C++17 scope lifetime. User tuple-like
bindings, reference/mutable/bit-field members, bases, nontrivial or volatile
records and static/global storage remain excluded. Legal
C++17 `if`/`switch` init-statements and ordinary `for` initialization remain
available; decomposition condition variables do not.

[Native fixed-array structured bindings](../docs/cpp-core-v2.md#local-array-structured-bindings)
use the same automatic local owners and aliases, including multidimensional
row bindings. Value forms copy or move elements through their selected supported
operations, including existing nontrivial source-record operations; prvalue
arrays directly initialize the hidden destination. Sources execute once,
copy/move default-argument temporaries are cleaned up per element, and an owned
array is destroyed once in reverse element order. Reference forms preserve
array qualification and exact temporary extension. Existing extent, type,
source and expansion limits remain in force.

[SDK structured bindings](../docs/cpp-core-v2.md#local-sdk-structured-bindings)
cover admitted `std::pair`, `std::tuple` and nonempty `std::array` storage,
including existing reference and mixed elements. Exact SDK traits and selected
`get` declarations are authenticated before initializing each hidden reference.
Value copies, shallow const, lvalue/rvalue reference collapsing and complete
owner temporary lifetimes follow C++17; custom tuple protocols and source
specializations stay excluded.

These binding forms also work as the declaration in an admitted C++17
[range-based `for`](../docs/cpp-core-v2.md#range-based-for). Each iteration
initializes one hidden owner from the selected dereference, then binds its
names. Copied array elements retain their selected copy/move and per-element
argument cleanup; iteration cleanup runs before increment or abrupt exit.
Range initialization and `begin/end` still execute once per loop.

[Function type metadata](../docs/cpp-core-v2.md#bare-function-type-metadata),
including direct lvalue/rvalue references, can pass through core v2 templates,
aliases, queries and transforms into existing callback pointers. Original
signature sources remain checked through deduction and type erasure; runtime
function-reference carriers remain unsupported.

Core v2 [unary type transforms](../docs/cpp-core-v2.md#unary-type-transforms)
check the original input, exact substituted source and result, including erased
aliases and consumed enum layout. Sixteen pinned kinds reuse existing type
metadata and runtime carriers.

Core v2 also admits exact angle includes of
[`<type_traits>`](../docs/cpp-core-v2.md#compile-time-type_traits) and
[`<cstdint>`](../docs/cpp-core-v2.md#fixed-width-integers-from-cstdint), plus
[`<limits>`](../docs/cpp-core-v2.md#numeric-bounds-from-limits) and
[`<ratio>`](../docs/cpp-core-v2.md#compile-time-rational-arithmetic-from-ratio),
[`<cstddef>`](../docs/cpp-core-v2.md#fundamental-types-and-bytes-from-cstddef), and
the bounded [`<utility>`](../docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility)
and [`<tuple>`](../docs/cpp-core-v2.md#value-tuples-from-tuple) and
[`<array>`](../docs/cpp-core-v2.md#fixed-value-arrays-from-array) surfaces,
[`<initializer_list>`](../docs/cpp-core-v2.md#initializer-list-views-from-initializer_list),
[`<optional>`](../docs/cpp-core-v2.md#value-optionals-from-optional),
[`<iterator>`](../docs/cpp-core-v2.md#iterator-metadata-from-iterator), and the
[`<algorithm>`](../docs/cpp-core-v2.md#algorithm-header-from-algorithm) pointer
algorithm surface. The exact
[`<numeric>`](../docs/cpp-core-v2.md#numeric-header-from-numeric) header is also
authenticated; its sequential scalar-pointer operations, default C++17
reductions and scans, function-pointer operations with directly convertible
scalar parameters and results including transformed scans, and integer
`gcd`/`lcm` lower directly. The exact
[`<new>`](../docs/cpp-core-v2.md#new-header-from-new) header is also
authenticated; its allocation metadata resolves at compile time and exact
`std::launder` calls on admitted object pointers plus exact standard placement
new expressions lower directly. The exact
[`<memory>`](../docs/cpp-core-v2.md#memory-header-from-memory) header has an
authenticated parsing boundary; raw-pointer `pointer_traits`, exact
`std::allocator` and exact `std::allocator_traits<std::allocator<T>>` metadata
resolve at compile time. Exact `uses_allocator<T, std::allocator<U>>`
identities, inherited aliases and constants also resolve. Exact single-object
`std::default_delete<T>` and unbounded-array `std::default_delete<T[]>`, where
`T` may have complete bounded inner extents, preserve their one-byte stateless
layout; their construction and calls lower directly through checked object or
flattened reverse array destruction and the checked scalar or array class/global delete selected by Clang.
`std::addressof`, `pointer_traits::pointer_to` and scalar-pointer destruction
and uninitialized construction algorithms lower directly. Exact runtime
allocator objects, C++17 `destroy`, and allocator-traits destruction,
`max_size` and copy selection also lower directly. Exact allocator and traits
allocation/deallocation forwarders call checked global new/delete
for complete default-new-aligned elements and nonoverflowing allocation
counts. One-byte elements accept runtime counts after target `size_t`
conversion; larger elements require nonoverflowing integer constant expressions
or an implicit conversion from a sufficiently narrow unsigned builtin type.
The complete source type's range must fit the target's `max_size`. Ownership
objects remain separate. The
exact [`<functional>`](../docs/cpp-core-v2.md#functional-header-from-functional)
header directly lowers calls on typed and transparent specializations of all 19
C++17 arithmetic, bitwise, comparison and logical standard function objects for
admitted arithmetic types, including heterogeneous transparent operands. Typed
comparison and logical objects also admit a top-level `const` scalar template
argument. Exact empty objects use a one-byte carrier and support trivial
construction, assignment, local and global storage, copying and by-value
passing. Exact integral `std::hash` specializations from `bool` through
`unsigned long`, plus `long long` and `unsigned long long`, use that carrier
and lower to target
`size_t` for temporary, stored, copied and by-value objects. Wide values retain
their bits on 64-bit targets and use libc++'s Murmur2 algorithm over eight
little-endian bytes on 32-bit targets. They support same-type copy and move
assignment and call directly or through `std::invoke`; the receiver is
evaluated once before its argument. The exact C++17 `nullptr_t` specialization
returns its pinned libc++ constant through the same object, assignment and
invocation boundary. Exact `float` and `double` specializations authenticate
the same scalar base and the outer zero-normalizing call operator. Both signed
zeros hash to zero; other values hash their IEEE representation. `float` uses
its 32-bit representation, while `double` preserves its bits on 64-bit targets
and uses the eight-byte Murmur2 path on 32-bit targets. Complete enum
specializations authenticate the primary `hash` and `__enum_hash<E, true>`
inheritance before delegating to the exact underlying integer hash. Exact non-volatile
object-, void- or fixed-arity ordinary function-pointer `std::hash<T *>`
specializations authenticate the pinned pointer
partial specialization, its pointer/`size_t` union and its exact internal hash
call. They reproduce libc++'s four-byte Murmur2 path on 32-bit targets and its
ABI-v1 eight-byte CityHash path on 64-bit targets through the same object,
assignment and invocation forms. Function pointers require the target's
object-pointer size and alignment. `long double`, volatile-object,
volatile-void and variadic function-pointer hashes remain outside this boundary.
Exact object
forms of `std::reference_wrapper<T>` and `std::reference_wrapper<const T>`, plus
exact fixed-arity function forms with admitted scalar, object-pointer or
fixed-arity ordinary function-pointer values, exact lvalue- or rvalue-reference
parameters, and exact lvalue- or rvalue-reference results, use
their authenticated target ABI layout: one pointer slot under the Itanium ABI
and an empty-base storage slot plus the pointer under the Microsoft ABI. They
lower direct matching-lvalue construction, direct and wrapper-taking
`std::ref`/`std::cref`, trivial copies, same-type assignment and `get()`.
Wrapper-taking calls preserve the referenced target; `cref` adds `const` for
object targets. Object forms also lower the implicit reference conversion.
Wrappers around admitted standard function
objects, stored fixed-arity function pointers and admitted function referents
are callable directly and through `std::invoke`, preserving exact scalar and
object-pointer, function-pointer, complete fixed-array or complete source-owned
record lvalue or rvalue references. Wrappers around
exact source-owned record callables use the same forms when Clang selects an
admitted defined lvalue or const-lvalue `operator()`, retaining the referenced
object's identity, mutations and reference results. The
exact C++17 `std::invoke` also lowers fixed-arity ordinary functions and stored
function pointers with directly convertible by-value scalar parameters, exact
fixed-arity ordinary function pointers including function-name decay, or exact
admitted lvalue or rvalue references to those values, complete fixed arrays or
complete source-owned records, or exact by-value complete source-owned
standard-layout records that are trivially copyable and trivially destructible,
and preserves the
corresponding scalar, object-pointer, function-pointer, lvalue- or
rvalue-reference, complete source-owned record value or `void` result, plus
the admitted typed and transparent standard function objects. Exact
source-owned record callables also lower when Clang selects an admitted defined
nonstatic `operator()` with the same argument and result boundary; lvalue,
const-lvalue and rvalue-qualified overloads, reference results, receiver-first
evaluation and temporary lifetime are preserved, including source-owned record
callables with fixed-array and source-owned-record reference parameters and
results, trivial source-record value parameters and complete source-record
value results. Across these checked invocation paths, const lvalue-reference
parameters also bind xvalues of the same unqualified type, including materialized
arguments. Their storage and original full-expression lifetime are preserved;
reference results do not extend that lifetime. These exact bindings still
exclude scalar conversions, base adjustments, nested pointer qualification
changes and volatile referents. Direct named
addresses of owned nonstatic member functions
also lower on exact-class lvalue, full-expression temporary, pointer or
`std::reference_wrapper` receivers. Admitted non-volatile scalar,
object-pointer or fixed-arity ordinary function-pointer fields lower on those
receiver forms and retain their qualified lvalue or xvalue result, including
reads of declared-const fields. Projected function pointers may be called or
reseated through that result. Exact directly
initialized local member pointers for admitted methods or data fields may also
be retained across statements and passed to `std::invoke`; their storage is
authenticated and erased before the same direct method call or field projection
is emitted. Exact same-type local copy/move-initializer chains, including chains
through the admitted `<utility>` reference adapters, are also authenticated and
erased. Those adapters may also wrap the authenticated pointer at its final
`std::invoke`, native member operation or `std::mem_fn` factory use. Direct addresses and stored data-member pointers may also be applied
with native `.*` or `->*` on an exact lvalue, pointer or full-expression
temporary receiver, preserving the field glvalue and the const qualification
contributed by the field or receiver. Temporary lifetime and native `.*`
reference extension follow the source boundary. Direct addresses and stored member-function pointers
may be called with native `.*` or `->*` on an exact object or pointer through
the same fixed-arity method boundary; exact full-expression temporary objects
are materialized and destroyed after the call, and `&&`-qualified methods
require that temporary receiver. This includes const methods, admitted lvalue-
or rvalue-reference parameters, and admitted lvalue- or rvalue-reference
results, including complete fixed arrays and source-owned records.
Complete source-owned record value results construct directly in the caller's
destination and retain their normal full-expression destruction. Exact
`std::mem_fn` wrappers built from those direct named addresses or authenticated
local member-pointer chains may be called
immediately, directly or as the callable of `std::invoke`, through the same
boundary. A directly initialized local wrapper for a method or data field may also be
retained across statements and called directly or through `std::invoke`; exact
same-type local copy/move-initializer chains are authenticated and erased with
their carriers, including chains through the admitted `<utility>` reference
adapters. Those adapters may also wrap an authenticated local wrapper at its
final direct call or `std::invoke` use. Reassigned or null member pointers,
`mem_fn` copies or moves from
parameters, reassigned `mem_fn` objects,
base adjustments, volatile receivers, references to incomplete or runtime-bound
arrays, nontrivial source-record value parameters,
and other unsupported reference or variadic function signatures stay
outside this boundary. Object-pointer values and exact scalar or object-pointer
lvalue- or rvalue-reference parameters and lvalue- or rvalue-reference results are preserved. The
frontend uses the pinned embedded libc++/resource VFS and exposes resolved type
aliases plus integral/enum constant results. It records all consumed header
hashes: 101 for the `<type_traits>` closure, nine for standalone `<cstdint>`,
16 for `<limits>`, 15 for `<ratio>`, 29 for `<cstddef>`, 87 for `<utility>`,
98 for `<tuple>`,
217 for `<array>`, 10 for `<initializer_list>`, 136 for `<optional>`, 171 for
`<iterator>`, 354 for `<algorithm>`, 126 for `<numeric>`, 37 for `<new>`,
267 for `<memory>` and 346 for `<functional>`.
The composite
array-plus-tuple-plus-utility fixture consumes a 231-file union closure; adding
`<optional>` produces a 253-file union closure on all eight targets.
Numeric limits fold the documented
zero-argument integer and IEEE floating queries to literals. Cstddef aliases,
layout queries and direct `std::byte` operations lower to existing scalar IR.
Utility directly lowers scalar `move`, `forward`, `move_if_noexcept`, `as_const`,
`exchange` and `swap`; scalar or recursively composite `pair` construction,
assignment, swaps, `make_pair` and `get`; recursive standard-composite pair
comparisons; and
`tuple_size`, `tuple_element` and integer-sequence size queries.
Reference-valued pairs admit exact compatible direct construction, same-type
copy/move construction, public field access and index- or unique-type `get`.
Same-type assignment writes through the destination bindings without changing
either pair's bindings. All six comparisons read referent values, including
mixed reference/value pairs. Member and free swap exchange referent values
without rebinding. Compatible converting construction binds value or reference
pair fields, while heterogeneous assignment converts values before writing
through destination bindings. `make_pair` unwraps authenticated `ref`/`cref`
arguments into reference fields, including mixed reference/value results with
public field access. Mixed pairs also admit exact compatible direct and
same-type copy/move construction, index- or unique-type `get`, and same-type or
compatible heterogeneous assignment. All six comparisons recurse through
reference and value fields. Member and free swap exchange values without
changing reference bindings. Compatible converting construction binds reference
fields and initializes value fields from value, reference or mixed pairs.
Ordinary value pairs accept those sources for heterogeneous construction and
assignment, copying or converting each field into independent storage.
Assignment writes `first` before `second` and returns the destination by
reference; recursive composite fields still require the exact unqualified type.
Authenticated `std::reference_wrapper` values are admitted as ordinary or
mixed pair value fields, including nested pairs. Construction and copying
preserve the wrapped binding; assignment copies that binding without
changing the referred-to object. Tuple member/free swap authenticates each
indexed SDK leaf projection and its selected element swap, including nested
ordinary pairs with wrapper values. Reference tuple elements exchange referent
values without rebinding; user ADL swaps and source specializations are rejected.
Pair and array member/free swap require exact SDK element swaps, trivial
selected copy/move operations and authenticated nested dispatch. Array proofs
include the range loop and iterator adapters; empty arrays perform no element
swap. Wrapper pairs with array siblings are supported. User ADL swaps and source
specializations remain rejected. Optional swaps authenticate both the selected
value swap and each one-engaged construction/reset path, including nested
wrapper pairs and tuples. Const
wrapper fields can be copied but not assigned, volatile wrapper fields
remain excluded, and comparisons involving wrapper-valued leaves remain
rejected.
Tuple
directly lowers authenticated empty and nonempty tuples of scalars, reference
wrappers
or recursively composite values, compatible scalar or exact composite
per-element construction and assignment from tuples or pairs, factories,
swaps, recursive same-length
heterogeneous lexicographic comparisons and index or unique-type access. Exact
`tie` and `forward_as_tuple` construct authenticated reference tuples; their
`get` and `apply` operations recover the referents, preserve value categories
and retain writes to the bound objects. `make_tuple` unwraps authenticated
`ref`/`cref` arguments, including mixed reference/value results with index- or
unique-type `get`. Mixed tuples also admit exact compatible direct and same-type
copy/move construction plus same-type or compatible heterogeneous assignment
from tuples and pairs. All six comparisons recurse through reference and value
elements. Member and free swap exchange values without changing reference
bindings. Compatible converting construction from tuples or pairs binds
reference elements and initializes value elements. `apply` recovers reference
elements and passes value elements through the existing checked callable rules.
Ordinary value tuples may copy or convert referent values from all-reference or
mixed tuple/pair sources during construction and assignment. Assignment stores
independent values in element order and returns the destination by reference.
Exact direct element construction and
same-type copy/move construction preserve those bindings; same-type assignment
writes values through the destination bindings without rebinding them.
Compatible converting construction binds source tuple elements, while
heterogeneous scalar assignment converts values before writing through.
Recursive comparisons operate on referent values and retain the value tuple
short-circuit and lexicographic rules. Conversions from admitted value pairs
either bind their fields during construction or assign through destination
references. Member and free swap exchange referent values while retaining each
tuple's bindings. Exact `apply` accepts authenticated tuples, value/reference/
mixed pairs and arrays whose elements satisfy the existing composite value and
callback rules. It lowers named functions, stored function pointers, exact
source-owned function objects, authenticated standard scalar function objects,
exact `reference_wrapper` forms, direct or stored source member pointers, and
temporary or stored `mem_fn` wrappers through the same checked callable paths.
By-value scalar conversions and exact trivial source-record copies retain
independent parameter objects; results may be scalar, void, references, or
complete source records constructed directly in the caller destination.
By-value SDK callback parameters and results remain excluded. Mutable and
const lvalues, rvalues and materialized source temporaries preserve element
cv/ref categories; pair reference fields keep their declared bindings. Const
lvalue-reference callback parameters also bind exact-type xvalue elements
without adding a copy or extending temporary lifetimes. Mutable lvalue-reference
parameters retain their lvalue requirement. Empty arrays invoke a nullary
callback after evaluating their source expression,
and nested array elements may bind admitted array-reference parameters.
Each callable and source expression is evaluated once. Exact SDK declaration,
index, type and forwarding checks authenticate every selected `get` before the
operation lowers to one callback call, member projection or scalar operation.
User-defined tuple-like protocols and SDK function or record specializations
remain outside that proof. Exact
`tuple_cat` handles zero arguments and authenticated scalar or recursively
composite tuple, pair or array sources, including all-reference and mixed
reference/value tuple or pair elements,
evaluating every source once before copying elements into the exact
concatenated tuple while preserving reference bindings. Array directly lowers
nonempty fixed scalar, trivial-record
and nested-array storage, iterators, element access, fill, swap, scalar and
recursive nested-array comparisons and tuple access. Authenticated array
objects, including zero-length and nested arrays, also bind reference tuple and
pair elements through direct construction, `tie`, `forward_as_tuple` and
`ref`/`cref` factory unwrapping. Access, assignment, swap and `tuple_cat` preserve
bindings and the existing array value semantics. Exact lvalue/rvalue array
reference parameters and results compose with checked `apply`, `invoke`, direct
reference-wrapper calls, native member-pointer calls and `mem_fn`; callable
definitions retain their source-owned checks. By-value SDK callback parameters
and results, volatile or nontrivial array elements and user array specializations
remain rejected.
Array layout also supports type-query source proof, including classifications,
reference binding and `decltype` of source-owned calls returning array
references and exact admitted `std::get` calls. Local aggregate initializers
and temporaries use authenticated implicit trivial array destruction with
recursive element-source checks, including zero extents and checked potentially
throwing trivial element destructors. Constant `get` results can supply array
dimensions; query operands remain unevaluated. Written template arguments and
caller expressions remain checked, while other SDK callees, selected
construction, nothrow-destruction roots and enum-source proofs remain separate.
Initializer-list objects retain libc++'s authenticated pointer-and-size view.
Braced backing arrays use the existing automatic, full-expression and static
lifetime machinery; default, copy/move and assignment operations, member and
free range access, nested lists and nontrivial source-record elements lower
without a libc++ runtime call.
Optional authenticates libc++'s private base, value union, engagement flag and
target layout before exposing value-plus-flag protocol records. Scalars,
source-owned trivial standard-layout records, arrays, pairs and tuples support
default, value, in-place, copy/move and converting
construction, assignment, emplacement, fallback, factories, engagement and
access, reset and swap without libc++. Swap proves the selected SDK engagement
tests and value projections, element swap and both transfer paths through exact
forwarding, placement construction and reset. The selected construction must be
trivial; user ADL swaps and source specializations are rejected. Both operands
are evaluated once, including empty and self swaps. Composite conversion
requires exact unqualified element types, and mutation requires recursive assignability;
value comparisons recurse through authenticated arrays, pairs and tuples to
the scalar leaf boundary, while `nullopt` comparisons work for every admitted
element.
Iterator exposes the pinned public header and pointer `iterator_traits` metadata
through a platform-free 171-file closure, and directly lowers bounded pointer,
array-range and reverse-iterator operations. Stream iterators remain disabled
until their C runtime character-state ABI is available on every target.
Authenticated string/vector forward wrappers also admit trivial copy/move
assignment, including assignment from a returned algorithm iterator, plus
direct `advance`, `distance`, `next` and `prev` navigation.
Algorithm directly lowers scalar-pointer `copy`, `move`, `copy_backward`,
`move_backward`, `copy_n`, `fill`, `fill_n`, `iter_swap`, `swap_ranges`,
`reverse`, `reverse_copy`, `rotate` and `rotate_copy` calls from its
platform-free 369-file closure; copy and move outputs accept checked direct
scalar conversions. The copy-producing equality and predicate algorithms below
accept the same conversions for their writable outputs. Built-in equality elements admit
`find`, `count`, three- or four-iterator `equal`, `adjacent_find`, `remove`,
`remove_copy`, `replace`, `replace_copy`, `unique`, `unique_copy`, `search`,
`find_end`, `find_first_of`, `search_n`, and three- or four-iterator `mismatch`.
Default three- or four-iterator `is_permutation` accepts those built-in equality
elements and exact source records with a uniquely selected Boolean equality
operator over independent authenticated wrapped or raw-pointer ranges.
Built-in arithmetic elements admit default-order
`min`, `max`, `clamp`, `minmax`, `min_element`, `max_element`,
`minmax_element`, `lower_bound`, `upper_bound`, `equal_range`, `binary_search`,
`is_sorted`, `is_sorted_until`, `is_heap`, `is_heap_until`, `make_heap`,
`push_heap`, `pop_heap`, `sort_heap`, `sort`, `stable_sort`, `inplace_merge`,
`partial_sort`,
`partial_sort_copy`, `nth_element`, `next_permutation`, `prev_permutation`,
`lexicographical_compare`, `includes`, `merge` and the four `set_*` range
algorithms. Default `lexicographical_compare` and `includes` additionally
accept independent wrapped or raw-pointer ranges of exact same-type trivial
source records with a uniquely selected Boolean `operator<`.
Their scalar comparator forms also accept independent wrapped or raw-pointer
ranges with a checked Boolean function pointer or standard comparison object,
proving both comparison directions.
Their checked comparator forms also accept two exact same-type trivial
source-record ranges through `bool(const Record&, const Record&)`, including
mixed wrapped and raw iterators. Both comparison directions use original
element references without requiring `operator<`.
Default `is_sorted` and `is_sorted_until` accept the same source-record
comparison in raw or authenticated wrapped ranges.
Their checked comparator forms also accept exact trivial source-record ranges
through `bool(const Record&, const Record&)`, preserving original adjacent
addresses and the iterator result of `is_sorted_until`.
Default `min_element`, `max_element` and `minmax_element` return original raw
or wrapped positions for those records, retaining the standard tie rules.
Their checked comparator forms also accept exact trivial source-record raw or
wrapped ranges through a `bool(const Record&, const Record&)` function pointer.
Comparisons retain original element addresses and need no `operator<`.
`max_element` additionally accepts a source-owned, trivially copyable
comparison object with one checked Boolean `operator()`. Exact record
references retain element addresses; scalar parameters may be by value or
exact const references. Empty and one-element ranges do not call the object.
Default `is_heap` and `is_heap_until` accept the same source-record ordering
for raw or authenticated wrapped ranges.
Their checked comparator forms likewise accept exact trivial source-record
ranges through that const-reference function pointer, preserving original
parent and child addresses and the first violation position.
Default direct `min`, `max`, `clamp` and `minmax` accept exact trivial source
records with that selected ordering and preserve returned reference identity.
Their checked comparator forms also accept those records through an exact
`bool(const Record&, const Record&)` function pointer without `operator<`,
preserving comparison referents and returned references.
They also accept a source-owned, trivially copyable comparison object with an
exact `bool operator()(const Record&, const Record&)` method. The selected
libc++ call is authenticated, the object argument is evaluated once, and the
method observes the original record references.
The scalar-reference forms also accept a source-owned, trivially copyable
comparison object with a Boolean `operator()` taking two directly convertible
scalar values by value or exact `const T&` parameters. It observes one
by-value copy of the object per call to the algorithm, and reference
parameters see the original bound scalar objects.
Default `lower_bound`, `upper_bound`, `binary_search` and `equal_range` accept
raw or authenticated wrapped source-record ranges and exact const record keys.
Their checked comparator forms accept `bool(const Record&, const Record&)`
function pointers without requiring `operator<`, preserving original element
and key addresses and the raw or wrapped bound results.
Default `sort` also accepts writable raw or authenticated wrapped ranges of
those trivial source records, using the selected Boolean `operator<` on current
range elements.
Its checked comparator form also accepts those writable records through a
`bool(const Record&, const Record&)` function pointer without `operator<`.
`iter_swap`, `swap_ranges` and `reverse` accept writable raw or authenticated
wrapped ranges of exact source-owned trivial records, preserving complete
record values and the returned output iterator form.
`rotate` admits those record ranges and writable wrapped scalar ranges,
returning the original iterator type.
Default `make_heap`, `push_heap`, `pop_heap` and `sort_heap` also admit writable
raw or authenticated wrapped trivial source-record ranges with a unique
Boolean `operator<`; wrapped scalar ranges retain their comparator forms.
Their checked comparator forms likewise accept exact trivial source-record
ranges through that const-reference function pointer, moving complete values.
Default `next_permutation` and `prev_permutation` admit the same writable
source-record ranges, while wrapped scalar ranges retain their comparator
forms.
Their checked comparator forms also accept those records through
`bool(const Record&, const Record&)`, preserving complete values on swaps.
Default `partial_sort` admits writable raw or authenticated wrapped trivial
source-record ranges; wrapped scalar ranges retain their comparator forms.
Its checked comparator form also accepts those records through
`bool(const Record&, const Record&)` and moves complete values.
Default `partial_sort_copy` admits raw or authenticated wrapped input and
writable output ranges of the same trivial source-record type, returning the
original output iterator form.
Its checked comparator form also accepts those same-type record ranges through
`bool(const Record&, const Record&)`, preserving complete output values and
the returned raw or wrapped iterator.
Its default scalar form and the default scalar `merge` and four ordered
`set_*` forms also accept independent authenticated wrapped or raw input and
output ranges under their checked comparison and conversion rules.
Their scalar comparator forms admit those ranges with checked Boolean function
pointers or standard comparison objects, proving every comparison direction.
Default `nth_element` admits the same record ranges through its retained pivot
and three-way partition, while wrapped scalars retain comparator forms.
Its checked comparator form also accepts those records through that exact
const-reference function pointer.
Default `stable_sort` and `inplace_merge` admit writable raw or authenticated
wrapped trivial source-record ranges with that selected ordering, preserving
the order of equivalent elements. Wrapped scalars retain their default and
checked comparator forms. Their checked comparator forms also accept those
records through that exact const-reference function pointer and retain stable
ordering.
Default `merge` and the four ordered `set_*` algorithms accept independent raw
or authenticated wrapped ranges of the same trivial source-record type and a
writable output of that type, preserving complete values and the output
iterator form.
Their checked comparator forms also accept those ranges through
`bool(const Record&, const Record&)`. Both input comparison directions use
original element references, and complete records are written to same-type
raw or wrapped outputs without requiring `operator<`.
Checked unary boolean function-pointer predicates with directly
convertible by-value scalar parameters admit `find_if`, `find_if_not`,
`count_if`, `all_of`, `any_of`, `none_of`,
`copy_if`, `remove_if`, `remove_copy_if`, `replace_if`, `replace_copy_if`,
`is_partitioned`, `partition`, `stable_partition`, `partition_copy` and
`partition_point`.
For the six predicate queries, authenticated mutable or const wrapped scalar
iterators are also accepted, and find results retain their iterator type.
Those six queries also accept raw or authenticated wrapped ranges of exact
trivial source records with a checked `bool(const Record&)` function pointer.
The callback receives each original element by reference.
The same record predicate boundary admits `copy_if`, `remove_copy_if`,
`remove_if`, `replace_if` and `replace_copy_if`. Copy forms require a writable
output of the same record type and retain mixed raw and wrapped iterator forms.
`for_each` and `for_each_n` also admit exact trivial source records through a
checked `Record&` callback on mutable ranges or `const Record&` callback on
mutable or const ranges. The callback acts on original elements, and the
counted form retains its raw or wrapped iterator result.
`is_partitioned`, `partition_point`, `partition`, `stable_partition` and
`partition_copy` accept the same trivial record ranges with a checked
`bool(const Record&)` predicate. Copying requires writable same-type outputs,
including independent wrapped or raw result positions.
Unary and binary `transform` also accept independently typed exact trivial
source records. Checked function-pointer arguments take each input as its own
`const Record&` and return the exact writable output record by value. Inputs
and output may independently be raw or wrapped; the callback sees the original
input elements and the result retains the output iterator form.
Record inputs may also project to a writable scalar output when the callback's
scalar result directly converts to that element. Both overloads retain the
const-reference input and output iterator rules.
The binary form may combine one checked scalar input and one exact trivial
record input in either order, returning a checked scalar or exact record for
a writable raw or wrapped output.
The five predicate copy, remove and replace forms also accept authenticated
wrapped scalar ranges under their existing writable-output and conversion
checks, including mixed raw and wrapped copy inputs or outputs.
`is_partitioned`, `partition_point`, `partition`, `stable_partition` and
`partition_copy` accept the same checked wrapped scalar ranges; partition
results keep their wrapped iterator type, including both copy outputs.
`rotate_copy` accepts raw or authenticated wrapped scalar inputs and writable
outputs independently, with checked direct output conversion and a result in
the output iterator type.
It also copies complete values of the same exact source-owned trivial record
type through raw or wrapped ranges, including const inputs and mixed outputs.
`reverse_copy` shares that record boundary, returning the original output
iterator type after copying complete records in reverse order.
The exact `copy`, three-iterator `move`, `copy_backward` and `move_backward`
forms also transfer complete values of that trivial record type across raw or
wrapped ranges, with a matching writable output and retained iterator result.
`copy_n` accepts the same record ranges and returns the unchanged output for
zero count.
`fill` and `fill_n` accept authenticated writable wrapped ranges of exact
trivial source records and an exact const record value; `fill_n` returns its
wrapped output iterator after the admitted count.
Default `unique` and `unique_copy` also accept raw or wrapped ranges of exact
trivial source records with one selected Boolean `operator==`. `unique` requires
a writable input; `unique_copy` requires a writable output of the same record
type and accepts mixed raw and wrapped iterators.
Checked predicate `adjacent_find`, `unique` and `unique_copy` also accept those
exact record ranges through a `bool(const Record&, const Record&)` function
pointer without requiring `operator==`. Comparisons retain original element
addresses; `unique` needs writable input and `unique_copy` needs writable
same-type output.
Value-based `remove`, `remove_copy`, `replace` and `replace_copy` accept those
record ranges with an exact const record value, including mixed raw and wrapped
copy destinations. Comparison and replacement value references remain live
through the loop.
Checked function-pointer binary predicates with directly
convertible by-value scalar parameters additionally admit `adjacent_find`, three-
and four-iterator `equal`, three- and four-iterator `mismatch`, and three- and
four-iterator `is_permutation`, plus `unique`, `unique_copy`, `search`,
`find_end`, `find_first_of` and `search_n`. Predicate `adjacent_find` also
accepts authenticated mutable or const wrapped scalar ranges and returns a
wrapped matching position.
Three- and four-iterator `equal` and `mismatch` also accept two independently
typed exact trivial source-record ranges with a checked function pointer taking
their original elements as matching `const Record&` parameters and returning
`bool`. Either range may use raw or authenticated wrapped iterators. Bounded
`equal` checks the two lengths before invoking its predicate.
Default and checked function-pointer `unique` and `unique_copy` also accept
authenticated wrapped scalar ranges, including writable wrapped outputs and
mixed raw-pointer inputs or outputs for `unique_copy`.
`remove`, `remove_copy`, `replace` and `replace_copy` accept those wrapped
ranges with their checked equality, writable destination and value conversion
rules; copying forms also admit mixed raw and wrapped ranges.
Checked predicate `search`, `find_end`, `find_first_of`, `search_n` and both
`is_permutation` forms accept authenticated wrapped scalar ranges with the
existing by-value callback conversion rules.
Predicate `search_n` also accepts exact trivial source-record raw or wrapped
ranges and an exact const record value through a checked
`bool(const Record&, const Record&)` function pointer. The value reference stays
live across comparisons, and zero count invokes no callback.
Both predicate forms of `is_permutation` also accept two exact same-type trivial
source-record ranges with a checked `bool(const Record&, const Record&)`
function pointer. The callback receives original elements, including comparisons
within the first range; raw and authenticated wrapped iterators may be mixed.
Predicate `search`, `find_end` and `find_first_of` also accept independently
typed exact trivial source-record ranges with a checked
`bool(const Left&, const Right&)` function pointer. Raw and authenticated
wrapped iterators may be mixed, and callbacks receive the original elements.
Checked function-pointer comparators with directly convertible by-value scalar
parameters additionally admit `min_element`, `max_element`,
`lower_bound`, `upper_bound`, `equal_range`, `binary_search`, `is_sorted` and
`is_sorted_until`, plus `min`, `max`, `clamp`, `minmax`, `minmax_element`,
`lexicographical_compare`, `includes`, `merge`, `set_union`,
`set_intersection`, `set_difference`, `set_symmetric_difference`, `is_heap`,
`is_heap_until`, `make_heap`, `push_heap`, `pop_heap`, `sort_heap`, `sort`,
`stable_sort`, `inplace_merge`, `partial_sort`, `partial_sort_copy`, `nth_element`,
`next_permutation` and `prev_permutation`.
Other checked callbacks with directly convertible by-value scalar parameters
admit `for_each`, `for_each_n`, unary and binary `transform`; transform results
and zero-parameter generator results may directly convert to the output scalar.
Function-pointer `for_each` and `for_each_n` also accept authenticated mutable
or const wrapped scalar inputs, with `for_each_n` returning the wrapped position.
Function-pointer unary and binary `transform` accept independent raw or
authenticated wrapped scalar inputs and writable outputs, returning the output
iterator type after checked callback and destination conversions.
Those scalar input forms also accept writable exact trivial source-record
outputs when the checked callback returns that output record by value; the
input parameters keep their direct scalar conversion rules.
Function-pointer `generate` and `generate_n` also accept writable wrapped
scalar outputs, with `generate_n` returning the wrapped output position.
They additionally accept writable raw or wrapped exact trivial source-record
ranges when a checked nullary function pointer returns that record by value.
Each result is stored as a complete record; `generate_n` keeps the original
output iterator form.
Predicate mutation algorithms require writable destinations; ordered output
algorithms accept directly convertible writable scalar destinations,
and `copy_n`, `fill_n` and `search_n` accept integral or non-scoped enum counts
through 64 bits after integer promotion.
`mismatch`, `equal_range` and `minmax_element` directly construct authenticated
scalar-pointer pair results; `minmax` uses its exact authenticated pair of two
const references. Direct `shuffle` and `sample` also lower admitted raw-pointer
or authenticated wrapped scalar ranges with a checked source-owned generator by
reference. The selected
`uniform_int_distribution` closure is pinned, preserving rejection draws,
generator state and sample order without a libc++ runtime call.
Numeric contributes an exact, platform-free 126-file C++17 header closure.
Matching non-promoted integer, `float` and `double` pointer ranges admit the
default `iota`, `accumulate`, `inner_product`, `partial_sum` and
`adjacent_difference` overloads, C++17 reductions and scans, transformed scans,
and exact same-type by-value function-pointer operations. Integer `gcd` and
`lcm` accept the documented built-in integer combinations.
Default `iota` also accepts authenticated writable wrapped scalar ranges,
including vector iterators, with its checked initial-value conversion.
Default and checked function-pointer `accumulate` also accept authenticated
mutable or const wrapped arithmetic ranges with their existing accumulator
conversion and callback rules.
Four-argument `accumulate` additionally accepts exact trivial source-record
raw or wrapped inputs with an arithmetic accumulator and a checked callback
taking the accumulator by value and each original record as `const Record&`.
The scalar callback result converts to the accumulator after every element.
Default and checked function-pointer `inner_product` accept independent raw or
authenticated wrapped arithmetic inputs; directly value-initialized standard
function-object pairs accept the same ranges.
The six-argument function-pointer form also accepts an exact trivial source
record in either raw or wrapped input range with arithmetic values in the
other. Its transform callback takes the original record as `const Record&`
and the directly convertible scalar by value in the matching parameter order,
then returns a scalar term for a checked arithmetic reduction callback.
Both inputs may instead hold independently typed exact trivial records; the
transform callback takes each original element as its matching `const Record&`
and returns a scalar term for the same reduction boundary.
Default, initialized and checked function-pointer `reduce` also accept
authenticated mutable or const wrapped arithmetic ranges.
Default two-range and checked function-pointer unary or two-range
`transform_reduce` accept raw or authenticated wrapped arithmetic inputs;
directly value-initialized standard function-object pairs accept the same
ranges.
The checked function-pointer forms additionally accept exact trivial source
records in raw or wrapped ranges. A unary transform takes the original element
as `const Record&`; a two-range transform takes each record as its matching
`const Record&` and each arithmetic element by value. Its scalar result feeds
the checked arithmetic reduction callback.
Default and checked function-pointer `partial_sum` and
`adjacent_difference` accept authenticated wrapped arithmetic inputs and
writable wrapped outputs, including mixed raw-pointer ranges, and return the
matching output iterator.
Default and checked function-pointer inclusive, exclusive and transformed scans
accept the same authenticated wrapped input and writable output ranges under
their existing initial-value and callback conversion rules.
The initialized function-pointer `transform_inclusive_scan` and
`transform_exclusive_scan` forms additionally accept exact trivial source-record
raw or wrapped inputs. Their unary callback takes each original element as
`const Record&` and produces a scalar term for the checked arithmetic
accumulator and writable scalar output.
Output ranges must be writable; other callable objects and element types
and custom iterators remain rejected. New contributes an exact, platform-free
37-file C++17 header closure. The standard allocation tag types and
interference-size values remain compile-time or scalar metadata, and exact
`std::launder` calls on admitted
non-volatile object pointers lower to their retained pointer value without a
libc++ call. Exact standard placement new and constant-bound placement new[]
reuse a captured storage pointer without a runtime call. Runtime tag objects,
direct allocation-function calls and allocation-failure handlers remain excluded.
Memory contributes an exact, platform-free
267-file C++17 header closure. Raw-pointer `pointer_traits` aliases resolve
through pinned libc++. Exact `std::allocator<T>` and
`std::allocator_traits<std::allocator<T>>` type identities, nested aliases,
rebinds and trait constants also resolve as compile-time metadata, including
exact `uses_allocator<T, std::allocator<U>>` identities, inherited aliases and
values. `std::addressof` and raw-pointer
`pointer_traits::pointer_to` return checked object addresses without a libc++
runtime call. Exact single-object `std::default_delete<T>` and unbounded-array
`std::default_delete<T[]>`, including complete bounded inner extents, use an
authenticated one-byte stateless carrier. Default, copy/move and admitted
cv-converting construction lower
directly. Calls evaluate the deleter and pointer once, destroy the complete
object or reverse array elements, and invoke the checked scalar or array class/global delete selected by Clang. Exact
single-object
`std::unique_ptr<T, std::default_delete<T>>` and unbounded-array
`std::unique_ptr<T[], std::default_delete<T[]>>`, including multidimensional
owners, use authenticated pointer-sized carriers. The same owner forms admit a
source-owned by-value deleter that is an empty, standard-layout, trivial
one-byte record with trivial special members and exactly one source-defined
`void operator()(pointer) noexcept`, optionally `const` and without a ref
qualifier. Their default, null, raw-pointer, matching raw-pointer/null plus a
deleter argument bound to the exact deleter lvalue or rvalue parameter, move,
assignment, observation, release, reset, swap, same-category comparisons across
deleter specializations with qualification-compatible raw pointers, and
destruction operations lower directly; scalar owners
expose dereference and arrow while array owners expose subscript.
Default deleters use Clang's selected checked scalar or array class/global
delete. An admitted custom deleter is invoked for a non-null pointer without
requiring a global delete definition. Exact single-object
`std::make_unique<T>(args...)`
and unbounded-array
`std::make_unique<T[]>(count)`, where `T` may have complete bounded inner
extents, for integer constant expressions from zero through 65536 lower through
the selected checked global/source-defined class-specific scalar new/delete or
array new[]/delete[]. They value-initialize
flattened scalars or call supported source-owned
non-template `noexcept` record constructors, including authenticated
source-owned trailing defaults. Defaults are evaluated once per constructed
object, including independently for every flattened array element. The
factories return the authenticated pointer-sized owner without a libc++ call.
Exact `destroy_at`, `destroy` and
`destroy_n` calls on scalar object pointers retain
argument evaluation and counted iterator results without emitting a trivial
destructor call. The ten C++17 `uninitialized_*` copy, move, fill, default and
value construction forms directly initialize scalar pointer ranges and retain
their exact iterator results. Exact allocator objects, member destruction and
allocator-traits destruction, construction, `max_size` and copy selection use
the same checked stateless-record and lifetime paths. Construction accepts
writable scalars and complete source-owned records whose selected constructor
is supported, source-owned and `noexcept`, including authenticated source-owned
trailing defaults evaluated once after supplied arguments. Allocation and
deallocation accept exact member and traits forwarding, including hints and
runtime deallocation counts, when an admitted global new/delete path exists and the allocation count is proven within `max_size`. One-byte elements
accept runtime counts, preserving the complete expression's conversions,
side effects and temporary cleanup. Larger elements accept integer constant
expressions or a final implicit conversion from an unsigned builtin type,
including `bool`, whose entire range fits the target's `max_size`. Allocation-failure handling, unproven allocation counts, over-aligned elements,
runtime-count array factories, stateful,
reference, non-raw-pointer, nontrivial, overloaded, ref-qualified or throwing
custom deleters, other smart pointers and ownership factories are not yet
admitted. The driver authenticates each
closure before emitting output. Standard-library objects and operations beyond
these documented surfaces, other standard headers and full C++/STL remain
unfinished.

Direct `std::shuffle` and C++17 `std::sample` also accept admitted scalar-pointer
ranges with a checked source-owned unsigned generator bound by reference.
Authenticated `std::__wrap_iter<T*>` inputs and outputs, including admitted
vector/string iterators and mixed raw/wrapped sample ranges, preserve the
original output iterator type.
Their direct lowering preserves libc++'s rejection sampling, independent-bit
assembly, generator state and stable sample order. Sample uses a nonnegative
built-in integral count of at most 64 bits and returns the final output pointer.
The selected SDK declaration closure rejects source replacements. The exact
generator and range boundary is documented in the
[algorithm contract](../docs/cpp-core-v2.md#algorithm-header-from-algorithm);
other `<random>` facilities, custom iterators and record elements remain outside
this subset.

Authenticated `linear_congruential_engine` and `mersenne_twister_engine`
instances are also admitted, including `minstd_rand0`, `minstd_rand`,
`default_random_engine`, `mt19937` and `mt19937_64`. Their default/scalar-seed
construction, copies, assignment, `seed`, generation, `discard`, `min`/`max`,
equality and use with the admitted random algorithms retain the pinned SDK
state and target layout. The [engine contract](../docs/cpp-core-v2.md#standard-random-engines-from-random)
records the checked bounds and remaining random-library interfaces. The other
deterministic engines/adaptors and `seed_seq` are admitted under that contract,
including nested bases, `base()`, integral list/raw/wrapped seed construction,
32/64-bit unsigned generation and engine seeding. Selected vector growth,
allocation and cleanup remain observable; the pinned zero-modulus LCG
seed-sequence division-by-zero path is rejected.

`uniform_int_distribution<T>` and `param_type` also retain the pinned layout,
constructors/defaults, parameters, state-free property/reset operations and both
generator call forms for admitted integers through 64 bits. Checked SDK engines
and the source-generator contract above preserve negative/narrow/full-width
range arithmetic, rejection draws and generator state. See the
[integer distribution contract](../docs/cpp-core-v2.md#uniform-integer-distributions-from-random).

Float/double `uniform_real_distribution` and `generate_canonical` also preserve
selected SDK formulas, draw order, bounds/parameters and source guards. The
existing core-v2 IEEE execution contract disables fast math and FMA contraction;
native references use the same flags. Bits zero and real singleton intervals
still consume generator draws. See the
[real distribution contract](../docs/cpp-core-v2.md#uniform-real-distributions-and-canonical-generation).

`bernoulli_distribution` and `param_type` preserve probability/parameter state,
constructors, property/reset/equality operations and both generator call forms.
Their selected double canonical draw sequence is retained even for probability
0 or 1, under the existing strict FP and source-proof contracts. See the
[Bernoulli contract](../docs/cpp-core-v2.md#bernoulli-distributions-from-random).

`discrete_distribution<IntType>` and `param_type` preserve normalized threshold
storage, raw/wrapped arithmetic ranges and lists, independent copies, moves,
parameter/probability results, assignment, equality, reset and generator calls.
Canonical draws and probability arithmetic retain pinned SDK order; internal and
returned vectors retain checked allocation and cleanup. See the
[discrete contract](../docs/cpp-core-v2.md#discrete-distributions-from-random).
The four-argument weight constructor also accepts unary function pointers with
core arithmetic value/reference signatures, retaining source bodies, bindings, midpoint and
conversion order, zero/one-count callback suppression and SDK vector growth.
Referenced double weights are read at element construction after allocation and
before old-buffer release, preserving source allocator side effects.
Ordinary source function objects retain checked source copies and
destruction, selected operators, actual parameter identities and mutable state.
The nested parameter ends after weight initialization; the outer/direct parameter
survives to the caller complete-expression end, even for zero/one counts. Checked concrete primary, partial and full class-template specializations also retain ordinary source checks when the selected non-template call operator is defined in the class body.

The adapter first inspects every owned declaration and expression, including
unused functions and unreachable statements. It accepts supported 32-bit integer,
bool and trivial aggregate operations, free functions, resolved namespaces and
overloads, scalar C exports, compile-time const globals, scoped locals, `if`,
`while`, `do`, `for`, `break`, `continue`, short circuit, comma and conditional
value expressions. The full [support matrix](../docs/support-matrix.md)
defines rejected forms. Direct storage/field assignment lvalues are supported;
conditional or other computed assignment lvalues are rejected.

All reads required by Clang's lvalue-to-rvalue conversions become explicit
snapshots; copying an aggregate snapshots that value. A member read snapshots
only that field, including nested paths through conditional/comma aggregate
glvalues such as `(condition ? a : b).inner.x`. Discarded nonvolatile glvalues such as `x;`, `p;`, `p.field;`,
`(x, 0);` and discarded glvalue conditionals do not load uninitialized storage.
Assignments evaluate RHS first, shifts evaluate LHS first, argument evaluations
complete left to right, and short-circuit/conditional work stays in its branch.
The shared emitter supplies the pinned signed-shift and conversion rules.

The core option allowlist independently accepts only `-std=c++17` and attached
`-DNAME[=VALUE]`/`-UNAME` options. Reserved macro redefinitions and unsupported
options fail with `TR0004`. Raw directives reject even inactive includes,
pragmas, numeric line markers and `#line` before source parsing; time/path and
environment-sensitive macros are rejected. Source hashes include macro
definitions written in the file, and the driver records admitted command-line
macro inputs. There is no source execution, compiler-command execution,
library-name guessing or fallback source emission.

## Owned projects

`cpp-project-v1` retains the scalar/aggregate language subset and accepts the
per-unit request fields in the [project protocol](../docs/p3-project-emission.md).
The internal frontend analyzes one selected unit at a time. NeverC's shared project merger
owns definition closure, ODR rejection and combined `translated.nc`/`translated.h`
emission; this adapter never concatenates original source units.

The independent project option allowlist accepts C++17, `-O0`/`-O2`, attached
`-D`/`-U`, and `-I`/`-iquote`/`-isystem` with existing directories whose real paths
are inside the declared root. It also accepts exactly `-Wall`, `-Wextra`,
`-Wpedantic`, `-Werror`, `-Wno-error` and `-Wno-unused-parameter`. Target, ABI,
packing, fast-math, forced-include and arbitrary warning options remain rejected.
Compilation directories and paths are independently validated even when a
request did not originate from NeverC's compilation-database parser.

Owned includes and include guards/`#pragma once` are supported. Every selected
owned header is checked with the same AST and preprocessing allowlist as source
files, including unused declarations and inactive unsupported directives. An
include resolving outside the root, including a symlink escape, fails `TR0203`;
missing generated headers also fail `TR0203`. Each dependency hash comes from
the exact SourceManager buffer consumed by Clang, and source locations report
the actual normalized relative source/header path. Standard-library/system
headers and runtime mappings are not boundaries in this profile.

Before constructing Clang's driver, the single-request NeverC frontend process clears
implicit compiler configuration variables, including `CPATH`,
`CPLUS_INCLUDE_PATH`, `C_INCLUDE_PATH`, `SDKROOT`, deployment-target variables,
Clang configuration-directory variables and `CCC_OVERRIDE_OPTIONS`. It still
passes `--no-default-config -nostdinc -nostdinc++`. This isolates the internal
child process without changing its caller's environment; project translation
needs no platform SDK or C++ library header search path.

Project responses export one canonical declaration for every admitted free
function/global, including declaration-only external entities, and one ODR
record per definition. Used inline functions and variables need definitions
within their own unit; internal entities cannot depend on a different unit's
definition. External declarations are resolved by the shared final merger.
Semantic IDs are full SHA-256 digests of normalized Clang identities. Internal
identities include the owning relative translation unit. Shared record fields
and inline function parameters/locals derive from their owning entity, allowing
corresponding definitions to agree across units. Internal C-language-linkage
functions have generated private names and `c_export=false`; only externally
visible scalar C exports preserve source spelling.

ODR token evidence captures token kinds/spellings from Clang's final expanded
token stream, before parser annotations. Resolved-binding evidence separately
records source declaration identities, types, operations and literal values
before lowering can discard expressions or fold values. Thus `-DVALUE=1` and
`-DVALUE=01` in a repeated inline definition have different token evidence even
though they produce the same IR. Binding the same inline body to different
TU-private functions also differs. Definitions whose token ranges cannot be
mapped to a bounded source-file range are conservatively rejected. Matching
hashes alone never bypass the shared consumer's typed body/signature checks.

## Approved binary64 math

`cpp-math-v1` adds `double` literals, storage/copy, aggregate fields, parameters,
results, unary plus/minus, comparisons and conversions with the existing
int/uint/bool types. Integer-to-double conversion is exact for all admitted
32-bit integers. Double-to-integer conversion has the source language's defined
truncation domain; it is not a saturating conversion. Double-to-bool treats NaN
as true. Floating literals and folded globals carry exactly 16 lowercase hex
digits encoding their IEEE binary64 bits, including negative zero and
subnormals. Double arithmetic, compound assignments, increment/decrement,
float and long double remain rejected, including in unused or folded code.

Only directly qualified `std::fabs(double)` and `std::floor(double)` calls cross
the library boundary. The adapter follows Clang's resolved `UsingShadowDecl`
from the approved libc++ `cmath` import to the exact C-linkage `double(double)`
declaration in the approved platform `usr/include/math.h`. It checks every
redeclaration, the original physical declaration position and header hash.
Owned redeclarations, an owned `std` namespace, bare `fabs`/`floor` calls,
namespace aliases, owned using declarations and other overloads/functions are
rejected. Matching a printed name never authorizes a mapping. Calls emit
sequenced `mapped_call` instructions; the shared emitter selects
`neverc_math_abs` or `neverc_math_floor` from its fixed mapping table.

The fixed floating contract is `cpp.math.binary64.masked.v1`: traps are masked,
FTZ/DAZ are disabled, and mapped operations support the four externally selected
rounding modes. The mapped boundary preserves errno and preexisting exception
flags, with newly raised exceptions and signaling-NaN quieting matching the
approved source operations. The separate C-client differential tests exercise
these observations. Source fenv/errno APIs and pragmas are not admitted.
The frontend explicitly passes `-fno-fast-math -ffp-contract=off` and cannot accept
source options that weaken this contract. Source `__builtin_nan`,
`__builtin_nans`, `__builtin_inf`, bit casts, unions, pointers and
`numeric_limits` constructions remain unsupported; in particular, a folded
signaling-NaN builtin argument is not silently mapped to a dynamic runtime call.
Dynamic binary64 parameters can still carry NaNs and infinities.

The immutable translation headers are embedded in NeverC as distribution
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5-r17`. The
[SDK catalog](../../../neverc/lib/Translate/Cpp/SDK/catalog.json) records all
664 approved header files and separate SDK metadata. The original header bytes
are preserved, including observable macros such as `M_PI` and `_LIBCPP_VERSION`.
The [SDK notices](../../../neverc/lib/Translate/Cpp/SDK/README.md) document
origins, redistribution terms and the minimal owned SDK configuration.
A typical `<cmath>` module consumes 203 embedded headers.

Before parsing, the frontend verifies the complete compiled file inventory and
its hashes. It maps those bytes into a reserved in-memory filesystem and
checks each consumed buffer again. Library declaration provenance requires the
controlled virtual file identity as well as its original source position and
hash. An owned copy of a header, even with identical bytes, cannot authorize a
library mapping. Owned headers still receive the complete source allowlist
check. SDK dependencies remain separate from owned project dependencies.

The SDK request contains only its distribution and catalog identities. External
SDK roots, descriptor files and SDK discovery are not part of translation.
No installed Apple SDK, libc++ headers or Clang resource directory is needed.
Only Apple macOS 15.0 x86_64/arm64 targets, including equivalent Darwin spellings
resolved by LLVM's target API, are approved for math. Embedding these headers
does not broaden that target or language contract.

## Verification

Run each protocol suite against the NeverC binary, using a new output directory:

```sh
python3 utils/translate-frontends/cpp/tests/verify.py \
  --neverc build/bin/neverc \
  --target arm64-apple-macosx15.0.0 \
  --output-dir /tmp/neverc-cpp-core-tests
python3 utils/translate-frontends/cpp/tests/project.py \
  --neverc build/bin/neverc \
  --target arm64-apple-macosx15.0.0 \
  --output-dir /tmp/neverc-cpp-project-tests
python3 utils/translate-frontends/cpp/tests/math_profile.py \
  --neverc build/bin/neverc \
  --target arm64-apple-macosx15.0.0 \
  --output-dir /tmp/neverc-cpp-math-tests
```

These scripts invoke NeverC's internal protocol entry. The core suite checks
source admission, structured diagnostics, target metadata, folded globals,
relocation, field-only reads, discarded values, response collision protection,
closed request fields and rejection of oversized, deeply nested or special-file
inputs without blocking.
The project suite adds owned-header hashes and locations, shared inline/field
identities, private C/C++ identities, declaration and ODR evidence, relocation
and hostile include environments.

The math suite checks exact binary64 bits, casts/comparisons, sequencing,
unsupported floating operations and NaN builtins, library declaration
provenance, macro compatibility, owned shadow headers, rejected external SDK
overrides, environment poisoning and disabled implicit Clang configurations.
Unreachable approved calls are still checked during source admission and leave
mapping metadata only when a lowered call remains; unreachable unsupported
calls still fail. The integrated `TranslateTests`, `TranslateProjectTests` and
math tests additionally verify merging, NC validation and runtime differentials
at O0/O2, including the documented floating-environment behavior.

## Installed-prefix verification

The installation test installs the configured `neverc`,
`neverc-resource-headers`, `neverc-std` and native runtime CMake components into
a fresh prefix. It verifies the installed SDK source snapshot and licenses.
Use the compiler and build directory from the same completed configuration:

```sh
python3 utils/translate-frontends/cpp/tests/install.py \
  --neverc build/bin/neverc \
  --neverc-build build \
  --prefix /tmp/neverc-cpp-installed-smoke
```

This includes the configured bundled Python runtime and installed relative
runtime search paths when enabled. The script installs no frontend helper or
SDK descriptor. With a minimal PATH and deliberately unavailable external
frontend/SDK environment settings, it translates core programs, C exports and
a three-unit project. On the approved macOS targets it also translates the two
math mappings. All generated programs and separate C clients compile, link and
run at both O0 and O2. The math client covers ordinary values, signed zero,
subnormals, infinity and quiet NaN.

The test recursively inspects the installed compiler's dynamic dependencies,
rejects Clang/LLVM dynamic libraries and requires non-platform dependencies to
resolve inside the new prefix. It also inspects generated objects and programs
for C++ ABI/exception imports. On macOS, generated executables must link only
libSystem; the exact C cleanup-registration symbol `___cxa_atexit` is permitted
because the default mimalloc runtime uses it.

On macOS, repeat `--deny-read PATH` for development source/build directories and
external LLVM/SDK installations to run NeverC and all generated programs under
an OS sandbox that denies reads from those paths. Read-only binary inspection
uses the test harness outside the sandbox. The script verifies the denial
before translating, compiling or running artifacts. It requires working
`sandbox-exec` support and fails rather than silently skipping isolation. The
new prefix must be outside every denied path. No developer or system directory
is renamed or modified.

`smoke-report.json` in the chosen prefix records commands, identities, artifact
sizes, runtime configuration, dependency inspection and sandbox coverage.
Reports contain machine-specific paths and belong outside version control.
A passing installation on one measured host does not establish additional
platform or cross-target support; consult the
[support matrix](../docs/support-matrix.md).

Piecewise constant float/double distributions retain the pinned three-vector
boundary/density/area layout through default/range/parameter construction,
copy-on-move ownership, assignment, vector queries, parameters, equality and
sampling. Normalization, canonical draws, interval selection, capacity reuse and
reserve-before-assignment order remain exact. Count/list callback constructors
retain checked arithmetic value/reference function pointers and source function
objects with their selected copies, default temporaries, destruction and strict
midpoint conversions. Count zero calls once; short lists suppress callbacks.
Long double and serialization require further support.

`piecewise_linear_distribution<float/double>` and `param_type` also retain default/range/count/list construction, arithmetic value/reference callbacks and admitted source function objects, copy-on-move ownership, assignment, vector queries, parameters, equality, reset and both sampling forms. Linear callbacks visit actual stored boundary lvalues, including matching reference identity; a zero count visits both endpoints, while lists with fewer than two boundaries do not call. Endpoint densities, trapezoidal normalization, cumulative areas and inverse sampling preserve the pinned operation order and strict float/double rounding. Sampling admits a typed square-root primitive only through the authenticated selected SDK math declarations and bodies; ordinary core-profile math calls remain restricted. Selected copies, default temporaries, destruction, allocation order and capacity reuse are retained. Long double and stream serialization remain separate work.

`exponential_distribution<float/double>` and `param_type` admit default, rate, parameter, copy and move construction, assignment, lambda, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. Sampling retains RealType canonical subtraction, logarithm, negation and division under the existing strict floating environment, including negative zero and the pinned canonical upper-endpoint infinity. Selected SDK log and infinity declarations, bodies and explicit redeclarations are authenticated; source replacements and ordinary core-profile math calls remain restricted. The typed logarithm uses the C23 backend and hosted CRT/libm log/logf, whose last bits can vary by platform; independent native sample comparisons use small ULP tolerances while generator state and parameter bits remain exact. These distributions need no heap storage. Long double and stream serialization remain separate work.

`extreme_value_distribution<float/double>` and `param_type` admit default, location/scale, parameter, copy and move construction, assignment, a/b, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. The two RealType parameters retain independent value storage and defaults zero/one. Sampling preserves one canonical draw and the pinned RealType expression a-b*log(-log(1-u)), including both logarithm bodies, signed infinite bounds and canonical endpoint results. Default arguments, selected SDK declarations/bodies and every explicit logarithm/infinity redeclaration are checked; source replacements and ordinary core math calls remain restricted. Hosted CRT/libm last bits may differ, so independent native samples use small absolute/relative tolerances while parameter bits, argument effects and generator states remain exact. Long double and stream serialization remain separate work.

`normal_distribution<float/double>` and `param_type` admit default, mean/deviation, parameter, copy and move construction, assignment, mean/stddev, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. Parameters default to zero/one; the distribution also retains its cached spare value and hot flag. Polar sampling preserves paired canonical draws, rejection order and the selected log/sqrt factor; a hot sample consumes no generator draws. Copy/move/assignment preserve cache state, parameter replacement keeps it, reset clears it, and cold-cache copying/comparison never reads the unused spare. Selected SDK layout, defaults, state operations, math bodies and explicit redeclarations are authenticated; source replacements and ordinary core math calls remain restricted. Independent native sampling comparisons allow small platform-math tolerances while cache/parameter behavior and generator states remain exact. Long double and stream serialization remain separate work.

`lognormal_distribution<float/double>` and `param_type` admit construction, copy/move, assignment, m/s, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. The exact nested normal state retains its cached spare across copy/move and parameter replacement; reset clears it and cold operations avoid unused spare reads. Sampling applies checked RealType exp to the selected normal result, preserving temporary parameters and generator consumption. Layouts, nested operations, selected log/sqrt/exp bodies and explicit redeclarations remain authenticated. Native comparisons allow small platform-math tolerances; parameters, cache behavior and generator states are exact. Core v2 exp emits a C23 builtin whose hosted CRT/libm import belongs to the backend. Long double and stream serialization remain separate work.

`weibull_distribution<float/double>` and `param_type` admit default, shape/scale, parameter, copy/move construction, assignment, a/b, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. Shape and scale default to one, bounds are positive zero/infinity, and sampling preserves b*pow(exponential(1),1/a), RealType arithmetic and generator consumption. The selected Weibull/exponential bodies, defaults and log/pow declarations are authenticated. Core v2 pow requires two matching float/double arguments and emits the C23 builtin; hosted CRT/libm imports remain backend-owned. Native values use small math tolerances while parameters and generator states remain exact. Long double and stream serialization remain separate work.

`gamma_distribution<float/double>` and `param_type` admit default, alpha/beta, parameter, copy/move construction, assignment, alpha/beta getters, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. Alpha and beta default to one; bounds are positive zero/infinity. Sampling preserves the pinned alpha-below/equal/above-one branches, paired draws, every rejection and selected sqrt/log/pow operations. Alpha is copied before generator calls and beta is read at return, preserving parameter mutations during sampling. Selected SDK layouts, defaults, parameter getters, exponential dependencies, math bodies and explicit redeclarations are authenticated. Native comparisons use small math tolerances with exact parameter effects and generator states. No new primitive or SDK/catalog revision is added beyond the existing power support. Long double and stream serialization remain separate work.

`chi_squared_distribution<float/double>` and `param_type` admit default, degree, parameter, copy/move construction, assignment, n, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. The exact layout owns one degree field, defaults to one and has no persistent gamma or cache; bounds are positive zero/infinity. Sampling snapshots the temporary gamma parameters n/2 and 2 before any generator call, then reuses the checked gamma rejection algorithm. Mutations of explicit or stored degrees during sampling remain visible afterwards while the current gamma retains its copied parameters. SDK constructors, getters, gamma/exponential bodies, math helpers and explicit redeclarations remain authenticated. Native values use small math tolerances with exact parameter effects and generator states. Existing math and SDK/catalog versions are unchanged. Long double and stream serialization remain separate work.

`fisher_f_distribution<float/double>` and `param_type` admit default, m/n, parameter, copy/move construction, assignment, getters, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. Both degrees default to one, bounds are positive zero/infinity and no cache is stored. Sampling constructs both Gamma shapes m*RealType(.5) and n*RealType(.5) before any draw, with unit scales, then preserves the SDK expression n*x/(m*y) and one permitted operand evaluation order. Later degree reads remain distinct from those copied shapes. The shared Gamma sampler and complete selected SDK constructors, accessors, exponential/math bodies and explicit redeclarations remain authenticated. Native values use small math tolerances with exact generator states and parameter effects. Existing math and SDK/catalog versions are unchanged. Long double and stream serialization remain separate work.

`student_t_distribution<float/double>` and `param_type` admit default, degree, parameter, copy/move construction, assignment, n, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. The degree defaults to one and bounds are negative/positive infinity. The exact layout keeps its own degree and a separate persistent normal state: construction starts the normal cache cold, copy/move/assignment preserve it, parameter replacement keeps it and reset clears it. The pinned Student equality compares only degree parameters, even when normal caches differ. Sampling copies the temporary Gamma shape before any draws, retains the SDK double .5 promotion, normal cache behavior and one permitted order for normal*sqrt(n/gamma). Normal, Gamma, exponential and sqrt/log/pow bodies and explicit redeclarations remain authenticated. Native values use small math tolerances with exact cache effects and engine states. No new primitive or SDK/catalog revision is added. Long double and stream serialization remain separate work.

`cauchy_distribution<float/double>` and `param_type` admit default, location/scale, parameter, copy/move construction, assignment, a/b, bounds, parameters, equality, reset and both sample forms with checked source or SDK generators. Defaults are zero/one and bounds negative/positive infinity, with no cache. The pinned SDK uses an unsuffixed double pi literal: even float canonical draws promote to double for pi multiplication, tan, scale multiplication and location addition, then narrow only the final result. Lowering preserves that path and a permitted parameter-read order, rather than substituting float pi/tanf or a shifted draw. Selected SDK bodies, actual double tangent signature and explicit redeclarations remain authenticated. The strict core-v2 tan primitive requires one matching float/double operand and emits a C23 builtin; hosted CRT/libm imports belong to the backend. Native values use small math tolerances with exact parameters and generator states. Long double and stream serialization remain separate work.

`poisson_distribution<IntType>` and `param_type` support the eight standard short/int/long/long long signed and unsigned result types, default/mean/parameter/copy/move construction, assignment, mean, bounds, parameters, equality, reset and both sampling forms with checked source or SDK generators. The mean defaults to one; either infinity is replaced by the largest double as in the pinned SDK. All ten double parameter fields are initialized and copied. Sampling retains the small-mean product loop and the large-mean cold normal, squeeze, exponential proposals and acceptance expressions, with exact generator consumption. Results use the SDK integer saturation thresholds before conversion, including 64-bit boundaries. Strict core-v2 trunc requires one matching float/double argument and emits a C23 builtin. Normal, exponential, math and clamp helpers and their explicit redeclarations remain authenticated. The C23 backend owns hosted math imports. Stream serialization remains separate work.

`negative_binomial_distribution<IntType>`, `geometric_distribution<IntType>` and their `param_type` support all eight standard integer result types, default/value/parameter/copy/move construction, assignment, getters, bounds, parameters, equality, reset and both sampling forms. Negative binomial snapshots k/p before draws, preserving the SDK repeated-Bernoulli path when k <= 21*p and its Gamma<double>→Poisson<IntType> mixture otherwise. Geometric constructs the same negative-binomial path with k=1, so its probability threshold also preserves the exact SDK engine consumption. Nested distributions are temporary; no persistent cache is introduced. Integer promotions, parameter mutation, nested SDK bodies and selected math/clamp redeclarations are checked. No new primitive or SDK/catalog revision is added. Stream serialization remains separate work.

`binomial_distribution<IntType>` and `param_type` support all eight standard integer result types, default/trial-count/probability/parameter/copy/move construction, assignment, t/p, bounds, parameters, equality, reset and both sampling forms. Defaults are one trial and probability .5; max is the current trial count. The exact SDK modal probability, odds ratio and mode are cached only for interior probabilities. Endpoint copies and parameter operations avoid reading indeterminate cached fields, and endpoint sampling consumes no engine values. Interior sampling preserves the SDK one-draw modal recurrence, lower branch before upper branch, integer promotions and live parameter reads. Strict double log-gamma retains the selected target ABI: lgamma_r with a local sign on non-Windows targets, lgamma on Windows. SDK helpers, selected exp/log/log-gamma declarations, imports and complete bodies remain checked. No SDK/catalog revision changes; stream serialization remains separate work.

The pinned C++17 `<unordered_map>` header admits metadata for `std::unordered_map<int, int>` with the exact SDK `std::hash<int>`, `std::equal_to<int>` and `std::allocator<std::pair<const int, int>>`. Aliases, type identity, public type members, `sizeof` and `alignof` retain the selected target layout.

The pinned `std::unordered_map<int, int>` with SDK default policies supports default construction, destruction, runtime pointers and references, size/load observers and load-limit mutation, plus `operator[]`, `count`, `erase(key)`, `clear`, `rehash`, `reserve`, `bucket`, `bucket_size`, `max_size`, `max_bucket_count` and member `swap`. Inserted nodes retain their addresses across rehash and swap. Allocation and deallocation use checked global hooks or authenticated defaults; inputs must stay within the SDK preconditions and successful, nonoverflowing allocation domain. `at`, other element types, custom policies and source SDK redeclarations remain rejected.

Mutable and const iterators support default/copy/move construction and assignment, mutable-to-const conversion, dereference, arrow, increment and equality. `begin`/`end`, `cbegin`/`cend`, `find`, range loops and structured bindings preserve the SDK node order and const-key references. Iterator and iterator-range `erase` return the next mutable iterator.

The same int/int map supports `insert`, `emplace`, `emplace_hint`, `try_emplace`, `insert_or_assign` for int key/value references and int/int pairs, with the available const-iterator hint overloads. Insert and emplace return a mutable iterator or a pair of iterator and insertion flag. Duplicate insertion preserves the mapped value; insert-or-assign replaces it. Arguments are captured once, and node construction reads the referenced values after allocation, before bucket growth. Other emplace argument types remain separate work.

Mutable and const `equal_range` return the matching iterator and its successor, or two end iterators when absent. Range `insert` accepts raw int/int-pair pointers and mutable/const iterators of the same default-policy map type; initializer-list insertion accepts value-type pairs. Endpoints are evaluated once, duplicate keys preserve their first value, and empty ranges perform no element access. SDK range-validity preconditions apply.

The default int/int unordered map also supports copy/move construction and assignment. Copies own independent nodes; copy assignment reuses existing nodes in the pinned SDK order and preserves retained bucket storage. Moves transfer node addresses and repair bucket predecessors, leaving the source empty with zero bucket count and its previous load limit. Copy self-assignment preserves the object. Source heap hooks retain SDK allocation, value-copy and deletion ordering.

Zero-argument `emplace()` and `emplace_hint(hint)` value-initialize the default int/int pair. The pinned SDK allocates and constructs a candidate node before lookup, including for duplicate keys; a rejected candidate is deleted before returning the existing iterator. Hint and receiver expressions are evaluated once. Heap hooks retain this allocation, load-limit and deletion ordering.

Piecewise `emplace` and `emplace_hint` accept the authenticated SDK tag and tuples with zero or one int value or reference for each field. Tuple objects are captured once, but their values and referents are read while constructing the candidate after node allocation and before bucket growth. Duplicate candidates are still allocated, constructed and deleted without replacing the existing mapped value. The tag is consumed by the operation; standalone tag storage remains outside this profile.

The default int/int unordered map supports allocator-only construction, copy/move construction with its exact default allocator, and `get_allocator()`. Allocator expressions retain their evaluation and lifetime before source state is copied or transferred. The allocator is stateless: copies still own independent nodes, equal-allocator moves preserve node addresses, and the getter returns the existing supported allocator value.

Bucket-count constructors accept the exact default hash, equality and allocator, including their omitted SDK defaults and allocator shorthand overloads. Every argument is evaluated once before allocation; the captured count selects the pinned SDK prime or power-of-two bucket count. Zero requests remain empty without allocating buckets, and source replacements remain rejected.

Unordered-map constructors accept raw int-pair ranges, default-map iterators and exact value_type initializer lists, including bucket-count/default-policy and allocator shorthand forms. Endpoints and arguments are captured once before initial bucket allocation. Insertion preserves source order, the first value for duplicate keys and allocation-time payload reads; empty lists avoid null pointer arithmetic.

`hash_function()` and `key_eq()` return the authenticated default `hash<int>` and `equal_to<int>` objects. Mutable and const receivers, returned functor copies, arrays and calls use existing functional-object semantics. Each receiver expression is evaluated once; the observers preserve map state and allocate no storage. Exact SDK type, declaration and body checks remain required.

Default-map local_iterator and const_local_iterator support bucket begin/end/cbegin/cend, copies, assignment, mutable-to-const conversion, dereference and increment. Their three-word storage retains the node, bucket index and captured bucket count; traversal stops at the bucket boundary. Receiver and bucket expressions are evaluated once. Valid bucket/iterator preconditions apply. Range insertion and constructors retain captured local bucket metadata and stop at that bucket end.

The default int/int unordered map supports node handles: empty/default and move construction, move assignment, destruction, bool/empty, mutable key/mapped references through const handles, get_allocator, member/ADL swap, extract by key or const iterator, and ordinary/hinted node insertion. Extraction and successful transfer preserve element addresses. Ordinary duplicate insertion returns ownership in result.node and empties the source; hinted duplicate insertion retains the source handle. Delete callbacks observe the old handle before it is cleared. Allocator and iterator validity preconditions apply. Custom map policies and other element types still need separate support. Insert results also support direct structured bindings by value or reference, retaining the owned node lifetime.

merge accepts lvalue/rvalue sources of the same default int/int unordered_map. Successful transfers preserve node addresses and duplicates remain in the source. Destination lookup and any bucket growth finish before source unlinking, so allocation callbacks still observe the source node. Empty and self merges preserve contents. The exact selected SDK template and both map storage graphs remain authenticated; custom policies and multimap sources require separate support.

The default int/int unordered_map supports initializer-list assignment with an aliased map reference result, retained bucket capacity/load policy, old-node reuse, unused-cache cleanup and first-key-wins duplicates. Assignment notation evaluates the list before the receiver; named operator= evaluates the receiver first. One known pinned libc++20 defect is corrected: a reused node rejected for a duplicate key is deleted instead of losing ownership. Ordinary paths retain native allocation traces; duplicate-cache paths intentionally add that required deletion. The native defect reproduction is retained separately from passing reference tests, and generated programs must release every allocation.

The exact default-map insert_return_type supports SDK move construction, move assignment and direct std::swap. Iterator and inserted members are copied before the owned node is moved; assignment updates those members before deleting the old node. A deletion callback observes that order, and source-node state is read after the callback. Moving keeps source iterator/bool values while emptying its node. Self move consumes the owned node; self swap retains it. Source specializations of the result, node operations or swap remain rejected.

The exact default unordered_map<int,int> supports operator==, operator!= and qualified or ADL nonmember swap. Equality compares key/value contents independently of bucket count, load limit and insertion order, without allocation. Swap preserves node addresses, exchanges the load limit and bucket storage, and repairs bucket predecessors, including self swap. Both operands are evaluated once before storage is read or changed. Reference-only use needs no heap definitions. Original SDK templates, selected bodies and nested pair comparisons remain authenticated; source replacements, function addresses and other policies/types stay rejected.

Exact default-map mutable/const global and local iterators compose with std::distance, std::advance and std::next, including the authenticated default next distance of one. Direct loops preserve SDK forward traversal, copied iterator arguments and local bucket metadata. Advance binds its iterator before evaluating the distance; a later argument may change the referenced iterator before traversal starts. Distance/next copy their iterator operands before subsequent arguments. Valid reachable ranges, representable distances and nonnegative forward offsets are required. SDK templates, iterator traits and selected helper bodies remain authenticated. Reference-only use needs no heap definitions.

Generic `std::swap<T>` also accepts the exact default map, its node handles and mutable/const global or local iterators. Both operands are captured once. Authenticated SDK moves leave their temporary empty; equivalent direct storage exchange preserves node ownership/addresses, map sentinel links and all local iterator metadata without allocation or deletion. Self swap preserves ownership. Original generic swap declarations and selected move/destructor bodies remain checked; source replacements and indirect function addresses remain rejected. Unused node_type and insert_return_type aliases, their pointer/reference/array forms and type identity queries also authenticate and complete SDK storage without constructing an owner or requiring heap definitions.

The exact SDK insert_return_type supports implicit default and value construction. Both create a singular iterator and empty node; value initialization also zeroes inserted, while default initialization leaves that bool unwritten. Arrays, source fields and factory returns preserve these rules and owning cleanup. Existing empty, partial and full aggregate initialization remains supported in field order. Original constructor bodies and nested iterator/node operations remain authenticated; owning lifetimes retain source delete requirements.


Ordinary default C++ heap operators are supported in the successful, nonoverflowing allocation domain. Untouched compiler-implicit declarations or exact pinned SDK declarations of scalar/array new and unsized/sized delete lower to private wrappers over the existing checked native malloc/free protocol. Zero-byte new requests at least one byte. Objects must fit the target’s ordinary new alignment and the existing hosted CRT/size_t ABI. Raw objects, arrays, admitted unique pointers, allocators, strings, vectors and unordered-map nodes share this contract.

Source-defined operators keep their existing precedence, including implicit sized delete forwarding to source unsized delete. A source declaration or definition of any of the six ordinary replaceable global signatures disables every new default fallback across that whole family. Typed source delete/default_delete also rejects unproved pairing with class or inherited allocation functions, or other source global allocation overloads, including placement/nothrow/aligned forms; explicit SDK global allocator calls retain their independent selection. Direct default operator calls, default function addresses, aligned/nothrow fallback, allocation failure handlers and exception unwinding remain excluded. Raw default new[] still requires a proven constant extent; runtime nonthrowing class allocation keeps its separate existing contract. Pure type/lifetime queries emit no heap wrappers or array-cookie runtime metadata. The NeverC source language remains C23-only. [C++17](../docs/cpp-core-v2.md#single-object-allocation-and-placement-reuse).


Exact pinned `std::make_from_tuple` accepts authenticated tuple, pair and array inputs and constructs admitted scalar or source-record results. The input is evaluated once; selected conversions, constructor templates, defaults and final return/field/array destinations retain their effects and C++17 copy elision. Helper argument/default temporaries end before the caller's tuple temporaries. Runtime operations require completed definitions; pure `decltype`/`noexcept` queries check both factory signatures and selected exception sources without runtime helpers. Reference inputs, reference results and direct reference fields require live origins; expired forwarding carriers, escaping temporaries, unknown or nested reference provenance, base-bearing result records, source replacements, private helper calls and standalone addresses remain rejected. NeverC's source language remains C23-only. [C++17](../docs/cpp-core-v2.md#make-from-tuple-result-construction).


Exact pinned direct `std::hash<std::string_view>` and `std::hash<std::string>` calls hash the logical bytes of admitted char carriers. The source is evaluated once; complete 32-bit Murmur2 and 64-bit CityHash paths preserve the SDK ABI1 arithmetic, including narrow intermediate truncation. Selected public, inherited and helper declaration chains are authenticated. Pure `decltype`/`noexcept` queries preserve argument exception sources without algorithm runtime helpers. Private helper calls, standalone method addresses, source replacements remain outside this contract. [C++17](../docs/cpp-core-v2.md#string-and-string-view-hashing).

Exact direct `std::exchange` calls also accept admitted scalar or array `std::unique_ptr` owners with the pinned default deleter, replacing them with a same-type rvalue owner or `nullptr`. Arguments evaluate once before ownership moves; self exchange, returned and ignored owners, outer assignment aliases and extended result lifetimes preserve their cleanup. Pure signature queries create no ownership or heap helpers. Selected SDK declaration, constructor, assignment, helper and exception-trait sources remain checked; custom or converting owners and standalone function addresses remain excluded.

Exact direct `std::hash<std::unique_ptr<T>>` calls support admitted scalar and array owners with the pinned default deleter. Hashing borrows the owner, evaluates receiver and argument once, and hashes its observed pointer with the checked SDK pointer algorithm; null, const and moved owners retain native behavior. Temporary arguments keep full-expression cleanup. Pure signature queries preserve the SDK operator’s potentially throwing signature without selecting pointee destruction or adding heap helpers. Enabling traits, selected get and hash declarations, byte helpers and ABI arithmetic retain source checks; stateful, reference, potentially throwing or fancy-pointer deleters, independent addresses remain excluded.

Direct `std::hash<std::unique_ptr<T,D>>` also accepts the existing admitted empty, trivial source-owned custom deleter with its checked noexcept body and plain element pointer. Hashing never invokes the deleter; original owner cleanup and temporary argument lifetimes retain their effects. Queries and borrowing add no hash or ownership helpers beyond the original source deleter definitions. The existing custom-deleter layout and source contract remains required.

Runtime `std::hash<T*>` authenticates the complete selected pinned byte algorithm, including its primary and partial declarations, word loads, memcpy and CityHash ABI branch. This proof also applies through invoke, apply and reference wrappers, including ignored results; unique-owner hashes reuse it. Signature queries retain their separate body-free checks and add no runtime helpers.

The admitted scalar/array `std::hash<std::unique_ptr<T,D>>` also supports invoke, apply, direct reference_wrapper calls and invoke/apply of the wrapper, with default or existing empty custom deleters. Exact owner arguments and tuple elements are borrowed at their original address; receiver/argument effects and temporary owner/tuple cleanup retain their original lifetime. Runtime checks authenticate the actual selected adapter and complete hash sources, including ignored results. Pure queries preserve the potentially throwing signature without requiring hash/get/byte bodies or adding runtime helpers. Source-to-owner conversions remain outside this increment.

Reference tuples borrow admitted owners. Value tuples use the selected same-type move constructor, clear the source owner, and destroy owning elements in reverse tuple order; owner elements never use bitwise copying.

Admitted float/double/long-long hashes authenticate the selected scalar declaration families, exact union initialization, parameter store and return, plus the complete selected byte algorithm for two-word delegates. The same runtime proof applies through invoke, apply and callable reference wrappers, including ignored results. Scalar apply and apply-of-wrapper signature queries bind their actual retained SDK call without consuming hash/scalar/byte bodies or adding runtime helpers; original source function definitions remain independent. Signed-zero normalization, scalar conversions and argument effects are preserved. This does not expand integer widths or admit long double.

Exact admitted char string/view hashes support invoke, apply, direct reference_wrapper calls and invoke/apply of the wrapper. String arguments borrow their original const-reference address; view parameters receive an independent value snapshot. Receiver and argument effects occur once. Selected flat and nested string/view tuple copy/move operations and temporary cleanup preserve ownership and lifetime. Runtime calls, including ignored results, require the complete selected SDK byte algorithm. Pure signature queries, including default view temporaries and substituted runtime byte-copy helpers, validate the actual selected call without adding algorithm or heap helpers. Source-to-carrier conversions remain deferred.

Exact SDK reference_wrapper arguments of admitted unique owners, char strings and string views are accepted by their hash calls and the existing invoke/apply/callable-wrapper routes. ref/cref, copied or const wrappers and selected tuple argument slots retain their actual referent binding. Owners and strings borrow the referent address; views receive the selected independent value copy. Wrapper/referent effects and full-expression cleanup keep their original order and lifetime. Runtime calls, including ignored results, authenticate the selected SDK conversion/access and complete hash sources. Pure signature queries retain original conversion and exception sources without requiring runtime bodies or adding hash/heap helpers. Source-defined conversions and substituted SDK access methods remain rejected.

Selected nested tuple values containing admitted char strings, string views and unique owners retain recursive operation proofs through construction, whole copy/move, get, tuple_cat and checked apply. String copies own independent buffers; view copies have independent bindings; owners move without copying and clear the source. Reference elements retain their referent binding. Every value layer preserves the selected SDK constructor, forwarding/getter sources, argument effects and reverse cleanup, including temporary nested tuples and ignored results. Layout and pure signature queries retain their original exception and source dependencies without granting runtime construction/destruction or adding heap/deletion helpers. This does not admit generic bitwise copies of owning tuples or source substitutions.

Pure tuple_cat signature queries accept authenticated tuple, pair and array inputs whose elements are admitted scalar, reference, char string/view, unique-owner or recursively nested carriers. Const and zero-length arrays retain their actual element identities. Layout and signature checks do not select owner construction or pointee deletion bodies; actual written arguments, SDK template families, defaults and exception sources remain checked. Query permissions belong to each exact call and its input/result types. Runtime construction, copying, movement and cleanup keep their separate selected-operation requirements.

Within the admitted value domain, direct std::pair constructor template calls authenticate their exact selected deduction, template defaults, constraint helpers and exception sources. Default construction checks the selected implicit-default path and the explicit primary fallback. Nested pair/array operations retain actual selected subobjects; source-owned inputs finish ordinary type, constructor-default, exception and destruction checks. Replaced SDK helpers and source macros remain refused, including native-valid replacements. Runtime value and cleanup support retains its separate requirements.
