**Languages**: [English](translate.md) | [简体中文](zh-CN/translate.md) | [繁體中文](zh-TW/translate.md) | [日本語](ja/translate.md) | [한국어](ko/translate.md) | [Français](fr/translate.md) | [Deutsch](de/translate.md) | [Español](es/translate.md) | [Italiano](it/translate.md) | [Русский](ru/translate.md) | [العربية](ar/translate.md)

[← Documentation](README.md)

# Translate C++ to NeverC

Core v2 now accepts the pinned embedded `<type_traits>` header for compile-time
type aliases and integral/enum constants. `std::remove_cv_t`, `std::is_same_v`,
supported construction/destruction traits and `std::integral_constant::value`
lower to existing core types and literals. The driver verifies and records all
101 consumed libc++/resource header hashes across the supported macOS, Linux and
Windows targets. Runtime trait support is limited to the checked function-pointer
`integral_constant` forms described below; other trait objects and trait storage
identities remain unsupported. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#compile-time-type_traits).

Core v2 also accepts the pinned embedded `<cstdint>` header. Fixed, least,
fast, pointer and maximum-width aliases plus the standard limit and constant
macros preserve the selected target's C++17 types and values. The driver records
the nine-file standalone closure, rejects quoted or C-header spellings, and
composes `<cstdint>` with `<type_traits>` in either include order. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#fixed-width-integers-from-cstdint).

Core v2 also accepts the pinned `<limits>` header. Integral/enum data members
and the standard zero-argument `numeric_limits` queries for admitted integers,
`float` and `double` fold to exact literals, including infinities and NaNs.
The 16-file closure is authenticated on all supported targets; runtime objects,
storage or method identity, object-qualified calls and `long double` remain
excluded. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#numeric-bounds-from-limits).

Core v2 also accepts the pinned `<cstddef>` header. Target `size_t`, `ptrdiff_t`
and `nullptr_t` aliases, `NULL`, checked `offsetof` and `max_align_t` layout
queries lower without libc++; `std::byte` storage, bitwise/shift operations and
`to_integer` lower directly to scalar operations. The 29-file closure is
authenticated on all supported targets, while quoted/C-header spellings, raw
builtins and standard-function addresses remain rejected. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#fundamental-types-and-bytes-from-cstddef).

Core v2 also accepts the pinned `<new>` header. Its 37-file closure is identical
and platform-free on all supported targets. `std::nothrow_t`, `std::align_val_t`
and the C++17 interference-size constants remain compile-time or scalar
metadata, while exact `std::launder` calls on admitted non-volatile object
pointers lower directly and evaluate their argument once. Exact standard
single-object and constant-array placement new expressions also reuse their
captured storage without a runtime call. Direct allocation-function calls,
nothrow objects, function addresses and default heap operations remain excluded.
[C++17](../utils/translate-frontends/docs/cpp-core-v2.md#new-header-from-new).

Core v2's pinned `<memory>` surface now resolves exact raw-pointer
`pointer_traits`, `std::allocator<T>` and
`std::allocator_traits<std::allocator<T>>` identities, nested aliases, rebinds
and trait constants as compile-time metadata. Exact
`uses_allocator<T, std::allocator<U>>` identities, inherited aliases and
compatible values also resolve. Exact single-object `std::default_delete<T>`
and unbounded-array `std::default_delete<T[]>`, including complete bounded
inner extents, use checked one-byte stateless carriers with default, copy/move
and admitted cv-converting construction. Their
call operators evaluate the receiver and pointer once, destroy the complete
object or reverse array elements, and call Clang's checked source-defined
scalar or array class/global delete. Exact single-object
`std::unique_ptr<T, std::default_delete<T>>` and unbounded-array
`std::unique_ptr<T[], std::default_delete<T[]>>`, including multidimensional
owners, use pointer-sized authenticated carriers. The same owners also admit a
source-owned by-value custom deleter when it is an empty, standard-layout,
trivial one-byte record with trivial special members and exactly one
source-defined `void operator()(pointer) noexcept`, optionally `const` and with
no ref qualifier. Default, null, raw-pointer, matching raw-pointer/null plus a
deleter argument bound to the exact deleter lvalue or rvalue parameter,
same-type move and const-adding default-deleter converting move construction,
corresponding move assignment, `nullptr`
assignment, `get`, mutable/const `get_deleter`, explicit boolean conversion,
`release`, `reset`, member and free `swap`, all six comparisons between
admitted owners with the same scalar/array category and qualification-compatible
raw pointers regardless of deleter specialization, all six
bidirectional `nullptr` comparisons and automatic destruction lower directly.
Single-object owners also admit `operator->` and dereference; array owners admit
`operator[]`.
Lowering preserves receiver/argument sequencing. Default deleters call the
exact checked source-defined scalar or array class delete selected by Clang,
or the matching checked global delete/delete[] when lookup remains global. An
admitted custom deleter is invoked once for a
non-null pointer and requires no global delete definition. Each admitted
nonstatic member accepts either an object receiver or an exact raw pointer to
that owner; pointer receivers retain pointee `const` and execute once. Exact
single-object
`std::make_unique<T>(args...)` and unbounded-array
`std::make_unique<T[]>(count)`, where `T` may have complete bounded inner
extents, with an integer constant
expression from zero through 65536 also lower directly. They authenticate the
pinned factory and selected allocation. A scalar factory calls the exact
source-defined global or class-specific `operator new` selected by Clang and
uses the owner's matching scalar delete path; an array factory calls the exact
source-defined global or class-specific `operator new[]` selected by Clang and
uses the matching array delete[] path. Factories value-initialize scalar
elements or call the exact source-owned non-template `noexcept` record
constructor, and install the resulting pointer in that owner. Multidimensional
construction uses the outer count and fixed inner extents while the checked
new[]/delete[] cookie path tracks flattened base elements; runtime array counts
remain rejected. Supplied constructor arguments and authenticated source-owned
trailing defaults are each evaluated once. Array factories evaluate those
defaults independently for every flattened base element.
Existing address operations, scalar and source-record destruction, scalar or
trivial source-record uninitialized construction, and nothrow zero-parameter
source-record default/value construction operations lower directly.
Uninitialized copy, fill and move also
call the exact source-owned non-template `noexcept` copy or move constructor
selected by the authenticated libc++ helper for complete source-owned records;
exact runtime allocator objects use a checked one-byte stateless carrier with
default/copy/move/converting construction, same-type assignment, heterogeneous
comparison, C++17 `address` and `max_size`. Exact C++17 allocator and
allocator-traits `construct` calls value-initialize writable scalars or call
checked source-owned `noexcept` record constructors, including exact
multi-argument, copy and move selection and authenticated source-owned trailing
defaults. Supplied arguments and defaults are each evaluated once. Exact
allocator and allocator-traits `allocate`/`deallocate` forwarders also lower
for complete default-new-aligned
elements when allocation uses a count proven within `max_size` and the
matching global new/delete definitions are source-owned. One-byte elements
accept runtime counts after conversion to target `size_t`; larger elements
accept nonoverflowing integer constant expressions or an implicit conversion
from an unsigned builtin type whose entire value range fits `max_size`.
Counts, hints, receivers and pointers retain one-time evaluation. Exact C++17
allocator `destroy` plus `allocator_traits` destruction, `max_size` and
copy-selection forwarding reuse the checked lifetime and stateless-allocator
paths without a libc++ call.
The authenticated 267-file closure is identical and platform-free across all
supported targets.
[C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Core v2 also accepts the pinned `<utility>` header. Scalar `move`, `forward`,
`move_if_noexcept`, `as_const`, `exchange` and `swap` lower directly. Exact
generic swaps of source-owned records invoke selected supported copy or move
construction, assignments and temporary destruction. Extra constructor defaults
retain their original parameter scope and finish their temporary cleanup before
the assignments, including native-array, array and pair element swaps. Scalar or
recursively admitted composite `std::pair` construction, assignment,
swapping, comparison, `make_pair` and `get`. Tuple metadata and
`integer_sequence::size()` remain compile-time values. The authenticated
87-file closure adds no libc++ runtime dependency; standard-function addresses
remain rejected.
Pair and tuple value elements also admit checked fixed-arity ordinary function
pointers, including nested pair, tuple and array fields. Construction,
assignment, factories, projection and swap preserve the pointer value; exact
compatible conversions include function-name decay, `nullptr`, removal of
`noexcept` and conversion to `bool`. Equality and inequality compare compatible
callback signatures. Original signature, alias, bound, exception and definition
source checks still apply.
Direct element-wise pair construction and `std::make_pair` also admit
source-owned nontrivial standard-layout record fields when the selected pinned
pair initializer constructs each such field through its source-owned copy or
move constructor. The factory's forwarding chain is authenticated. Pair
fields are destroyed in reverse order. Same-type whole-pair copy/move also
authenticates the defaulted pair constructor's member initializers and selected
source-owned element constructors; reference fields keep their bindings. Pair
converting construction from another admitted pair also selects and proves
each owned field's source-owned copy or move constructor. Value and reference
sources retain their checked cv and value categories; reference destination
fields retain their bindings. Selected element constructors can have extra
defaults with their original parameter scope and per-use source checks. Each
member initializer destroys its default temporaries before the next field.
Direct construction and `make_pair` evaluate all caller arguments once before
constructing fields, preserving lvalue aliases, captured prvalue values and
outer temporary lifetimes. Array factory arguments decay to element pointers.
Same-type and compatible heterogeneous pair
assignment also proves the pinned pair body and invokes each selected
source-owned field assignment in order, including writes through reference
fields. Member and free pair swap also accept nontrivial source-owned fields
when the pinned swap selects supported move construction and assignments.
They destroy each swap temporary; reference fields keep their bindings,
including self-swap.
Reference-valued pairs additionally support exact compatible direct
construction, same-type copy/move construction and index- or unique-type
`get`; same-type assignment writes through their stored bindings without
rebinding either pair. All six comparisons read referent values and retain the
ordinary heterogeneous scalar comparison rules. Member and free swap exchange
referent values without changing pair bindings. Compatible converting
construction binds source pair fields, while heterogeneous assignment converts
field values before writing through destination references. `make_pair` unwraps
authenticated `ref`/`cref` arguments into the corresponding reference fields,
including one-reference/one-value results. Mixed reference/value pairs also
support exact compatible direct construction, same-type copy/move construction,
public field access, index- or unique-type `get`, and same-type or compatible
heterogeneous assignment with per-field reference/value behavior. All six
comparisons use the same recursive heterogeneous value rules. Member and free
swap exchange per-field values without changing reference bindings. Compatible
converting construction binds reference fields and initializes value fields.
Ordinary value pairs also support heterogeneous construction and assignment
from value, reference or mixed pairs, copying or converting each field into
independent storage. Assignment writes `first` before `second` and returns the
destination by reference. Authenticated `std::reference_wrapper` values
are also admitted as pair fields, including nested pairs. Construction
and copying retain the wrapped binding; assignment copies bindings without
assigning the referred-to objects. Member/free swap exchanges wrapper bindings
only after authenticating both selected element swaps and their nested pair
or array operations. All supported pair and array swaps, including source-record
leaves, require this recursive proof. Array siblings are supported; zero-length
arrays skip element swaps while evaluating both operands once. User ADL swaps
and source specializations remain rejected. Optional member/free swap also
authenticates engagement tests, value projections and both one-engaged transfer
paths, including the selected trivial construction and reset operations.
Nested wrapper pairs and tuples are supported;
user ADL swaps and source operation specializations remain rejected.
Comparisons with wrapper-valued leaves remain rejected.
[C++17](../utils/translate-frontends/docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility).

Core v2 accepts authenticated empty and nonempty `<tuple>` values, including
construction, assignment, factories, swaps, comparisons and `get`. `std::tie`
and `std::forward_as_tuple` create authenticated reference tuples whose `get`
and `apply` operations preserve aliases, value categories and writes to the
bound objects. `std::make_tuple` unwraps authenticated `ref`/`cref` arguments,
including mixed reference/value results with exact compatible direct and
same-type copy/move construction, index- or unique-type `get`, and same-type or
compatible heterogeneous assignment from tuples or pairs. All six comparisons
use the same recursive heterogeneous value rules. Member and free swap exchange
per-element values without changing reference bindings. Compatible converting
construction from tuples or pairs binds reference elements and independently
initializes value elements. `std::apply` passes reference elements as their
referents and value elements through the existing checked callable boundary.
Direct element-wise construction and `std::make_tuple` also admit source-owned
nontrivial standard-layout records when the selected libc++ leaf copy or move
constructor matches the supplied source and value category. The factory
authenticates forwarding into that constructor, including mixed owned values
and `ref`/`cref` elements. Same-type whole-tuple copy/move construction also
accepts these owned records when the defaulted libc++ tuple, implementation,
and leaf constructors select supported source-owned element constructors.
Mixed scalar and reference elements retain their values and bindings, and an
rvalue may select an element's copy constructor when no move is available.
The source tuple is evaluated once; owned fields are constructed in element
order and destroyed in reverse order. Selected element constructors may have
extra defaults with their original parameter scope and per-use source checks.
Each leaf member initializer finishes default temporary cleanup before the
next element. Direct construction and `make_tuple` evaluate all caller
arguments once before constructing elements, retaining aliases, captured
prvalues and outer temporary lifetimes. The same default and lifetime rules
apply to selected result constructors in `tuple_cat`, including array sources.
Other owning tuple constructors remain
restricted.
Ordinary value tuples may likewise copy or convert referent values from
all-reference or mixed tuple/pair sources during construction and assignment.
Assignment stores independent values in element order and returns the
destination tuple by reference.
Reference tuples may also be constructed directly from exact
compatible references and copy-constructed while preserving their bindings.
Same-type assignment writes through those bindings element by element. Exact
compatible converting construction preserves references to source tuple
elements, and heterogeneous scalar assignment converts source referent values
before writing through destination bindings. Recursive comparisons read the
referents and use the same heterogeneous scalar leaves as value tuples. Pair
conversion binds compatible pair fields during construction or writes their
converted values through destination references during assignment. Member and
free swap exchange referent values element by element without changing tuple
bindings. Every tuple swap authenticates the selected SDK implementation,
indexed leaf projections and element swaps; ordinary nested pairs containing
reference wrappers are supported. Wrapper values exchange their bindings, while
user ADL swaps and source specializations remain rejected. Exact `std::apply` accepts authenticated tuples, value/reference/mixed
pairs, and arrays whose elements satisfy the existing composite value and
callback rules. Named functions, stored function pointers, source and standard
function objects, reference wrappers, source member pointers and `mem_fn`
wrappers use the same checked parameter and result boundary. Mutable and const
lvalues, rvalues and materialized temporaries preserve element qualification
and reference categories. Const lvalue-reference parameters also bind xvalues
of the same unqualified type through `apply`, `invoke`, reference-wrapper calls
and member adapters. These bindings retain the original storage and temporary
lifetime; returning a reference does not extend it. Empty arrays call a nullary
callback after evaluating the array expression; nested array elements may bind
admitted array-reference parameters. By-value SDK callback parameters and
results remain excluded.
The callable and source object are each evaluated once, and the selected
`get` operations require exact SDK declaration, index and forwarding proof
before lowering to a scalar operation, member projection or ordinary call.
Source-owned record value parameters also use the selected copy or move
constructor for owned or reference tuple/pair fields and array elements,
including authenticated extra constructor defaults. Their default temporaries
survive the callback and parameter destruction, then finish inside the SDK
invocation before the caller continues. Caller-created callable and source
temporaries, reference results and returned records retain their outer lifetime.
The same selected by-value constructor and extra-default handling applies to
`std::invoke` on functions, function pointers, source function objects, member
functions and `mem_fn`, and to direct or invoked `reference_wrapper` calls.
All caller arguments are evaluated before the SDK constructs callback parameters;
constructor-default temporaries survive the call and parameter destruction, then
finish before the caller continues. Caller temporaries and returned values retain
their outer lifetime, and unevaluated queries keep their separate source rules.
SDK calls retain argument bindings before invocation, then read forwarded scalar
values, function-pointer variables, wrapper bindings and receiver pointers after
all caller arguments have been evaluated. The selected function pointer is
captured before callback parameter construction. Prvalue callables keep their
captured value, and ordinary indirect calls retain their callee-before-arguments
evaluation rule.
By-value callback arguments consistently accept checked function-to-pointer decay,
removal of `noexcept` from compatible function pointers, `nullptr` to an admitted
function pointer, and function or function-pointer values to `bool`. These rules
apply to functions, function objects, member-function and `mem_fn` adapters, and
direct or invoked reference wrappers through `invoke` and `apply`. A null function
pointer converts to `false`. Top-level `const` on a value parameter does not alter
the conversion; original signatures and exact reference bindings remain checked.
Checked ordinary function-pointer values also use `std::optional` and
`std::vector` storage, including `noexcept` signatures and admitted composite
and nested vector elements. Construction, copying, mutation and swap preserve
pointer values. Optional conversions preserve empty sources and convert contained
pointer values; callback equality accepts compatible signatures, function names
and `nullptr`. Vector emplace accepts checked callback conversions and preserves
aliased inputs through growth and shifts. Callback ordering, throwing optional
access and the existing vector allocation-definition requirement retain their
separate boundaries. The source predicate objects of `find_if`, `find_if_not`, `none_of`, `all_of`,
`any_of`, `count_if`, `copy_if`, `remove_copy_if`, `is_partitioned`,
`partition_point`, `partition_copy`, `replace_if`, `replace_copy_if` and
`remove_if` accept checked conversion from a compatible `noexcept` function
pointer to an ordinary function pointer parameter; element, replacement and
output storage types remain exact.
See the [optional](../utils/translate-frontends/docs/cpp-core-v2.md#value-optionals-from-optional)
and [vector](../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector)
contracts.

`std::equal_to`, `std::not_equal_to`, `std::logical_and`, `std::logical_or`
and `std::logical_not` support ordinary function-pointer values, including
`noexcept` signatures and typed `const` arguments. Transparent calls accept
function designators, compatible signatures and `nullptr` paired with a callback.
Stored objects and `std::invoke` preserve checked conversions, operand effects,
aliases and temporary cleanup. Logical calls evaluate both arguments; callback
targets are never invoked, and callback ordering retains its existing boundary.
See the [functional objects](../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional) contract.

The 14 unary predicate algorithms also accept pinned `std::logical_not<F>`
and transparent `std::logical_not<>` for function-pointer elements, including
`noexcept` signatures, typed `const` and permitted const input. The selected
parameter preserves compatible removal of `noexcept` and checked pointer
temporaries. Element, replacement and output storage remain exact; empty ranges
and caller temporary cleanup retain their timing, and callback targets are not
invoked.

Typed and transparent `std::equal_to`, `std::not_equal_to`, `std::logical_and`,
`std::logical_or` and `std::logical_not` also support direct and `std::invoke`
calls used only in `decltype`, `noexcept` and constant `sizeof` expressions.
The pinned, substituted SDK signature supplies the Boolean result without
instantiating unused adapter or operator bodies. Supported scalar and callback
conversions retain their exact parameter and exception types. Caller aliases,
operands, selected defaults and temporary cleanup keep their source checks;
these queries do not evaluate them. Independent SDK function addresses and
substituted declarations retain their existing restrictions.

`std::invoke` queries around supported functions and function pointers now
also work when no evaluated invocation has instantiated an SDK adapter body.
Scalar, `void` and reference parameters or results retain the original
function signature, argument conversions and `noexcept` specification; record
values still require the existing materialized invocation path. The query
checks the pinned SDK result and dispatch declarations without executing the
callable, defaults or temporary cleanup. Caller definitions, written aliases
and operand sources keep their existing checks.

Typed and transparent `std::logical_and`, `std::logical_or` and
`std::logical_not` also accept supported complete object pointers, `void*` and
`decltype(nullptr)` values, including admitted `const` types. Direct calls,
`std::invoke` and their type queries keep the same Boolean conversions and
argument evaluation. The fourteen supported unary predicate algorithms accept
pinned `std::logical_not` on object-pointer and `void*` ranges. Incomplete or
volatile pointees, user-defined conversions and replaced SDK declarations
retain their existing checks.

Source-owned ordinary function declarations also provide metadata for direct
calls and `std::invoke` queries without a definition when the whole declaration
family has no runtime use. Supported scalar, `void`, reference and callback
results retain their original signatures. Aliases, array bounds, exception
expressions, selected defaults and temporary cleanup remain checked without
executing query operands. Runtime calls and stored function addresses still
require definitions; C exports and member declarations keep their existing
requirements.

`std::declval<T>()` now supplies admitted scalar, record, array, callback and
`void` type metadata in unevaluated queries. Reference collapse, original
aliases and function signatures remain checked through the pinned public and
selected internal SDK declarations. Member access, result traits, `noexcept`
and constant `sizeof` queries execute no object construction, destruction or
callback. Independent SDK function addresses, replaced declarations and
unsupported written types retain their existing restrictions.

Unevaluated `std::invoke` queries for supported source-owned function objects
now use the actual selected `operator()` signature even when the SDK adapter
body has not been generated. Lvalue, const and rvalue overloads retain their
exact result and exception sources, including scalar, callback, array-reference,
record-reference and `void` results. The selected source method still requires
a fully checked definition. Query operands execute no callbacks, construction
or cleanup. By-value record parameters/results and lazy source method templates
retain their separate adapter requirements.

All 19 supported arithmetic, bitwise, comparison and logical function-object
families now supply signatures for direct and `std::invoke` unevaluated queries,
for both typed and transparent objects. Queries preserve narrowing return types,
integer promotions, mixed numeric types and admitted object-pointer comparisons.
They execute no operands, defaults, conversions or temporary cleanup, including
queries of division or remainder by zero. SDK declarations, exception metadata
and caller-written sources remain checked. Pointer arithmetic and overloaded
source operators retain their separate requirements.

Admitted `reference_wrapper` calls and outer `std::invoke` queries can now
use signatures without first calling the wrapper. Function and function-pointer
referents, supported SDK function objects and defined source-owned call operators
retain their exact scalar, reference or `void` results and exception sources.
The pinned private result trait and `get` declaration remain checked alongside
wrapper storage, caller operands and source definitions. Queries execute no
callbacks, defaults, conversions or temporary cleanup. By-value record signatures
and lazy source method templates keep their separate adapter requirements.
Exact `move`, `forward`, `as_const` and reference-template `move_if_noexcept` casts retain the wrapper’s pinned storage and original operand sources.

Pure signature queries also cover admitted `std::hash<T>` carriers for integral
and null-pointer types, wide integers, `float`/`double`, supported enums and
pointers. Direct calls, `std::invoke`, `reference_wrapper` calls and outer
invoke retain the exact `std::size_t` result and declared `noexcept` signature
before any hash call. The pinned public carrier, selected SDK method family,
inherited public-to-base view and actual argument conversion are checked
together. Caller aliases, operands, defaults, source definitions and temporary
cleanup retain their own checks; runtime hashing uses its existing operation
proof. Namespace and block imports of `std::hash` and `std::invoke` authenticate
their pinned lookup families; each actual type and call retains its own source
proof.

Windows builds preserve the Microsoft SDK’s original internal `log10`/`pow`
templates and isolate their compiled symbols, retaining SDK-specific calculations
and floating-state behavior. The actual SDK and runtime differential checks
require the implementing revision’s native CI.
Symbol isolation also restores MSVC’s original non-associative section-record
numbers in ordinary COFF and bigobj archives, keeping the final auxiliary
records byte exact.
[Windows ABI](../utils/translate-frontends/docs/design.md#private-windows-math-templates).

Exact `tuple_cat` accepts zero arguments or value, reference and mixed-reference
tuple/pair sources plus scalar and recursively composite arrays, evaluates all
sources once before reading their elements, and constructs the exact
concatenated tuple directly while preserving reference bindings. Nontrivial
source-owned standard-layout array elements use the copy or move constructor
selected by the pinned libc++ result construction; their result tuple destroys
elements in reverse order. Such result tuples can themselves supply owned
elements to another `tuple_cat`. This preserves lvalue copies, rvalue moves,
reference bindings and temporary cleanup through nested concatenation. They
also supply reference and selected by-value callback arguments to `std::apply`,
with the same source categories and parameter lifetimes as owned arrays.
[C++17](../utils/translate-frontends/docs/cpp-core-v2.md#value-tuples-from-tuple).

Authenticated `std::array` objects, including zero-length and nested arrays,
also compose with reference tuples and pairs, `ref`/`cref`, `tie`,
`forward_as_tuple` and the checked `apply`, `invoke`, reference-wrapper,
member-pointer and `mem_fn` call forms. Reference parameters and results retain
object identity, qualification and temporary lifetime; assignment through tuple
or pair bindings writes array values without rebinding. Callable definitions
retain their source-owned checks. By-value SDK callback parameters/results,
volatile array elements and user `std::array` specializations remain excluded.
Source-owned nontrivial standard-layout elements may be forwarded by reference
or copied into source-owned by-value callbacks and `tuple_cat` results through
their selected constructors. The resulting tuple can be concatenated again or
used as an `apply` source; other owning tuple operations remain restricted.
Member/free array swap also admits these elements when the pinned iterator
chain selects supported source-owned move operations. Each element's swap
temporary is destroyed before the next element; empty arrays perform no
element operation.
Array `fill` and raw-pointer `std::fill`/`fill_n` admit source-owned record
elements when the pinned fill chain selects a supported copy assignment. Each
write reads its bound source at that point, including an aliased array element.
Defaulted `std::array` copy/move assignment also runs the selected source-owned
element assignment in index order when Clang generates that exact array loop.
Defaulted `std::array` copy/move construction likewise runs the selected
source-owned element constructor in index order when Clang generates its
single-field array initializer. The source array is evaluated once; a move
may select an element copy constructor.
[C++17](../utils/translate-frontends/docs/cpp-core-v2.md#fixed-value-arrays-from-array).

Array layout also supplies authenticated evidence for type queries, including
direct classifications, reference binding and `decltype` of source-owned calls
returning array references and exact admitted `std::get` calls. Aggregate local
initializers and array temporaries retain recursive element destruction checks,
including zero extents and potentially throwing trivial destructors. Constant
`get` results can supply dimensions without executing query operands. Written
arguments and expressions remain checked; other SDK callees, construction,
nothrow-destruction roots and enum-source proofs remain separate.
[C++17](../utils/translate-frontends/docs/cpp-core-v2.md#fixed-value-arrays-from-array).

Core v2 type metadata now accepts owned incomplete non-union classes, including forward declarations and uninstantiated template types. Classification and array dimensions retain exact source identity without generating record storage; runtime carriers and callbacks still require complete admitted types. Reference/pointer operation queries can use these identities with exact retained source proof; selected lazy method return signatures keep their complete-carrier checks. Native verification requires the implementing revision’s CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#incomplete-record-type-metadata).

Core v2 operation queries now inspect unknown-bound arrays through checked type metadata. Construction/destruction short circuits, exact reference bindings and array-to-pointer conversions preserve their retained source evidence; runtime storage remains restricted. Native verification requires the implementing revision’s CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#unknown-bound-array-operation-types).

Core v2 accepts checked declaration-only namespace template signatures in unevaluated calls, including the reference, array, function and void fallback pattern used by `declval`. Exact selection, erased template defaults and consumed function defaults retain a final source check. Runtime calls and addresses still require definitions. Native validation requires the implementing revision’s CI; standard headers and complete C++/STL remain unfinished. The same source check covers unused namespace templates with owned definitions; their bodies stay lazy. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#unevaluated-declaration-only-template-signatures).

Core v2 implements default sized-delete forwarding: an untouched implicit global sized delete/delete[] can call the corresponding source-defined unsized operator. Explicit sized definitions and original array cookie layout remain authoritative. This adds no default allocator or exception runtime; native verification requires the implementing revision’s CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#single-object-allocation-and-placement-reuse).

Core v2 now accepts ordinary function types and their direct lvalue/rvalue references in templates, aliases, queries and transforms, including function-pointer deduction. Original signature sources, adjusted array bounds and noexcept dependencies remain checked. Local function-reference variables, fields, parameters and results reuse the checked callback carrier, with the original signature and source-definition checks. Cross-platform native verification requires CI. Function rvalue references also preserve function-lvalue semantics in invoke, reference wrappers, pair/tuple construction, get and apply; a call declared to return F&& has the query result F&. Static function references also support constant addresses, ordered dynamic startup, initialization once on first use, and admitted record/pair/tuple fields; TLS and invalid bindings remain rejected. Checked ordinary function addresses and function lvalue references can also be non-type template arguments, with exact declaration identity, signature and original-source checks for defaults, auto parameters and bounded packs. Pinned integral_constant function-pointer values now retain checked function addresses for callback calls, invoke/apply and value elements in pair/tuple construction and make_pair/make_tuple, without exposing trait storage identity. Checked function-pointer integral_constant objects also support one-byte empty storage, copy/move/assignment, exact conversion and zero-argument value calls, and zero-argument invoke, while preserving receiver effects. These objects also support pair value elements through direct construction, make_pair, copy/move/assignment and mixed-reference pairs. Tuple value elements are now supported through direct construction, `make_tuple`, copy/move/assignment and mixed-reference tuples. Checked empty-base projections preserve native layout, element addresses and argument side effects without writing over neighboring values. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#bare-function-type-metadata).

Checked function-pointer trait objects in tuples also support member `swap` and `std::swap`. Value elements exchange values, reference elements exchange their referent values without rebinding, and empty elements retain their addresses without field stores. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Checked empty function-pointer trait elements may also share a tuple with supported source-owned objects. Direct construction, `make_tuple`, and whole-tuple copy/move preserve selected source constructors, default-argument effects and destruction. Repeated empty elements retain distinct addresses. Assignment and swap still require their existing element lifecycle checks. These tuples also compose through `tuple_cat` and `apply` to checked source callables. By-value trait parameters have independent empty storage; selected owned copies/moves and their cleanup retain their existing checks. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Checked function-pointer trait objects preserve reference identity through `invoke` and `apply` lvalue, const and rvalue parameters, reference results, reference tuple bindings and wrapper access. Existing cv and value-category checks remain enforced; other trait domains and SDK `value` storage aliases stay excluded. `invoke` also accepts these traits by value in checked source functions, call operators, member calls and wrapped callables. Parameters have independent empty storage; selected owned construction, default-argument effects and cleanup remain checked. `invoke` and `apply` also accept these checked trait objects as by-value results, using the existing result destination and source-body checks. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Checked function-pointer trait objects also serve as `array` elements. Initialization, const access, copy/move, nested and zero-sized arrays, `fill`, and member/free `swap` retain existing storage and operation checks. Distinct elements keep distinct addresses; volatile elements and other trait domains remain excluded. Unused `value` initializers remain lazy for default carriers and empty arrays; actual `value` expressions keep their existing evaluated-constant checks. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Admitted empty standard function objects and checked callback trait objects also support default construction in fixed raw arrays, including const and multidimensional arrays. Existing element construction gives each object its own storage and enforces array bounds. Local, static and global arrays, constant-size `new[]` and nested-vector range sources use the same constructor proof. Source callback definitions, allocation/deallocation and unsupported trait or functor domains retain their existing checks. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Checked `reference_wrapper` arguments now bind matching supported lvalue-reference parameters through `invoke` and `apply`, including explicit tuples of wrapper values. The exact pinned SDK conversion and forwarded parameter are authenticated. Calls preserve the original referent address, writes and const qualification. Checked function and function-pointer wrappers also supply ordinary callback value parameters, including compatible `noexcept` removal. Values are read after caller operands, preserving later updates; null pointers remain values. Arbitrary source conversions, function-wrapper-to-boolean conversions and volatile referents remain excluded. Checked arithmetic and object-pointer wrappers also supply admitted scalar value parameters, including arithmetic-to-bool and qualified pointer conversions, through the same invocation paths and delayed read. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

Typed standard function objects also accept checked scalar wrapper operands through `invoke`, `apply` and direct or invoked wrappers around the callable. Arithmetic, comparison and logical operations use the selected parameter conversions, including floating, narrowing and arithmetic-to-bool conversions. The exact SDK wrapper conversion and forwarding flow remain authenticated. Caller operands complete before referents are read, preserving later updates and single evaluation. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

Transparent standard function objects also accept checked wrappers of arithmetic, object-pointer and admitted function-pointer operands in direct calls, `invoke`, `apply` and wrapped callable calls. The pinned SDK conversion must unwrap the exact forwarded parameter before the selected built-in operation. Promotions, pointer comparisons and callback logical operations retain existing type checks; callback targets are not called. Caller arguments finish before referents are read, preserving later updates and single evaluation. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

Checked transparent standard-object calls with wrapper operands also support `decltype` and `noexcept` queries, including outer `invoke`. The proof follows the exact SDK conversion and forwarding chain in the selected built-in noexcept expression without instantiating an unused operator body. Queries execute no caller effects; source conversions, replacements and unsupported referents retain their checks. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

Checked `reference_wrapper` values also serve as `array` elements. Explicit initialization, const access, copy/move, assignment, `fill` and checked swap preserve or exchange wrapper bindings without assigning referents. Nested and zero-sized arrays keep existing layout checks; filling from an element captures its binding before writes, and empty fill still evaluates its argument once. Original referent types and source targets remain checked; comparisons with wrapper leaves and user ADL swap retain their restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#fixed-value-arrays-from-array).

`vector` also admits checked `reference_wrapper` value elements with the pinned trivial copy, move, assignment and destruction properties. Construction, copy/move, assignment, value emplace, insertion, fill, erasure, growth and swap preserve wrapper bindings and existing allocation checks. Aliased element values are retained across growth; destroying wrappers does not destroy referents. Const referents, checked callbacks and source-owned record referents keep their own checks. User conversions and comparisons of wrapper elements retain their separate boundaries. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

Checked wrapper vectors also support `emplace_back(referent)` and positional `emplace(pos, referent)` from an exact lvalue referent. Every selected SDK forwarding branch, allocator adapter and wrapper constructor remains authenticated. Binding stores the referent address, evaluates the argument once, and retains existing growth and insertion behavior. Const binding and supported function, callback, array and source-record referents keep their checks; user conversions and source replacements of SDK adapters remain rejected. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

Checked wrapper optionals also support `emplace(referent)` from an exact lvalue referent. The selected optional reset, storage address, forwarding, placement construction, direct wrapper constructor and engaged flag are authenticated together. Empty and engaged optionals bind the original address, evaluate the argument once and return the stored wrapper reference. Const, function, callback, array and source-record referents retain their checks and lifetimes; user conversions and source replacements of SDK operations remain rejected. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#value-optionals-from-optional).

Checked `make_unique<reference_wrapper<T>>` also constructs from an exact lvalue referent or copies/moves an exact wrapper value. The selected SDK factory, allocation, forwarding, wrapper constructor and owner construction remain authenticated. Arguments are evaluated once before allocation; wrapper copies read their source after allocation, preserving allocation callback effects. The owner destroys wrapper storage without destroying the referent. Const bindings and supported referents retain their checks; arbitrary user conversions, SDK source replacements and default wrapper construction remain restricted. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure single-object `make_unique<T>` result queries now authenticate the exact pinned factory signature and pointer-sized owner layout without requiring instantiated allocation, construction or deletion bodies. `decltype`, `sizeof`, `alignof` and `noexcept` evaluate no arguments and make no ownership calls; the factory remains potentially throwing. Supported element and operand types, source bodies, explicit arguments and SDK provenance remain checked. A query grants no runtime construction permission. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `make_unique<T[]>` queries now authenticate the exact unbounded-array overload, its `size_t` parameter and the checked array-owner layout without instantiating allocation or element construction. Dynamic or large outer extents remain unevaluated metadata and select no array allocation, cookie or cleanup. Bounded inner dimensions, element types, source count bodies and SDK provenance remain checked. Evaluated factories retain their constant extent limit and constructor/deleter requirements. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `unique_ptr` observation queries now accept the exact pinned member signature even when its body and default deletion body have not been instantiated. This covers `get`, mutable/const `get_deleter`, boolean conversion, scalar arrow/dereference and array subscript, preserving result qualification and resolved exception metadata. Receiver and index expressions stay unevaluated; their source, selected defaults, owner layout and SDK provenance remain checked. Evaluated operations and query temporaries retain their separate lifetime proofs. Independent member addresses and SDK source replacements remain rejected. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `unique_ptr` observation queries can also complete the exact pinned `reference_wrapper` element layout when it is still lazy. Pointer results from `get` and scalar arrow keep the wrapper type, constness and pointer-sized ABI without instantiating wrapper constructors, getters or deletion bodies. Scalar, array, function, callback and source-record referents retain their own source and type checks. SDK partial specializations, source replacements and unsupported referents remain rejected. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `unique_ptr::release()` result queries now authenticate the exact pinned signature without requiring its body or a default deletion body. Mutable borrowed scalar and array owners preserve raw pointer type, element qualification and bounded inner dimensions; checked custom deleters keep their source requirements. Queries evaluate no receiver, clear no owner and select no pointee construction or cleanup. SDK wrapper element layouts use the same authenticated completion proof. Evaluated release and other ownership operations retain their separate runtime proofs. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `unique_ptr::reset()` queries now authenticate the pinned signature without instantiating reset or default deletion bodies. They preserve `void` and resolved exception metadata for checked scalar, array and custom-deleter owners. The exact SDK zero-pointer or `nullptr` default is proved at its specific query use. Receivers, replacements and defaults remain unevaluated; actual reset and cleanup retain their independent runtime proofs. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure member `unique_ptr::swap()` queries now authenticate the exact pinned signature without instantiating member or default deletion bodies. Checked mutable owner lvalues preserve `void` and nonthrowing metadata for scalar, array, wrapper and custom-deleter owners. Both operands remain unevaluated and no pointers are exchanged. Source and lifetime checks remain independent; evaluated ownership operations keep their existing complete proofs. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure free `std::swap(unique_ptr<T, D>&, unique_ptr<T, D>&)` queries now authenticate the exact pinned overload without instantiating its wrapper, member or default deletion bodies. Two mutable lvalues of the same checked owner type retain `void` and nonthrowing metadata, including scalar, array, wrapper and custom-deleter forms. Deduced and explicit template arguments keep their original source checks. Neither operand executes and no pointers are exchanged; evaluated operations and independent function addresses keep their existing complete proofs. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `unique_ptr::operator=(nullptr_t)` queries now validate the exact pinned signature without instantiating assignment, reset or default deletion bodies. Operator syntax and explicit member calls preserve the owner lvalue-reference result and nonthrowing metadata for scalar, array, wrapper and custom-deleter owners. Receiver and null operand stay unevaluated, with no deletion or pointer store. Source and lifetime checks remain independent, and evaluated assignment keeps its full deletion proof. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure same-type and const-adding `unique_ptr` move-assignment queries now authenticate exact pinned signatures without assignment, release, reset or default deletion bodies. Scalar, array, multidimensional and wrapper owners preserve the destination `Owner&` result and nonthrowing metadata. Both checked owner layouts and original operand sources remain required. Queries execute neither operand, transfer no pointers and select no deletion; evaluated move assignment keeps its complete proof. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure owner-to-owner `unique_ptr` equality and inequality queries now authenticate exact pinned signatures without comparison, getter or default deletion bodies. Scalar, array, qualified, wrapper and custom-deleter owners retain `bool` and the C++17 potentially throwing signature. Both checked layouts, explicit template arguments and operand sources remain required. Queries execute neither operand nor compare pointer values; evaluated comparisons keep their complete wrapper/getter proof. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure owner-to-owner `unique_ptr` ordering queries now authenticate the exact pinned `<`, `>`, `<=` and `>=` signatures without wrapper, getter, comparator or default deletion bodies. Checked scalar, array, qualified, wrapper and custom-deleter owners retain `bool` and potentially throwing C++17 metadata. Original operands and explicit template types remain checked. Queries read no pointers and execute no operands; evaluated ordering keeps its full comparator and total-order proof. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `unique_ptr`/`nullptr` comparison queries now authenticate exact pinned signatures in both operand orders without wrapper, getter, Boolean conversion, comparator or default deletion bodies. Equality and inequality retain `bool` and `noexcept`; `<`, `>`, `<=` and `>=` retain `bool` and potentially throwing C++17 metadata. Checked scalar, array, qualified, wrapper and custom-deleter owners keep their original owner and null sources. Queries execute neither operand nor select deletion; evaluated comparisons retain their complete proofs. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Pure `default_delete` call-result queries now authenticate the pinned `void` and `noexcept` signatures without a deletion body. Scalar, qualified, array, multidimensional and checked wrapper pointees retain the empty one-byte deleter layout and original receiver, pointer and template sources. No pointer is read, no lifetime or deallocation is selected, and operand effects remain unevaluated. Evaluated deletion keeps its complete body and lifetime proof. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Checked function-pointer trait wrappers also provide exact by-value trait parameters through `invoke`, `apply` and direct wrapped calls, including const referents and xvalue wrappers. The pinned trait copy and wrapper conversion remain authenticated. Each parameter has independent one-byte storage; callable and argument effects occur once, and the original source function targets remain checked. Other trait domains and SDK `value` storage aliases retain their restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Checked wrappers of exact source-owned standard-layout records also provide by-value parameters through `invoke`, `apply` and direct wrapped calls. The selected source copy constructor, its defaults and dependencies remain checked. Parameters have independent storage; later argument updates are observed before copying, and selected default effects and parameter cleanup retain their invocation lifetime. An rvalue wrapper still supplies its lvalue referent. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

Checked zero-argument `optional::emplace()` now value-initializes admitted non-const scalars, trivial source-owned records, checked function-pointer trait objects and recursively admitted arrays of these elements. The pinned reset, placement construction and reference-return bodies are authenticated. Re-emplacement preserves contained storage identity; scalar and pointer values reset to zero or null. Nontrivial default constructors, default member initializers and volatile elements remain outside this path. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#value-optionals-from-optional).

Lazy function-pointer `integral_constant` specializations now retain type identity for unused aliases, type transforms, compatible type queries and nested pair, tuple and reference-wrapper metadata. The exact pinned primary initializer, declaration arguments and source function signatures are checked without completing the trait or emitting trait storage. Source function bodies retain their independent profile checks. Actual objects, callback addresses and SDK `value` storage aliases keep their existing definition, layout and operation checks. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Core v2 unary type transforms now check original inputs, actual substituted sources and results, including aliases whose template parameters disappear from the final type. Sixteen pinned kinds reuse existing type metadata and runtime carriers. Native verification remains CI-only. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#unary-type-transforms).

Core v2 also supports checked `__array_rank` and `__array_extent` with nonnegative constant integer indices, including template indices for fixed array types. Both operands remain inspected before folding; unsupported array types and implicit class-conversion indices remain excluded. Native results require the implementing revision’s CI. Unknown-bound arrays such as `int[][3]` now retain rank, zero outer extent and known inner bounds in type-only metadata, aliases and template arguments. Runtime types and operation queries keep their separate restrictions; standard headers and full C++/STL remain unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#array-type-queries).

Core v2 adds checked builtin type classification, including `__is_integral`, `__is_pointer` and `__is_same`. Queries preserve C++ type identity and inspect written operands before producing a boolean. Unsupported operand types and other trait kinds remain restricted. O0/O2 and relocation checks require the implementing revision’s CI; standard headers and complete C++/STL remain unfinished. Record queries also cover aggregate, empty, standard-layout, trivial, trivially-copyable and POD properties, plus polymorphic/abstract status and `__is_base_of` for admitted types. Standard `final` classes and the `__is_final`, `__is_literal` and `__has_unique_object_representations` queries are also covered. Source padding, empty-class representation and C++17 literal rules remain authoritative; other attributes and virtual methods retain their restrictions. Destructibility and trivial destruction now also cover admitted records, including private/deleted destructors and references, while preserving lazy template bodies and exception specifications. Concrete classification and array queries now validate their complete type/value source dependencies before lowering, including alias bounds and cached or generated constants, even for false or out-of-range results. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#builtin-type-classification).

Core v2 adds construction, assignment, conversion and destruction type queries for admitted non-record operands, including pointers to admitted records, references and fixed arrays. Written types and selected defaults remain checked without executing operand effects. Retained record operation roots admit implicit trivial operations and ordinary operations with completed definitions, converted arguments and checked destruction. Construction can also use unchanged ordinary defaults with exact parameter and lifetime source checks; nothrow defaults additionally require already-resolved actual callee exception specifications. Record-value and fixed-array nothrow destruction now retain the selected destructor and resolved prototype, checking implicit, ordinary explicitly defaulted or completed ordinary user destruction source; deleted/access and reference short circuits preserve their original results. Implicit trivial construction uses the same separate destruction proof. Already materialized template operations with checked original inline definitions also qualify. Unmaterialized bodies, separate template definitions, copied member-template origins and unresolved exception dependencies still need source evidence. Native O0/O2, relocation and eight-ABI results require implementing CI; standard headers and full C++/STL remain unfinished. A terminal call result inside `decltype`, including parentheses and built-in comma-right operands, preserves lazy destruction; arguments and callee source remain checked. Rebuilt exception expressions retain both source identities; referenced inline friends keep unused bodies lazy after exact source and concrete signature checks. Query source can use checked defaulting through completed generated operations or complete retained subobject selections, including original signatures, defaults, layout and destruction source. Inline template defaulting additionally requires actual defaulting, completed concrete signatures and the exact inline origin; an actually visited retained destructor query can trigger the first concrete signature check, while unused template bodies stay lazy. Checked defaulted constructors and assignments also qualify as query roots; complete retained selections can prove a query-only operation without generating its body. Selected ordinary defaults can use the same proof for generated construction and assignment, including selection of declarations preceding their defaulted definitions. Already instantiated defaults of checked concrete template constructors also qualify after exact parameter and initializer source completion; unselected defaults remain lazy. The consumed owning base, field and array graph can also complete the first check of existing resolved defaulted destructor signatures, preserving the root query result. A complete retained record-prvalue result of a construction, conversion or assignment query can supply the same owning-signature checks; reference results do not consume referent destruction. Construction, assignment and conversion queries can compose checked ordinary class-template constructor and operator signatures while their unused inline bodies stay uninstantiated. Each exact selected expression in the complete retained operation needs its own proof, including nested conversions and constructor arguments; defaults retain their separate source checks, and consumed record temporaries retain full owning destruction checks. Authorized ordinary destructor signatures can supply query-source dependencies. Real calls and destruction still require checked bodies. Member-template constructors, assignments and conversions also require exact retained selection and template-argument source, including a separate proof of consumed defaults even when the body already exists. Copied primaries also require a bounded original declaration chain and exact outer/inner argument identities; later definitions and other specializations cannot supply missing proof. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#non-record-operation-traits).

Core v2 adds checked direct `malloc/calloc/realloc/free` calls through exact source-owned global C declarations. Source-defined C++ allocators can use the real native heap; allocation, resizing and release interoperate in both directions with an independent C client. A volatile indirect bridge preserves the process allocator's actual zero-size and failure behavior for `realloc`; native width, calling convention, source and IR checks remain enforced. O0/O2 validation requires implementing CI. Default throwing C++ allocation, standard headers and complete C++/STL remain unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#native-c-heap-calls).

Core v2 now registers destruction of admitted static records, arrays and lifetime-extended temporaries when each complete object finishes construction. Constant roots retain their values; local registration occurs at first passage and nonlocal registration runs during native startup. Native CRT callbacks preserve exit and module-unload order. O0/O2, library-unload and concurrent-first-use fixtures require implementing CI. TLS, exception unwinding, default heap/standard headers and complete C++/STL remain unfinished; manual/DynCode loading is unsupported. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#static-destruction).

Core v2 now supports nonlocal dynamic initialization of admitted objects and references, including materialized template instances. A checked internal native constructor runs before main in definition order, after zero/constant initialization; ordinary temporaries are cleaned after each initializer. Local statics retain first-use initialization. Program and separate C-client O0/O2 tests require implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#nonlocal-dynamic-initialization).

Runtime new[] now also accepts source-defined noexcept class allocators without placement arguments. Invalid lengths return null before calling the allocator; valid elements use a construction loop with per-element default-argument cleanup. Explicit-prefix and bound temporaries retain the enclosing full expression. Aggregate fillers now support actual element initialization without separate temporary objects, including nested lists and references to live storage. Fillers needing extra temporaries, throwing or placement runtime allocation, the default heap and full C++/STL remain unfinished. Native verification awaits implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#runtime-array-allocation).

Core v2 supports single-object new/delete through checked source-defined allocation functions, including class/template placement overloads, plus exact standard placement new from the embedded `<new>` header. Storage identity, argument cleanup, explicit destruction and placement reconstruction preserve later automatic cleanup obligations. Native verification requires implementing CI. Default heap runtime, placement runtime new[] lengths, exceptions, remaining standard-library behavior and full C++/STL remain unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#single-object-allocation-and-placement-reuse).

Experimental `neverc translate` emits reviewable `.nc` source through `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` and `cpp-math-v1`.

**Only C++ input translation is currently implemented.** Support for E Language (易语言, `.e`), Python, Go, Rust, TypeScript and JavaScript is planned; their translators are not yet available.

Partial declarations retain their successful primary argument checks, including copied member variable partials whose inner arguments remain dependent. Already resolved parameter type source is checked even if the partial is never selected. Unknown-length argument expansions retain their checked prefix and actual source without inventing argument slots. Pending class-scope full member variable declarations retain the same source guarantees. Full C++/STL remains unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#partial-declaration-source-checks).

Retaining an argument check does not make a partial specialization valid: it must still be more specialized and its parameters must be deducible. Invalid pack patterns and an extern instantiation after an instantiation definition remain C++ diagnostics (`TR0202`).

Core v2 supports named ordinary nested classes in admitted generic classes, their concrete copies and own explicit member specializations. Ordinary class bodies add no template parameter level. Unused bodies remain lazy; materialized fields, selected methods, member templates, lifetimes and scalar static storage retain their exact source and owners. Explicit instantiation directives preserve every written qualifier, including extern and no-effect repetitions. Inheritance, allocation and complete C++/STL remain unfinished. Only C++ input is implemented; E Language and Python are planned. Translation uses embedded Clang libraries without an external Clang executable. Native results require CI of the implementing revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#ordinary-nested-classes-in-generic-owners).

Core v2 supports scalar static member variable templates in admitted records and concrete class-template instances, including defaults, bounded packs, partial/full specializations and out-of-line definitions. Equivalent instances share storage; distinct outer or inner arguments retain separate objects. Checked non-inline const values can be read without storage, while evaluated addresses and references require a definition. Both declarations and definitions retain their source checks, and object access preserves receiver effects and temporary cleanup. Class-scope full specializations in dependent outer classes retain original and concrete declaration source. Resolved arguments, defaults and parameter types are checked immediately; outer-dependent source is checked after instantiation. Other result types and full C++/STL remain unfinished. Translation uses embedded Clang libraries without an external Clang executable. Only C++ input is implemented; E Language, Python and other frontends remain planned. Native validation requires the implementing CI revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#concrete-member-variable-templates).

Core v2 supports member alias templates in admitted ordinary records, including nested records, and concrete class-template instances. Type/scalar defaults and bounded packs may refer to outer class or inner alias parameters; actual arguments and substituted source types remain checked separately. Aliases preserve the existing value, reference, array and record semantics without creating runtime template objects. Nondependent declarations are checked even when unused; dependent underlying types remain lazy until substitution. Translation uses embedded Clang libraries without an external executable. Native validation requires the implementing CI revision. Full C++/STL remains unfinished. Only C++ input is implemented; E Language, Python and other frontends remain planned. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#concrete-member-alias-templates).

Core v2 supports concrete member function templates in admitted records and class-template instances: named/static methods, operators, constructors and conversions, with bounded type/scalar packs, defaults, out-of-line definitions and specialization. Inner and outer arguments retain distinct identities; equivalent calls share code and static storage. Selected calls, defaults and materialized bodies keep source and lifetime checks. Translation uses embedded Clang libraries without launching an external Clang executable. Native results require the implementing CI revision. Inheritance, allocation and full C++/STL remain unfinished. Only C++ input is implemented; E Language, Python and other input frontends remain planned. Concrete outer qualifiers on member-template declarations remain source-checked even without an instantiated body. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#concrete-member-function-templates).

Core v2 supports namespace variable templates with integer, boolean or enum results, including deduced auto, selected partial specializations, full specializations, defaults and bounded type/scalar packs. C++17 traits can feed static assertions, if constexpr and other template arguments. Equivalent instances share typed global storage; distinct instances retain separate values and addresses. Fixed-type queries do not force an unused initializer or allocate storage. Actual arguments, selected defaults, declaration/definition types and materialized initializers remain checked. other result types, full C++/STL remain unfinished; native validation requires the implementing revision’s CI.

A direct sizeof(T) or alignof(T) in a non-type parameter’s written type may remain declaration metadata when T is the exact type parameter from that same list. Selected substitutions still require concrete checked types; other dependent query forms are not covered by this exception. An extern template declaration alone does not provide the concrete definition required by the current closed-unit translation contract.

Core v2 uses standard C++17 template parsing on every supported target, including Windows x64 and ARM64. Template definitions are parsed without MSVC delayed parsing; duplicate explicit instantiation definitions and incompatible exception specifications remain language errors. Unused dependent bodies still instantiate only when needed. This does not complete C++/STL support. Explicit function specializations still need their own definition in the source unit; repeated declarations alone do not provide it.

Core v2 supports mutable namespace-scope integer, boolean and enum globals with zero initialization or a fully checked constant initializer. Their storage and addresses persist across calls; references, pointers and parameter defaults access the same variable. An `extern` declaration must resolve to a definition in this source unit. Const globals remain read-only. global records/arrays/pointers/references, thread-local storage and static locals retain their separate restrictions.

Core v2 supports automatic local structured bindings of source-owned non-volatile
trivial standard-layout aggregates whose direct public fields are supported non-volatile
scalars or object pointers. `auto` creates a hidden value object; `auto&`,
`const auto&` and `auto&&` preserve aliases and field qualification. The
initializer runs once, and a reference decomposition extends a temporary only
when its exact hidden owner qualifies for C++17 lifetime extension. User tuple-like
decomposition, reference/mutable/bit-field members, bases, nontrivial
records and static/global storage remain excluded. C++17
`if`/`switch` init-statements and ordinary `for` initialization are allowed;
structured bindings used as condition variables remain rejected.
[Record contract](../utils/translate-frontends/docs/cpp-core-v2.md#local-record-structured-bindings).

Native fixed arrays also support automatic local structured bindings, including
multidimensional arrays. Value forms initialize one independent hidden array;
reference forms alias the original elements or rows. The selected element
copy/move operations, per-element argument cleanup and reverse destruction
retain their existing semantics. A prvalue array initializes its destination
directly, and exact temporary owners retain their scope lifetime. The source
expression executes once. Variable/unknown/zero-length arrays,
unsupported element operations and the excluded declaration positions remain
outside this contract.
[Array contract](../utils/translate-frontends/docs/cpp-core-v2.md#local-array-structured-bindings).

Authenticated `std::pair`, `std::tuple` and nonempty `std::array` objects also
support automatic local structured bindings. Value owners remain independent,
reference elements preserve their referents, and const owners retain shallow
const qualification. Each hidden reference uses the exact selected SDK `get`;
`tuple_size` and `tuple_element` are checked through their actual SDK
specializations and const forwarding. Owner initialization and temporary
cleanup precede binding-reference initialization. User tuple-like protocols,
source trait/get specializations and unsupported container elements remain
excluded. [SDK binding contract](../utils/translate-frontends/docs/cpp-core-v2.md#local-sdk-structured-bindings).

Core v2 supports C++17 range-based `for` over supported fixed arrays and source-defined ranges with resolved member or ADL `begin/end` calls. Value/reference loop variables, admitted record/native-array/SDK structured bindings, record iterators and different sentinel types preserve ordinary call and lifetime rules. Range initialization and `begin/end` run once; iteration objects are destroyed before increment or exit, including `continue`, `break` and `return`. Other class-template forms, standard headers, STL containers and C++20 range initializers remain outside this increment. Native validation requires CI from the implementing revision.

Core v2 supports private and protected data members in otherwise supported standard-layout classes. Embedded Clang checks access before translation; authorized methods, constructors, factories, default arguments and generated copy/move operations use the same typed member storage. Illegal outside access remains a C++ diagnostic. Access labels are source rules, not a runtime secrecy feature. Mixed-access non-standard-layout classes, unsupported dependent friend class-template forms, inheritance and unsupported field types retain their restrictions. Native results require the implementing revision’s CI.

Core v2 supports ordinary non-template friend functions instantiated from admitted class templates, partials, member class templates and copied full or ordinary nested classes. Hidden-friend ADL, private access, free operators, namespace redeclarations, local classes and scalar static locals retain actual function identity and ordinary typed calls without an implicit receiver. Unused template bodies stay lazy; selected definitions and retained original signatures are checked. A different normalized incoming signature is admitted only with complete nondependent source evidence, including its own defaults and resolved noexcept. Unsupported dependent friend class-template forms, friend type expansions, complete C++/STL remain unfinished. Existing nondependent friend types and member friends retain their rules. Only C++ input is implemented; E Language and Python are planned. Translation uses embedded Clang libraries without invoking an external Clang executable. Native validation requires CI of the implementing revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#instantiated-non-template-friend-functions).

Core v2 supports resolved non-template friend types from admitted generic class bodies, including `friend T;`, aliases, dependent nested types and class specializations. Embedded Sema checks private access and legal ignored nonclass friends. Exact written and substituted type sources are checked; unproved copied origins, unsupported dependent friend class-template forms and type pack expansions remain unsupported. These grants add no runtime function, receiver or storage. Native behavior requires implementing-revision CI; complete C++/STL remains unfinished. Only C++ input is implemented; E Language and Python are planned. Clang libraries are embedded, with no external Clang executable. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#instantiated-non-template-friend-types).

Core v2 supports free friend function templates in admitted ordinary and generic classes, including supported operators, ADL, private access, outer and inner parameters, defaults and packs. Concrete calls preserve references, object lifetimes and separate instance/static identities. Original declarations and the selected body context remain checked. Unsupported dependent friend class-template forms, unsupported source-copy chains and complete C++/STL remain unfinished; native behavior requires implementing-revision CI. Only C++ input is implemented; E Language and Python are planned. Clang libraries are embedded; no external Clang executable is used. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#friend-function-templates).

Core v2 supports ordinary friend class-template declarations in admitted ordinary and generic classes, including namespace introductions, existing and supported qualified targets, and access by their specializations. Repeated grants share the target class and static storage. Original written defaults are rejected; inherited defaults keep their source checks. Unsupported dependent friend forms and complete C++/STL remain unfinished, and native behavior requires CI of the implementing revision. Only C++ input is implemented; E Language and Python are planned. Clang libraries are embedded without an external Clang executable. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#friend-class-templates).

Core v2 supports typed pointers to ordinary defined free functions and static methods, including indirect calls, callback parameters/results, reference carriers, arrays and fields, and constant-initialized global, static-local and ordinary static-member callback storage. The selected callback is captured before arguments. Only the default function ABI and supported scalar/reference signatures are admitted; record values, nonstatic member pointers, variadics, lambdas and cross-TU callbacks remain separate work. Complete C++/STL is unfinished; native validation requires CI. Only C++ input is implemented; E Language and Python are planned. Clang libraries are embedded without an external Clang executable. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#ordinary-function-pointers).

Core v2 also supports pointers to supported concrete free-function and static-member template instances, using explicit arguments or deduction from the expected pointer type. Source, definition and ABI checks remain mandatory. Direct and indirect calls share canonical functions and static-local storage; different instances stay distinct. Full C++/STL remains unfinished and native validation requires CI. Only C++ input is implemented; E Language and Python are planned. Clang libraries are embedded without an external Clang executable. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#concrete-function-template-pointers).

Core v2 also supports constant or zero-initialized callbacks in concrete namespace and static-member variable templates. Equivalent instances share storage; different instances remain independent even when they point to the same function. Directly written auto retains exact source checks; pointer-valued template arguments remain unsupported. Full C++/STL is unfinished and native validation requires CI. Input remains C++ only; E Language and Python are planned, with embedded Clang libraries and no external Clang executable. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#callback-variable-templates).

Core v2 supports named non-template nested records, including private/protected types exposed through legal aliases or factories. Each nested object has its own storage and receiver; explicit outer references, canonical scope identities, member/array copy and move, cleanup and nested iterators retain ordinary semantics. By-value dependencies are emitted first. Anonymous nested records, other class-template forms, inheritance and full STL remain outside current support; native validation requires the implementing revision’s CI.

Core v2 supports defined integer, boolean and enum static data members, including inline/constexpr and out-of-line definitions, with constant or zero initialization. All instances share one typed storage object. Receiver effects and temporary cleanup remain intact; references to static members outlive temporary receivers. Non-inline const integer, boolean and enum members with checked in-class constant initializers can also be read as values or discarded without a separate definition. These uses create no global object; taking an address or binding a reference to the member still requires a definition in this source unit. other static types and complete STL remain unfinished; native validation requires the implementing revision’s CI.

Core v2 preserves constant initialization and synchronized first-use initialization of admitted local static objects and references. Parameters, automatic locals and this remain available to dynamic initializers. One lock-free 32-bit guard publishes construction and ordinary temporary cleanup; constant objects requiring destruction use a guard only for registration. References preserve existing aliases and can extend admitted temporary lifetimes in permanent storage. Required destruction follows the registration contract above; TLS and statics in constexpr functions remain excluded. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#dynamic-local-static-initialization) [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#dynamic-local-static-temporaries)

Core v2 supports object-reference members, including array, pointer and callback objects. Construction stores the binding; member access and copy/move preserve aliases. Const containers retain mutable referents. Aggregate initialization preserves extended temporary lifetimes and destruction order, including synchronized local static groups and distinct runtime array fillers. Materializing default array elements now have distinct semantic identities within bounded expansion; direct element default constructors retain their argument cleanup boundaries. Full C++/STL remains unfinished; native validation requires the implementing CI revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#reference-members).

Core v2 supports inline namespaces, namespace aliases, using-directives and resolved using-declarations in namespace and block scopes. Imported functions, variables, types and unscoped enumerators keep their original identities, lookup rules, access checks and object lifetimes. Inline namespaces preserve combined parent lookup and argument-dependent lookup in both directions, including nested and reopened namespaces. Class member imports, inherited constructors, other class-template forms and C++20 enumerator or explicit nested-inline syntax remain outside this stage; full C++/STL is still unfinished.

Core v2 supports resolved C++17 if constexpr in ordinary functions and admitted concrete function-template instances. Only the selected branch emits runtime code, with initializer storage and scope cleanup preserved. Ordinary functions still check both source branches; Clang does not instantiate a discarded dependent branch. The existing full-definition restriction for ordinary declared functions/globals remains. C++23 if consteval is excluded.

Core v2 supports free function templates with up to 64 parameters, mixing supported types with integer, boolean and enum values. Deduction, explicit instantiation/specialization, type defaults, resolved function defaults, recursion and namespace imports use ordinary typed functions. Each instance retains its own local types and static scalar storage, including distinct primary templates with identical signatures. Generic bodies and unused lazy defaults are checked when instantiated; every materialized body is checked. Signature-only calls under sizeof/noexcept still require a definition. Unsupported dependent friend class-template forms, standard headers and complete STL remain unfinished. Native validation requires CI from the implementing revision.

Scalar C++17 auto arguments, dependent scalar parameter types, array-bound deduction and finite constexpr template recursion are supported. Value arguments become typed constants, not runtime parameters. Equivalent arguments share an instance; different values or deduced types retain separate static storage. Written argument expressions and parameter types are checked before folding erases them. Pointer/reference/class value arguments remain outside this increment.

Core v2 supports concrete namespace standard-layout class templates with up to 64 type or scalar value parameters. Type defaults, explicit instantiation/specialization, field aliases, fixed arrays and selected field initializers produce ordinary typed records. Equivalent arguments share a type; different values, types and primaries retain distinct identities. Existing memberwise copy, reference, return and destruction rules apply. Written types, defaults and argument expressions are checked before erasure. Full C++/STL is unfinished. Native results require CI from the implementing revision.

These standard-layout class templates also support ordinary named member functions, including const and ref-qualified overloads, static factories, member ranges and per-instance scalar static locals. Unused generic bodies and defaults stay lazy; selected calls and every materialized body receive the existing type, definition and lifetime checks. Written outer parameter types on out-of-line definitions are checked too. Methods use the ordinary receiver and result ABI, without runtime template parameters.

User-provided constructors are supported for these class templates, including default/parameter, explicit/converting, copy and move constructors, out-of-line definitions and explicit instantiation/specialization. Unused bodies, initializers and defaults stay lazy; selected construction requires a checked definition. Copy defaults are evaluated only when omitted. Initialization uses destination storage in member declaration order, with existing temporary, field and array lifetimes. Inherited constructors remain excluded. Native validation requires the implementing CI revision.

These class templates also support user-provided nonvirtual destructors, including out-of-line definitions and explicit instantiation/specialization. Definitions are required when Clang marks the destructor used, including direct object returns. Type-only and unevaluated queries keep unused bodies lazy while checking selected exception specifications. Destruction functions use explicit receiver storage, run the user body first, then destroy members and array elements in reverse order. Full-expression, extended-reference and by-value lifetimes retain their existing rules. Explicit destructor calls and complete C++/STL remain unfinished; native validation requires the implementing CI revision.

These class templates support explicitly defaulted default/copy/move constructors, copy/move assignment and nonvirtual destructors, including in-class and out-of-line definitions. Selected operations retain member order, source and destination identity, assignment reference results and reverse cleanup. Value initialization preserves the distinction between first-declaration and out-of-line defaulting. Unused bodies remain lazy; queries check resolved written exception specifications, and used nontrivial operations require owned generated definitions. Explicit specialization cannot inherit defaulting from the primary. Full C++17/STL remains unfinished; native validation requires CI from the implementing revision.

These class templates also support user-provided operators and conversion functions, including copy/move assignment, iterator-style dereference/increment/subscript/arrow, callable objects and explicit bool conversion. Selected overloads preserve C++17 evaluation order, reference aliases, final result storage and existing lifetimes. Unused bodies and defaults remain lazy; selected declarations and every materialized body retain source and definition checks. Out-of-line definitions, explicit instantiation/specialization and per-instance scalar static locals use ordinary typed functions without runtime template parameters. Allocation operators, standard headers and complete C++17/STL remain unfinished; native results require CI from the implementing revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#class-template-operators-and-conversions).

Local classes instantiated within admitted free-function or class-member templates retain their ordinary methods, constructors/destructors, operators, conversions and selected defaulted operations. Their concrete member origins, source types, receiver storage and per-instance scalar static locals are checked; every materialized local body remains inspected. Explicit function-instantiation directives retain each written argument, type, conversion name, qualifier and attribute check, including extern and no-effect repetitions. This prevents canonical declaration reuse from hiding unsupported source. Native results require implementing CI; complete C++17/STL remains unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#instantiated-local-classes-and-written-directives).

Core v2 also supports ordinary operator function templates at namespace scope. Arithmetic, comparison, reference-returning operations, iterator operators and record results retain selected overloads, C++17 sequencing, actual storage and temporary cleanup. ADL, explicit calls, specialization/instantiation and per-instance local/static identities use the existing typed function model with no runtime template parameters. Unused bodies stay lazy; selected source and every materialized body remain checked, including written explicit directives. Unsupported dependent friend class-template forms, allocation, standard headers and complete C++17/STL remain unfinished; native validation requires the implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#namespace-operator-function-templates).

These class templates also support integer, boolean and enum static data members with zero or constant initialization, including inline/constexpr and out-of-line definitions. Equivalent template arguments share storage; different instances retain separate objects. Checked non-inline const values need no invented storage, while evaluated address/reference uses require a definition. Unused initializers remain lazy; materialized initializers and every explicit directive retain source checks. Receiver effects and temporary cleanup use ordinary typed operations. other static types, complete C++17/STL remain unfinished; native validation requires implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#class-template-scalar-static-data).

Core v2 supports scalar non-type template parameter defaults for admitted function, operator and class templates. Earlier parameters, scalar auto, inherited defaults and checked constexpr conversions retain Clang selection and deduction. Equivalent omitted/explicit arguments share functions, records and static objects. Nondependent written defaults are checked; unused dependent defaults stay lazy. Successful conversions preserve original and converted source, while failed substitution/conversion keeps normal overload fallback. Full C++17/STL remains unfinished; native validation requires implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#scalar-template-parameter-defaults).

Deduced reference conversions preserve aliases and constness, including decltype(auto) and auto&& reference collapsing. The embedded frontend resolves candidate return types while keeping unrelated value-returning template bodies lazy. Complete C++17/STL remains unfinished; native validation requires implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#deduced-reference-conversions). Fresh reference arguments deduce necessary conversion results before compatibility checks. The direct-binding filter does not exempt a candidate from return-type deduction required by later indirect initialization; an invalid competing auto& body retains its normal diagnostic.

Core v2 supports concrete type and scalar parameter packs in admitted namespace function/operator and class templates. Each primary has at most 64 parameters and each pack at most 64 elements. Expanded parameters keep distinct storage; sizeof... emits a typed count, and resolved folds preserve sequencing, short circuit and cleanup. A dependent count in the same template parameter list may remain metadata in a written parameter type. Empty-fold patterns stay uninstantiated; seeds and materialized elements are checked. Complete C++17/STL remains unfinished; native results require implementing CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#concrete-parameter-packs).

Core v2 supports owned namespace alias templates for admitted scalar, void, pointer, reference, fixed-array and class types, with type/scalar defaults and concrete packs. Aliases reuse canonical types and static storage. Written arguments, selected defaults and substituted underlying source are checked at reached uses; unselected candidates and uninstantiated dependent bodies stay lazy. Explicit arguments keep caller context, including recursive templates and this expressions. Limits remain 64 parameters, pack elements and source depth, with the shared source budget. Complete C++17/STL remains unfinished; native validation requires the implementing CI revision. Only C++ input is implemented; E Language, Python and other frontends remain planned. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#namespace-alias-templates).
Selected non-type template arguments also preserve their parameter type source, including explicit function arguments and empty deduced packs; unsupported source erased by an alias still receives checks.

Nondependent type defaults are checked even in unused templates. Qualified function-template calls and concrete conversion-function definitions retain their exact written source, including out-of-line alias spellings. An lvalue conversion can create a widened temporary for a const-reference argument; an rvalue-reference argument retains the C++ overload restriction.
Core v2 also supports owned namespace class-template partial specializations. The selected pattern and its deduced parameters are checked separately from the primary arguments. Aliases share the same concrete record and static storage; admitted members retain their calls, references and lifetimes. Parameters and pack elements remain bounded at 64. Complete C++17/STL remains unfinished; native validation requires the implementing CI revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#class-template-partial-specializations).

Core v2 supports the distinct `decltype(nullptr)` value type, including aliases, references, fields/arrays, callback signatures and zero or constant scalar static and variable-template storage. Pointer conversions retain source effects and temporary cleanup; separate null objects retain separate addresses. `nullptr_t` template arguments now support checked defaults, packs and existing template source contexts, with canonical identity distinct from integer zero and boolean false. Pointer-typed null template arguments and standard headers beyond the pinned set remain unsupported. Native validation requires CI of the implementing revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#null-pointer-values).

Core v2 supports delegating constructors in admitted ordinary and generic classes, including chains and selected constructor templates. The target and delegating body use the same final object; member initialization runs once, and argument temporaries are destroyed before the delegating body executes. Source selection and definition checks remain in force. Native validation requires CI of the implementing revision. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#delegating-constructors).

Core v2 supports const data members with admitted scalar, record, pointer, callback and array types, including generic and nested classes. Initialization targets final storage; source-selected copying/moving, distinct member addresses and const reference/pointer types are preserved. Clang rejects invalid writes and deleted assignments. This supports source-defined const-key/value classes; standard headers and full STL remain unfinished. Native validation requires implementing-revision CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#const-data-members).

Core v2 admits supported deleted functions and defaulted special members defined as deleted, including their admitted template and friend forms. Clang retains overload/access diagnostics; deleted declarations emit no executable definitions. Move-only objects, selected copy fallbacks and C++17 guaranteed elision preserve existing lifetime handling. Implementing-revision CI is required; complete C++/STL remains unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#deleted-function-declarations).

Core v2 admits IEEE `float` and `double`, arithmetic and conversions, references, fields/arrays, callbacks and scalar static objects with zero or constant initialization. Exact literal bits and independent target/layout checks preserve representation; execution requires the default floating environment, with fast math, excess precision and implicit contraction disabled. Implementing-revision CI is required; `long double`, standard headers and complete C++/STL remain unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#binary-floating-point-values).

Core v2 admits narrow, UTF-8, UTF-16, UTF-32 and wide string literals with exact code units and static read-only storage, character-array initialization with zero fill, and fully constant namespace arrays. Aliases, field copies and source lifetimes use existing typed operations. The pinned `<string>` header also supports a bounded `std::string` construction, access and destruction subset for `char` with the default allocator; other string operations remain unfinished. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#string-header-and-metadata-from-string).

Core v2 supports zero or constant initialization of mutable and const arrays at namespace scope, in function-local statics and in class static members, including admitted template instances. Canonical arrays retain shared state and addresses across calls; different instances keep separate storage. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#fixed-array-static-storage).

Core v2 supports zero or constant initialization of static object pointers, including global/literal addresses, array and field paths, and valid one-past pointers. Namespace, local, class and template storage preserve pointer identity, mutability and const access. Forward addresses become C23 constants without runtime initialization. Static references, allocation and complete STL remain in development. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#static-object-pointer-storage).

Static references can bind to existing static objects, array/record subobjects and strings, including local/class statics and admitted template instances. Reads and assignments preserve the original object and const permissions. See the [static reference contract](../utils/translate-frontends/docs/cpp-core-v2.md#static-reference-bindings).

Core v2 supports constant-initialized temporary scalars, arrays and records whose lifetimes extend to static references. Complete objects, subobject aliases, self pointers, const permissions and template-instance identity retain persistent storage. Native validation requires CI of this implementation. C++17 constant initialization of these temporary objects still requires trivial destruction; nontrivial static temporaries use dynamic initialization and registration. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#static-reference-temporary-lifetime-extension).

Static records now support constant initialization and shared mutable state, including local/class statics and admitted template instances. Typed self addresses, constexpr construction and static zero initialization retain object identity. See the [static record contract](../utils/translate-frontends/docs/cpp-core-v2.md#static-record-objects).


Core v2 supports checked all-empty base chains with ordinary/template constructors, copy/move operations, assignment and destruction. Each base uses real first-member C storage with independently checked layout. Construction preserves base roles through delegation, shares static initialization with complete-object calls, and retains parameter cleanup and derived-to-base destruction order. Nonempty derived storage, inherited constructors, standard headers and full C++/STL remain unfinished. Native validation requires implementing-revision CI. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#empty-base-chains).

Checked function-pointer trait objects can serve as `vector` elements, including approved array, pair, optional and nested-vector elements. Existing growth, copy/move, insertion, erasure and range operations preserve independent element storage and checked source callbacks. Boolean vectors, custom allocators, unproved element comparisons and SDK `value` storage aliases retain their existing restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

`make_unique` supports checked function-pointer trait objects through default, copy and move construction, including constant-length arrays. The selected pinned constructor and exact SDK forwarding are verified; caller arguments retain their evaluation count, and owned allocation/deallocation definitions and cleanup remain required. Const sources, release/reset and vector ownership use the existing checked paths. Runtime array lengths, unsupported trait signatures and SDK `value` storage aliases retain their restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

`vector::emplace` and `emplace_back` accept exact checked trait values for either or both fields of an admitted pair element. Const, lvalue, rvalue and checked source-function results use independent pair storage; existing scalar conversions, argument evaluation, growth aliases and returned element references are preserved. Implicit user conversions, unrelated SDK objects and trait-to-callback conversions still require their existing independent proofs. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Pair fields constructed by vector emplacement also accept checked ordinary function pointers and function references, compatible `noexcept` removal and `nullptr`. Direct lowering preserves callback signatures, source definitions and independent pair storage. Forwarding bindings are captured for all caller arguments before pair field values are read, so later arguments can update referenced values without an early snapshot. Implicit user or trait conversions, volatile pointers and SDK function addresses retain their independent restrictions. On growth, field values are read after the checked source allocation and before existing elements are relocated; in-place construction needs no allocation. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Nested `vector` elements support direct `emplace` and `emplace_back` construction from a checked constant count in `0..65536`, optionally followed by an exact fill value or an admitted scalar or callback conversion. Inner buffers are independent. Exact fill references stay live through allocation; converted values are saved after outer allocation and before inner allocation. Zero counts evaluate the fill argument without allocating an inner buffer. Dynamic or effectful counts, user-defined fill conversions and custom allocators retain their existing restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

Nested `vector` range emplacement also accepts exact raw pointers, array decay and authenticated wrapped iterators for copyable inner elements. Endpoint bindings are captured once; endpoint values are read after outer allocation and before inner allocation. Empty ranges allocate no inner buffer. Source ranges may belong to the destination vector, and copied resources remain independent. Converting ranges, reverse or arbitrary iterators and move-only elements retain their existing restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

Nested `vector` elements can also be emplaced from an exact `std::initializer_list<T>`, including direct temporary lists, const lists and empty lists. The list pointer and size are read after outer allocation and saved before inner allocation. Resource elements use their existing copy operation, and direct SDK callback constants are copied as values. Temporary backing arrays retain their required lifetime; user-defined list conversions and SDK storage aliases retain their restrictions. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

## Setup and scalar translation

Use a normal NeverC installation with its standard resources. The C++ frontend and approved SDK headers are built into NeverC; no separate Clang installation is needed. See the [frontend build notes](../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

The `cpp-core-v1` profile accepts one self-contained C++17 source without includes. It supports `int`, `unsigned int`, `bool`, `void`, trivial aggregate types, free functions, namespaces, overloads and the documented control flow. Every declaration in the input is checked, including unused code.

Select `--profile cpp-core-v2` to add checked `typedef`/`using` aliases, enums with supported integral underlying types, `static_assert`, bounded object pointers and lvalue references. References preserve aliases, including parameters and returned references; nested pointee `const` and null pointers are supported. The single-source and no-include restrictions still apply. See the [core v2 contract](../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 also adds fixed-size local arrays and array fields, multidimensional indexing, and pointers/references to arrays. Partial initialization zero-initializes omitted scalar elements; record elements follow their selected initialization; element initialization preserves source order and aliasing. Extents and initializer expansion are bounded. Global arrays and variable-length arrays remain unsupported.

Core v2 supports `switch`/`case`/`default`, including C++17 init-statements, fallthrough and validated `[[fallthrough]]` annotations. Selector evaluation occurs once; nested switches and loops retain their own `break` and `continue` targets. GNU case ranges and other statement attributes remain rejected.

Core v2 now supports signed and unsigned 8-, 16-, 32- and 64-bit integers, character types and literals, and constant `sizeof`/`alignof` queries. Source promotions and overload resolution precede width normalization; `long`, `wchar_t` and the size type follow the selected target. This includes narrow and wide enum underlying types. The standard library is still being developed.

Core v2 also supports object-pointer offsets, differences, increment/decrement and compound assignments, including array iteration and multidimensional strides. Generated helpers preserve C++17 null-pointer plus/minus zero and null-pointer difference. Object pointer ordering also uses independently verified unsigned addresses on native x86/AArch64 targets; native acceptance requires CI for the implementing revision. Source pointer/integer casts and full STL remain unfinished.

Core v2 verifies source sizes and ABI alignments against NeverC’s own target model, including aggregate sizes and field offsets. Generated assertions check the recorded layout again during compilation, and the manifest records this evidence.

Core v2 supports named nonvirtual member functions of the admitted records, including const overloads, lvalue-qualified methods, static methods and `this`. Calls preserve the original object and evaluate the receiver before arguments.

Core v2 also supports ordinary user-provided constructors for standard-layout records with admitted copy operations. Locals, record fields and array elements are constructed directly in their final storage; fields initialize in declaration order. Exception unwinding remains unsupported.

Core v2 gives each by-value record parameter a separate object and writes record results directly into the caller’s destination. Constructors and methods use the same rules; source-required copies and reference aliases remain intact. For eligible trivial records, other C++17 implementations may introduce additional argument/result copies.

Core v2 supports ordinary user-defined destructors and implicit member destruction on normal exits. Local objects, record fields and array elements are destroyed in reverse order; temporaries end at full-expression boundaries after their values are captured. Returns, branches, loops, break and continue perform the required cleanup. By-value parameters are destroyed when the callee exits; returned objects belong to the caller. Exception unwinding remains unsupported.

Core v2 executes ordinary user-defined copy constructors and copy assignment operators with a source parameter of type `R&` or `const R&`. Copying uses the actual destination and preserves the function’s side effects and returned reference. Assignment syntax evaluates the right operand first; explicit `operator=` calls evaluate the receiver first.

Core v2 also supports generated/defaulted default constructors and defaulted destructors, including nested record and array members. Default construction uses the actual destination and initializes members in declaration order; value initialization performs only the zero-initialization required by C++. Out-of-line defaulting retains its different initialization rules. Unused or unevaluated defaulted constructors do not require an invented function body.

Core v2 supports implicit and explicitly defaulted copy constructors, including nested records and multidimensional arrays. Selected member copy constructors run in declaration and element order at the actual destination; the source array address is evaluated once. Trivial copies preserve stored pointer fields unchanged. By-value arguments and source returns retain copy effects, while direct prvalue forwarding adds no copies.

Core v2 also supports implicit and explicitly defaulted copy assignment. Members and array elements retain their selected assignment operations and order; generated trivial array copies become typed element assignments with no external memory-copy call. Trivial assignment preserves stored pointers and returns the actual receiver. Operator syntax captures the source reference before the receiver; explicit member syntax captures the receiver first. Source values are read after those effects, and assignment introduces no extra constructed or destroyed object.

Core v2 supports default member initializers for admitted fields, including earlier-member access, ordinary calls, nested records and arrays. A selected default uses its actual owning object as `this`; explicit aggregate clauses retain the caller’s `this`. Explicit initialization overrides that member’s default. Implicit/defaulted copying and assignment do not rerun defaults; a user copy constructor can select defaults for omitted members. Constructor member initializers and aggregate initialization retain their respective temporary cleanup boundaries. Templates and full STL remain in development.

Core v2 supports rvalue references to existing live objects, including scalar, pointer, record and array aliases, reference parameters/results, conditional xvalues and ordinary `&&`-qualified methods. A cast such as `static_cast<R&&>(live)` preserves the same object; named rvalue-reference variables remain lvalues. Clang’s selected overload and existing copy behavior remain authoritative. References introduce no extra cleanup owner.

Core v2 executes ordinary user-defined move constructors and move assignment from a live `R&&` or `const R&&` source. Construction uses the actual destination; assignment preserves source mutations, operand order and the returned `R&` alias, including `&`/`&&`-qualified receivers. Named rvalue references still select lvalue overloads, and explicit constructors retain their initialization rules. Moving does not end the source lifetime: source and destination keep their normal destruction.

Core v2 also supports implicit and explicitly defaulted move construction and assignment, including nested records and multidimensional arrays. Members use the copy or move selected by C++, in declaration and element order. Generated moves do not rerun default member initializers; trivial operations preserve stored pointer values. Sources keep their normal destruction, and unused or trivial generated operations need no invented body.

Core v2 supports resolved standard `noexcept`, `noexcept(true/false)` and C++17 `throw()` declarations, plus constant `noexcept(expression)` queries. Queries preserve the selected function and destructor specifications without executing their operands. Source inspection still checks every operand and written specification, including unused code. Throwing, catching, stack unwinding, templates and full STL remain in development.

Core v2 supports ordinary member and free overloaded operators, including arithmetic, comparisons, subscript, dereference, increments, functors and general assignment signatures. Calls preserve selected functions, reference aliases and object results. Operator notation retains C++17 sequencing; overloaded logical operators evaluate both operands. Free assignment operators consistently initialize parameters right to left and destroy them in reverse order. Templates, allocation and full STL remain in development.

Core v2 also supports ordinary conversion functions on live objects: implicit and explicit integral, enum and pointer conversions, contextual explicit `bool`, and reference or object results. Each conversion executes the selected member function once. References preserve aliases; object prvalues initialize their actual destination, while reference-to-value conversions retain the selected copy or move. Const/ref qualifiers, constexpr and noexcept follow C++17 selection.

Core v2 now supports temporary object receivers and scalar/record temporary reference arguments that live until the enclosing full-expression ends, including constructors, methods, operators and conversion functions. Each evaluation has actual storage; object results keep their final destination and references keep their aliases. Cleanup occurs after the call and callee parameter destruction, in reverse construction order, including conditional and repeated loop execution. Array subobjects of temporary records are supported.

Core v2 now extends temporary lifetimes for ordinary automatic local references, including `const T& r{T{...}}` and `T&& r = T{...}`. Scalar, enum, pointer and record temporaries use actual storage; references to members or array elements keep the complete record alive when C++ grants extension. Braced and equal-braced reference initialization preserve the same object. Destruction follows the reference scope, including conditions, loops and early exits; other temporaries inside its initializer still end at their own full-expression. Aliases add no owner, and later copies or moves keep their selected operations. [C++17](../utils/translate-frontends/docs/cpp-core-v2.md#reference-members).

Core v2 also supports standalone fixed-array temporaries in calls, decay/indexing, discarded expressions and automatic local references. Each array has one actual destination; scalar stores, record constructors and shared default fillers initialize individual elements in order without creating extra element owners. Multidimensional row/element references keep the complete array alive when C++ grants extension. Cleanup destroys elements in reverse order at the full-expression or reference-scope boundary. Array references retain their typed addresses, and existing extent, storage and expansion limits apply. Full C++/STL remains in development.

Core v2 supports empty standard-layout classes and stateless callable/conversion objects. Empty objects keep their C++ size and alignment of one byte, distinct storage where required, selected constructors/operators and normal cleanup. Trivial copies still evaluate their operands; arrays and containing records count empty elements against storage limits. The generated NC uses an internal storage byte, while source fields remain empty. Inheritance, templates and complete STL remain in development.

Core v2 supports casts to void, `void()` and `void{}`, including supported void aliases, calls, returns, comma expressions and conditional branches. Discarding a nonvolatile lvalue preserves receiver/index effects without reading its stored value; discarding a temporary still constructs and destroys it at the existing full-expression boundary. No void variable or value carrier is emitted. Constant and noexcept operands remain fully checked. Volatile objects, unsupported operand types, templates and complete STL remain outside this increment.

Core v2 supports resolved default arguments for admitted functions, methods, call operators and user constructors, including trailing defaults on user copy/move constructors. Defaults retain declaration-time name lookup and are evaluated on each call that omits the argument. Reference and value arguments keep their normal identity and cleanup. Array elements with omitted initializers and generated array copies clean up default-argument temporaries before the next element; explicit array clauses retain the enclosing full expression. Unused and overridden defaults remain checked. Templates and complete STL remain unfinished.

## Multi-file projects

Select the translation units explicitly from a compilation database and set the project root directory. The built-in frontend analyzes each unit separately; the merger checks definitions, linkage and shared types, and conservatively verifies the one-definition rule (ODR).

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

Project output is `translated.nc` plus `translated.h`. Only project headers within that root are admitted. If one file has several configurations, select its zero-based database index with `--compdb-entry src/a.cpp=4`. Stored compiler commands are parsed as data and are never executed.

## Bounded double math

`cpp-math-v1` extends projects with `double`, documented conversions/comparisons, and exactly `std::fabs(double)` and `std::floor(double)`. It uses the built-in Clang 20.1.8 / libc++ 200100 / macOS 15.5 header set and requires an explicit macOS 15.0 target (arm64 or x86_64). General floating arithmetic remains unsupported. The floating environment requires masked traps and no flush-to-zero mode; all four standard rounding modes are tested.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

Math translation also verifies installed NeverC math headers, embedded implementation identities and an actual link probe. Generated math modules use the NeverC runtime. With `-fno-builtin-std`, translation fails before writing output only if the final code needs `fabs`/`floor` mappings. Math code requiring neither can still be translated.

## Validation and output

Use `--check` instead of an output option to run the same analysis, emission, syntax and object validation without retaining generated files. `--report PATH` writes structured diagnostics. Translation never runs the source program.

Outputs and sidecars are never overwritten. `--out-dir` requires a new directory with an existing parent; `-o` requires a new `.nc` path. The manifest records target requirements, input/output hashes and the compilation recipe; the source map relates generated lines to original locations.

CI results, execution and installation checks, and skipped tests are recorded by platform. Native macOS arm64 and macOS x86_64 under Rosetta remain distinct validation environments. General C++/STL support, exceptions, unrestricted templates, and string/vector behavior beyond the documented subsets are outside the advertised contract. See the [support matrix](../utils/translate-frontends/docs/support-matrix.md), [protocol and recovery rules](../utils/translate-frontends/docs/protocol.md), and [project fixture](../tests/neverc/Inputs/translate/cpp/project).
