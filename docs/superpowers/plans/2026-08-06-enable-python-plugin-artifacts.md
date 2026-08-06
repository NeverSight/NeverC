# Enable Python Plugins In Official Artifacts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship every official NeverC compiler artifact with Python plugin support enabled and with a relocatable CPython runtime, while retaining and continuously testing the explicit Python-disabled build mode.

**Architecture:** Artifact-producing workflows provision CPython 3.12 and pass `NEVERC_ENABLE_PYTHON_PLUGINS=ON`, `NEVERC_BUNDLE_PYTHON_RUNTIME=ON`, and the exact interpreter to CMake. Installation copies a trimmed, redistributable CPython runtime beside NeverC; the host selects that adjacent runtime before interpreter initialization, and the installed executable uses a relative runtime search path. CI extracts each archive to a fresh prefix, removes all build-time Python paths from its environment, and loads the installed minimal plugin. A discovery-based policy check governs every workflow that both installs and uploads a NeverC compiler, including future producers.

**Tech Stack:** C++17, CPython C API 3.10+, CMake, Python 3, GitHub Actions, Bash

---

**Files and responsibilities:**

- `neverc/CMakeLists.txt`: define `NEVERC_BUNDLE_PYTHON_RUNTIME`, validate its relationship to Python plugin support, and give installed Unix/macOS executables a relative runtime search path.
- `neverc/lib/Plugin/Python/PythonPluginLoader.cpp`: select `<neverc>/../python` as `PyConfig.home` when an official bundled runtime is present.
- `utils/release/bundle-python-runtime.py`: copy the selected interpreter's shared runtime, trimmed standard library, extension modules, transitive non-system native dependencies, and attributable licenses into an install prefix; repair relative loader paths where required.
- `utils/release/test-python-plugin-package.py`: extract or inspect a package in a fresh temporary location, hide all build-time Python environment paths, inspect platform loader metadata, and load a plugin that imports representative native standard-library modules.
- `utils/ci/check-python-plugin-artifacts.py`: discover all workflows that install and upload a NeverC compiler and enforce the enablement, bundling, interpreter, relocation-smoke, and explicit-OFF contracts.
- `.github/workflows/python-plugin-bindings.yml`: run the policy check, add a real `OFF` build, and trigger when any governed workflow or packaging input changes.
- `.github/workflows/build-*.yml`: enable/bundle CPython in normal, LTO, and PGO compiler artifacts and test relocated archives.
- `.github/workflows/release-{linux,macos,windows}-*.yml`: enable/bundle CPython in release artifacts, sign the bundled macOS runtime, include it in curl-installer archives, and test relocated archives.
- `utils/build/build_pgo.sh`: pass the workflow-selected Python enablement, bundling mode, and interpreter into both PGO configure phases.
- `docs/plugin-api/python*.md`: document the bundled official runtime and the still-optional source-build mode.

### Task 1: Add failing policy and packaging tests

**Files:**
- Create: `utils/ci/check-python-plugin-artifacts.py`
- Create: `utils/release/test-python-plugin-package.py`
- Modify: `.github/workflows/python-plugin-bindings.yml`

- [ ] **Step 1: Write the producer-discovery policy check**

  Discover every `.github/workflows/*.yml` file containing `actions/upload-artifact@`, a NeverC install command (`cmake --install` or `--target install`), and a NeverC compiler archive. Assert the currently known count is 15 so changes to discovery are reviewed. For direct CMake producers require a pinned/setup CPython step, `-DPython3_EXECUTABLE=...`, `-DNEVERC_ENABLE_PYTHON_PLUGINS=ON`, `-DNEVERC_BUNDLE_PYTHON_RUNTIME=ON`, the bundler invocation, and the package verifier invocation. For PGO producers require the equivalent three workflow environment inputs and the same bundler/verifier calls. Parse every uploaded archive path and classify all archives that contain `bin/neverc[.exe]`: full and compiler-only archives in normal/LTO/PGO workflows, full archives in Windows releases, and full plus curl-installer archives in Linux/macOS releases. Require a verifier call for every classified archive while excluding runtime-only, PDB, profile-data, and log artifacts. Require the feature workflow to contain an explicit OFF job.

- [ ] **Step 2: Write the package verifier interface**

  Support exactly one of:

  ```sh
  python3 utils/release/test-python-plugin-package.py --prefix /absolute/install
  python3 utils/release/test-python-plugin-package.py --archive /absolute/artifact.zip
  ```

  For archives, extract with `zipfile` into a newly created temporary directory and accept either a top-level `install/` tree or a root-layout curl archive. Resolve `bin/neverc[.exe]` and `pluginsdk/python`; unset `PYTHONPATH`, `PYTHONHOME`, `Python3_ROOT_DIR`, `LD_LIBRARY_PATH`, and `DYLD_LIBRARY_PATH`; remove entries below `pythonLocation` from `PATH`; and reject loader metadata containing the setup action's hosted-toolcache path or any absolute build-time Python prefix. On Linux inspect `readelf -d`: the Python dependency must be a basename and `RUNPATH`/`RPATH` must contain `$ORIGIN/../python/lib`. On macOS inspect `otool -L` and `otool -l`: the dependency must be `@rpath/<libpython>` and the rpath must contain `@executable_path/../python/lib`. On Windows inspect imports with `dumpbin /dependents` or `llvm-readobj --coff-imports`: the import must be the `python312.dll` basename and that DLL must exist beside `neverc.exe`.

  Generate a temporary plugin that imports `ssl`, `hashlib`, `ctypes`, `bz2`, `lzma`, and `sqlite3` before registering a minimal NeverC plugin, then compile a temporary C file with it and `-fsyntax-only`. This makes missing OpenSSL, libffi, bzip2, xz, SQLite, or extension-module dependencies fail after extraction. Report captured stdout/stderr on failure.

- [ ] **Step 3: Wire checks into the feature workflow**

  Broaden the workflow path filter to `.github/workflows/**`, `utils/build/build_pgo.sh`, `utils/ci/check-python-plugin-artifacts.py`, `utils/release/**`, and the existing implementation/docs paths. Add a fast policy job that runs the checker.

- [ ] **Step 4: Run the policy check and verify it fails**

  Run: `python3 utils/ci/check-python-plugin-artifacts.py`

  Expected: failure listing the 15 producers that still omit enablement/bundling/relocation verification and the missing OFF job.

### Task 2: Make the embedded interpreter relocatable

**Files:**
- Modify: `neverc/CMakeLists.txt`
- Modify: `neverc/lib/Plugin/Python/PythonPluginLoader.cpp`
- Modify: `tests/neverc/PythonPluginTests.cpp`

- [ ] **Step 1: Add the bundle-mode CMake contract**

  Define `NEVERC_BUNDLE_PYTHON_RUNTIME` with default `OFF`. Reject `ON` unless `NEVERC_ENABLE_PYTHON_PLUGINS=ON`. When bundled, set the installed `neverc` runtime path to `$ORIGIN/../python/lib` on ELF and `@executable_path/../python/lib` on Mach-O. Windows locates the bundled DLL beside `neverc.exe`.

- [ ] **Step 2: Add adjacent-home path tests**

  Extract the adjacent-runtime path calculation into a small helper and cover executable paths with and without an adjacent `python` directory, including path normalization. The absence case must preserve external-interpreter discovery.

- [ ] **Step 3: Configure `PyConfig.home` before initialization**

  If `<directory containing neverc>/../python` exists, decode it for CPython and set `Config.home` before `Py_InitializeFromConfig`. Preserve the existing process-wide initialization, error propagation, GIL, and no-`Py_Finalize` rules.

- [ ] **Step 4: Run focused lifecycle tests**

  Run: `cmake --build build-neverc --target neverc-plugin-core-tests -j2`

  Run: `ctest --test-dir build-neverc --output-on-failure -R '^PythonPluginTest\.'`

  Expected: all Python plugin tests pass with no bundled runtime present, proving external discovery remains valid.

### Task 3: Bundle a trimmed CPython runtime

**Files:**
- Create: `utils/release/bundle-python-runtime.py`
- Create: `utils/release/test-bundle-python-runtime.py`

- [ ] **Step 1: Write bundler unit tests against a synthetic Python layout**

  Cover POSIX and Windows destination layouts, exclusion of `__pycache__`, `site-packages`, CPython's own test suite, `idlelib`, `tkinter`, `ensurepip`, and `venv`, preservation of `lib-dynload`/`DLLs`, copying of the exact shared runtime and `LICENSE.txt`, recursive native-dependency discovery, system-library allowlists, dependency-license attribution, and manifest generation. Mock `readelf`/`ldd`, `otool`/`install_name_tool`, and PE import-tool command execution for deterministic Linux, macOS, and Windows assertions.

- [ ] **Step 2: Run tests and verify the implementation is missing**

  Run: `python3 -m unittest utils/release/test-bundle-python-runtime.py -v`

  Expected: failure because the bundler module does not exist.

- [ ] **Step 3: Implement the bundler**

  Interface:

  ```sh
  python3 utils/release/bundle-python-runtime.py \
    --prefix /absolute/install \
    --neverc /absolute/install/bin/neverc
  ```

  Use the running interpreter's `sys.base_prefix` and `sysconfig` paths. Copy the standard library to `python/lib/pythonX.Y` on POSIX or `python/Lib` on Windows, excluding only the documented development/test/UI/package-management trees. Copy `lib-dynload` or `DLLs`, the CPython license, and the runtime shared library. On Windows place `pythonXY.dll` beside `neverc.exe`; on Unix place the runtime under `python/lib`.

  Starting from NeverC, libpython, and every copied extension module, recursively resolve native dependencies. Leave only an explicit platform system allowlist external (`/lib*` and `/usr/lib*` on Linux, `/System/Library` and `/usr/lib` on macOS, documented Windows system DLLs); copy every other dependency into the bundled runtime, recursively inspect it, and fail on an unresolved import. Put POSIX dependencies in `python/lib` and Windows dependencies beside the executable so the platform loader can find them. On ELF use `patchelf` to give copied extension modules/libraries relative rpaths back to `python/lib`; on macOS rewrite copied dependency IDs/imports with `install_name_tool`, rewrite NeverC's Python dependency to `@rpath/<runtime>`, and use `@loader_path`/`@rpath` references only. Preserve each copied third-party dependency's discoverable license under `python/licenses/`, record a dependency-to-license mapping, and fail rather than ship an unattributed third-party runtime.

  Write `python/runtime.json` containing version, ABI, platform, source layout, copied runtime name, exclusions, copied native dependency closure, system-library allowlist matches, and license mapping.

- [ ] **Step 4: Run unit and local-layout tests**

  Run: `python3 -m unittest utils/release/test-bundle-python-runtime.py -v`

  Run the bundler against a temporary prefix with the locally installed CPython and inspect `python/runtime.json`. Run the package verifier so its loader-metadata checks and representative imports prove the copied dependency closure works without host Python paths.

  Expected: tests pass; no headers, executables, `site-packages`, or bytecode caches are copied.

### Task 4: Enable and verify normal and LTO artifacts

**Files:**
- Modify: `.github/workflows/build-linux-x64.yml`
- Modify: `.github/workflows/build-linux-arm64.yml`
- Modify: `.github/workflows/build-macos-arm64.yml`
- Modify: `.github/workflows/build-windows-x64.yml`
- Modify: `.github/workflows/build-windows-arm64.yml`
- Modify: `.github/workflows/build-windows-x64-clang-lto.yml`
- Modify: `.github/workflows/build-windows-arm64-clang-lto.yml`

- [ ] **Step 1: Provision CPython 3.12**

  Add an `actions/setup-python` step with id `python` after checkout. Use `architecture: arm64` on Windows ARM64 runners. Pin the action to commit `a26af69be951a213d495a4c3e4e4022e16d87065` (`v5.6.0`). Add `patchelf` to every Linux normal-build dependency install because the bundler repairs extension-module and copied-library rpaths.

- [ ] **Step 2: Enable the feature and bundle mode explicitly**

  Add `-DPython3_EXECUTABLE="${{ steps.python.outputs.python-path }}"`, `-DNEVERC_ENABLE_PYTHON_PLUGINS=ON`, and `-DNEVERC_BUNDLE_PYTHON_RUNTIME=ON` to every configure command.

- [ ] **Step 3: Bundle before packaging**

  Immediately after installation, invoke the bundler with the setup action's interpreter and the install prefix. On macOS this occurs before any future signing step.

- [ ] **Step 4: Verify every actual compiler archive after relocation**

  After creating each full compiler archive, run `test-python-plugin-package.py --archive <archive>`. Normal and LTO workflows also create a compiler-only archive after temporarily removing `install/runtime`; verify that archive independently before upload. Runtime-only, PDB, log, and profile-data archives are intentionally excluded. The verifier must run with build-time Python paths hidden and must validate both loader metadata and representative native-module imports.

### Task 5: Forward the same feature set through PGO

**Files:**
- Modify: `utils/build/build_pgo.sh`
- Modify: `.github/workflows/build-linux-x64-pgo.yml`
- Modify: `.github/workflows/build-linux-arm64-pgo.yml`
- Modify: `.github/workflows/build-macos-arm64-pgo.yml`

- [ ] **Step 1: Add validated PGO inputs**

  Read `NEVERC_ENABLE_PYTHON_PLUGINS` and `NEVERC_BUNDLE_PYTHON_RUNTIME` with `OFF` defaults and reject non-boolean values. When Python is enabled, require `NEVERC_PGO_PYTHON3_EXECUTABLE` to be an executable file.

- [ ] **Step 2: Pass the inputs to both configure phases**

  Build an argument array containing the two feature flags and optional exact interpreter. Expand it in both `generate` and `use` CMake invocations so profile and final compilers match.

- [ ] **Step 3: Configure both independent PGO jobs**

  Set both feature variables to `"ON"`, provision pinned CPython 3.12 in profile and use jobs, and set `NEVERC_PGO_PYTHON3_EXECUTABLE` from the matching setup output on every `build_pgo.sh` invocation. Install `patchelf` in every Linux PGO job that invokes the bundler/package verifier (in particular the final `use` job).

- [ ] **Step 4: Bundle and verify every relocated PGO compiler archive**

  Bundle after the PGO install. Verify both the full archive and the compiler-only archive produced after `install/runtime` is moved aside. Do not apply the compiler verifier to the runtime-only or profile-data archives.

### Task 6: Enable, bundle, sign, and verify releases

**Files:**
- Modify: `.github/workflows/release-linux-x64.yml`
- Modify: `.github/workflows/release-linux-arm64.yml`
- Modify: `.github/workflows/release-macos-arm64.yml`
- Modify: `.github/workflows/release-windows-x64.yml`
- Modify: `.github/workflows/release-windows-arm64.yml`

- [ ] **Step 1: Provision and configure CPython**

  Add pinned CPython 3.12 setup and all three explicit CMake arguments to every release producer. Add `patchelf` to each Linux release dependency install.

- [ ] **Step 2: Bundle before signing or packaging**

  Run the bundler immediately after install. The macOS release's existing recursive Mach-O signing step then signs the copied libpython and extension modules before notarization.

- [ ] **Step 3: Include the runtime in every distribution shape**

  Full archives already include the entire install tree. Add `python` to the Linux/macOS curl-installer archive roots beside `bin`, `lib`, and `pluginsdk`.

- [ ] **Step 4: Verify extracted artifacts**

  Run the package verifier on every full release archive and on each Linux/macOS curl-installer archive. Its loader inspection must reject build-host Python paths and its plugin must import `ssl`, `hashlib`, `ctypes`, `bz2`, `lzma`, and `sqlite3`. Perform these checks after signing/notarization on macOS so the tested bytes are the shipped bytes.

### Task 7: Retain the explicit disabled build

**Files:**
- Modify: `.github/workflows/python-plugin-bindings.yml`

- [ ] **Step 1: Add a Linux x64 OFF job**

  Configure with `-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF`, `-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF`, and tests enabled. Build `neverc` and `neverc-plugin-core-tests`.

- [ ] **Step 2: Assert disabled behavior**

  Run the existing `PluginRegistryTest` disabled diagnostic test, invoke the compiler with a real `.py` path and require the actionable `NEVERC_ENABLE_PYTHON_PLUGINS=ON` diagnostic, and assert `neverc` has no libpython dependency (`ldd` must not report one).

- [ ] **Step 3: Guard the OFF job structurally**

  Extend `check-python-plugin-artifacts.py` to require the job name, both explicit OFF values, disabled diagnostic test, and no-libpython assertion.

### Task 8: Document and locally verify the complete result

**Files:**
- Modify: `docs/plugin-api/python.md`
- Modify: `docs/plugin-api/python.zh-CN.md`
- Modify: the remaining `docs/plugin-api/python.<locale>.md` translations

- [ ] **Step 1: Document official artifact behavior**

  State in every locale that official compiler archives enable Python plugins and carry a relocatable CPython 3.12 runtime plus standard library, while custom source builds remain optional and may use an external CPython 3.10+ runtime.

- [ ] **Step 2: Run static validation**

  Run: `python3 -m unittest utils/release/test-bundle-python-runtime.py -v`

  Run: `python3 -m unittest utils/release/test-python-plugin-package.py -v`

  Run: `python3 utils/ci/check-python-plugin-artifacts.py`

  Run: `python3 utils/plugin-api/check-docs-links.py`

  Run: `python3 utils/plugin-api/check-docs-facts.py`

  Run: `bash -n utils/build/build_pgo.sh`

  Run: `shellcheck utils/build/build_pgo.sh`

  Run: `command -v patchelf` on Linux before the local bundle/relocation test, installing it first when absent.

  Run: `git diff --check`

  Expected: all commands pass.

- [ ] **Step 3: Run an exact local enabled/relocation test**

  Run:

  ```sh
  cmake -S llvm -B build-neverc \
    -DNEVERC_ENABLE_PYTHON_PLUGINS=ON \
    -DNEVERC_BUNDLE_PYTHON_RUNTIME=ON \
    -DPython3_EXECUTABLE="$(command -v python3)" \
    -DNEVERC_INCLUDE_TESTS=ON
  cmake --build build-neverc --target neverc neverc-plugin-core-tests neverc-pluginsdk -j2
  ctest --test-dir build-neverc --output-on-failure -R '^PythonPluginTest\.|^PluginPythonPhaseBindings$|^PluginPythonSDKTests$'
  install_prefix=$(mktemp -d /tmp/neverc-python-install.XXXXXX)
  cmake --install build-neverc --prefix "$install_prefix/install"
  python3 utils/release/bundle-python-runtime.py \
    --prefix "$install_prefix/install" \
    --neverc "$install_prefix/install/bin/neverc"
  (cd "$install_prefix" && zip -qr neverc.zip install)
  python3 utils/release/test-python-plugin-package.py \
    --archive "$install_prefix/neverc.zip"
  ```

  Expected: configure/build/tests pass, and the relocated archive loads the installed plugin after build-time Python paths are hidden.

- [ ] **Step 4: Commit, push, and inspect CI**

  Commit on `ci/enable-python-plugin-builds`, push it, manually dispatch the feature workflow if necessary, and verify the policy, explicit OFF job, minimum CPython 3.10 matrix, and all ordinary Python-enabled artifact workflows start.
