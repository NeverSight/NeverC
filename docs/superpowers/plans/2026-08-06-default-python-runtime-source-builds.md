# Default Bundled Python Runtime For Source Builds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a normal NeverC source build enable Python plugins by default and make `cmake --install` produce a relocatable compiler carrying the selected CPython runtime, while preserving an explicitly testable Python-disabled build.

**Architecture:** CMake owns the complete feature contract: it discovers one CPython 3.10+ interpreter plus embedding library, links the Python bridge, gives the installed compiler a relative loader path, and invokes the existing runtime bundler as part of the `neverc` install component. The runtime remains selected at build time (official builds pin CPython 3.12; local builds may select any supported 3.10+ CPython), but the installed compiler runs only from its adjacent `python/` tree. Packaging policy and a relocated install smoke test guard that behavior, while a focused bundler fix resolves Debian packages whose license documentation is shared through `/usr/share/doc` symlinks.

**Tech Stack:** CMake 3.20+, C++17, CPython C API 3.10+, Python 3 `unittest`, GitHub Actions

---

**Files and responsibilities:**

- `neverc/CMakeLists.txt`: change Python feature defaults, centralize Python discovery, configure the install-time bundler, and retain relative installed loader paths.
- `neverc/cmake/install-bundled-python.cmake.in`: run the selected build-time interpreter against the installed `neverc`, honoring `--prefix`, `DESTDIR`, platform executable suffixes, and install failures.
- `neverc/lib/Plugin/CMakeLists.txt`: consume the parent Python discovery instead of discovering a second interpreter.
- `neverc/cmake/caches/NeverC.cmake`: make the canonical local-build cache state the Python-enabled/bundled contract explicitly.
- `utils/ci/check-python-plugin-artifacts.py`: enforce the source-build defaults and CMake-owned install bundling in addition to official workflow policy.
- `.github/workflows/python-plugin-bindings.yml`: configure the enabled matrix without explicit feature flags, assert the cache defaults, and exercise a relocated install created solely by `cmake --install`.
- `.github/workflows/build-*.yml` and `.github/workflows/release-*.yml`: stop invoking the bundler a second time after CMake installation; continue provisioning the selected CPython and verifying every relocated archive.
- `utils/release/bundle-python-runtime.py`: find Debian package licenses in shared `/usr/share/doc/<package>` directories and provide a deterministic CPython-license fallback for source-installed interpreters.
- `utils/release/test-bundle-python-runtime.py`: reproduce Debian shared-document attribution and source-installed CPython layouts.
- `utils/release/licenses/CPython-LICENSE.txt`: carry the upstream CPython license used only when the selected interpreter did not install its own copy.
- `tests/neverc/CMakeLists.txt`: normalize native Windows Python paths before embedding them in the C++ SDK-install test harness.
- `docs/plugin-api/python*.md`: explain default source-build behavior, build-time Python requirements, official CPython 3.12 pinning, and runtime independence.

### Task 1: Lock the default source-build contract with a failing policy check

**Files:**

- Modify: `utils/ci/check-python-plugin-artifacts.py`
- Modify: `.github/workflows/python-plugin-bindings.yml`

- [ ] **Step 1: Extend the policy checker with user-visible source-build assertions**

  Require `NEVERC_ENABLE_PYTHON_PLUGINS` and `NEVERC_BUNDLE_PYTHON_RUNTIME` to default to `ON`, require an install-time script containing the bundler command, require producers not to invoke the bundler directly, and require the feature workflow to assert both generated cache values. These are observable distribution contracts rather than checks of CMake helper implementation details.

- [ ] **Step 2: Make the enabled matrix rely on defaults**

  Remove the explicit `-DNEVERC_ENABLE_PYTHON_PLUGINS=ON` argument from the Python-enabled matrix and add exact `CMakeCache.txt` assertions for both default values. Keep the separate disabled job's explicit `OFF/OFF` arguments.

- [ ] **Step 3: Add a CMake-only install/relocation smoke on Linux**

  Install `patchelf`; set `DESTDIR` to a path containing spaces; use a non-default absolute `--prefix` that also contains spaces; run `cmake --install` separately for components `neverc` and `neverc-pluginsdk` without calling `bundle-python-runtime.py` directly; archive the physical `$DESTDIR$prefix` tree; and run `test-python-plugin-package.py`. This proves component installation, prefix override, staging roots, quoting, and the public standalone-compiler contract.

- [ ] **Step 4: Run the checker and confirm RED**

  Run: `python3 utils/ci/check-python-plugin-artifacts.py`

  Expected: failure stating that the CMake defaults remain `OFF` and automatic install bundling is absent.

### Task 2: Make CMake install a self-contained Python distribution

**Files:**

- Modify: `neverc/CMakeLists.txt`
- Create: `neverc/cmake/install-bundled-python.cmake.in`
- Modify: `neverc/lib/Plugin/CMakeLists.txt`
- Modify: `neverc/cmake/caches/NeverC.cmake`

- [ ] **Step 1: Enable both feature options by default**

  Change the option defaults to `ON`, retain the fatal `BUNDLE=ON` plus `PLUGINS=OFF` validation, and set both values explicitly in the canonical `NeverC.cmake` preload cache. The explicit `OFF/OFF` interface remains supported. If `CMAKE_CROSSCOMPILING` and bundling are both enabled, fail configuration with an actionable message: the current public interface can only bundle the runnable host interpreter and must never place it beside a target-architecture compiler.

- [ ] **Step 2: Discover one supported embedding interpreter**

  In `neverc/CMakeLists.txt`, use `find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development.Embed)` when Python plugins are enabled and interpreter-only discovery when disabled. Remove the duplicated discovery from `neverc/lib/Plugin/CMakeLists.txt`; imported targets and variables flow from the parent directory.

- [ ] **Step 3: Stage the bundler and license inputs in the build tree**

  Copy `utils/release/bundle-python-runtime.py` and its fallback license into a build-tree runtime-bundler directory so a configured build can be installed without consulting mutable source files.

- [ ] **Step 4: Install and invoke the bundler after `neverc`**

  Configure `install-bundled-python.cmake.in` with the selected interpreter, staged bundler, bindir, and executable suffix. Register it as part of component `neverc` after `install(TARGETS neverc ...)`. At install time compute the physical prefix as `$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}`, call the bundler, echo captured output, and fail installation on any nonzero status.

- [ ] **Step 5: Remove duplicate bundler mutations from official producers**

  Delete the explicit `bundle-python-runtime.py` command from all 12 governed build/release workflows. Keep setup-python, exact `Python3_EXECUTABLE`, both explicit `ON` values for official producers, Linux `patchelf` installation, and every package relocation verifier. Update policy so a producer containing a direct bundler command fails.

- [ ] **Step 6: Run policy and configure checks and confirm GREEN**

  Run: `python3 utils/ci/check-python-plugin-artifacts.py`

  Run: `cmake -S llvm -B <fresh-build> -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON`

  Expected: policy passes; fresh cache contains both options as `ON` and a supported `Python3::Python` embedding target.

### Task 3: Fix deterministic native-dependency license attribution

**Files:**

- Modify: `utils/release/test-bundle-python-runtime.py`
- Modify: `utils/release/bundle-python-runtime.py`
- Create: `utils/release/licenses/CPython-LICENSE.txt`

- [ ] **Step 1: Reproduce the Debian shared-doc failure**

  Add a test where `dpkg-query -S` reports `libncursesw6:arm64`, `dpkg-query -L` lists only its documentation directory, and that directory resolves to a shared package copyright file. Call `dependency_license` and require the copied attribution path.

- [ ] **Step 2: Run the focused test and confirm RED**

  Run: `python3 -m unittest utils/release/test-bundle-python-runtime.py -v`

  Expected: the new test raises `no attributable license found`.

- [ ] **Step 3: Search Debian documentation directories safely**

  After package ownership is established, inspect both files listed by `dpkg-query -L` and the conventional `/usr/share/doc/<package-without-architecture>/` directory. Follow the directory symlink through normal `Path.is_file()` semantics and copy only recognized copyright/license filenames.

- [ ] **Step 4: Cover source-installed CPython without an installed license**

  Add a test layout with no license under `sys.base_prefix`, stage the vendored upstream CPython license beside the bundler, and require `find_license` to select it. Prefer a license supplied by the interpreter whenever one exists.

- [ ] **Step 5: Run focused tests and confirm GREEN**

  Run: `python3 -m unittest utils/release/test-bundle-python-runtime.py utils/release/test-python-plugin-package-unit.py -v`

  Expected: all bundler and package-helper tests pass.

### Task 4: Update the build and runtime contract documentation

**Files:**

- Modify: `docs/plugin-api/python.md`
- Modify: `docs/plugin-api/python.zh-CN.md`
- Modify: `docs/plugin-api/python.zh-TW.md`
- Modify: `docs/plugin-api/python.ja.md`
- Modify: `docs/plugin-api/python.ko.md`
- Modify: `docs/plugin-api/python.fr.md`
- Modify: `docs/plugin-api/python.de.md`
- Modify: `docs/plugin-api/python.es.md`
- Modify: `docs/plugin-api/python.it.md`
- Modify: `docs/plugin-api/python.ru.md`
- Modify: `docs/plugin-api/python.ar.md`

- [ ] **Step 1: Document the new default and exact boundary**

  State that a fresh normal source configure defaults to Python plugins plus install-time bundling, `cmake --install` creates the adjacent runtime, and `OFF/OFF` remains available for intentionally Python-free compilers. Distinguish the raw build-tree executable (which may use the build interpreter) from the installed distribution. Document `patchelf` as a Linux install-time dependency and state that cross-compiling with automatic host-runtime bundling is rejected; cross builds must disable bundling until a target-runtime layout is supplied explicitly.

- [ ] **Step 2: Document versions without overpromising**

  State that official artifacts pin CPython 3.12, bindings support CPython 3.10+, and a local source build bundles the exact supported interpreter selected at configure time. Build time needs its embedding development files; installed runtime use needs no external Python or `PYTHONPATH`.

- [ ] **Step 3: Run documentation checks**

  Run: `python3 utils/plugin-api/check-docs-links.py`

  Run: `python3 utils/plugin-api/check-docs-facts.py`

  Expected: both pass.

### Task 5: Build, install, relocate, and prove runtime independence locally

**Files:**

- Test only: fresh temporary build and install directories

- [ ] **Step 1: Configure without Python feature flags**

  Run a fresh canonical configure without either NeverC Python option. Verify both cache values are `ON` and record the selected CPython version and shared runtime.

- [ ] **Step 2: Build the compiler, Python tests, and SDK**

  Build `neverc`, `neverc-plugin-core-tests`, and `neverc-pluginsdk`; run the focused Python lifecycle, phase-binding, and SDK tests.

- [ ] **Step 3: Install without a manual bundler command**

  Set a `DESTDIR` containing spaces, use a non-default absolute `--prefix` containing spaces, run `cmake --install <build> --component neverc` and `cmake --install <build> --component neverc-pluginsdk`, and verify the resulting physical prefix contains `python/runtime.json`, the adjacent shared runtime, standard library, license files, compiler, and installed SDK.

- [ ] **Step 4: Relocate and hide the build interpreter**

  Move/archive the install tree to a new temporary path, clear `PYTHONHOME`, `PYTHONPATH`, loader-path variables, and build-time Python entries from `PATH`, then run `test-python-plugin-package.py`. Also invoke the installed compiler directly with the installed minimal Python plugin and `-fsyntax-only`.

- [ ] **Step 5: Run final validation**

  Run: `python3 utils/ci/check-python-plugin-artifacts.py`

  Run: `python3 -m unittest utils/release/test-bundle-python-runtime.py utils/release/test-python-plugin-package-unit.py -v`

  Run: `git diff --check`

  Expected: every check passes and the relocated compiler works without the build-time interpreter paths.

### Task 6: Inspect current Actions state and commit

**Files:**

- Commit all implementation, tests, workflow, documentation, license, and plan changes.

- [ ] **Step 1: Re-check relevant Actions**

  Inspect PR #5 and current `dev` runs. Record pre-existing failures separately from failures caused by this change; specifically confirm the Linux ARM64 license-attribution signature is addressed by the new unit test.

- [ ] **Step 2: Fix Windows test-runner interpreter path embedding**

  Confirm from Actions logs whether Windows failures occur before or after package relocation verification. If MSVC reports invalid C++ escape sequences in `Python3_EXECUTABLE`, normalize native backslashes before putting the path in `NEVERC_PYTHON`, add a policy regression check, and rerun `PluginSDKInstallTest.InstallsAndConsumesSDKFromCleanPrefix` locally.

- [ ] **Step 3: Review the final diff**

  Confirm no unrelated user changes are present, the explicit disabled mode remains covered, and workflow YAML parses structurally.

- [ ] **Step 4: Commit directly on `dev`**

  Stage only the scoped files and commit with a message such as `build: bundle Python in source installs by default`.
