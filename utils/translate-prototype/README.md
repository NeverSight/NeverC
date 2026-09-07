# C++ frontend feasibility prototype (P0)

This directory is a historical isolated experiment for issue #16. Current
translation uses the [built-in frontend](../translate-frontends/cpp/README.md)
and [embedded SDK catalog](../../neverc/lib/Translate/Cpp/SDK/catalog.json).
The experiment is not the production frontend protocol and does not advertise `cpp-core-v1` support. A pinned full Clang
parses and resolves C++; a deliberately bounded emitter checks the difficult
scalar sequencing boundary by compiling its generated `.nc` with NeverC.

## Reproduce the historical experiment on macOS arm64

Prerequisites are CMake 3.20+, Ninja, Python 3, LLVM/Clang **20.1.8** development
headers/libraries, and an existing NeverC build. The measured installation is
Homebrew LLVM 20.1.8 and Apple's Command Line Tools. The helper builds as C++17;
it does not require changing NeverC's C++ standard. CMake rejects another LLVM
or Clang package version. Paths below are development dependencies, not bundled
release assets.

```sh
cmake -S utils/translate-prototype -B /tmp/neverc-cpp-prototype-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang++ \
  -DLLVM_DIR=/opt/homebrew/opt/llvm@20/lib/cmake/llvm \
  -DClang_DIR=/opt/homebrew/opt/llvm@20/lib/cmake/clang
/usr/bin/time -l cmake --build /tmp/neverc-cpp-prototype-build
python3 utils/translate-prototype/verify.py \
  --helper /tmp/neverc-cpp-prototype-build/neverc-cpp-prototype \
  --neverc build-neverc/bin/neverc \
  --llvm /opt/homebrew/opt/llvm@20 \
  --output-dir /tmp/neverc-cpp-p0-verification
python3 utils/translate-prototype/measure.py \
  --helper /tmp/neverc-cpp-prototype-build/neverc-cpp-prototype \
  --neverc build-neverc/bin/neverc \
  --llvm /opt/homebrew/opt/llvm@20 \
  --output /tmp/neverc-cpp-p0-measurements.json
```

Use a new verification directory on each run. The script leaves JSON, generated
NC, reference/generated objects, executables, and `verification.json` for review.
Its harness executes only these checked-in fixtures; the helper and emitter do
not execute input programs. `measure.py` includes process startup in parse/export
latency and saves machine-specific evidence outside deterministic artifacts.

Direct semantic extraction is also possible:

```sh
/tmp/neverc-cpp-prototype-build/neverc-cpp-prototype \
  utils/translate-prototype/fixtures/identities.cpp -- \
  -std=c++17 -nostdinc -nostdinc++ \
  -resource-dir /opt/homebrew/opt/llvm@20/lib/clang/20
```

The no-include scalar fixtures parse without C++ or platform SDK headers.
Building the helper and compiling the C `stdio` harness still require the
development toolchain and platform SDK. See [header probes](header-probes) and
the [SDK inventory](../translate-frontends/docs/p0-sdk-probes.md) for future mappings.

## What the experiment establishes

`semantic-export.cpp` uses LibTooling and emits resolved namespace/function/field
USRs, overload call targets, source positions, canonical types, scalar widths,
record size/alignment/field offsets, and selected typed expression operations.
The two `example::score` overloads have distinct identities and each call refers
to its resolved declaration. Aggregate layout evidence comes from ASTContext,
not a source spelling guess. Syntax errors produce nonzero status and no JSON.

`emit-sequencing.py` accepts only the scalar operations required for the fixture.
It snapshots value reads, completes arguments left to right, evaluates assignment
RHS first, evaluates shift LHS first, and places short-circuit effects inside
conditional blocks. It generates from exported expression data; the expected NC
is not a hardcoded copy of the fixture. The generated functions keep scalar C
linkage so the same C harness can observe their full results.

The fixture's C++17 argument evaluations permit either `pack(2, 3)` or
`pack(3, 2)`. The complete first result must therefore be **2235286 or 2325286**;
the generated left-to-right choice must be **2235286**. The deterministic second
result must be **1603**. The checks compare those full values at `-O0` and `-O2`,
not process exit-code truncations. They also verify unchanged semantic JSON
after relocating the single source to a different root.

## Measurements and process isolation

Measured on macOS 15.6.1 arm64 with Clang 20.1.8 and NeverC 3389.1.5. These are
local samples under concurrent development load, not performance guarantees or
an installed-package acceptance test. Seven process launches were recorded for
each latency measurement; these are not cold filesystem-cache measurements.

| Measurement | Observed value |
| --- | ---: |
| First successful Release object compile and link | 14.18 s wall time |
| Peak resident memory for that build | 599,408,640 bytes |
| Helper executable | 336,184 bytes |
| `libclang-cpp.dylib` | 62,718,384 bytes |
| `libLLVM.dylib` | 138,247,104 bytes |
| Clang resource include tree | 14,890,412 bytes |
| Helper `--help` process median | 107.817 ms |
| Namespace/overload/aggregate export median | 95.180 ms |
| Sequencing fixture export median | 90.402 ms |

The development dynamic-library payload alone is about **192 MiB** for the helper,
full Clang, and LLVM, before resource headers, zstd, C++ headers, platform SDK,
licenses, or packaging. An advertised self-contained release must package an
approved dependency closure and test its installation. No such package or size
optimization is implemented by this experiment.

`otool -L` shows that only the helper loads full `libclang-cpp` and `libLLVM`.
NeverC's existing executable loads neither dynamic library. The helper has 117
undefined Clang/LLVM C++ symbols resolved through its own dylibs; process
separation prevents those symbols from colliding with NeverC's stripped frontend.
Full Clang also loads system libc++/libSystem, CoreServices/CoreFoundation, while
LLVM loads system ffi/edit/z/xml2 and Homebrew `libzstd.1.dylib`. Absolute Homebrew
install names and the LLVM `@rpath` dependency require explicit relocation or
retained external installation requirements in any release packaging design.

## Decisions and limits carried into production

- Keep full Clang in a separate executable. Pin frontend and protocol versions
  independently; the JSON here is marked `neverc-cpp-feasibility-only` to prevent
  accidental use as the production contract.
- CMake selected x86_64 by default in this desktop process even though installed
  LLVM dylibs are arm64. Explicit native architecture configuration is necessary
  here. Other host platforms have not been built or executed by this prototype.
- Map Clang type kinds, not printed text. Default `QualType::getAsString()` uses
  a C printing policy and spells C++ bool `_Bool`; this exporter explicitly uses
  the translation unit's language options. Own temporary strings placed into
  `llvm::json`; `StringRef` does not extend a temporary buffer's lifetime.
- USRs and basenames suffice for these fixtures but are not a project identity
  policy. Production needs canonical declarations, normalized project-relative
  internal-linkage identity, deterministic generated names, and collision checks.
- Lower reads and effects explicitly before NC emission. Preserve C++17 signed
  shifts and implementation-defined conversions with dedicated rules; this
  fixture uses small positive shifts only and makes no general shift guarantee.
- This exporter records selected AST forms without a complete allowlist. It has
  no production protocol validation, artifact transaction, option policy,
  include/macro ownership policy, resource sandbox, aggregate emitter, ABI
  guarantee, or runtime mapping. Production must reject every unsupported form.

The evidence supports proceeding to a native, no-include scalar production
adapter with a typed, verified instruction protocol. P3 packaging and library
mappings remain gated by their separate SDK/runtime and installation checks.
