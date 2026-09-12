# C++ translation support matrix

Status: experimental built-in C++ implementation of `cpp-core-v1`,
`cpp-core-v2`, `cpp-project-v1`, and `cpp-math-v1`. NeverC statically contains the pinned full
Clang frontend and approved SDK headers. No separate frontend executable,
Clang/LLVM installation or SDK descriptor is needed for translation. Only C++
input translation is implemented; other language adapters remain future work.
The [design](design.md) defines commands, diagnostics, and artifacts.

## Implementation status

| Capability | Evidence/status |
| --- | --- |
| Built-in full-Clang frontend | Private LLVM/Clang 20.1.8 static build, isolated symbols and C ABI, invoked through the current NeverC executable. The separate `utils/translate-prototype/` experiment is historical feasibility evidence. |
| C++ SDK parsing | The approved `<cmath>` header union and minimal SDK version metadata are embedded. Historical `<string>` and `<vector>` syntax probes establish no translation support. See [source provenance](../../../neverc/lib/Translate/Cpp/SDK/README.md). |
| Builtin driver, semantic protocol, artifact checks | Implemented with CLI/IR/artifact tests; production protocol is separate from the P0 prototype format. |
| Experimental `cpp-core-v1` | Implemented: programs/modules, `-O0`/`-O2` full-value comparison, rejection, relocation, output ownership and cancellation tests. Platform and delivery evidence is listed below. |
| Experimental `cpp-core-v2` | Adds 8/16/32/64-bit integers, character literals, constant size/alignment queries, checked type aliases and integral enums, static assertions, bounded object pointers, lvalue references, fixed local arrays/array fields, array decay/indexing and switch control flow to the single-source core contract. Source/carrier sizes, ABI alignments and record field offsets are independently checked against NeverC and guarded in generated code. See the [core v2 contract](cpp-core-v2.md); earlier v1 CI evidence does not establish v2 execution support. |
| Fixed arrays in `cpp-core-v2` | Implemented with O0/O2 execution and protocol/forged-IR regression cases. Includes multidimensional arrays, pointer/reference aliasing, source-order initialization and subscript evaluation. Extent/expansion limits and global/VLA exclusions are in the [core v2 contract](cpp-core-v2.md). New array cases still require revision-specific native CI evidence; the older results below do not verify them. |
| Switch in `cpp-core-v2` | Implemented using existing verified branch/label/jump IR, with O0/O2 execution and source rejection cases for selector effects, fallthrough, nested entry and loop control. GNU ranges and other attributes remain excluded. New switch cases still require revision-specific native CI evidence. |
| Pointer arithmetic in `cpp-core-v2` | Object-pointer offsets/differences and increment/compound assignment use verified strides and typed null/zero helpers. Includes forged-IR bounds checks and O0/O2 execution fixtures for arrays, rows, records and operand sequencing. Native results must be established for the implementing revision; pointer ordering and STL remain excluded. |
| Multi-file project translation | P3A implemented: compilation-database selection, owned headers, per-unit semantic analysis, ODR/linkage checks, combined source/header emission, and original/generated program/library comparison at `-O0`/`-O2`. |
| NeverC math mappings | P3B implemented behind explicit `cpp-math-v1`, pinned SDK/declaration provenance, exact runtime capability checks, and a runtime link probe. The [mapping gates](runtime-mapping.md) include numeric/environment differentials and installed output execution. |
| C++ byte strings / trivial vectors | Follow-up profiles; no translation support implied by parsing their headers. |
| Built-in frontend/SDK delivery | Implemented in normal NeverC builds; upstream Clang is a pinned build-time source input. Runtime and installation evidence is tracked separately below. |

## `cpp-core-v1` acceptance contract — P2

One C++17 source unit, native hosted user space, no includes. The profile is
immutable after release; broader behavior requires an explicitly selected new
profile. All unlisted constructs remain unsupported until entered into this
matrix with lowering and rejection tests.

| Construct | Contract |
| --- | --- |
| Types | `int`, `unsigned int` (both exactly 32 bits in this initial profile), `bool`, `void`; source target widths/conversions must agree with the generated target. |
| Aggregates | Nonempty simple records composed of supported fields, explicit aggregate initialization, copy, and field access. Reject recursive by-value records, user-defined constructors/destructors/member functions, inheritance, unions, bitfields, access/lifetime features outside this subset. |
| Functions | Supported by-value arguments/results and locals; ordinary functions and scalar recursion; namespace and overload resolution performed by Clang. No external function dependencies. |
| Initialization | Preserve explicit initialization and permitted source initialization semantics. Do not silently zero-initialize an uninitialized C++ local. No dynamic global initialization or static locals. |
| Arithmetic/conversions | Source integer promotions, usual signed/unsigned conversions, unsigned wrap, comparisons, and bool conversion. The guarantee covers defined source executions; it does not promise complete static detection of arbitrary undefined behavior. |
| Control flow | Branches/loops, early return, short-circuit `&&`/`||`, and supported scalar expressions. Lower effects explicitly; do not inherit C evaluation order accidentally. Additional statement forms require explicit support/tests. |
| Names | Namespaces and resolved overloads; deterministic emitted identities, with file identity for internal linkage. Avoid NeverC/runtime reserved names. |
| Globals | Only explicitly supported compile-time global constants. Mutable/dynamically initialized global state is outside this profile. |
| Entry points | No-argument `int main()` when present; modules may omit `main`. Never invent an entry point. |
| C ABI exports | Explicit supported scalar parameter/result signatures; no aggregate ABI, class ABI, or foreign-object identity guarantee. |
| Preprocessing | All includes rejected, including unused includes. Time-dependent and path-observing macros (`__FILE__`, `__BASE_FILE__`, `__FILE_NAME__`) are rejected. Admitted macro inputs are recorded. Unsupported directives/pragma behavior must be diagnosed. |

The following are rejected: pointers/references, arrays, floating-point types,
enums, unions/bitfields, user-defined special members/member functions,
templates, inheritance/virtual dispatch, exceptions, RTTI, lambdas, variadics,
volatile/atomic operations, static locals, dynamic global initialization,
foreign-function dependencies, and unsupported preprocessing/options. Rejection
applies to all selected owned declarations/definitions, even unused code.

This is a source migration contract. It does not preserve arbitrary C++ binary
ABI, support the whole C++ standard, or automatically translate an ecosystem.
Naïve source-to-source printing is not an accepted implementation.

## Explicit extension profiles

| Profile | Extension | Status |
| --- | --- | --- |
| `cpp-project-v1` | `cpp-core-v1` plus explicitly selected compilation-database inputs, owned project headers, dependency closure, shared types/linkage, scalar C ABI boundaries, and explicit target validation. | Implemented; host and delivery evidence below. |
| `cpp-math-v1` | `cpp-project-v1` plus the bounded binary64 operations below and approved `std::fabs(double)`/`std::floor(double)` mappings. | Implemented; SDK/runtime gates and host evidence below. |
| String follow-up | Exact byte operations with encoding/NUL/aliasing/ownership/lifetime/allocation-error contract. NeverC UTF-8 `string` is not automatically `std::string`. | Unversioned and unimplemented until the operation list is fixed. |
| Trivial-vector follow-up | Exact element/operation list with bounds, invalidation, storage, lifetime, growth/overflow, and allocation-error behavior. | Unversioned and unimplemented. |

Project and math profiles require `--compdb`, `--project-root`, explicitly selected
source files, and `--out-dir` or `--check`. They emit one `translated.nc` and
`translated.h`, after analyzing each original translation unit independently.
The [context contract](p3-compilation-context.md) defines supported options,
response files, selection, and raw versus normalized input hashes; the
[project contract](p3-project-emission.md) defines linkage and ODR restrictions.

Math accepts binary64 literals, value copies, parameters/results and aggregate
fields; conversions between `double` and supported bool/int32/uint32 values;
unary `+`/`-`; and comparisons. General double arithmetic, compound arithmetic,
increment/decrement, `float`, `long double`, and source floating-environment APIs
remain rejected. Source builtin NaN/infinity constructors, bit casts, and
`numeric_limits` are rejected; runtime NaNs and infinities may enter through
module parameters. Constant folding cannot hide an unsupported source construct.
The accepted source options fix no fast math and disabled FP contraction. The
FP contract `cpp.math.binary64.masked.v1` requires masked traps and the documented
binary64 environment; it does not authorize arbitrary FP options or operations.

Math requires an explicit x86_64 or arm64 macOS 15.0 target and the approved
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5` distribution. Equivalent Darwin
target spellings are accepted only when their effective deployment is macOS
15.0. The immutable built-in catalog approves the 209 original headers and
NeverC-authored version metadata; there is no external SDK selection or host
discovery. Standard-library overloads or distributions outside this boundary fail.

P3B uses P3A include/context handling. Full Clang may parse standard-library
internals to resolve approved operations, but those bodies are not recursively
translated. An ignored header must not hide owned code, custom specializations,
or unapproved calls. Broader profiles are never inferred from an include.

## Host/target evidence

The original process-separated implementation passed 126 integrated tests on
both native arm64 and Rosetta x86_64, including SDK/runtime gates, environment
isolation, O0/O2 execution and full-value differentials. Its installed generated
output also ran independently of the old frontend and SDK descriptor. Those
runs are a semantic regression baseline, not proof of the new built-in delivery.

| Host / source target | Built-in implementation evidence | Support boundary |
| --- | --- | --- |
| macOS arm64 / `arm64-apple-macosx15.0.0` | Earlier recorded built-in core protocol suite: 73 passed. Earlier built-in project and math suites passed 33 and 49 cases respectively. Isolated fresh-prefix installation, translation and O0/O2 execution passed with default mimalloc, Python plugins and bundled Python enabled. Full integrated-suite status is recorded below. | Native macOS runner; bounded profiles only. |
| macOS under Rosetta x86_64 / `x86_64-apple-darwin24.6.0` | Prior 126-test process-separated baseline above. Local built-in validation was not completed. | Math target is explicitly macOS 15.0; built-in and native Intel execution are not established. |
| Linux x64 / arm64 | At `b02468ad`, the [x64](https://github.com/NeverSight/NeverC/actions/runs/34596323748) and [arm64](https://github.com/NeverSight/NeverC/actions/runs/34596323441) formal CI workflows completed successfully. | Workflow completion evidence only; per-test execution and installed-prefix translation were not separately reviewed here. The math profile remains restricted to its approved macOS targets. |
| Windows x64 / arm64 | At `b02468ad`, the [x64 Clang](https://github.com/NeverSight/NeverC/actions/runs/34596323650) and [arm64 Clang](https://github.com/NeverSight/NeverC/actions/runs/34596323464) logs verify the built-in build, embedded-runtime relink, installation and full CTest run. Counts and skips are recorded below. | Bounded profiles and explicit test skips; installation of the compiler is distinct from an installed-prefix C++ translation smoke. The math profile remains restricted to its approved macOS targets. |
| Android, kernel, freestanding, dyncode | Outside initial hosted translation profile. Existing NeverC support for such modes is not translator validation. | Not advertised. |

The arm64 SDK probes explicitly select their target. A NeverC build under Rosetta
can default to x86_64 on the same arm64 machine; its source and generated targets
must be aligned explicitly and its execution evidence recorded separately.

The passing arm64 installation smoke used the real CMake installation components
for NeverC, resource headers, standard resources and the macOS arm64 runtime.
Its sandbox denied both development source/build trees, Homebrew and the system
developer directory. Inside that sandbox, the installed compiler translated
core, project and math fixtures, built and executed their output at `-O0` and
`-O2`, and linked separate C clients for the generated modules. Recursive dynamic
dependency inspection found no external Clang/LLVM libraries. Installed license
notices and all 210 catalog source entries were verified by hash: 209 original
headers and NeverC's version metadata. The installed compiler retained its
bundled Python and ordinary system dependencies. This smoke does not substitute
for a complete regression run on every advertised host.

Before built-in integration, an additional arm64-compiler → x86_64-target probe with default mimalloc enabled
could not complete its runtime link on this host. This combination is not in the
accepted matrix. After the process-cleanup fix, the driver returned `TR0404` at
the link stage in about 60 seconds, published a failure report, and removed staging
without publishing generated artifacts. The OS kept the killed compiler child
unreapable; the driver reported that condition instead of waiting indefinitely.
The old x86_64 baseline used the matching Rosetta compiler with mimalloc off.

NeverC's existing compiler target matrix must not be copied into the translator's
support claims. Each advertised combination requires matching source/generated
data models, SDK provenance, runtime capabilities when used, and execution on a
matching runner. Cross-compilation alone is insufficient.

Other language adapters remain planned in issue #16's order: E Language after
the C++ P3 baseline, then Python, Go, Rust, TS, and JS. C compilation continues
through the existing direct NeverC path.

## Repository regression validation

The early local built-in arm64 integrated run passed 131 of 132 tests. Its
remaining shadow-header assertion was corrected; the second local run stopped
when full validation moved to GitHub CI. These are historical results. The
earlier local protocol and installation evidence above predates the numeric byte
initializer for MSVC, Windows path normalization and full-host symbol audit.

At code revision [`b02468ad1ccda72b16393e4fab9147c201f2ee1f`](https://github.com/NeverSight/NeverC/commit/b02468ad1ccda72b16393e4fab9147c201f2ee1f),
all ten formal CI workflows completed successfully: seven platform builds,
the C++ frontend tools, code quality, and documentation/test-dependency checks.
This includes the [macOS arm64 workflow](https://github.com/NeverSight/NeverC/actions/runs/34596323438).
Linux and macOS evidence for this revision is workflow completion only; no
additional per-test, numerical-observation or installed-prefix smoke results
are inferred here from those workflow statuses.

The Windows Clang build, installation and test logs were reviewed separately.
Both architectures completed the initial and embedded-runtime executable links,
passed the private/host ABI audit, and installed `neverc.exe`. Each CTest
inventory contained 2,899 tests:

| Windows Clang runner | Passed | Skipped | Failed |
| --- | ---: | ---: | ---: |
| x64 | 2,829 | 70 | 0 |
| arm64 | 2,780 | 119 | 0 |

Each runner's `neverc-translate.*` group contained 130 passed and 21 skipped
tests. The skips cover three independent reference-compiler comparisons,
15 macOS-specific math tests, two POSIX process fixtures and one controlled
long-path test. The logs do not identify which long-path environment condition
caused that skip. Core/project protocol tests and the executable core translation
fixtures passed. Windows does not register the math protocol suite.

The separate `cpp-frontend-tools` workflow exercises the archive-index parser
and ABI collision checks on Linux, Windows and macOS before the full compiler
builds finish. Compiler builds also run those checks against their real archives,
including Clang LTO and MSVC LTCG inputs.
The macOS workflow includes the fresh-prefix installation smoke with reads of
the checkout, selected Xcode developer directory and installed Homebrew/developer
roots denied to NeverC and its generated programs. That gate checks installed
core, project and math translation at O0/O2. Its current detailed smoke log was
not separately reviewed for the CI summary above; earlier measured installation
evidence remains recorded in its own section.

The repository gate requires default mimalloc and Python-plugin options enabled,
with bundled Python for the installed configuration. `PluginGlobalState`
validates symbols from the enabled Python plugin implementation; a development build with that option disabled does not
satisfy that artifact check. Python plugins are an existing compiler extension
mechanism and do not provide Python-to-NeverC source translation.

The external Clang reference compiler and its own headers/SDK are independent
test oracles only. Production translation and built-in SDK admission tests do
not select an external frontend or require an SDK environment variable.
Arm64 CI builds must use a native execution environment so CMake's recorded host
architecture matches the compiler and managed Python runtime. The DynCode test
loader also explicitly selects arm64 when its test harness is arm64.

The focused translation suite covers source/IR equivalence, SDK/runtime
admission, project closure, bounded file and JSON input, process cancellation,
artifact publication and environment isolation. The frontend protocol suites
exercise all owned declarations, including unreachable unsupported source, and
ensure pruned mapped calls leave no unused mapping metadata. Installed-prefix
checks use normal CMake installation components and verify translation plus
generated project/math execution at O0/O2 with development Clang/SDK/source paths
unavailable. They also inspect symbols and dependencies outside that sandbox.
No repository test exclusion is added by this change.
