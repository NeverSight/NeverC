# NeverC C++20 Support — Conformance Matrix

Scope lock:
- Target: ISO C++20 (`-std=c++20` / `gnu++20` / `c++2a` / `gnu++2a`)
- ABI: NeverC-only v1 (Itanium-inspired mangling; **not** system libstdc++/libc++ ABI)
- Runtime: bundled under `runtime/cxx` (`neverc_cxx_runtime`)
- Explicitly excluded: iostream / stream facilities (`NoStreams` — headers hard-error)
- Verification: **build/tests deferred until implementation is 100% complete**

| Feature area | Parser | Sema | AST | IR/Codegen | Runtime | Tests | Status |
|---|---|---|---|---|---|---|---|
| Language mode / `-std=c++20` | n/a | n/a | n/a | n/a | n/a | deferred | partial |
| C++ keywords / punctuators | partial | n/a | tokens | n/a | n/a | deferred | partial |
| No-stream policy | done | policy | n/a | n/a | headers | deferred | done |
| Namespaces / using / linkage | partial | partial | partial | mangle/linkage | n/a | deferred | in progress |
| Reference types | done | done | done | done | n/a | deferred | done |
| Nested-name-specifier | partial | partial | done | n/a | n/a | deferred | in progress |
| Classes / access / members | partial | access+derived protected | partial | vtable + virt call | n/a | deferred | in progress |
| `this` / ctors / dtors / methods | ctor-init parse | SetCtorInitializers | CXXCtorInitializer | vptr + mem-init emit + construct call | rt | deferred | in progress |
| Operator function-ids | partial | multi-kind + FormBinOp member op+args | partial | op call emit | n/a | deferred | in progress |
| Inheritance / bases / virtual | partial | virtual→polymorphic | partial | vptr + primary-base + virtual call slots | RTTI | deferred | in progress |
| Overload resolution | partial | ULE + ICS-ranked BestViable | ULE | n/a | n/a | deferred | in progress |
| Templates / concepts | partial | SubstType + SubstExpr(+cast/cond/sub/this/op/member) + SubstStmt + requires-clause/expr + instantiate + partial order | TTP + DeclTemplate + RequiresClause + RequiresExpr | planned | n/a | deferred | in progress |
| Lambdas | typed params+captures | closure + call op + capture types | capture metadata | capture field init | n/a | deferred | in progress |
| new/delete | partial | DeclSpec→ptr | partial | new+vptr, delete+dtor | operator new | deferred | in progress |
| Named casts / typeid | partial | OnCXXTypeid* | partial | dynamic_cast + typeid→RTTI | RTTI | deferred | in progress |
| Exceptions / noexcept | noexcept parse | EST_* multi | try+EHCatchScope | NeverC_CXX LP | cxa_* + begin/end catch | deferred | in progress |
| Range-for | hook + parse | array + member/free begin/end calls + ADL-lite | CXXForRangeStmt | classic+scaffold | n/a | deferred | in progress |
| Coroutines | partial | CoreturnExpr + coawait/yield | CoreturnExpr | coawait/coreturn→resume | coro helpers | deferred | in progress |
| constexpr / consteval | partial | body required + ICE eval | partial | planned | n/a | deferred | in progress |
| Modules | module name parse | export BMI blob + import readFrom | planned | planned | BMI.h v0 R/W | deferred | in progress |
| DeclarationName multi-kind | n/a | operators/ctors | names | mangle C1/D1/op | n/a | deferred | in progress |
| C++ mangling (NeverC ABI v1) | n/a | n/a | n/a | `_Z` + nested | n/a | deferred | in progress |
| Non-stream stdlib | n/a | n/a | n/a | link | ~98 headers scaffolds | deferred | in progress |
| C++ ABI runtime | n/a | n/a | n/a | link | `runtime/cxx` | deferred | in progress |

## Implementation phases

1. **Frontend foundations** — language mode, keywords, DeclSpec C++ bits, namespaces/linkage scaffolds, references, NNS partial, CXX record/method/ctor/dtor, `this`, operator-id partial, bases, templates DeclNodes + parse stubs, lambda/new/delete/cast/throw/try/co_* AST stubs. **Mostly done.**
2. **Sema depth** — overload (ULE + `OnCallExpr` candidate set), noexcept → `EST_*` (basic/false/dependent), virtual → polymorphic, `DeclarationName` multi-kind, C++ mangling + `CXXLanguageLinkage`, `TemplateTypeParmType` + `SubstType`, function/class template instantiate scaffold, range-for AST/Sema. **In progress (core paths in).**
3. **Codegen / ABI** — `NeverCCXXABI` vtable/RTTI/VTT, EHCatchScope try, typeid→RTTI, new vptr, delete+element dtor hook, bases layout, ctor-init list emit, range-for classic loop, lambda capture field init. **Deepened; construction vtables / full cookie array-new still open.**
4. **Runtime + std** — `runtime/cxx` (new/delete, RTTI, cxa_*, guards, coro), non-stream std (`type_traits`, `concepts`, `span`, `ranges`, `optional`, `memory`, …); stream headers hard-error. **Deepened; not exhaustive.**
5. **Tests** — **deferred until 100% complete** (user constraint).
6. **Docs / gates** — this matrix; driver auto-link of `neverc_cxx_runtime` finalization.

## NeverC ABI v1 notes

- Mangling: Itanium-inspired (`_Z`, `N…E`, `C1`/`D1`, operator codes) but **not** guaranteed interchangeable with system toolchains.
- Vtable layout: `[offset-to-top, RTTI, virtual function pointers…]` (complete-object simplified).
- VTT: `_ZTT` + N + name for classes with virtual bases (`NeverCCXXABI::emitVTT`); construction vtables still open.
- RTTI: `_ZTI` + `.name` string; `__neverc_dynamic_cast` runtime symbol.
- Exception personality: `__neverc_personality_v0` stub until landing pads land.

## Explicit non-goals (this effort)

- iostream / stream-dependent library surface
- System libstdc++ / libc++ / MSVC STL ABI compatibility
- C++23 features
- Building or running the tree until the implementation is marked complete

## Recent session progress (Phase 2–4 depth)

- Lambdas: unique `CXXRecordDecl` + `operator()` body.
- EH: typed catch uses RTTI from exception decl when present.
- Range-for: class types with unique `begin`/`end` methods get Cond/Inc.
- Construct: ScalarEmitter calls ctor via addrOfFunction + temp.
- Lambda emit: zero-init closure temporary.
- SubstStmt: break/continue + control-flow cases noted.
- Delete: call complete-object dtor before `_ZdlPv` when present.
- SubstType: FunctionNoProto/Atomic/Paren/Decayed.
- EH: bind catch parameter from `__cxa_begin_catch`.
- Layout: simplified unique virtual-base region (no VTT).
- Operator call: AST carries args; emit via addrOfFunction.
- Unary FormUnaryOp: unique member operatorOP rewrite.
- new: DeclSpec builtin types → pointer-to-T (else void*).
- Conversions: apply unique CXXConversionDecl via operator-call.
- Access: same-class public/protected/private scaffold.
- Templates: SubstExpr for DeclRef/binary/unary/paren/cstyle cast.

- Range-for: array ranges build `__range_begin`/`__range_end`, Cond `!=`, Inc `++`.

- Layout: non-virtual bases laid out after optional vptr (VTT/virtual bases later).
- constexpr: require body; EvaluateAsConstantExpr uses ICE fallback.
- FormBinOp: unique member operatorOP → CXXOperatorCallExpr.

- Layout: dynamic `CXXRecordDecl` reserves leading vptr in `ItaniumRecordLayoutBuilder::LayoutFields` (NeverC ABI v1).

- std: bitset/forward_list/valarray/numeric/random/regex/filesystem/execution/version + C library bridges + pmr/system_error/typeindex scaffolds.
- Templates: `isAtLeastAsSpecialized` partial-ordering scaffold.
- Modules: `OnModuleDecl` + ParseModuleDecl wiring (no BMI yet).

- AST: `CXXConstructExpr` StmtNode + class; ScalarEmitter visit.

- AST: scaffold classes for CXXMemberCall/OperatorCall/MaterializeTemporary/DefaultArg/DefaultInit/ScalarValueInit/UnresolvedConstruct/DependentScopeMember.
- ScalarEmitter visits for the new CXX expr nodes.

- EH: `EHPersonality::NeverC_CXX` → `__neverc_personality_v0` for C++ TUs.
- Templates: function/class specializations shallow-attach pattern bodies via `setBody`.

- Try/catch: `CXXTryStmt::Create` stores handlers; `genCXXTryStmt` scaffold; personality still open.
- Virtual calls: `genCallee` loads NeverC ABI v1 vtable slot for virtual methods.
- std: unordered_set/deque/list/queue/stack/any/charconv/complex/ratio/future/condition_variable/shared_mutex/stop_token/barrier/latch/semaphore scaffolds.

- ScalarEmitter: `VisitCXXThisExpr` / `New` / `Delete` / `Throw` / `DynamicCast` call `FE.ME.getCXXABI()`; `FunctionEmitter::CXXThisValue` set from implicit this after prolog.
- NeverCCXXABI: IR for `_Znwm` / `_ZdlPv`, `__cxa_throw` / allocate, `__neverc_dynamic_cast`; vtable slots via `addrOfFunction` / `__cxa_pure_virtual`.
- Range-for: `genCXXForRangeStmt` + StmtEmitter dispatch (classic for-shape when Cond/Inc present).
- Driver: `AddRunTimeLibs` links `-lneverc_cxx_runtime` for C++ jobs.

- Exception specs: 3-bit `ExceptionSpecType`; parse `noexcept` / `noexcept(true|false|expr)`; SemaType → `EST_BasicNoexcept` / `EST_NoexceptFalse` / `EST_DependentNoexcept`.
- Overload: `UnresolvedLookupExpr` + call-site `BestViableFunction` path.
- Mangling: repaired `mangleSimpleTypeName`; nested names; ctor/dtor/operator encodings.
- Templates: `getTemplateTypeParmType` factory; `SubstType` (pointer/ref/function/array decay); `InstantiateFunctionTemplate` / `InstantiateClassTemplate` scaffolds.
- ABI: `NeverCCXXABI::emitVTable` / `getAddrOfRTTIDescriptor`.
- Range-for: expanded `CXXForRangeStmt` children; Sema retains range expr + body.
- std/runtime: expanded non-stream headers + `runtime/cxx` implementations.

## Phase 3-4 continuation notes (auto)
- Overload: AddOverloadCandidate uses Sema::TryImplicitConversion ICS.
- Lambda: capture-default [=]/[&] + capture-ids -> closure FieldDecls; LambdaExpr stores capture metadata; return type inferred from body.
- Class templates: InstantiateClassTemplate clones methods (subst body), fields (SubstType), and direct bases (SubstType).
- Function templates: body via substStmtRecursive (if/while/for/do + exprs).

## Remaining-features batch (no tests/polish/build)
- CXXCtorInitializer AST + Sema BuildMember/Base + SetCtorInitializers; parse `:` mem-initializer-list; FunctionEmitter emits member stores after vptr.
- Overload ICS: setUserDefined / TryUserDefinedConversion via CXXConversionDecl.
- Lambda: ParseTypeName-lite param types → OnLambdaExpr(ArrayRef<QualType>); capture field types from enclosing params; ScalarEmitter stores capture fields from locals.
- Range-for: member/free begin/end CallExpr inits; ADL-lite associated namespaces of class type.
- Concepts: CheckRequiresClause walks &&/||/!/RequiresExpr/ConceptDecl constraints; RequiresExpr emit bool from body literals.
- VTT: NeverCCXXABI::emitVTT `_ZTT` for virtual bases; layout notes updated.
- Coroutines: OnCoreturnStmt → CoreturnExpr + VisitCoreturnExpr; coawait resume path retained.
- BMI: export fills export-list from TU NamedDecls; import readFrom(LastBMIBlob) injects Idents.
- constexpr: EvaluateAsConstantExpr → EvaluateAsConstantExpr/RValue/Int + float→int.
- NTTP: DeclSpec type-spec + pointer/ref chunks map to QualType.
- Array delete: dtor hook on array form (element loop cookie later).
- Construct emit: dynamic class vptr store scaffold before ctor call.
- requires-clause: reject zero IntegerLiteral; accept other constraints.
- BMI v0: BMIWriter/BMIReader serialize module name + export list.
- Modules: parse captures dotted module name; export module writes BMI v0 blob via BMIWriter into Sema::LastBMIBlob.
- Template partial ordering: arity + variadic + dependent-type mentions.
- Class templates: clone ctors/dtors/methods/fields/bases; Switch/Case subst.
- Ctor IR: FunctionEmitter generateCode stores primary vptr before body for dynamic CXXConstructorDecl.
- Lambda: NumParams counted in parser; call operator gets IntTy scaffold parms + capture fields.
- Still open: full ADL begin/end, concept subsumption, VTT, full ctor init-lists, lambda param AST, constexpr evaluator depth, Phase 5 tests.
- Constraint: no builds until 100% complete.
