# C++ core v2: integral types, declarations and object storage

`cpp-core-v2` is an experimental, explicitly selected extension of the
single-source `cpp-core-v1` contract. It adds the declarations, bounded pointer
and reference operations, fixed arrays, integer widths, size queries and ordinary
record methods and constructors below. It is a step toward broader C++17 translation; it does not
claim complete C++17 or STL support. The existing core, project and math v1
profiles retain their accepted-input contracts.

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
and enumerator initializers. A cast to `void` cannot hide an unsupported operand
such as a floating literal or runtime string. A successful
`static_assert(true, "message")` does not admit runtime string literals or
`std::string`.

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

- Local variables and supported function parameters/results may use object pointers
  and lvalue references. Supported pointees are the admitted integer and character types, `bool`,
  the admitted enums, complete admitted records and admitted fixed arrays; `void *` is supported without
  dereferencing `void`. Function and member pointers remain unsupported.
- Address, dereference, `->`, pointer equality/inequality, conversion to `bool`,
  null initialization (`nullptr`, zero and value initialization), qualification
  conversions and pointer/`void *` round trips preserve the admitted source
  behavior. `const_cast` may adjust qualifications on supported pointer and
  reference types. Pointer ordering, unary pointer plus and pointer/integer
  reinterpretation remain rejected. Offsets and differences follow the rules below.
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
record subobjects, follows the dedicated contract below. Reference fields and
pointer/reference globals (including pointer-valued global aggregate fields)
remain rejected.
Unused/dead bindings are checked too. Standalone
`nullptr_t` variables are not supported. The contract covers accesses to live
objects; it does not define dangling/invalid pointer behavior or remove C++
undefined behavior. In particular, casting away `const` does not make an
originally const object writable. Generated `.nc` source targets NeverC's
pointer aliasing behavior, not an arbitrary C compiler's alias rules.

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
in dead code. Reference fields/globals, function references, volatile types,
static lifetime extension and arbitrary dangling-reference analysis are not enabled. Invalid C++ category/qualification uses retain source diagnostics.
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
volatile/restrict methods and templates remain unsupported. Defined scalar
static data follows its separate storage contract below. Ordinary
constructors and destructors follow the separate rules below. The existing
implicit trivial copy paths are unchanged.
These methods do not establish STL container or iterator support. V1 profiles
retain their rejected-method boundary.

## Default arguments

Core v2 supports resolved default arguments on admitted non-template free
functions, methods, call operators and user constructors. User-provided copy and
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

Both declared defaults and selected call-site semantic expressions are fully
inspected, even when unused, explicitly overridden, folded in constexpr code or
inside a noexcept query. Queries create no runtime default effects. Unsupported
operand types, volatile objects, strings, function/member pointers, templates,
allocation and throwing remain diagnosed under their existing boundaries.
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
admit function pointers, method values or unresolved overload sets. Discarded
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
source ownership and type checks as other records. No bases, unions, nesting,
virtual dispatch, templates, packing or custom alignment are added. Other
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
normal definition checking. Nontrivial global destruction remains unsupported.

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

## Ordinary record constructors

Core v2 admits ordinary user-provided default, converting and multi-argument
constructors, including `explicit`, `constexpr` and out-of-line definitions.
Records must be standard-layout with no bases; named non-template nested
records follow the scope contract below. Empty records
follow the storage contract above. Each
selected construction, copy or assignment must follow its admitted operation
contract, including the user moves described below.
Destruction follows the separate lifetime contract below. Fields remain non-mutable, non-const,
non-reference and non-bitfield. Default member initializers follow the contract below. Ordinary
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

Deleted, delegating, inherited, template and variadic constructors
remain rejected. Exception unwinding, allocation, static
guards, inheritance, virtual dispatch and STL are still outside this increment.
Compile-time const scalar/record globals may use an admitted constexpr
constructor after source inspection; records requiring destruction and existing
dynamic, pointer and array global forms remain rejected. V1 profiles continue to reject user constructors.

## Named nested records

Core v2 admits named non-template nested classes and structs with the supported
standard layout and fields. Public, private and protected nested types preserve
C++ source access, including legal aliases, factories, nested member definitions
and out-of-line definitions. A nested member can access its enclosing class's
private fields through an explicit enclosing object. The enclosing class does
not gain access to the nested class's private members without a valid grant.
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
non-nested anonymous-record behavior is unchanged. Unions, templates, inheritance,
virtual dispatch and unsupported field/body operations retain their restrictions,
including unused nested definitions. Older profiles continue to reject nested
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
not exempt. Template/dependent/pack friend forms and any friend marked unsupported
by the embedded frontend are rejected. Existing class, field, storage and type
restrictions remain, as do older profiles' friend rejection. Templates and full
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

Mixed-access non-standard-layout classes, friend templates, inheritance,
anonymous nested records and templates remain outside current support. Bitfields and
mutable, const, reference or unsupported numeric fields retain their existing
restrictions. A getter returning a field's ordinary `T*` address does not admit
pointer-to-member types such as `T C::*`. Older profiles keep their contracts.
Native O0/O2 fixtures cover state, aliasing, defaulted array copying/moving and
nontrivial member cleanup; protocol fixtures cover storage, signatures, layout
and relocation. Native results require CI from the implementing revision.

## Range-based for

Core v2 admits resolved C++17 range-based `for` statements over supported fixed
arrays and source-defined records. It preserves the embedded frontend's selected
member or ADL `begin/end` calls, including default arguments, overloaded iterator
comparison/dereference/increment and a different C++17 sentinel type. Every
selected declaration, initializer, operation and body must satisfy the existing
type, definition and source-ownership checks. This includes unused or folded
source operations. The frontend remains built in; no external Clang executable
is used.

The range initializer runs once, followed by `begin` and then `end`, also once.
The condition precedes each iteration. A value loop variable uses its selected
copy or direct prvalue destination; references alias the selected element.
The loop variable and any temporary whose lifetime it extends are destroyed
after the body and before increment. `continue` performs that cleanup before
increment, `break` skips increment and leaves the range scope, and `return`
captures its result before cleaning up the body, iteration and range scopes.
The end iterator is destroyed before the begin iterator and then the range.
Nested loops and switches retain their own control targets.

Only the exact three hidden declarations belonging to a checked range statement
are admitted. Their names do not grant access to implicit declarations generally.
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

Templates, standard headers and STL containers, initializer-list ranges,
structured bindings, C++20 range init-statements and coroutine range loops
remain outside this increment. Existing extent and expansion budgets still
apply, and older profiles retain their previous range-loop rejection.

## Defined scalar static data members

Core v2 admits source-owned definitions of non-volatile integer, boolean and enum
static data members in supported non-template classes. This includes C++17
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

Dynamic initialization, static pointer/reference/array/record objects, TLS,
static locals and templates retain their restrictions. There is no startup or
static-destruction function in this representation. Older profiles retain their
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
TLS, static locals, other static types, dynamic initialization and templates
retain their separate restrictions. V1 profiles retain their previous behavior.

Paired native and protocol fixtures cover value widths, constant-expression
contexts, selected branches, missing-definition diagnostics, receiver effects,
cleanup timing, actual storage identity and deterministic relocation. Native
O0/O2 results require CI from the implementing revision. Full C++/STL support
remains unfinished.

## Mutable scalar globals

Core v2 admits mutable namespace/file-scope globals of the supported integer,
boolean and enum types. Definitions without an initializer receive C++ static
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

Const globals retain their existing read-only representation. Mutable record,
array, pointer and reference globals, floating-point/volatile/atomic globals,
dynamic initialization, thread-local storage and static locals remain outside
current support. Defined scalar static data members follow the contract below. Nontrivial global object destruction
still requires separate lifetime support. Other profiles retain their prior
constant-global contract.

The optional global IR field `mutable` defaults to `false`. Only core v2 accepts
this field, and only supported integer/boolean carriers can set it to `true`.
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
need no call. Deleted/defaulted-deleted functions remain rejected. Exception unwinding and STL remain later
milestones. Actual execution evidence must come from the implementing revision's CI.

## Default member initializers

Core v2 admits brace-or-equal initializers on the supported non-mutable,
non-const, non-reference and non-bitfield record fields. Scalar and pointer
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
outside the current contract, exceptions, templates and STL are not enabled
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
members, templates, variadic/default
arguments,
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
templates and STL remain outside this increment. Temporary source-reference
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
templates and STL still require further work; v1 profiles are unchanged.

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
templates and STL headers remain outside this increment. Array extent, object
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
deleted functions remain rejected. Standard resolved exception specifications
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

## Record destruction and normal lifetimes

Core v2 admits ordinary user-provided destructors of the records described
above, including out-of-line definitions and implicit destruction of containing
records. Copy construction and assignment must be admitted independently; a user
destructor does not by itself require nontrivial copying. The source still has no bases,
virtual dispatch, unions, reference members or unsupported field layouts.
Deleted destructors and explicit destructor calls remain rejected, including in
dead code. Ordinary and defaulted destructors accept implicit exception
specifications and the resolved standard written forms described below. Every ordinary
user destructor needs an owned body; a supported `= default` destructor uses the
member cleanup described below without requiring a materialized body.

Each record needing destruction has one internal ordinary void function with a
mutable pointer to its object. It runs the user body, destroys its body locals,
and then destroys members in reverse declaration order. Arrays recurse in
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
partial construction rollback, allocation/deallocation, static/global object
destruction remain rejected. Temporary calls and automatic local reference
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
increment results. Taking an overloaded operator's function/member address still
requires later function-pointer support. Conversion functions follow their separate
contract below. This stage does not admit templates, friend declarations,
virtual dispatch or allocation/deallocation operators. Temporary call operands
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
templates, library headers and complete STL remain in development. Native evidence
must come from the implementing revision's CI.

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

Conversion functions have no explicit parameters. Virtual/template conversions,
volatile/restrict receivers, unsupported result types and function/member
pointers remain excluded. Full-expression temporary receivers and converted
reference arguments follow the next section. Automatic local reference extension
has its own contract below. Static lifetime extension, allocation,
exception execution, templates and complete STL still require further work.
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
types, exception execution, templates and complete STL still require further
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

Static/global/thread-local reference lifetimes, reference fields, structured
bindings, unsupported types, exception unwinding,
templates and complete STL remain outside this increment. V1 is unchanged. O0/O2
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
external helper or memory-copy call. Static/global/thread-local lifetimes,
reference fields, fresh reference returns, non-extended pointer-derived bindings,
unsupported element types, explicit destruction, allocation, unwinding, templates
and complete STL remain outside this increment. V1 and protocol major 1 are unchanged.

O0/O2 no-inline fixtures cover real element addresses, reference calls, decay,
subscripts, default fillers, discarded arrays, nested temporaries, multidimensional
ownership and early exits. Protocol assertions inspect complete typed signatures,
actual array/element destinations, one cleanup guard per array, reverse destruction,
query purity and relocation. Native execution evidence requires the implementing CI.

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
remain rejected; unevaluated source does not admit templates, function pointers,
unsupported lifetime extension or explicit destruction. Missing ordinary owned
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

Pointer ordering (`<`, `<=`, `>`, `>=`), unary pointer `+`, pointer/integer casts,
void/function/member-pointer arithmetic, and general iterator/container/STL
implementations remain outside this increment. Pointer `==`/`!=` remain supported.

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

Global arrays and global records containing arrays, zero-length and variable-length
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
entries named `bool`, `i8`, `u8`, `i16`, `u16`, `int`, `uint`, `i64`, `u64` and
`default-pointer`. These name the native emission carriers for the admitted source types.

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

128-bit and extended integers, floating-point types,
exception unwinding, templates,
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
