**Languages**: [English](translate.md) | [简体中文](zh-CN/translate.md) | [繁體中文](zh-TW/translate.md) | [日本語](ja/translate.md) | [한국어](ko/translate.md) | [Français](fr/translate.md) | [Deutsch](de/translate.md) | [Español](es/translate.md) | [Italiano](it/translate.md) | [Русский](ru/translate.md) | [العربية](ar/translate.md)

[← Documentation](README.md)

# Translate C++ to NeverC

Experimental `neverc translate` emits reviewable `.nc` source through `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` and `cpp-math-v1`.

**Only C++ input translation is currently implemented.** Support for E Language (易语言, `.e`), Python, Go, Rust, TypeScript and JavaScript is planned; their translators are not yet available.

Core v2 supports mutable namespace-scope integer, boolean and enum globals with zero initialization or a fully checked constant initializer. Their storage and addresses persist across calls; references, pointers and parameter defaults access the same variable. An `extern` declaration must resolve to a definition in this source unit. Const globals remain read-only. Dynamic initialization, global records/arrays/pointers/references, thread-local storage and static locals retain their separate restrictions.

Core v2 supports C++17 range-based `for` over supported fixed arrays and source-defined ranges with resolved member or ADL `begin/end` calls. Value/reference loop variables, record iterators and different sentinel types preserve ordinary call and lifetime rules. Range initialization and `begin/end` run once; iteration objects are destroyed before increment or exit, including `continue`, `break` and `return`. Templates, standard headers, STL containers, structured bindings and C++20 range initializers remain outside this increment. Native validation requires CI from the implementing revision.

Core v2 supports private and protected data members in otherwise supported standard-layout classes. Embedded Clang checks access before translation; authorized methods, constructors, factories, default arguments and generated copy/move operations use the same typed member storage. Illegal outside access remains a C++ diagnostic. Access labels are source rules, not a runtime secrecy feature. Mixed-access non-standard-layout classes, friends, inheritance, nested records and unsupported field types retain their restrictions. Native results require the implementing revision’s CI.

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

Core v2 now supports signed and unsigned 8-, 16-, 32- and 64-bit integers, character types and literals, and constant `sizeof`/`alignof` queries. Source promotions and overload resolution precede width normalization; `long`, `wchar_t` and the size type follow the selected target. This includes narrow and wide enum underlying types. Runtime strings and STL are still being developed.

Core v2 also supports object-pointer offsets, differences, increment/decrement and compound assignments, including array iteration and multidimensional strides. Generated helpers preserve C++17 null-pointer plus/minus zero and null-pointer difference. Pointer ordering and pointer/integer casts remain unsupported; this does not yet provide STL containers or algorithms.

Core v2 verifies source sizes and ABI alignments against NeverC’s own target model, including aggregate sizes and field offsets. Generated assertions check the recorded layout again during compilation, and the manifest records this evidence.

Core v2 supports named nonvirtual member functions of the admitted records, including const overloads, lvalue-qualified methods, static methods and `this`. Calls preserve the original object and evaluate the receiver before arguments.

Core v2 also supports ordinary user-provided constructors for standard-layout records with admitted copy operations. Locals, record fields and array elements are constructed directly in their final storage; fields initialize in declaration order. Delegating constructors and exceptions remain unsupported.

Core v2 gives each by-value record parameter a separate object and writes record results directly into the caller’s destination. Constructors and methods use the same rules; source-required copies and reference aliases remain intact. For eligible trivial records, other C++17 implementations may introduce additional argument/result copies.

Core v2 supports ordinary user-defined destructors and implicit member destruction on normal exits. Local objects, record fields and array elements are destroyed in reverse order; temporaries end at full-expression boundaries after their values are captured. Returns, branches, loops, break and continue perform the required cleanup. By-value parameters are destroyed when the callee exits; returned objects belong to the caller. Explicit destructor calls, static destruction and exception unwinding remain unsupported.

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

Core v2 now extends temporary lifetimes for ordinary automatic local references, including `const T& r{T{...}}` and `T&& r = T{...}`. Scalar, enum, pointer and record temporaries use actual storage; references to members or array elements keep the complete record alive when C++ grants extension. Braced and equal-braced reference initialization preserve the same object. Destruction follows the reference scope, including conditions, loops and early exits; other temporaries inside its initializer still end at their own full-expression. Aliases add no owner, and later copies or moves keep their selected operations. Static/global/thread-local references, reference fields, templates and complete STL remain in development.

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

CI results, execution and installation checks, and skipped tests are recorded by platform. Native macOS arm64 and macOS x86_64 under Rosetta remain distinct validation environments. General C++/STL support, exceptions, templates, strings and `std::vector` are outside the advertised contract. See the [support matrix](../utils/translate-frontends/docs/support-matrix.md), [protocol and recovery rules](../utils/translate-frontends/docs/protocol.md), and [project fixture](../tests/neverc/Inputs/translate/cpp/project).
