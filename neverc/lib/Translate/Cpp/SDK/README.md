# Embedded C++ SDK sources and licenses

This directory supplies the immutable header inputs for NeverC's built-in C++
frontend. The distribution is
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5-r9`. It contains the 562 header
files admitted by the current
`clang20.1.8-libcxx200100-macos15.5` catalog: 466 libc++ headers, 14 Clang
resource headers, one NeverC resource header, and 81 Darwin platform headers.
The upstream-source bytes, including copyright and license notices, are
preserved; the NeverC-authored C string declaration shim is identified
separately below. This is a fixed input set for the supported translation
profiles, not a complete Apple SDK.

Core v2 uses only the catalog's `libcxx` and `resource` roots for its
compile-time `<type_traits>` and fixed-width `<cstdint>` surfaces. The
standalone closures contain 101 and nine files respectively; their deduplicated
union is recorded when both headers are included. Every consumed path and hash
is verified against this catalog. The `<limits>` surface has a 16-file closure
and folds its supported `numeric_limits` members without a runtime libc++ link.
The `<cstddef>` surface has a 29-file closure and provides its target aliases,
folded layout queries and directly lowered `std::byte` operations. The
`<utility>` surface has an 87-file closure and directly lowers scalar
`move`, `forward`, `move_if_noexcept`, `as_const`, `exchange` and `swap`, plus
scalar or recursively composite `pair` construction, assignment, swaps,
`make_pair` and `get`, recursive standard-composite pair comparisons,
`tuple_size`, `tuple_element` and integer-sequence size queries. Pair elements
may be source-owned trivial
records, admitted arrays or nested pairs; the array-plus-utility union has a
222-file closure. These surfaces do not require a runtime libc++ link. The
`<array>` surface has a 217-file libc++/resource closure and directly lowers
fixed scalar, trivial-record and nested arrays, including the dedicated
zero-length partial specialization, capacity, pointer and reverse iterators,
element access where the extent is nonzero, fill, swap, scalar and recursive
nested-array comparisons and index-based `get`, without a runtime libc++ link.
The `<tuple>` surface has a 98-file libc++/resource closure on every core-v2
target. It authenticates libc++'s private implementation and indexed leaf
offsets for nonempty tuples and the dedicated empty specialization, then
directly lowers scalar or recursively composite construction, compatible
per-element converting construction and assignment from tuples or pairs,
factories, swaps, recursive same-length heterogeneous comparisons and index or
unique-type access without a runtime libc++ link. Composite elements may be
source-owned nonempty trivial standard-layout records, admitted arrays or
pairs, or nonempty nested tuples; their conversions keep the exact unqualified
type and mutation requires recursive assignability. Programs using scalar pair
conversion include both `<tuple>` and `<utility>` and have a 108-file union
closure. The array-plus-tuple-plus-utility composite fixture has a 231-file
union closure on every core-v2 target.
The `<initializer_list>` surface has a 10-file libc++/resource closure on every
core-v2 target. It retains the pinned two-field pointer-and-size layout and
directly lowers braced backing-array materialization, default and copy/move
construction, assignment, member and free range access, and normal backing
element destruction. Nested lists and complete admitted non-volatile object
elements use the existing aggregate and lifetime machinery without a runtime
libc++ link.
The `<optional>` surface has a 136-file libc++/resource closure on every core-v2
target. It authenticates libc++'s private base and union representation and
directly lowers optional construction, assignment, engagement queries,
zero/single-value in-place construction, dereference, arrow, reset,
single-value emplacement, value fallback, member/free swap and zero/single-value
factories. Elements may be admitted scalars, source-owned trivial
standard-layout records, arrays, pairs or tuples; construction and
conversion use exact composite types, while mutation additionally requires
recursive assignability. Scalar conversions include the standard
`nullptr_t`-to-object-pointer conversion. C++17 value comparisons recurse
through authenticated arrays, pairs and tuples to the same scalar,
heterogeneous arithmetic and qualification-compatible pointer leaf boundary;
comparisons with `nullopt` work for every admitted element. The
array-plus-tuple-plus-utility-plus-optional composite fixture has a 253-file
platform-free union closure on all eight targets, with no runtime libc++ link.
The `<iterator>` surface has a 171-file libc++/resource closure on every core-v2
target. It exposes pointer `iterator_traits` metadata and directly lowered
pointer, array-range and raw-pointer reverse-iterator operations while retaining
the exact upstream iterator headers. Four stream-iterator declarations stay
disabled until the core SDK has an authenticated cross-target C runtime
`mbstate_t` boundary.
The `<algorithm>` surface has a 354-file libc++/resource closure on every
core-v2 target. Its upstream declarations and the NeverC C string declaration
shim are available without platform headers. Exact scalar-pointer `copy`,
`copy_n`, `move`, `copy_backward`, `move_backward`, `fill`, `fill_n`,
`iter_swap`, `swap_ranges`, `reverse`, `reverse_copy`, `rotate` and
`rotate_copy` calls lower without a libc++ runtime dependency.
The same is true for built-in equality elements with `find`, `count`, three- or
four-iterator `equal`, `adjacent_find`, `remove`, `remove_copy`, `replace`,
`replace_copy`, `unique`, `unique_copy`, `search`, `find_end`,
`find_first_of`, `search_n`, three- or four-iterator `mismatch` and three- or
four-iterator `is_permutation`, and for
arithmetic elements with `min`, `max`, `clamp`, `minmax`, `min_element`,
`max_element`, `minmax_element`, `lower_bound`, `upper_bound`, `equal_range`,
`binary_search`, `is_sorted`, `is_sorted_until`, `is_heap`, `is_heap_until`,
`make_heap`, `push_heap`, `pop_heap`, `sort_heap`,
`sort`, `stable_sort`, `inplace_merge`, `partial_sort`, `partial_sort_copy`,
`nth_element`,
`next_permutation`, `prev_permutation`,
`lexicographical_compare`, `includes`, `merge`, `set_union`, `set_intersection`,
`set_difference` and `set_symmetric_difference`. Checked by-value unary
boolean function-pointer predicates admit `find_if`, `find_if_not`,
`count_if`, `all_of`, `any_of`, `none_of`, `copy_if`, `remove_if`,
`remove_copy_if`, `replace_if`, `replace_copy_if`, `is_partitioned`,
`partition`, `stable_partition`, `partition_copy` and `partition_point`. Exact
function-pointer
binary predicates additionally admit `adjacent_find`, three- and four-iterator
`equal`, three- and four-iterator `mismatch`, and three- and four-iterator
`is_permutation`, plus `unique`, `unique_copy`, `search`, `find_end`,
`find_first_of` and `search_n`.
Exact function-pointer comparators admit `min_element`, `max_element`,
`lower_bound`, `upper_bound`, `equal_range`, `binary_search`, `is_sorted` and
`is_sorted_until`, plus `min`, `max`, `clamp`, `minmax` and `minmax_element` on
same-type scalar ranges and values, and `lexicographical_compare`, `includes`,
`merge`, `set_union`, `set_intersection`, `set_difference` and
`set_symmetric_difference` on same-element scalar ranges. The same comparator
boundary admits `is_heap`, `is_heap_until`, `make_heap`, `push_heap`, `pop_heap`
and `sort_heap`, plus `sort`, `stable_sort`, `inplace_merge`, `partial_sort`,
`partial_sort_copy`, `nth_element`, `next_permutation` and `prev_permutation`.
Exact function-pointer callbacks also admit `for_each`, `for_each_n`, unary and
binary `transform`, `generate` and `generate_n`.
Predicate mutation and ordered output algorithms require writable destinations;
`copy_n`, `fill_n` and `search_n` accept at-most-64-bit integral and non-scoped
enum counts after the libc++ integer promotion.
`shuffle` and `sample` stay disabled until the same authenticated cross-target
`mbstate_t` boundary is available through their random-distribution dependency.
The `<numeric>` surface has a 126-file libc++/resource closure on every
core-v2 target. The exact default `iota`, `accumulate`, `inner_product`,
`partial_sum`, `adjacent_difference`, `reduce`, two-range `transform_reduce`,
`inclusive_scan` and `exclusive_scan` overloads lower directly for matching
scalar arithmetic pointer ranges. Their operation-taking overloads, including
unary `transform_reduce`, initialized `inclusive_scan`, both
`transform_inclusive_scan` forms and `transform_exclusive_scan`, accept exact
same-element by-value function pointers. `gcd` and `lcm` lower for non-boolean
built-in integer combinations through 64 bits. None of these operations
requires platform headers or a libc++ runtime. Callable objects, converted
callback signatures and the remaining C++17 numeric components stay outside
the admitted runtime boundary.
The `<new>` surface has a 37-file libc++/resource closure on every core-v2
target. Its exact public C++17 header exposes `std::nothrow_t`, the scalar
`std::align_val_t` enum and folded hardware interference-size constants.
Exact `std::launder` calls on admitted non-volatile scalar, array or record
object pointers lower to the checked pointer value without a libc++ call and
retain their argument once. Exact standard placement new and constant-bound
placement new[] expressions capture and reuse their storage pointer without a
libc++ call. Runtime nothrow tags, direct allocation-function calls, default heap
operations and standard-function addresses remain outside the direct lowering
boundary.
The `<memory>` surface has a 267-file libc++/resource closure on every core-v2
target. The exact C++17 public header and all consumed component headers are
authenticated without platform headers. Raw-pointer `pointer_traits` aliases,
exact `std::allocator<T>` and
`std::allocator_traits<std::allocator<T>>` types and their nested aliases are
admitted as compile-time metadata. Exact `uses_allocator<T,
std::allocator<U>>` identities, inherited aliases and constants also resolve
without materializing a standard-library object. Exact single-object
`std::default_delete<T>` and unbounded-array `std::default_delete<T[]>`, where
`T` may have complete bounded inner extents, preserve the pinned empty one-byte
layout through a synthetic
byte carrier. Default, copy/move and admitted cv-converting construction lower
directly. Their authenticated call operator evaluates the receiver and pointer
once, destroys the complete object or reverse array elements, and calls a
checked source-defined global sized or unsized delete/delete[]. Volatile base
elements and function addresses remain rejected. Exact single-object
`std::unique_ptr<T, std::default_delete<T>>` and unbounded-array
`std::unique_ptr<T[], std::default_delete<T[]>>`, including multidimensional
owners, use an authenticated pointer-sized carrier. The same owner forms admit
a source-owned by-value custom deleter only when it is an empty,
standard-layout, trivial one-byte record with trivial special members and one
source-defined `void operator()(pointer) noexcept`, optionally `const` and
without a ref qualifier. Default, null, raw-pointer, matching raw-pointer/null
plus a deleter argument bound to the exact deleter lvalue or rvalue parameter,
same-type move and const-adding default-deleter converting move construction;
corresponding move assignment;
`nullptr` assignment; pointer access, mutable/const `get_deleter`, release,
reset, member/free swap, same-specialization and
default-deleter qualification-compatible same-element comparisons, all six
bidirectional `nullptr` comparisons and destruction lower directly without a
libc++ call.
Default deleters use the matching checked global-delete or global-delete[]
path. An admitted custom deleter is called once for a non-null pointer and
needs no global delete definition. Single-object owners expose dereference and
arrow; array owners expose subscript and use checked cookie-based reverse
destruction only for default deletion. Exact raw pointers to the owner may also
receive every admitted nonstatic member; their pointee qualification and
one-time receiver evaluation are preserved.
Exact single-object `std::make_unique<T>(args...)` authenticates the pinned
factory body and lowers through checked source-defined global new/delete. Exact
unbounded-array `std::make_unique<T[]>(count)`, where `T` may have complete
bounded inner extents, additionally accepts an integer constant expression from
zero through 65536 and lowers through the matching checked global new[]/delete[]
cookie path. The scalar factory value-initializes
its object or calls an exact source-owned non-template `noexcept` record
constructor with the call-site arguments. The array factory value-initializes
each scalar element or calls an exact zero-parameter source-owned non-template
`noexcept` record constructor. Multidimensional factories apply the outer count
to the fixed inner shape and flatten base-element construction and destruction.
Both return the same pointer-sized owner without a libc++ call. Exact runtime allocator specializations use an authenticated
one-byte stateless carrier. Default,
copy/move and non-void converting construction, same-type assignment,
heterogeneous equality, deprecated C++17 `address` and complete-element
`max_size` lower directly without a libc++ call. Exact allocator `destroy`
accepts its matching raw element pointer. Exact
`allocator_traits<std::allocator<T>>` destruction accepts an admitted raw
scalar or complete source-record pointer and preserves the pinned member or
`destroy_at` fallback selection; its `max_size` and
`select_on_container_copy_construction` also lower through the checked
stateless allocator path. Exact allocator and allocator-traits `construct`
calls authenticate the instantiated libc++ placement-new or forwarding body.
They value-initialize writable scalars or call the selected supported,
source-owned `noexcept` constructor of a complete source-owned non-union
record. Exact scalar conversion, multi-argument, copy and move forms preserve
their call-site argument evaluation without a libc++ runtime call.
Exact allocator and allocator-traits `allocate`/`deallocate` calls authenticate
the pinned member and traits forwarding bodies. Allocation requires a complete
non-void element no more aligned than the target's default new alignment, an
element count that is an integer constant expression proven no greater than
`max_size`, and a checked source-defined global `operator new(size_t)`.
Deallocation accepts a runtime count, calls an exact source-defined sized delete
when present, and otherwise permits the pinned standard sized declaration to
forward to a checked source-defined unsized delete. Receivers, allocator
references, hints, pointers and counts retain their evaluation; hints do not
change the selected global allocation function. These paths use ordinary source
calls, enable the lifetime alias policy and add no libc++ runtime dependency.
`std::addressof` and
raw-pointer `pointer_traits::pointer_to` are admitted for checked non-volatile
object lvalues. `std::destroy_at`, `std::destroy` and `std::destroy_n`
additionally lower for scalar and complete source-owned non-union record object
pointers. Trivial destruction has no runtime body; nontrivial
records use their checked destruction helper in forward order, while argument
evaluation, the counted return iterator and the lifetime alias policy are
retained. The scalar-pointer and trivial source-record `uninitialized_copy`,
`uninitialized_copy_n`, `uninitialized_fill`, `uninitialized_fill_n`,
`uninitialized_default_construct`, `uninitialized_default_construct_n`,
`uninitialized_value_construct`, `uninitialized_value_construct_n`,
`uninitialized_move` and `uninitialized_move_n` forms also lower directly.
The record form requires a complete source-owned non-union, standard-layout
trivial element type. The four default/value-construction forms additionally
accept a complete source-owned non-union record with an exact zero-parameter
source-owned `noexcept` default constructor. They call that constructor for
each selected object; value construction first zeroes a complete object when
the constructor is not user-provided. The six copy/fill/move forms additionally
accept a complete source-owned non-union record when the authenticated libc++
helper selects a source-owned, non-template copy or move constructor with
exactly one same-record reference parameter, a supported definition and a
resolved `noexcept(true)` specification. Move preserves Clang's selected move
or copy fallback. Default-heap allocation, dynamic or overflowing allocation
counts, over-aligned elements, other allocator-traits forwarding calls,
potentially throwing, default-argument or
constructor-template source-record construction, nontrivial by-value record
parameters, runtime-count ownership factories, other smart pointers and the
remaining memory operations stay outside the direct lowering boundary.
Core v2 never admits the `platform` root.
Math v1 continues to use its separately checked libc++, resource and Darwin
platform closure for `<cmath>`.

[catalog.json](catalog.json) pins the distribution's contents.
[provenance.json](provenance.json) lists each header's catalog root, relative path,
SHA-256, license basis, and public upstream source reference. Upstream references
identify the source or generator and its license provenance; they do not claim
that every exported SDK header is identical to the referenced source revision.
Generated availability headers, configured libc++ headers, and exported kernel
headers can differ from their upstream templates. The catalog hashes identify
the exact header bytes supplied here.

## License inventory

| Header group | Files | Applicable notices |
| --- | ---: | --- |
| libc++ and Clang resource headers | 464 | Apache-2.0 with LLVM exceptions |
| NeverC C string declaration shim | 1 | AGPL-3.0-only |
| Darwin headers with an APSL notice | 63 | APSL-2.0 |
| Darwin headers with APSL and Berkeley notices | 12 | APSL-2.0 and BSD-4-Clause |
| `arm/endian.h`, `arm/types.h` | 2 | BSD-4-Clause |
| `math.h` | 1 | Original APSL-1.1 notice; APSL-2.0 selected as described below |
| Headers with a project-level license basis | 3 | APSL-2.0 from the public upstream distribution, as described below |

The license texts are copied from these upstream sources:

- [licenses/LLVM.txt](licenses/LLVM.txt): [LLVM 20.1.8 libc++ license](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/libcxx/LICENSE.TXT), including the LLVM exceptions and legacy notices.
- [licenses/APSL-1.1.txt](licenses/APSL-1.1.txt): [Apple's XNU 123.5 license](https://github.com/apple-oss-distributions/xnu/blob/xnu-123.5/APPLE_LICENSE).
- [licenses/APSL-2.0.txt](licenses/APSL-2.0.txt): [Apple's XNU license](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/APPLE_LICENSE).
- [licenses/BSD-4-NOTICES.txt](licenses/BSD-4-NOTICES.txt): the original Berkeley redistribution comments from all 14 applicable headers, grouped by catalog path without replacing the original authors or terms.

The NeverC C string declaration shim follows the repository's
[AGPL-3.0-only project license](../../../../../LICENSE).

This product includes software developed by the University of California,
Berkeley and its contributors.

The original header notices, including the Apple OS reference notices and
Berkeley attribution conditions, remain applicable. The license texts in this
directory supplement those notices; they do not replace them.

## Files with project-level license provenance

These three SDK headers contain copyright notices without a separate per-file
license grant. Their license attribution follows their publication in Apple's
official source distributions under the respective root `APPLE_LICENSE`, rather
than treating the copyright notice itself as a grant:

| Catalog path | Public source | Correspondence |
| --- | --- | --- |
| `platform/usr/include/arm/_types.h` | [XNU `bsd/arm/_types.h`](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/arm/_types.h) | SDK export omits the upstream `KERNEL`-only conditional branch. |
| `platform/usr/include/arm/signal.h` | [XNU `bsd/arm/signal.h`](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/arm/signal.h) | Identical bytes. |
| `platform/usr/include/stdint.h` | [Libc `include/stdint.h`](https://github.com/apple-oss-distributions/Libc/blob/71bbe350ab79eef58113991d817ccc6165061a64/include/stdint.h) | Identical bytes. |

The corresponding project licenses are
[XNU's APSL-2.0](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/APPLE_LICENSE)
and [Libc's APSL-2.0](https://github.com/apple-oss-distributions/Libc/blob/71bbe350ab79eef58113991d817ccc6165061a64/APPLE_LICENSE).
The original Apple and NeXT notices are retained.

## License selection for `math.h`

`platform/usr/include/math.h` retains its original APSL-1.1 notice. For this
distribution, NeverC elects to use that covered code under APSL-2.0, as permitted
by section 7 of APSL-1.1. Both license versions are included above so that the
original notice and this selection can be traced to Apple's published terms.
The selection does not change the header's bytes or its declarations.

APSL-2.0 sections 2.1 through 2.3 require preserving notices, supplying the
license, and providing the required source-availability information with
executable distributions. Section 4 permits combination with other code in a
larger work. Keep the original header files and the embedding build scripts in
the corresponding source release. Binary packages must include these notices
and identify how to obtain their corresponding source release; a generated
resource array alone is not a substitute for that source distribution.

## Platform metadata

The original Apple `SDKSettings.json` is not redistributed. The embedded
`platform/SDKSettings.json` is a NeverC-authored declaration of two version facts:
`Version` is `15.5` and `MaximumDeploymentTarget` is `15.5.99`. Its own hash is in
the current catalog. The former metadata hash appears only as migration provenance
in `provenance.json`; the header license inventory does not claim to license the
former SDK configuration file.
