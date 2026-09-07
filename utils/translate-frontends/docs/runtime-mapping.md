# C++ runtime mapping inventory

Status: `cpp-math-v1` implements the two bounded mappings below. Core and project
profiles enable no library mappings. SDK admission, resolved declaration checks,
typed IR verification, exact runtime capability checks, and a separate link
probe run in the driver. The
[support matrix](support-matrix.md) records exact platform coverage. These two
mappings do not establish a general C++ library contract.

## Mapping policy and required record

Prefer an existing NeverC runtime/std API, then a thin semantic wrapper, then a
narrow reusable helper or std improvement. Reject the operation if the source
contract cannot be preserved. Never copy library implementation sources into
generated projects, match only by spelling, or retain an undeclared foreign
runtime dependency.

The built-in frontend resolves the exact operation, signature, imported declaration and
all redeclarations while Clang retains their source semantics. The shared IR
carries an operation ID and source evidence. A consumer-owned table selects the
runtime symbol and header; incoming data cannot supply arbitrary emitted names.
Manifests record SDK and declaration provenance, the FP contract, runtime/header
identities, required modules/options, and generated-file hashes. Reports list
applied mappings. Empty requirements are explicit for mapping-free inputs.

## Implemented operations

| Resolved source operation | Operation ID | NeverC API/header | Required modules |
| --- | --- | --- | --- |
| Approved `std::fabs(double)` | `cpp.math.fabs.f64.v1` | `double neverc_math_abs(double)` in `<neverc/std/math.h>` | `math_abs`; no nonintrinsic external dependencies. |
| Approved `std::floor(double)` | `cpp.math.floor.f64.v1` | `double neverc_math_floor(double)` in `<neverc/std/math.h>` | `math_floor`; no nonintrinsic external dependencies. |

Do not generalize to `std::abs`, other overloads, arbitrary `<cmath>` functions,
or another library distribution. The approved source context is Clang 20.1.8,
libc++ 200100, and the approved macOS 15.5 headers embedded in NeverC, with
explicit x86_64/arm64 macOS 15.0 targets.
The libc++ `cmath` using declarations import platform `math.h` declarations;
approval checks both boundaries. Owned replacements, redeclarations, namespace
aliases, and lookalikes cannot acquire approval by sharing their spelling.

The [support matrix](support-matrix.md) defines the bounded double language
operations and rejected source constructs. Dynamic NaNs/infinities are covered
at module boundaries. Source builtin NaN/infinity construction is rejected:
a constant signaling-NaN floor at Clang `-O2` was observed to quiet the value
without raising the same exception as a runtime call. The implementation does
not erase that difference by silently mapping the constant call.

## Exact NeverC implementations

Public declarations are in [std/include/neverc/std/math.h](../../../std/include/neverc/std/math.h)
and source inventory is in [std/manifest.json](../../../std/manifest.json).
[abs.c](../../../std/src/math/abs.c) clears the binary64 sign bit and preserves
all other bits. This yields positive zero and infinity and retains NaN payload
and quiet/signaling bits while clearing their sign.

[floor.c](../../../std/src/math/floor.c) now rounds finite values using integer
binary64 bits. Zeros, integral values, and infinities return unchanged; a
nonzero magnitude below one returns negative one or positive zero as appropriate.
Other fractional values clear their fraction after incrementing a negative
magnitude. Only NaNs execute `x + x`, which quiets signaling NaNs and raises
invalid under the approved masked-trap environment. There is no `modf` or `trunc`
call in the approved floor payload.

The previous floating comparison/modf/subtraction implementation failed probes
for subnormal exception flags, signaling NaN quieting, and large negative inputs
under some rounding/optimization settings. Old embedded payloads are rejected
even if their public symbol and signature still match. There is no explicit
`errno` assignment in either current mapping implementation; observable behavior
is established by differential tests, not that source observation alone.

## Numeric and observable-error evidence

The accepted FP contract is `cpp.math.binary64.masked.v1`. Tests compare result
bits, `errno`, exception flags, and retained rounding mode, with masked traps and
normal binary64 subnormal handling. Inputs include ordinary signed fractions,
both zeros, subnormals, infinities, quiet/signaling NaNs, integer-precision
boundaries, and deterministic seeded bit patterns. Tests preserve a preexisting
divide-by-zero flag and exercise all four standard rounding modes.

Before built-in frontend integration, the runtime candidate passed 32,944 observations for each x86_64/arm64 and
`-O0`/`-O2` pair. The native arm64 integrated suite also executed the real
C++-to-NC module through the same 32,944-case harness at both optimization levels.
Externally supplied parameters prevent constant folding from removing tested
calls. Source constant-construction rejection and double IR tests cover the
separate compile-time boundary. The full installed-target matrix and final
regressions must stay green before declaring P3B acceptance complete.

The fixtures are [math.cpp](../../../tests/neverc/Inputs/translate/cpp/stdlib/math.cpp)
and [math-contract-harness.c](../../../tests/neverc/Inputs/translate/cpp/stdlib/math-contract-harness.c).
The existing [std math suite](../../../tests/neverc/std/test_math.c) supplements
these checks but does not replace translation differential tests.

## SDK and runtime capabilities

[CppSdk.cpp](../../../neverc/lib/Translate/Cpp/CppSdk.cpp) loads the built-in SDK
only for math, validates its immutable 209-header inventory and version metadata,
and separately verifies every consumed SDK dependency. The frontend and consumer
use the same catalog and embedded bytes. Caller-supplied context or hashes cannot
approve a different SDK. Source, owned headers, compilation database, response
files, SDK evidence and the installed runtime header are rechecked before
publication. The SDK [source provenance and notices](../../../neverc/lib/Translate/Cpp/SDK/README.md)
accompany the embedded distribution. See the [capability contract](p3-math-capabilities.md).

[LibraryMappings.cpp](../../../neverc/lib/Translate/Cpp/LibraryMappings.cpp)
checks the exact installed math header, builtin-std policy, target payload,
function type/calling convention, definition closure, and fixed canonical-IR
implementation identity. Its approved abs/floor payloads have no nonintrinsic
external dependencies. Missing, stale, malformed, wrong-target, or wrong-signature
payloads fail `TR0403`; a nonzero embedded module count is insufficient.

The fingerprint policy retains all semantic IR, target/layout and attributes;
it removes only specified source/debug/compiler-identification metadata. Raw
payload hashes are also recorded as provenance. Relocated source builds and
native arm64 versus Rosetta builders produced the same four approved canonical
identities. A changed implementation requires a reviewed IR diff and the numeric
and installed-output gates before the fixed registry is updated.

The driver validates generated NC syntax and object code, then links a separate
probe that calls each required runtime API with a runtime value. It does not
execute the user's source or require a module to define `main`. Link failure is
`TR0404`. `-fno-builtin-std` is an explicit math validation policy flag: required
mappings fail capability checks when disabled. Mapping-free math inputs do not
require unused payloads. Compiler child environments and default configuration
must not replace the header or policy whose identity was checked.

## Installation boundary

Translation uses the installed NeverC executable's built-in frontend and SDK.
No separate frontend, Clang/LLVM library installation or SDK descriptor is used.
The approved headers retain their original bytes and notices; NeverC supplies
minimal version metadata rather than redistributing the original complete SDK
metadata. Generated math code still requires the ordinary NeverC math resources
whose identities the driver verifies. Moving or copying only the executable does
not supply a missing installed runtime header.

Fresh-prefix checks must translate and execute program/module output at O0/O2
with development frontend and SDK paths unavailable, and exercise a separate C
client through the generated header. Dependency inspection must verify that
generated output has no C++ standard library, exception, Clang or LLVM dependency,
and that NeverC itself does not depend on external Clang/LLVM libraries. Existing
NeverC allocator or optional plugin dependencies remain separate. The
[support matrix](support-matrix.md) records completed runs.

Installation execution must be recorded for each advertised target. A successful
header parse or syntax-only run is insufficient.
Final acceptance also checks runtime-call emission, unresolved symbols, program
and module execution, wrapper overhead, binary size, and platform dependencies.

## String/vector prerequisites

`std::string` is a mutable byte sequence with counted lengths and possible
embedded NULs; NeverC's value-semantic UTF-8 `string` is not automatically an
equivalent representation. A follow-up must list exact accepted operations and
specify encoding, indexing units, aliasing, mutation, lifetime, allocation
failure, and length-overflow behavior. Select bytes when text semantics do not
fit. Parsing `<string>` alone enables no mapping.

A trivial-vector follow-up similarly needs an exact element/operation list,
alignment, initialization/destruction, element identity, iterator/reference
invalidation, bounds, growth/overflow, and allocation/error rules. References,
lifetimes, and supported error propagation must exist before the affected
operations can ship. Full exception handling and arbitrary templates remain
independent work rather than prerequisites for the bounded P3 math baseline.
