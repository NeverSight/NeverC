# C++ translation support matrix

Status: experimental C++ P0–P3 implementation of `cpp-core-v1`,
`cpp-project-v1`, and `cpp-math-v1`. Both native macOS arm64 and Rosetta x86_64
passed all 126 integrated translation tests, including environment isolation,
SDK/runtime verification, O0/O2 execution, and full-value differential checks.
Fresh-prefix installation and generated-code independence are verified with the
declared external LLVM/Clang 20.1.8 and Apple SDK dependencies. This is an
external-toolchain installation, not a self-contained frontend/SDK release.
The [design](design.md) defines commands, diagnostics, and artifacts.

## Implementation status

| Capability | Evidence/status |
| --- | --- |
| Full-Clang frontend candidate | Separate LLVM/Clang 20.1.8 feasibility prototype under `utils/translate-prototype/`; its README records build and measurements. |
| C++ SDK parsing | Reproduced `<cmath>`, `<string>`, and `<vector>` syntax probes with libc++ headers from the 20.1.8 distribution and Apple SDK 15.5. See [measured inventory](p0-sdk-probes.md). |
| Builtin driver, semantic protocol, artifact checks | Implemented with CLI/IR/artifact tests; production protocol is separate from the P0 prototype format. |
| Experimental `cpp-core-v1` | Implemented and locally exercised through the actual helper and NeverC: programs/modules, `-O0`/`-O2` full-value comparison, rejection, relocation, output ownership, and cancellation tests. Installed helper discovery and independent generated-code builds are verified; platform coverage is listed below. |
| Multi-file project translation | P3A implemented: compilation-database selection, owned headers, per-unit semantic analysis, ODR/linkage checks, combined source/header emission, and actual-helper program/library comparison at `-O0`/`-O2`. |
| NeverC math mappings | P3B implemented behind explicit `cpp-math-v1`, pinned SDK/declaration provenance, exact runtime capability checks, and a runtime link probe. The [mapping gates](runtime-mapping.md) include numeric/environment differentials and installed output execution. |
| C++ byte strings / trivial vectors | Follow-up profiles; no translation support implied by parsing their headers. |
| Self-contained frontend/SDK distribution | Unvalidated. Current probes require an external development toolchain and Apple SDK. |

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
| `cpp-project-v1` | `cpp-core-v1` plus explicitly selected compilation-database inputs, owned project headers, dependency closure, shared types/linkage, scalar C ABI boundaries, and explicit target validation. | Implemented and integrated on both measured macOS architectures. |
| `cpp-math-v1` | `cpp-project-v1` plus the bounded binary64 operations below and approved `std::fabs(double)`/`std::floor(double)` mappings. | Implemented; 126-test integrated suite and installed output execution verified. |
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
Clang 20.1.8 / libc++ 200100 / macOS SDK 15.5 distribution. Equivalent Darwin
target spellings are accepted only when their effective deployment is macOS
15.0. The descriptor `--cpp-sdk PATH` defaults to `neverc-cpp-sdk.json` beside
the helper. It locates external roots; only the compiled catalog approves their
contents. Standard-library overloads or distributions outside this boundary fail.

P3B uses P3A include/context handling. Full Clang may parse standard-library
internals to resolve approved operations, but those bodies are not recursively
translated. An ignored header must not hide owned code, custom specializations,
or unapproved calls. Broader profiles are never inferred from an include.

## Host/target evidence

| Host / source target | Development evidence | Released translation support |
| --- | --- | --- |
| macOS arm64 / `arm64-apple-macosx15.0.0` | 126 integrated tests, real SDK/runtime gates, generated module numeric/environment comparison, and fresh-prefix O0/O2 execution with default mimalloc enabled. | External-LLVM/SDK installation verified; no self-contained SDK package claim. |
| macOS under Rosetta x86_64 / `x86_64-apple-darwin24.6.0` | 126 integrated tests and actual-helper program/project/math differential execution at `-O0`/`-O2`; the development compiler disables mimalloc. The arm64 helper receives an explicit x86_64 source target. | External-LLVM/SDK installation only; math target is explicitly macOS 15.0. |
| Linux x64 / arm64 | No frontend/SDK/differential installation evidence in this P0 measurement. | Not advertised. |
| Windows x64 / arm64 | No frontend/SDK/differential installation evidence in this P0 measurement. | Not advertised. |
| Android, kernel, freestanding, dyncode | Outside initial hosted translation profile. Existing NeverC support for such modes is not translator validation. | Not advertised. |

The arm64 SDK probes explicitly select their target. A NeverC build under Rosetta
can default to x86_64 on the same arm64 machine; its source and generated targets
must be aligned explicitly and its execution evidence recorded separately.

An additional arm64-compiler → x86_64-target probe with default mimalloc enabled
could not complete its runtime link on this host. This combination is not in the
accepted matrix. After the process-cleanup fix, the driver returned `TR0404` at
the link stage in about 60 seconds, published a failure report, and removed staging
without publishing generated artifacts. The OS kept the killed compiler child
unreapable; the driver reported that condition instead of waiting indefinitely.
The x86_64 evidence above uses the matching Rosetta compiler with mimalloc off.

NeverC's existing compiler target matrix must not be copied into the translator's
support claims. Each advertised combination requires matching source/generated
data models, SDK provenance, runtime capabilities when used, and execution on a
matching runner. Cross-compilation alone is insufficient.

Other language adapters remain planned in issue #16's order: E Language after
the C++ P3 baseline, then Python, Go, Rust, TS, and JS. C compilation continues
through the existing direct NeverC path.

## Repository regression validation

Run the native repository gate with the default mimalloc and Python-plugin
options enabled. `PluginGlobalState` validates symbols from the enabled Python
plugin implementation; a development build with that option disabled does not
satisfy that artifact check. Python plugins are an existing compiler extension
mechanism and do not provide Python-to-NeverC source translation.

```sh
NEVERC_CPP_FRONTEND=/path/to/neverc-cpp-frontend \
NEVERC_CPP_REFERENCE_COMPILER=/path/to/clang++ \
NEVERC_CPP_SDK=/path/to/neverc-cpp-sdk.json \
CTEST_PARALLEL_LEVEL=4 \
cmake --build build-neverc --target check-neverc
```

Use the pinned Clang reference compiler and approved SDK described above.
Configure and run arm64 builds from a native shell so CMake's recorded host
architecture matches the compiler and managed Python runtime. The DynCode test
loader also explicitly selects arm64 when its test harness is arm64.

The focused translation suite covers source/IR equivalence, SDK/runtime
admission, project closure, bounded file and JSON input, process cancellation,
artifact publication and environment isolation. The separate helper suites
exercise all owned declarations, including unreachable unsupported source, and
ensure pruned mapped calls leave no unused mapping metadata. Installed-prefix
checks verify generated project/math execution at O0/O2 after removing the C++
helper and SDK descriptor. No repository test exclusion is added by this change.
