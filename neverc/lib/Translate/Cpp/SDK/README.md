# Embedded C++ SDK sources and licenses

This directory supplies the immutable header inputs for NeverC's built-in C++
frontend. The distribution is
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5-r7`. It contains the 533 header
files admitted by the current
`clang20.1.8-libcxx200100-macos15.5` catalog: 437 libc++ headers, 14 Clang
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
`make_pair` and `get`, scalar-pair comparisons, `tuple_size`, `tuple_element`
and integer-sequence size queries. Pair elements may be source-owned trivial
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
directly lowers construction, compatible per-element converting construction
and assignment from tuples or scalar pairs, factories, swaps, same-length
heterogeneous comparisons and index or unique-type access without a runtime
libc++ link.
Programs using the pair conversion include both `<tuple>` and `<utility>` and
have a 108-file union closure on every core-v2 target.
The `<initializer_list>` surface has a 10-file libc++/resource closure on every
core-v2 target. It retains the pinned two-field pointer-and-size layout and
directly lowers braced backing-array materialization, default and copy/move
construction, assignment, member and free range access, and normal backing
element destruction. Nested lists and complete admitted non-volatile object
elements use the existing aggregate and lifetime machinery without a runtime
libc++ link.
The `<optional>` surface has a 136-file libc++/resource closure on every core-v2
target. It authenticates libc++'s private base and union representation and
directly lowers scalar optional construction, assignment, engagement queries,
zero/single-value in-place construction, dereference, arrow, reset,
single-value emplacement, scalar value fallback, member/free swap,
zero/single-value factories, scalar converting optional construction and
assignment including standard `nullptr_t`-to-object-pointer conversions, and
C++17 same-type, heterogeneous arithmetic or qualification-compatible pointer
comparisons without a runtime libc++ link.
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
| libc++ and Clang resource headers | 451 | Apache-2.0 with LLVM exceptions |
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
