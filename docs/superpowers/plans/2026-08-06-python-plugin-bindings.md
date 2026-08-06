# Python Plugin Bindings v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional, lifecycle-correct Python plugins that load from `-fplugin=*.py`, provide a small generated Python SDK, and preserve the native C plugin ABI.

**Architecture:** `PluginModule` receives host-internal lifecycle dispatch methods and an optional `PluginRuntime`; native modules dispatch to their existing descriptor callbacks, while each Python script owns a `PythonPluginRuntime` backed by one shared embedded CPython interpreter. Python registration is observer-first, with checked capsule handles providing options, diagnostics, lifecycle state, generic frames, and raw driver arguments.

**Tech Stack:** C++17, LLVM Support, NeverC C plugin ABI, CPython C API 3.10+, CMake, Python 3.10+, GoogleTest/CTest

---

**Specification:** `docs/superpowers/specs/2026-08-06-python-plugin-bindings.md`

**Files and responsibilities:**

- `neverc/lib/Plugin/Core/PluginRuntime.h`: private polymorphic lifecycle boundary.
- `neverc/include/neverc/Plugin/Host/PluginRegistry.h` and
  `neverc/lib/Plugin/Core/PluginRegistry.cpp`: runtime ownership, `.py`
  discovery, native/Python-neutral lifecycle dispatch.
- `neverc/lib/Plugin/Python/PythonPluginLoader.h` and
  `neverc/lib/Plugin/Python/PythonPluginLoader.cpp`: interpreter singleton,
  script import/metadata, Python runtime, native bridge module, checked handles,
  observers/options/diagnostics/driver arguments.
- `pluginsdk/python/neverc_plugin/`: pure-Python authoring API and generated
  phase constants.
- `utils/plugin-api/gen-python-phases.py`: deterministic phase binding
  generator.
- `tests/neverc/PythonPluginTests.cpp` and Python inputs: registry/lifecycle
  unit coverage.
- `tests/neverc/PluginPythonBridgeTests.cpp`: full `neverc -fplugin=*.py`
  driver coverage.
- `docs/plugin-api/python.md`, `docs/plugin-api/README.md`, and SDK READMEs:
  build/use/limitations documentation.

### Task 1: Add a native-neutral runtime lifecycle boundary

**Files:**
- Create: `neverc/lib/Plugin/Core/PluginRuntime.h`
- Modify: `neverc/include/neverc/Plugin/Host/PluginRegistry.h`
- Modify: `neverc/lib/Plugin/Core/PluginRegistry.cpp`
- Modify: `neverc/lib/Plugin/Core/PluginRegistration.cpp`
- Modify: `neverc/lib/Plugin/Core/PluginSession.cpp`
- Modify: `neverc/lib/Plugin/Core/PluginTaskContext.cpp`
- Test: `tests/neverc/PluginLifecycleTests.cpp`

- [ ] **Step 1: Add a failing lifecycle-dispatch test**

  Extend the lifecycle tests with an internal fixture runtime and assert that
  process, register, session, task, destroy, and rollback dispatch retain their
  current ordering. The test must also retain an existing native fixture to
  prove descriptor callbacks still work.

- [ ] **Step 2: Run the focused test and verify it fails to compile**

  Run: `cmake --build build-neverc --target neverc-plugin-core-tests -j2`

  Expected: failure because `PluginRuntime` and `PluginModule` dispatch methods
  do not exist.

- [ ] **Step 3: Introduce the private runtime contract**

  Define virtual `has*` and `invoke*` methods for all seven lifecycle callbacks,
  plus `lastError()`. Add `PluginModule::{has,invoke}{ProcessBegin,Register,
  SessionBegin,SessionEnd,TaskBegin,TaskEnd,Destroy}` methods. Runtime-backed
  modules call the adapter; native modules call the unchanged descriptor
  function pointers.

- [ ] **Step 4: Route every host lifecycle call through `PluginModule`**

  Replace direct callback checks/invocations in registration, session, task,
  unload, and shutdown. Preserve callback leases, exception guards, state
  validation, reverse-order rollback, and diagnostic transactions.

- [ ] **Step 5: Run native plugin lifecycle and registry tests**

  Run: `build-neverc/bin/neverc-plugin-core-tests --gtest_filter='PluginLifecycleTest.*:PluginRegistryTest.*:PluginSessionTest.*'`

  Expected: all existing native tests pass.

### Task 2: Add optional `.py` discovery and build plumbing

**Files:**
- Modify: `neverc/CMakeLists.txt`
- Modify: `neverc/lib/Plugin/CMakeLists.txt`
- Modify: `neverc/lib/Plugin/Core/PluginRegistry.cpp`
- Create: `neverc/lib/Plugin/Python/PythonPluginLoader.h`
- Test: `tests/neverc/PluginRegistryTests.cpp`

- [ ] **Step 1: Add disabled-feature and suffix-routing tests**

  Use a real temporary `.py` file. With the feature disabled, expect an error
  naming `NEVERC_ENABLE_PYTHON_PLUGINS`; confirm native missing-entry behavior is
  unchanged. Propagate `NEVERC_ENABLE_PYTHON_PLUGINS` to the test target as a
  `0`/`1` compile definition and compile the disabled diagnostic assertion only
  for the `0` branch. The `1` branch is covered by `PythonPluginTests.cpp`, so
  reconfiguring the same build directory cannot leave a contradictory test.

- [ ] **Step 2: Add `NEVERC_ENABLE_PYTHON_PLUGINS`**

  Default it to `OFF`. When enabled, require
  `find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development.Embed)`,
  compile the loader, define the feature privately, and link
  `Python3::Python` into `nevercPluginCore`.

- [ ] **Step 3: Branch on the canonical path extension before `dlopen`**

  Keep filesystem identity lookup/deduplication common. Route `.py`
  case-insensitively to `loadPythonPlugin`; otherwise execute the existing
  dynamic-library path without behavioral changes.

- [ ] **Step 4: Verify an OFF build**

  Run: `cmake -S llvm -B build-neverc -DNEVERC_ENABLE_PYTHON_PLUGINS=OFF`

  Run: `cmake --build build-neverc --target neverc-plugin-core-tests -j2`

  Expected: configure/build succeeds without Python development libraries being
  part of the link.

### Task 3: Build the pure-Python SDK and generated phase catalog

**Files:**
- Create: `pluginsdk/python/pyproject.toml`
- Create: `pluginsdk/python/README.md`
- Create: `pluginsdk/python/neverc_plugin/__init__.py`
- Create: `pluginsdk/python/neverc_plugin/api.py`
- Create: `pluginsdk/python/neverc_plugin/phases.py`
- Create: `pluginsdk/python/neverc_plugin/domains/__init__.py`
- Create: `pluginsdk/python/neverc_plugin/domains/driver.py`
- Create: `utils/plugin-api/gen-python-phases.py`
- Create: `pluginsdk/python/tests/test_api.py`
- Modify: `neverc/lib/Plugin/CMakeLists.txt`

- [ ] **Step 1: Write Python SDK tests first**

  Cover canonical plugin IDs, strict SemVer, duplicate decorators, option
  argument validation, observer point normalization, immutable phase values,
  and stale native-handle errors through a fake `_neverc_plugin` module.

- [ ] **Step 2: Run the SDK tests and observe missing-package failure**

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s pluginsdk/python/tests -v`

  Expected: import failure for `neverc_plugin`.

- [ ] **Step 3: Implement the authoring API**

  `Plugin(...)` stores one immutable `PluginSpec` in the defining module.
  `RegistrationContext.option()` validates and maps string enums before calling
  `_neverc_plugin.register_option`. `observer()` accepts `fn=` or decorator
  form and calls `_neverc_plugin.register_observer`. Context/frame methods
  delegate only checked operations to the native module.

- [ ] **Step 4: Generate all phase constants deterministically**

  Parse `PhaseSchema.json`, emit `Phase(high, low, name)` definitions grouped by
  domain, an ID-to-name map, and driver re-exports. Add a CMake `--check` target
  alongside the existing schema checks.

- [ ] **Step 5: Run generator and SDK tests**

  Run: `python3 utils/plugin-api/gen-python-phases.py --output pluginsdk/python/neverc_plugin/phases.py`

  Run: `python3 utils/plugin-api/gen-python-phases.py --check --output pluginsdk/python/neverc_plugin/phases.py`

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s pluginsdk/python/tests -v`

  Expected: generator check and all SDK tests pass.

### Task 4: Implement CPython initialization, import, metadata, and lifecycle

**Files:**
- Create: `neverc/lib/Plugin/Python/PythonPluginLoader.cpp`
- Modify: `neverc/lib/Plugin/Python/PythonPluginLoader.h`
- Modify: `neverc/lib/Plugin/Core/PluginRegistry.cpp`
- Create: `tests/neverc/PythonPluginTests.cpp`
- Create: `tests/neverc/Inputs/Plugin/Python/MinimalPlugin.py`
- Create: `tests/neverc/Inputs/Plugin/Python/LifecyclePlugin.py`
- Create: `tests/neverc/Inputs/Plugin/Python/InvalidPlugin.py`
- Modify: `tests/neverc/CMakeLists.txt`

- [ ] **Step 1: Add failing metadata and lifecycle tests**

  Assert a Python descriptor's ID/name/version/concurrency, same-file dedup,
  independent loading of two scripts, import traceback quality, duplicate ID
  rejection, process/session/task ordering, state propagation, unload, and
  reload. In CMake, give Python-enabled GoogleTest discovery and execution
  `PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk/python` (preserving an
  existing `PYTHONPATH` with the platform path separator). This source-tree SDK
  path is available from Task 3 and deliberately does not depend on the
  build-tree SDK staging added in Task 7.

- [ ] **Step 2: Configure the ON build and verify tests fail**

  Run: `cmake -S llvm -B build-neverc -DNEVERC_ENABLE_PYTHON_PLUGINS=ON -DNEVERC_INCLUDE_TESTS=ON`

  Run: `cmake --build build-neverc --target neverc-plugin-core-tests -j2`

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" build-neverc/bin/neverc-plugin-core-tests --gtest_filter='PythonPluginTest.*'`

  Expected: new Python tests fail because the loader is incomplete, not because
  `neverc_plugin` is missing.

- [ ] **Step 3: Initialize one process-wide interpreter safely**

  Use `PyConfig` with argument parsing and signal handlers disabled. Install the
  private `_neverc_plugin` native module, release the initialization-thread GIL,
  use `PyGILState_Ensure` on every later entry, and intentionally do not call
  `Py_Finalize`.

- [ ] **Step 4: Import scripts with unique module names**

  Use `importlib.util.spec_from_file_location`, publish the module in
  `sys.modules` before execution, make the script directory available during
  import, and remove partially initialized modules on failure. Format errors
  through Python's `traceback` module.

- [ ] **Step 5: Validate metadata and create a runtime-backed module**

  Read the SDK's `PluginSpec`; independently validate UTF-8, canonical ID,
  strict SemVer, class callability, and unsupported metadata. Synthesize the
  host-owned descriptor with session-serial/non-reentrant policy and attach the
  per-script runtime.

- [ ] **Step 6: Implement lifecycle contexts and hook dispatch**

  Instantiate the class at process begin. Create checked process/session/task
  contexts, preserve Python `state`, invoke optional hooks, require `None`
  returns except begin-state values, invalidate contexts at scope end, and
  release all Python references in reverse lifecycle order.

- [ ] **Step 7: Run focused lifecycle tests**

  Run: `build-neverc/bin/neverc-plugin-core-tests --gtest_filter='PythonPluginTest.*:PluginRegistryTest.*:PluginLifecycleTest.*'`

  (Prefix the command with the Task 4 Step 2 `PYTHONPATH=...` assignment when
  running it directly rather than through CTest.) Expected: all tests pass.

### Task 5: Bind options, observers, frames, diagnostics, and driver arguments

**Files:**
- Modify: `neverc/lib/Plugin/Python/PythonPluginLoader.cpp`
- Modify: `pluginsdk/python/neverc_plugin/api.py`
- Create: `tests/neverc/Inputs/Plugin/Python/DriverTracePlugin.py`
- Create: `tests/neverc/Inputs/Plugin/Python/ExceptionPlugin.py`
- Modify: `tests/neverc/PythonPluginTests.cpp`

- [ ] **Step 1: Add failing registration and callback tests**

  Cover option registration/parsing, before/after observer delivery, generic
  frame fields, raw argument values, option lookup, structured remarks, stale
  retained frames, non-`None` callback returns, and raised exceptions with a
  traceback diagnostic/detail token.

- [ ] **Step 2: Implement checked native capsule handles**

  Store kind, runtime, scope pointers/handles, and an active flag in each native
  handle. Every private module function validates capsule name, kind, active
  state, and runtime ownership before touching NeverC data.

- [ ] **Step 3: Implement option registration**

  Convert validated Python values into temporary `NevercOptionDescriptor` and
  string-view arrays. Let the existing registrar copy/validate them. Do not
  expose Python validators in v1.

- [ ] **Step 4: Implement observer ownership and callback dispatch**

  Allocate one binding per registered callable, transfer it only after a
  successful `RegisterObserver`, decref it through `DestroyUserData`, acquire the
  GIL in the callback, build/invalidate a frame, and require a `None` result.

- [ ] **Step 5: Implement frame services**

  Bind immutable phase, observer point, route, input/output handles, cancellation
  checks, session option values, and raw driver argument `(value, origin,
  source, position)` records via the negotiated `NevercDriverAPI`.

- [ ] **Step 6: Implement diagnostic and traceback translation**

  Build `NevercDiagnosticDescriptor` values for remark/warning/error/fatal. On a
  Python exception, format the full traceback, emit an error diagnostic in the
  active transaction, and return `NEVERC_STATUS_PLUGIN_EXCEPTION` with the
  emitted diagnostic's detail token.

- [ ] **Step 7: Run focused core tests**

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" build-neverc/bin/neverc-plugin-core-tests --gtest_filter='PythonPluginTest.*:PluginOptionTest.*:PluginPhaseExecutionTest.*'`

  Expected: all tests pass under the ON build.

### Task 6: Prove full driver and mixed-plugin integration

**Files:**
- Create: `tests/neverc/PluginPythonBridgeTests.cpp`
- Modify: `tests/neverc/CMakeLists.txt`
- Reuse: `tests/neverc/Inputs/Plugin/Python/DriverTracePlugin.py`
- Reuse: `tests/neverc/Inputs/Plugin/Python/ExceptionPlugin.py`

- [ ] **Step 1: Add end-to-end driver tests**

  Spawn the freshly built compiler with `-fplugin=<script.py>` and assert option
  parsing, raw-argument observation, diagnostics, successful compilation,
  exception failure with traceback, two Python plugins together, and mixed
  native/Python loading. Give both the `neverc-tests` CTest entries and their
  spawned compiler process the same source-tree SDK `PYTHONPATH` established in
  Task 4; do not rely on Task 7's later build-tree staging.

- [ ] **Step 2: Build and run the driver tests**

  Run: `cmake --build build-neverc --target neverc-tests -j2`

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" build-neverc/bin/neverc-tests --gtest_filter='PluginPythonBridgeTest.*'`

  Expected: all Python-only and mixed-plugin commands behave as asserted.

- [ ] **Step 3: Run native regression slices**

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" build-neverc/bin/neverc-plugin-core-tests --gtest_filter='PluginRegistryTest.*:PluginLifecycleTest.*:PluginBootstrapTest.*'`

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" build-neverc/bin/neverc-tests --gtest_filter='PluginDiagnosticDriverTest.*:PluginArgumentPhaseTest.*:PluginSDKExampleTest.*'`

  Expected: all pass.

### Task 7: Package, document, and verify both build modes

**Files:**
- Modify: `pluginsdk/CMakeLists.txt`
- Create: `pluginsdk/python/examples/minimal.py`
- Create: `pluginsdk/python/examples/driver_trace.py`
- Create: `docs/plugin-api/python.md`
- Modify: `docs/plugin-api/README.md`
- Modify: `pluginsdk/README.md`
- Modify: `development.md`

- [ ] **Step 1: Stage/install the Python SDK**

  Extend the existing Plugin SDK stamp dependencies and copy/install rules for
  `pyproject.toml`, package sources, README, and examples. The embedded runtime
  adds `<executable>/../pluginsdk/python` to `sys.path` when it exists.

- [ ] **Step 2: Write user and developer documentation**

  Document enabling the feature, CPython requirements, authoring API, lifecycle,
  option/observer examples, packaging, trust model, exception behavior, current
  limitations, and the reason v1 does not use `ctypes` or expose mutations.

- [ ] **Step 3: Verify source and package quality**

  Run: `PYTHONPATH="$PWD/pluginsdk/python${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s pluginsdk/python/tests -v`

  Run: `python3 utils/plugin-api/gen-python-phases.py --check --output pluginsdk/python/neverc_plugin/phases.py`

  Run: `python3 -m build pluginsdk/python` (when the `build` module is available;
  otherwise run `python3 -m pip wheel --no-deps pluginsdk/python` in a temporary
  directory).

- [ ] **Step 4: Verify the ON configuration**

  Run: `cmake -S llvm -B build-neverc -DNEVERC_ENABLE_PYTHON_PLUGINS=ON -DNEVERC_INCLUDE_TESTS=ON`

  Run: `cmake --build build-neverc --target neverc-plugin-core-tests neverc-tests nevercPluginPhaseSchema nevercPythonPluginPhaseSchema -j2`

  Run the Python-focused and native regression filters from Tasks 5 and 6.

- [ ] **Step 5: Verify a clean OFF configuration**

  Run: `cmake -S llvm -B build-python-plugins-off -DLLVM_ENABLE_PROJECTS=neverc -DNEVERC_ENABLE_PYTHON_PLUGINS=OFF -DNEVERC_INCLUDE_TESTS=OFF -DCMAKE_BUILD_TYPE=Debug`

  Run: `cmake --build build-python-plugins-off --target neverc -j2`

  Expected: NeverC builds without linking CPython; passing an existing `.py`
  plugin produces the actionable disabled-feature diagnostic.

- [ ] **Step 6: Review the final diff and working tree**

  Run: `git diff --check`

  Run: `git status --short`

  Expected: no whitespace errors, generated files are current, no build outputs
  are tracked, and only issue-related source/docs/tests are modified.
