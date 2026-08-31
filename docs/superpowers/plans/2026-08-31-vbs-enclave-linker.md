# VBS Enclave COFF Linker Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add original Microsoft-compatible `/ENCLAVE` and `/GUARD:MIXED` support to NeverC's COFF linker, with local structural tests and an actually executed Windows CI differential against `link.exe`.

**Architecture:** Keep `MIXED` as a distinct linker-policy flag while reusing the existing CFG table emitter's conservative guarded/unguarded target discovery. In enclave mode, replace the legacy zero-valued `__enclave_config` fallback with forced symbol resolution and validate the final relocated `EnclaveConfigurationPointer` in the emitted 64-bit PE load configuration. A focused in-memory COFF test suite proves deterministic behavior locally; Windows CI uses Microsoft tools as the compatibility oracle and separates fail-hard PE validation from environment-dependent runtime loading.

**Tech Stack:** C++17, NeverC COFF linker, LLVM MC/Object APIs, GoogleTest, PowerShell, Python 3, MSVC `cl.exe`/`link.exe`, Windows SDK VEIID and SignTool, GitHub Actions.

---

## File map

- Create `tests/neverc/COFFEnclaveLinkerTests.cpp`: in-memory COFF assembly, link helpers, PE inspection, and feature/negative tests.
- Modify `tests/neverc/CMakeLists.txt`: compile the tests into `neverc-plugin-link-tests`.
- Modify `neverc/lib/Linker/Backends/COFF/Options.td.h`: declare `--enclave`.
- Modify `neverc/include/neverc/Linker/COFF/Config.h`: store enclave mode and mixed-input policy.
- Modify `neverc/lib/Linker/Backends/COFF/Driver/CoffCommandLine.cpp`: parse `mixed` without inventing a PE bit or enabling long-jump metadata.
- Modify `neverc/lib/Linker/Backends/COFF/Driver/CoffDriver.cpp`: resolve/root enclave symbols and reject explicit incremental linking.
- Modify `neverc/lib/Linker/Backends/COFF/Emit/CoffImageEmitter.cpp`: perform bounded load-config and final VA validation.
- Modify `neverc/lib/Invoke/ToolChains/MSVC.cpp`: normalize forwarded MSVC linker spellings only at option-origin call sites.
- Modify `tests/neverc/DriverTests.cpp`: verify public option forwarding.
- Create `tests/neverc/Inputs/VBSEnclave/{enclave,guarded,legacy,host}.cpp`: Windows reference and runtime fixtures.
- Create `utils/ci/verify-vbs-enclave-pe.py`: semantic PE/load-config/CFG verifier.
- Create `utils/ci/run-vbs-enclave-ci.ps1`: reference/candidate build and runtime orchestration.
- Create `.github/workflows/vbs-enclave.yml`: Windows static gate and differential runtime job.

### Task 1: Add the focused COFF test harness and prove the current red state

**Files:**
- Create: `tests/neverc/COFFEnclaveLinkerTests.cpp`
- Modify: `tests/neverc/CMakeLists.txt`

- [ ] **Step 1: Add reusable in-memory COFF assembly and link helpers**

Follow the existing `PluginCOFFContextIsolationTests.cpp` pattern with
`BuiltinLLVMAsmParser`, `InMemoryFileStore`, `LinkerExecutionContext`, and
`linker::coff::link`. Provide:

```cpp
llvm::Expected<llvm::SmallVector<char, 0>>
assembleCOFF(llvm::StringRef Triple, llvm::StringRef Assembly);

struct LinkResult {
  bool Succeeded;
  std::string Diagnostics;
  std::string Image;
};

LinkResult linkCOFF(llvm::ArrayRef<llvm::StringRef> Options,
                    llvm::ArrayRef<InMemoryObject> Objects);

llvm::Expected<llvm::SmallVector<char, 0>>
archiveCOFF(llvm::StringRef MemberName, llvm::ArrayRef<char> Object);
```

Use a temporary output, reproducible mode, no default libraries, a fresh
execution context, and RAII cleanup of the in-memory store. Build the archive
helper with LLVM's archive writer so Task 3 can exercise real lazy-member
extraction without invoking an external tool.

- [ ] **Step 2: Add a minimal x64 fixture and failing parser test**

Define an entry, `__enclave_config`, a 64-bit `_load_config_used` whose pointer
relocation is at offset `0xf8`, and a data relocation to the entry:

```asm
.text
.globl enclave_entry
enclave_entry:
  retq
.section .rdata,"dr"
.p2align 3
.globl __enclave_config
__enclave_config:
  .long 80
  .long 76
  .zero 56
  .quad 0x200000
  .long 1
  .long 1
.p2align 3
.globl _load_config_used
_load_config_used:
  .long 0x100
  .zero 0x7c
  .quad __guard_fids_table
  .quad __guard_fids_count
  .quad __guard_flags
  .zero 0x60
  .quad __enclave_config
.globl enclave_entry_address
enclave_entry_address:
  .quad enclave_entry
```

The three Guard relocations mirror the public load-config ABI: the table VA is
at `0x80`, count at `0x88`, GuardFlags at `0x90`, and enclave pointer at
`0xf8`. Without those relocations NeverC has nothing to patch when it creates
the synthetic CFG symbols, so the fixture would not be able to assert CFG
output.

Add `AcceptsGuardMixed`; link a no-entry DLL with `--guard=mixed` and assert
CFG DLL/load-config flags and table presence.

- [ ] **Step 3: Build and run the focused test to verify RED**

Run:

```bash
cmake -S llvm -B build-vbs -G Ninja \
  -C neverc/cmake/caches/NeverC.cmake \
  -DNEVERC_INCLUDE_TESTS=ON \
  -DNEVERC_ENABLE_PYTHON_PLUGINS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_LTO=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-vbs --target neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.AcceptsGuardMixed'
```

Expected: FAIL with `invalid argument to --guard: mixed`.

- [ ] **Step 4: Commit the red test slice**

```bash
git add tests/neverc/COFFEnclaveLinkerTests.cpp tests/neverc/CMakeLists.txt
git commit -m "test: define VBS enclave COFF linker contract"
```

### Task 2: Implement the distinct `/GUARD:MIXED` parser state

**Files:**
- Modify: `neverc/include/neverc/Linker/COFF/Config.h:48-58,132-140`
- Modify: `neverc/lib/Linker/Backends/COFF/Driver/CoffCommandLine.cpp:93-110`
- Test: `tests/neverc/COFFEnclaveLinkerTests.cpp`

- [ ] **Step 1: Add parser-order and mixed-table tests**

Cover `mixed`, `mixed,no`, `mixed,nolongjmp`, and `mixed,ehcont`. Add
`MixedIncludesGuardedAndUnguardedTargets` with one object carrying guarded
function metadata and one unguarded object whose code symbol has an address
relocation. Assert that the sorted GFID table contains both unique RVAs and
that plain `mixed` does not emit a long-jump table.

- [ ] **Step 2: Run the tests to verify RED**

```bash
cmake --build build-vbs --target neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.GuardMixed*:COFFEnclaveLinkerTest.Mixed*'
```

Expected: parser rejection or missing CFG output.

- [ ] **Step 3: Add minimal state and parsing**

Add a policy boolean rather than overloading the table-selection mask:

```cpp
// /guard:mixed permits guarded and legacy COFF inputs in one CFG image.
bool guardCFMixed = false;
```

Parse the token as:

```cpp
} else if (s == "mixed") {
  ctx.config.guardCF |= GuardCFLevel::CF;
  ctx.config.guardCFMixed = true;
} else if (s == "no") {
  ctx.config.guardCF = GuardCFLevel::Off;
  ctx.config.guardCFMixed = false;
```

Do not add a GuardFlags bit and do not set `LongJmp`. Reuse the existing
conservative relocation scan.

- [ ] **Step 4: Run focused and existing CFG tests to verify GREEN**

```bash
cmake --build build-vbs --target neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.GuardMixed*:COFFEnclaveLinkerTest.Mixed*:PluginCOFFContextIsolationTest.*'
```

Expected: PASS.

- [ ] **Step 5: Commit the guard slice**

```bash
git add neverc/include/neverc/Linker/COFF/Config.h \
  neverc/lib/Linker/Backends/COFF/Driver/CoffCommandLine.cpp \
  tests/neverc/COFFEnclaveLinkerTests.cpp
git commit -m "feat: support mixed CFG COFF inputs"
```

### Task 3: Add `/ENCLAVE` symbol-resolution and incremental semantics

**Files:**
- Modify: `neverc/lib/Linker/Backends/COFF/Options.td.h:120-175`
- Modify: `neverc/include/neverc/Linker/COFF/Config.h:210-225`
- Modify: `neverc/lib/Linker/Backends/COFF/Driver/CoffDriver.cpp:1450-1505,1725-1790`
- Test: `tests/neverc/COFFEnclaveLinkerTests.cpp`

- [ ] **Step 1: Write failing resolution and compatibility tests**

Add `EnclaveRequiresConfig`, `EnclaveConfigArchiveMemberIsExtracted`,
`EnclaveConfigSurvivesReferenceGC`, `EnclaveRequiresLoadConfig`,
`ExplicitIncrementalIsRejected`, and `NormalLinkRetainsZeroConfigFallback`.
The archive test puts only `__enclave_config` in a lazy member; the GC test uses
a COMDAT; the normal-link test proves the old fallback remains outside enclave
mode.

- [ ] **Step 2: Run the tests to verify RED**

```bash
cmake --build build-vbs --target neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.Enclave*:COFFEnclaveLinkerTest.ExplicitIncremental*:COFFEnclaveLinkerTest.NormalLink*'
```

Expected: unknown `--enclave`, missing diagnostics, or archive/GC failure.

- [ ] **Step 3: Declare and parse enclave mode**

Add the sorted option entry:

```cpp
OPTION(prefix_1, "--enclave", enclave, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Create a VBS enclave image", nullptr, nullptr)
```

Set `config->enclave = args.hasArg(OPT_enclave)` before incremental checks.

- [ ] **Step 4: Replace the zero fallback only in enclave mode**

During the unresolved-symbol fixpoint:

```cpp
if (config->enclave) {
  addUndefined(mangle("__enclave_config"));
  addUndefined(mangle("_load_config_used"));
} else {
  ctx.symtab.addAbsolute(mangle("__enclave_config"), 0);
}
```

Keep both resolved regular symbols in `config->gcroot` after LTO and before
`markLive`.

- [ ] **Step 5: Reject an explicit incremental request**

Inspect the effective last `--incremental`/`--no-incremental`. If enclave mode
and the last explicit choice is incremental, emit a deterministic NeverC error
and return before opening an output. Do not infer DLL, integrity, or guard
settings from `--enclave`.

- [ ] **Step 6: Run focused tests to verify GREEN**

Run the command from Step 2. Expected: PASS.

- [ ] **Step 7: Commit the enclave-resolution slice**

```bash
git add neverc/lib/Linker/Backends/COFF/Options.td.h \
  neverc/include/neverc/Linker/COFF/Config.h \
  neverc/lib/Linker/Backends/COFF/Driver/CoffDriver.cpp \
  tests/neverc/COFFEnclaveLinkerTests.cpp
git commit -m "feat: resolve VBS enclave configuration"
```

### Task 4: Validate the final PE load-config pointer safely

**Files:**
- Modify: `neverc/lib/Linker/Backends/COFF/Emit/CoffImageEmitter.cpp:220-270,2335-2425`
- Test: `tests/neverc/COFFEnclaveLinkerTests.cpp`

- [ ] **Step 1: Write malformed and cross-architecture tests**

Add `EnclaveSetsLoadConfigPointerX64`, `EnclaveSetsLoadConfigPointerArm64`,
`EnclaveRejectsShortLoadConfig`, `EnclaveRejectsWrongPointer`, and
`EnclaveRejectsAbsoluteConfig`. Match stable diagnostic substrings.

- [ ] **Step 2: Run the tests to verify RED**

```bash
cmake --build build-vbs --target neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.EnclaveSets*:COFFEnclaveLinkerTest.EnclaveRejects*'
```

Expected: malformed images link or required diagnostics are absent.

- [ ] **Step 3: Bound access to `_load_config_used`**

Before casting output bytes, prove the symbol lies within its live
`SectionChunk`, four bytes of `Size` are readable, and the declared size fits
within the remaining chunk. In enclave mode require the end of
`EnclaveConfigurationPointer`. Emit an error and return on failure.

- [ ] **Step 4: Validate the published enclave VA**

Implement an enclave-only check equivalent to:

```cpp
auto *Config = dyn_cast_or_null<DefinedRegular>(
    ctx.symtab.findUnderscore("__enclave_config"));
if (!Config || !Config->getChunk() || !Config->getChunk()->live)
  error("__enclave_config must refer to live image data");
else if (LoadConfig->EnclaveConfigurationPointer !=
         ctx.config.imageBase + Config->getRVA())
  error("EnclaveConfigurationPointer not set correctly in '_load_config_used'");
```

Do not rewrite the field; require the object/CRT relocation to create its VA
and base relocation. Do not validate versioned policy/import fields here.

- [ ] **Step 5: Run the full focused linker suite**

```bash
cmake --build build-vbs --target neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.*'
ctest --test-dir build-vbs --output-on-failure -R 'neverc-plugin-link-tests'
```

Expected: PASS.

- [ ] **Step 6: Commit the writer slice**

```bash
git add neverc/lib/Linker/Backends/COFF/Emit/CoffImageEmitter.cpp \
  tests/neverc/COFFEnclaveLinkerTests.cpp
git commit -m "feat: validate VBS enclave load configuration"
```

### Task 5: Preserve normal MSVC spellings through the NeverC driver

**Files:**
- Modify: `neverc/lib/Invoke/ToolChains/MSVC.cpp:190-310`
- Modify: `tests/neverc/DriverTests.cpp:1180-1240`

- [ ] **Step 1: Add failing `-###` forwarding tests**

Test both the CL-style `/link` route and GNU-compatible `-Wl,` forwarding. The
rendered in-process linker command must contain:

```text
--enclave
--guard=mixed
--integritycheck
--no-incremental
```

and must not treat those options as input files.

- [ ] **Step 2: Run the driver tests to verify RED**

```bash
cmake --build build-vbs --target neverc-tests --parallel 4
build-vbs/bin/neverc-tests \
  --gtest_filter='DriverTest.WindowsVbsEnclaveLinkOptions*'
```

Expected: FAIL because raw `/...` values reach the normalized backend.

- [ ] **Step 3: Normalize only option-origin values**

Add a helper converting `/NAME` to `--name` and `/NAME:VALUE` to
`--name=value`. Apply it only to values originating from `OPT_Xmslink` and
`OPT_Wl_COMMA`; never apply it to ordinary input filenames, because POSIX
cross-links legitimately pass absolute `/tmp/...obj` paths.

Keep `-fms-guard=` code generation unchanged: `MIXED` is a linker policy,
while code requiring instrumentation still compiles with CFG.

- [ ] **Step 4: Run driver and enclave tests to verify GREEN**

```bash
cmake --build build-vbs --target neverc-tests neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-tests \
  --gtest_filter='DriverTest.WindowsVbsEnclaveLinkOptions*'
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.*'
```

Expected: PASS.

- [ ] **Step 5: Commit the driver slice**

```bash
git add neverc/lib/Invoke/ToolChains/MSVC.cpp tests/neverc/DriverTests.cpp
git commit -m "feat: forward MSVC VBS enclave linker options"
```

### Task 6: Build the Windows differential fixture and semantic verifier

**Files:**
- Create: `tests/neverc/Inputs/VBSEnclave/enclave.cpp`
- Create: `tests/neverc/Inputs/VBSEnclave/guarded.cpp`
- Create: `tests/neverc/Inputs/VBSEnclave/legacy.cpp`
- Create: `tests/neverc/Inputs/VBSEnclave/host.cpp`
- Create: `utils/ci/verify-vbs-enclave-pe.py`
- Create: `utils/ci/run-vbs-enclave-ci.ps1`

- [ ] **Step 1: Add deterministic fixture sources**

`enclave.cpp` defines `extern "C" const IMAGE_ENCLAVE_CONFIG
__enclave_config` with fixed nonzero family/image IDs, 512 MiB enclave size,
one thread, and `IMAGE_ENCLAVE_FLAG_PRIMARY_IMAGE`. `guarded.cpp` contains a
real indirect call and is compiled with CFG. `legacy.cpp` exposes an
address-taken function and is compiled without CFG. `host.cpp` reports each
stage and `GetLastError`:

```text
IsEnclaveTypeSupported
CreateEnclave
LoadEnclaveImage
InitializeEnclave
```

- [ ] **Step 2: Write PE verifier self-tests first**

The verifier must bounds-check DOS/PE headers, sections, load-config directory,
VA mapping, enclave configuration, guard table, and base relocations. A
`self-test` subcommand mutates each required field and proves truncation,
zero/wrong pointers, missing flags, and invalid GFIDs are rejected.

- [ ] **Step 3: Implement semantic inspection and comparison**

Expose:

```bash
python utils/ci/verify-vbs-enclave-pe.py inspect image.dll --json image.json
python utils/ci/verify-vbs-enclave-pe.py compare reference.dll candidate.dll
python utils/ci/verify-vbs-enclave-pe.py self-test
```

Compare architecture, DLL/DynamicBase/FORCE_INTEGRITY/GUARD_CF flags,
load-config presence/size, enclave config values, GuardFlags, GFID entries, and
the enclave pointer's base relocation. Ignore timestamps, image bases, concrete
RVAs, signatures, and whole-file bytes.

- [ ] **Step 4: Implement the PowerShell build matrix**

Produce:

```text
msvc-msvc.dll       MSVC objects + link.exe
neverc-msvc.dll     NeverC objects + link.exe
neverc-neverc.dll   the same NeverC objects + NeverC linker
```

All links explicitly request DLL, no incremental link, no default libraries,
enclave mode, integrity checking, and mixed guard mode. Resolve and print exact
paths for enclave CRT, enclave UCRT, `vertdll`, `bcrypt`, VEIID, SignTool, and
MSVC `link.exe`; invoke the resolved executable so a POSIX `link` cannot shadow
it.

- [ ] **Step 5: Enforce transform/sign order and classify runtime results**

Inspect unsigned images first, copy runtime candidates, run VEIID, then sign.
Run the host reference-first. If the Microsoft reference cannot load, return an
explicit environment-skip unless runtime is required. If the reference loads
but either candidate fails, return failure.

- [ ] **Step 6: Run platform-independent verifier checks locally**

```bash
python3 utils/ci/verify-vbs-enclave-pe.py self-test
```

Expected: PASS.

- [ ] **Step 7: Commit the fixture slice**

```bash
git add tests/neverc/Inputs/VBSEnclave utils/ci/verify-vbs-enclave-pe.py \
  utils/ci/run-vbs-enclave-ci.ps1
git commit -m "test: add Windows VBS enclave differential harness"
```

### Task 7: Add the Windows GitHub Actions workflow

**Files:**
- Create: `.github/workflows/vbs-enclave.yml`

- [ ] **Step 1: Define safe triggers and concurrency**

Use manual inputs and a branch-scoped push trigger because GitHub cannot
manually dispatch a brand-new workflow until it exists on the default branch:

```yaml
on:
  push:
    branches: [dev]
  workflow_dispatch:
    inputs:
      runtime_runner:
        type: choice
        options: [windows-2025, vbs-enclave-windows-x64]
        default: windows-2025
      require_runtime:
        type: boolean
        default: false
permissions:
  contents: read
concurrency:
  group: vbs-enclave-${{ github.ref }}
  cancel-in-progress: false
```

- [ ] **Step 2: Add the fail-hard static/reference job**

Run on `windows-2022`. Reuse pinned CMake 3.31.11 and MSVC setup, disable LTO,
cap compile/link jobs to 2/1, build the compiler and
focused tests, run `COFFEnclaveLinkerTest.*`, then invoke the PowerShell static
phase. Always upload DLLs, `dumpbin` text, JSON, versions, and logs.

- [ ] **Step 3: Add the differential runtime job**

Download artifacts on the selected runner, record OS and Device Guard/VBS
state, and invoke the runtime phase with `require_runtime`. Always write
PASS/FAIL/SKIP plus the exact stage/error to the job summary.

- [ ] **Step 4: Validate YAML and PowerShell syntax locally**

```bash
python3 - <<'PY'
import pathlib, yaml
yaml.safe_load(pathlib.Path('.github/workflows/vbs-enclave.yml').read_text())
PY
pwsh -NoProfile -Command \
  "[void][scriptblock]::Create((Get-Content -Raw utils/ci/run-vbs-enclave-ci.ps1))"
```

Expected: exit 0. If local PowerShell is unavailable, record a local skip;
Windows CI remains authoritative.

- [ ] **Step 5: Commit the workflow**

```bash
git add .github/workflows/vbs-enclave.yml
git commit -m "ci: verify VBS enclave linking on Windows"
```

### Task 8: Run local gates, push, and iterate on real Windows CI

**Files:**
- Modify only files implicated by test or CI evidence.

- [ ] **Step 1: Run focused local gates**

```bash
cmake --build build-vbs --target neverc-tests neverc-plugin-link-tests --parallel 4
build-vbs/bin/neverc-plugin-link-tests \
  --gtest_filter='COFFEnclaveLinkerTest.*:PluginCOFFContextIsolationTest.*'
build-vbs/bin/neverc-tests \
  --gtest_filter='DriverTest.WindowsVbsEnclaveLinkOptions*'
python3 utils/ci/verify-vbs-enclave-pe.py self-test
git diff --check
```

Expected: PASS and no whitespace errors.

- [ ] **Step 2: Push directly to the development branch**

```bash
git push origin HEAD:dev
```

Expected: push succeeds and the development-branch workflow starts.

- [ ] **Step 3: Watch CI to a terminal state**

```bash
gh run list --workflow vbs-enclave.yml --branch dev --limit 1
gh run watch <run-id> --exit-status
```

Expected: static/reference passes; hosted runtime passes or reports an explicit
environment SKIP. Candidate-only runtime failure is a test failure.

- [ ] **Step 4: Debug only from evidence**

Apply `@systematic-debugging` to deterministic failures. For Windows-only,
flaky, architecture-specific, or locally irreproducible failures, also apply
`@github-ci-runtime-debugging`. Change one proven cause at a time, add a
regression assertion, push, and wait for the replacement run.

- [ ] **Step 5: Record the final outcome**

Re-run Step 1 after the last CI fix. Record the final commit, local results,
Windows run status, static comparison, and runtime PASS/SKIP classification.

- [ ] **Step 6: Confirm a clean pushed worktree**

```bash
git status --short --branch
git log --oneline --decorate -8
```

Expected: clean worktree with required commits pushed to `dev`.

## Skill routing during execution

- Use `@tdd` for Tasks 1-5 and every regression fix.
- Use `@compiler-development` for COFF parser, resolution, CFG, PE load-config,
  and driver changes.
- Use `@systematic-debugging` before fixing unexpected local failures.
- Use `@github-ci-runtime-debugging` for hosted/self-hosted Windows divergence,
  runner-only crashes, or flaky CI behavior.
