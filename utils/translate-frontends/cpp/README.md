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
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5`. The
[SDK catalog](../../../neverc/lib/Translate/Cpp/SDK/catalog.json) records all
209 approved header files and separate SDK metadata. The original header bytes
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
