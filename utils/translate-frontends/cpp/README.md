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
flattened reverse array destruction and the checked source-defined scalar
or array class/global delete selected by Clang.
`std::addressof`, `pointer_traits::pointer_to` and scalar-pointer destruction
and uninitialized construction algorithms lower directly. Exact runtime
allocator objects, C++17 `destroy`, and allocator-traits destruction,
`max_size` and copy selection also lower directly. Exact allocator and traits
allocation/deallocation forwarders call checked source-defined global new/delete
for complete default-new-aligned elements and constant nonoverflowing allocation
counts; ownership objects remain separate. The
frontend uses the pinned embedded libc++/resource VFS and exposes resolved type
aliases plus integral/enum constant results. It records all consumed header
hashes: 101 for the `<type_traits>` closure, nine for standalone `<cstdint>`,
16 for `<limits>`, 29 for `<cstddef>`, 87 for `<utility>`, 98 for `<tuple>`,
217 for `<array>`, 10 for `<initializer_list>`, 136 for `<optional>`, 171 for
`<iterator>`, 354 for `<algorithm>`, 126 for `<numeric>`, 37 for `<new>` and
267 for `<memory>`.
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
`tuple_size`, `tuple_element` and integer-sequence size queries. Tuple directly
lowers authenticated empty and nonempty scalar or recursively composite tuple
construction, compatible scalar or exact composite per-element construction
and assignment from tuples or pairs, factories, swaps, recursive same-length
heterogeneous lexicographic comparisons and index or unique-type access. Exact
`apply` over scalar tuples lowers named functions or stored function pointers
with directly convertible by-value scalar parameters to one indirect call,
including empty tuples and scalar or void results. Exact `tuple_cat` handles
zero arguments and authenticated scalar or recursively composite tuple, pair
or array sources,
evaluating every source once before copying elements into the exact
concatenated tuple. Array directly lowers nonempty fixed scalar, trivial-record
and nested-array storage, iterators, element access, fill, swap, scalar and
recursive nested-array comparisons and tuple access.
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
access, reset and swap without libc++. Composite conversion requires exact
unqualified element types, and mutation requires recursive assignability;
value comparisons recurse through authenticated arrays, pairs and tuples to
the scalar leaf boundary, while `nullopt` comparisons work for every admitted
element.
Iterator exposes the pinned public header and pointer `iterator_traits` metadata
through a platform-free 171-file closure, and directly lowers bounded pointer,
array-range and reverse-iterator operations. Stream iterators remain disabled
until their C runtime character-state ABI is available on every target.
Algorithm directly lowers scalar-pointer `copy`, `move`, `copy_backward`,
`move_backward`, `copy_n`, `fill`, `fill_n`, `iter_swap`, `swap_ranges`,
`reverse`, `reverse_copy`, `rotate` and `rotate_copy` calls from its
platform-free 354-file closure; copy and move outputs accept checked direct
scalar conversions. Built-in equality elements admit
`find`, `count`, three- or four-iterator `equal`, `adjacent_find`, `remove`,
`remove_copy`, `replace`, `replace_copy`, `unique`, `unique_copy`, `search`,
`find_end`, `find_first_of`, `search_n`, three- or four-iterator `mismatch` and
three- or four-iterator `is_permutation`;
built-in arithmetic elements admit default-order
`min`, `max`, `clamp`, `minmax`, `min_element`, `max_element`,
`minmax_element`, `lower_bound`, `upper_bound`, `equal_range`, `binary_search`,
`is_sorted`, `is_sorted_until`, `is_heap`, `is_heap_until`, `make_heap`,
`push_heap`, `pop_heap`, `sort_heap`, `sort`, `stable_sort`, `inplace_merge`,
`partial_sort`,
`partial_sort_copy`, `nth_element`, `next_permutation`, `prev_permutation`,
`lexicographical_compare`, `includes`, `merge` and the four `set_*` range
algorithms. Checked unary boolean function-pointer predicates with directly
convertible by-value scalar parameters admit `find_if`, `find_if_not`,
`count_if`, `all_of`, `any_of`, `none_of`,
`copy_if`, `remove_if`, `remove_copy_if`, `replace_if`, `replace_copy_if`,
`is_partitioned`, `partition`, `stable_partition`, `partition_copy` and
`partition_point`. Checked function-pointer binary predicates with directly
convertible by-value scalar parameters additionally admit `adjacent_find`, three-
and four-iterator `equal`, three- and four-iterator `mismatch`, and three- and
four-iterator `is_permutation`, plus `unique`, `unique_copy`, `search`,
`find_end`, `find_first_of` and `search_n`. Checked function-pointer comparators
with directly convertible by-value scalar parameters additionally admit `min_element`, `max_element`,
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
Predicate mutation and ordered output algorithms require writable destinations,
and `copy_n`, `fill_n` and `search_n` accept integral or non-scoped enum counts
through 64 bits after integer promotion.
`mismatch`, `equal_range` and `minmax_element` directly construct authenticated
scalar-pointer pair results; `minmax` uses its exact authenticated pair of two
const references. `shuffle` and `sample` remain disabled at this stage because
their libc++ implementation reaches the same target C runtime character-state
ABI through `uniform_int_distribution`.
Numeric contributes an exact, platform-free 126-file C++17 header closure.
Matching non-promoted integer, `float` and `double` pointer ranges admit the
default `iota`, `accumulate`, `inner_product`, `partial_sum` and
`adjacent_difference` overloads, C++17 reductions and scans, transformed scans,
and exact same-type by-value function-pointer operations. Integer `gcd` and
`lcm` accept the documented built-in integer combinations. Output ranges must
be writable; callable objects, heterogeneous element types and custom iterators
remain rejected. New contributes an exact, platform-free 37-file C++17 header
closure. The standard allocation tag types and interference-size values remain
compile-time or scalar metadata, and exact `std::launder` calls on admitted
non-volatile object pointers lower to their retained pointer value without a
libc++ call. Exact standard placement new and constant-bound placement new[]
reuse a captured storage pointer without a runtime call. Runtime tag objects,
direct allocation-function calls and default heap operations remain excluded.
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
object or reverse array elements, and invoke the checked source-defined scalar
or array class/global delete selected by Clang. Exact
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
the selected checked source-defined global/class-specific scalar new/delete or
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
runtime deallocation counts, when a source-defined global new/delete path
exists and the allocation count is a constant proven within `max_size`. Default
heap allocation, dynamic allocation counts, over-aligned elements,
runtime-count array factories, stateful,
reference, non-raw-pointer, nontrivial, overloaded, ref-qualified or throwing
custom deleters, other smart pointers and ownership factories are not yet
admitted. The driver authenticates each
closure before emitting output. Standard-library objects and operations beyond
these documented surfaces, other standard headers and full C++/STL remain
unfinished.

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
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5-r9`. The
[SDK catalog](../../../neverc/lib/Translate/Cpp/SDK/catalog.json) records all
562 approved header files and separate SDK metadata. The original header bytes
are preserved, including observable macros such as `M_PI` and `_LIBCPP_VERSION`.
The [SDK notices](../../../neverc/lib/Translate/Cpp/SDK/README.md) document
origins, redistribution terms and the minimal owned SDK configuration.
A typical `<cmath>` module consumes 201 embedded headers.

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
