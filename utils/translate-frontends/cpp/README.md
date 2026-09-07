# Pinned C++ frontend profiles

`neverc-cpp-frontend` is the separate full-Clang adapter for NeverC's experimental
`cpp-core-v1`, owned-project `cpp-project-v1`, and bounded `cpp-math-v1`
translation pipelines. It reads `--request request.json`, writes a new
`--output response.json`, and returns nonzero on source/protocol/subset errors.
The [protocol](../docs/protocol.md) is independent of generated
manifest/report/map schemas. Existing output files are never overwritten.

## Development build and installation

LLVM and Clang **20.1.8** development packages are mandatory and checked by CMake.
The helper builds as C++17, independently of NeverC's stripped frontend. On the
tested macOS arm64 development host:

```sh
cmake -S utils/translate-frontends/cpp -B /tmp/neverc-cpp-frontend-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang++ \
  -DLLVM_DIR=/opt/homebrew/opt/llvm@20/lib/cmake/llvm \
  -DClang_DIR=/opt/homebrew/opt/llvm@20/lib/cmake/clang
cmake --build /tmp/neverc-cpp-frontend-build
cmake --install /tmp/neverc-cpp-frontend-build --prefix /tmp/neverc-cpp-frontend-install
```

On other build hosts select that host's supported compiler/package paths; omit
the macOS architecture setting. Their release installation/execution acceptance
is separate from this measured host. An arm64 helper process can parse the exact
requested x86_64 target; executable architecture and source target are distinct.

The installed executable currently retains external LLVM20 `libclang-cpp`,
`libLLVM`, their system dependencies, and the Homebrew zstd dependency. Installing
this target does **not** copy or relocate those libraries. This is an explicit
external-toolchain development installation, not a self-contained release.
The [P0 measurements](../../translate-prototype/README.md) record that dependency
closure and its size. Clang/runtime library versions must remain pinned.

Core translation passes `--no-default-config -nostdinc -nostdinc++` and admits no
includes. It requires no C++ standard-library headers, Clang resource headers, or
platform SDK at translation time. Building the helper still needs its development
headers and a platform toolchain. Broader profiles require the separately tracked
SDK inventory and licensing/packaging work. Generated scalar NC needs only the
ordinary NeverC distribution and declared platform runtime, and contains no
full-Clang or C++ standard-library dependency.

The `frontend.build` identifier includes SHA-256 of the adapter sources, header,
CMake configuration source, and the Clang version pin. CMake reconfigures when
those source files change so manifests distinguish adapter revisions.

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
The helper analyzes one selected unit at a time. NeverC's shared project merger
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

Before constructing Clang's driver, the single-request helper process clears
implicit compiler configuration variables, including `CPATH`,
`CPLUS_INCLUDE_PATH`, `C_INCLUDE_PATH`, `SDKROOT`, deployment-target variables,
Clang configuration-directory variables and `CCC_OVERRIDE_OPTIONS`. It still
passes `--no-default-config -nostdinc -nostdinc++`. This isolates the helper's
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
The helper explicitly passes `-fno-fast-math -ffp-contract=off` and cannot accept
source options that weaken this contract. Source `__builtin_nan`,
`__builtin_nans`, `__builtin_inf`, bit casts, unions, pointers and
`numeric_limits` constructions remain unsupported; in particular, a folded
signaling-NaN builtin argument is not silently mapped to a dynamic runtime call.
Dynamic binary64 parameters can still carry NaNs and infinities.

The approved translation SDK is
`clang20.1.8-libcxx200100-macos15.5`, with catalog SHA-256
`0c72f368ab38180821ca5c51fdab9f7fd2c3e9d6106d5c327fa09a8c8c795fa4`.
The exact catalog is compiled into the helper. It covers 209 headers in the
union consumed by the two approved target probes, plus separate
`SDKSettings.json` metadata. Each typical `<cmath>` module consumes 201 headers.
Before parsing, the helper checks the complete catalog against the configured
SDK, so a missing header cannot silently alter an SDK `__has_include` branch.
It then hashes the actual consumed buffers again and records them separately
as SDK dependencies. SDK and owned-project roots must not contain each other;
every owned header still receives the complete source allowlist check.

Only Apple macOS 15.0 x86_64/arm64 targets, including equivalent Darwin spellings
resolved by LLVM's target API, are approved for math. The helper can analyze both
from the measured arm64 host; runtime acceptance is supplied separately by the
matching NeverC target builds. Other SDK distributions and target families are
not enabled by this catalog.

The descriptor [example](sdk/neverc-cpp-sdk.example.json) declares the external
libc++ header root, Clang resource root, and Apple SDK root. CMake installs this
example under `share/neverc`; it does not install or redistribute the SDK.
Set its paths for the exact approved local installation, then either pass it to
NeverC with `--cpp-sdk PATH` or place it beside the installed helper as
`neverc-cpp-sdk.json`. The helper receives a validated descriptor identity and
canonical roots through its request, and independently verifies the catalog.
After translation, generated code uses only NeverC's installed headers,
embedded runtime payload and normal platform runtime. The C++ SDK and helper
are not generated-code build dependencies.

## Verification

```sh
python3 utils/translate-frontends/cpp/tests/verify.py \
  --helper /tmp/neverc-cpp-frontend-build/neverc-cpp-frontend \
  --target arm64-apple-darwin24.6.0 \
  --output-dir /tmp/neverc-cpp-frontend-tests
```

Use a new output directory. Repeat with the actual NeverC native target, for
example `x86_64-apple-darwin24.6.0` for the tested Rosetta compiler build. This
suite checks source admission, structured diagnostics, target metadata, folded
globals, resolved fixtures, byte-identical relocated responses, field-only and
discarded-value behavior, and response collision protection. It does not execute
translated user code. The repository `TranslateTests` suite additionally checks
shared protocol/NC validation, generated program and C ABI harness execution at
`-O0`/`-O2`, permitted unspecified-order results, driver failures and artifacts.

The independent project suite adds owned-header hashes and diagnostic locations,
shared inline/field identities, private C/C++ identities, const declarations,
strong/signature conflict evidence, missing per-unit inline definitions,
macro-token/binding differences, relocation and hostile include environments:

```sh
python3 utils/translate-frontends/cpp/tests/project.py \
  --helper /tmp/neverc-cpp-frontend-build/neverc-cpp-frontend \
  --target x86_64-apple-darwin24.6.0 \
  --output-dir /tmp/neverc-cpp-project-tests
```

The project suite passes **33 cases**. The shared consumer independently
parses, verifies and merges actual helper responses, emits the combined
program/header, and builds/runs that program at O0/O2. Macro-token and
private-binding disagreement pairs fail with `TR0301`; missing external
definitions fail with `TR0203`. `TranslateProjectTests` and the installation
checks below additionally cover the complete driver and generated runtime use.

Run the math SDK/provenance and source-admission suite with the approved local
descriptor and a fresh output directory:

```sh
python3 utils/translate-frontends/cpp/tests/math_profile.py \
  --helper /tmp/neverc-cpp-frontend-build/neverc-cpp-frontend \
  --sdk utils/translate-frontends/cpp/sdk/neverc-cpp-sdk.example.json \
  --target x86_64-apple-macosx15.0.0 \
  --output-dir /tmp/neverc-cpp-math-tests
```

The final helper build
`cpp-frontend-1-e73e074bd48f85a3bc0c70a70b57c52ed06a5220291ae1f8252d23832746c8dc`
passed **44 math cases**, **33 project cases** and **52 core cases for each of
arm64 and x86_64** on 2026-09-07. Math checks include exact zero/subnormal/maximal
finite bits, casts/comparisons, member-only loads and sequencing, unsupported
floating operations and NaN builtins, fake library declarations, SDK copying
and tampering/missing files, environment include poisoning, and default Clang
configuration poisoning from HOME and adjacent files. Unreachable approved
calls are still checked during source admission but leave mapping metadata only
when a lowered call remains; unreachable unsupported calls still fail. The latter checks verify
that the explicit `--no-default-config` survives the LibTooling path.

## Installed-prefix verification

Run the installation test with a fresh output prefix and your configured SDK
descriptor. This example uses a native arm64 NeverC build; substitute the path
of the compiler configuration being tested:

```sh
python3 utils/translate-frontends/cpp/tests/install.py \
  --neverc build-translate-arm64/bin/neverc \
  --neverc-build build-translate-arm64 \
  --helper-build /tmp/neverc-cpp-frontend-build \
  --cpp-sdk /path/to/neverc-cpp-sdk.json \
  --prefix /tmp/neverc-cpp-installed-smoke
```

With `--neverc-build`, the script installs the build's `neverc` CMake component
into the new prefix, including the configured bundled Python runtime and the
installed executable's relative runtime search paths. Use this mode for the
default configuration with Python plugins and Python bundling enabled. The
`--neverc` path must name the compiler from that build. Omitting `--neverc-build`
retains the executable-copy mode for development configurations, such as the
tested build with Python plugins disabled; copying a build-tree executable does
not validate a relocatable Python installation.

Both modes copy NeverC's resource directory and install the independently built
helper. With
`NEVERC_CPP_FRONTEND` unset and a minimal PATH, it checks adjacent helper and SDK
descriptor discovery, translates the core executable and C ABI module, and tests
a three-unit project plus the approved math mappings.

It then removes the installed helper and descriptor from discovery. Another
translation must fail with `TR0101`, while the already generated project and
math module plus a separately compiled C client must still compile, link and run
at both O0 and O2. The math client checks ordinary values, signed zero, a negative
subnormal, infinity and a quiet NaN. The broader integrated differential suite
checks the complete documented binary64 and floating-environment contract.

The native arm64 default configuration passed the CMake-component installation
check with mimalloc, Python plugins and bundled CPython enabled. The installed
compiler uses the adjacent Python runtime through its relative search path.
The Rosetta x86_64 development compiler, with mimalloc and Python plugins
disabled, passed the executable-copy check. This is an
external-LLVM/SDK installation test; it does not establish a self-contained SDK
package, a physical Intel-host installation, or additional cross-target/runtime
combinations. See the [support matrix](../docs/support-matrix.md).

The generated executables link only the platform libSystem dependency, without
C++ standard-library, Clang or LLVM dynamic dependencies. The default allocator
adds ordinary platform imports, including `___cxa_atexit` for cleanup
registration. This exact Darwin symbol is allowed because mimalloc's C destructor
attribute uses NeverC's C ABI cleanup registration and the platform exports it.
All other C++ ABI/exception symbols and C++/Clang/LLVM dynamic dependencies remain
rejected by the inspection.

`smoke-report.json` in the chosen prefix records commands, compiler/helper
identities, executable hashes, artifact sizes, runtime configuration and symbol
inspection. The installed helper and descriptor remain renamed to `.disabled`
so compilation without translation dependencies can be inspected. These reports
contain machine-specific paths and belong outside version control.

External LLVM libraries and the approved SDK remain translation-time
prerequisites. The descriptor locates an existing approved SDK and does not
redistribute it.
