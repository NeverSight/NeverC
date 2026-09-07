# P0 C++ SDK feasibility measurements

Measured on macOS with Clang 20.1.8 for the C++ frontend evaluation. These are
translation-time **syntax/header-discovery** probes, not library translation,
numeric-equivalence, executable-link, or installed-package tests. The separate
[prototype README](../../translate-prototype/README.md) records the
full-Clang helper experiment and its build/size/latency evidence.

## Selected development configuration

| Input | Measured identity |
| --- | --- |
| Full frontend | Homebrew Clang **20.1.8**, `/opt/homebrew/Cellar/llvm@20/20.1.8/bin/clang++`; `/opt/homebrew/opt/llvm@20` resolves to that cellar directory. |
| Compiler binary SHA-256 | `c6ce2e418d70299ef7ce16ab63e728e1c74314c80e869ad0f1c886064d050f9c` |
| Default host triple | `arm64-apple-darwin24.6.0` |
| Explicit probe target | `arm64-apple-macosx15.0.0` |
| Source language | C++17 |
| Clang resource directory | `/opt/homebrew/Cellar/llvm@20/20.1.8/lib/clang/20` |
| Standard-library headers | `/opt/homebrew/Cellar/llvm@20/20.1.8/include/c++/v1` from the pinned LLVM 20.1.8 installation. `__config` defines `_LIBCPP_VERSION` as **200100**; do not infer 200108 from the package patch version. |
| Platform SDK | `/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk`; `SDKSettings.json` reports `Version: 15.5`. |
| SDK settings SHA-256 | `58a133735f0a55a624a1703067059f6e78925e51725ec6e1f966072e142c9c42` |

The default Homebrew invocation reads
`/opt/homebrew/etc/clang/arm64-apple-darwin24.cfg`, which injects
`-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk`; that SDK path
resolves to `MacOSX15.5.sdk`. `xcrun --show-sdk-path` reports the less specific
`MacOSX.sdk`. Neither symlink is a sufficient version pin.

The runner disables Clang's default configuration and automatic system include
search, passes the SDK/resource/library roots explicitly, and clears header/SDK
environment overrides. The actual include search order is:

1. `/opt/homebrew/Cellar/llvm@20/20.1.8/include/c++/v1`
2. `/opt/homebrew/Cellar/llvm@20/20.1.8/lib/clang/20/include`
3. `/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk/usr/include`
4. `/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk/System/Library/Frameworks`
   (framework directory)

## Reproduction and recorded evidence

Requirements: Python 3.9 or newer, the exact installed Clang/library version, and
the named local Apple SDK. No package downloads or source-program execution occur.
Run from the repository root with a **new** report path:

```sh
python3 utils/translate-prototype/header-probes/run.py \
  --llvm-root /opt/homebrew/opt/llvm@20 \
  --sdk /Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk \
  --report /tmp/neverc-p0-sdk-probes.json
```

The runner invokes each checked-in fixture with this source-context shape:

```text
clang++ --no-default-config -target arm64-apple-macosx15.0.0 -std=c++17
  -isysroot <sdk> -resource-dir <llvm>/lib/clang/20
  -nostdinc -nostdinc++
  -isystem <llvm>/include/c++/v1
  -isystem <llvm>/lib/clang/20/include
  -isystem <sdk>/usr/include
  -iframework <sdk>/System/Library/Frameworks
  -fsyntax-only -H -v <fixture.cpp>
```

The JSON report records exact commands, full compiler include diagnostics,
compiler/license/SDK-settings hashes, each fixture hash, and all discovered
headers by distribution-relative path and SHA-256. It fails if a header comes
from an undeclared root, a parse fails, or the pinned toolchain/SDK identity is
wrong. Existing report files are refused. Its raw absolute paths and timings
belong to an operational measurement, not a reproducible translation manifest.

| Fixture | Result | Unique libc++ headers | Resource headers | SDK headers | Syntax-only elapsed |
| --- | --- | ---: | ---: | ---: | ---: |
| `cmath.cpp` — `std::floor(std::fabs(double))` | Pass | 127 | 1 | 73 | 56.068 ms |
| `string.cpp` — counted byte construction, `push_back`, return | Pass | 510 | 18 | 125 | 203.442 ms |
| `vector.cpp` — `vector<int>` initialization, `push_back`, return | Pass | 557 | 18 | 169 | 218.938 ms |

One `clang++ --no-default-config --version` invocation measured **12.826 ms**.
These are single wall-clock samples on this development host, including process
startup, rather than benchmarks or release latency bounds. Header counts are
unique real paths observed through `-H`; they do not count the source fixture.
A repeated run with deliberately different `CPATH`, `CPLUS_INCLUDE_PATH`, and
`SDKROOT` environment values passed with identical dependency paths and hashes.
An attempted reuse of the original report path was rejected with exit status 2
and left the report intact.

Installed directory sizes measured with `du -sk` were 19,536 KiB for libc++
headers, 15,160 KiB for Clang resource headers, and 18,380 KiB for the SDK's
`usr/include`. These are allocated directory sizes, not a compressed package
estimate. They omit SDK frameworks, libraries, Clang libraries, the helper, and
its transitive dependencies. The header footprint alone is not a distribution
size estimate.

## Header provenance and future mapping boundaries

The measured libc++ distribution resolves the double math implementations through
`__math/abs.h` and `__math/rounding_functions.h`, including compiler builtins.
The production math profile uses resolved declaration identity, overload and
approved header provenance; finding the spelling `fabs` or `floor` is insufficient. These probes
only establish that the frontend can parse and resolve the selected headers.

NeverC's C-oriented runtime tree is not the selected C++ SDK. There is an Android
libc++ header tree under `runtime/android/arm64/usr/include/c++/v1`, and Apple
kernel C++ headers elsewhere in the tree; their existence does not qualify
either as the hosted macOS C++ standard-library distribution. No repository
runtime/header mixture was used by these probes.

Only this exact development distribution is measured. Other LLVM/libc++ releases,
Apple's alternative libc++ headers, libstdc++, MSVC STL, and Android libc++ have
no approved translation/mapping status. A new library/version requires an
inventory, source-operation identity checks, and profile acceptance tests.

## Licenses and deployment decision

The installed LLVM `LICENSE.TXT` identifies Apache-2.0 with LLVM exceptions; its
SHA-256 is `8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee`.
The sampled libc++ headers carry that SPDX identifier. A future helper/resource
package must retain applicable notices and license texts, inventory component
exceptions and transitive libraries, and record any modifications. LLVM's
published policy also notes legacy-covered portions; do not replace a component
inventory with the assumption that every file has one license. See the
[LLVM 20 license policy](https://releases.llvm.org/20.1.0/docs/DeveloperPolicy.html#copyright-license-and-patents).

The Apple SDK remains an external development prerequisite in this configuration.
The [Xcode and Apple SDKs agreement](https://www.apple.com/legal/sla/docs/xcode.pdf)
defines SDK headers as Apple Software and restricts redistribution except where
expressly permitted. This probe does not copy SDK contents into the repository
or authorize packaging them. Selecting and validating an approved release
arrangement is a separate distribution gate.

The helper stays a separate executable. That isolates full-Clang C++ symbols
from NeverC, but its dependent libraries still need an installed-package
inventory and relocatable discovery. A Homebrew-linked development helper is
not automatically portable to a host without Homebrew. Exact helper build
requirements and dependency sizes belong to the prototype measurement.
These probes and the helper select arm64 explicitly. A separate NeverC build
launched under Rosetta can use x86_64 by default; arm64 header parsing must not
be reported as source/generated execution coverage for that compiler target.

Translation-time requirements (helper, resource headers, libc++ headers, platform
SDK) are distinct from generated-program requirements (NeverC and its declared
platform/runtime payloads). Removing the C++ runtime from generated output does
not remove the SDK needed to understand the original C++ input. No self-contained
release or “no SDK needed” claim is made here. P2's no-include profile may have a
smaller frontend runtime requirement than these P3 header probes, but that must
be established by its own installation test.

## Relationship to the implemented translator

The [prototype README](../../translate-prototype/README.md) records the isolated
frontend/sequencing experiment. The later production implementation adds owned
compilation contexts, header provenance, project merging, exact runtime mapping
and artifact publication. Its [support matrix](support-matrix.md) records the
integrated and installed-output acceptance evidence; the
[runtime mapping inventory](runtime-mapping.md) defines the approved math gates.
Those later tests, rather than syntax-only header probes, establish the currently
published C++ profiles.

The string/vector fixtures above are frontend feasibility probes only. Neither
profile supports those library types or operations yet. A self-contained SDK
package and any new host, target or source-library distribution still require
separate installation and semantic validation. No Linux, Windows or alternate
SDK support follows from this macOS experiment.
