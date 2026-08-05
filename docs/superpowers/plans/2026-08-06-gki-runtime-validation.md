# GKI Runtime Release Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reproducible CI gate that validates the current Android kernel runtime against pinned 5.10–6.18 GKI release evidence and actually loads/unloads a current-commit module in each released kernel under QEMU.

**Architecture:** A checked JSON lock pins the six GitHub Release assets. A small Python verifier safely extracts one archive, re-generates its complete layout manifest from `vmlinux`, verifies optional config/export evidence, and invokes the independent module-relocation checker. A read-only reusable workflow is called from the exact same Linux build run/check suite, reuses that run's compiler artifact, runs all current runtime compile/link suites, uploads six smoke modules, then fans out release verification and QEMU boot/load tests. A separate manual entry point resolves an exact matching compiler run for diagnosis.

**Tech Stack:** GitHub Actions, Bash, Python 3 standard library + pyelftools, NeverC, ELF/binutils, QEMU system emulation, AArch64 cross GCC, initramfs/cpio

---

**Specification:** `docs/superpowers/specs/2026-08-06-gki-runtime-validation.md`

**Files and responsibilities:**

- `runtime/android/kernel/arm64/gki-release.json`: immutable release asset identity, sizes, SHA-256 digests, exact packaged vermagic strings, and module-entry KCFI type IDs.
- `runtime/android/kernel/arm64/gki-manifests/{510,515,601,606,612}.json`: re-anchor stale layout/config evidence to the pinned release output.
- `runtime/android/kernel/tools/verify-gki-release.py`: safe extraction and full manifest/evidence/module-offset validation for one profile.
- `runtime/android/kernel/tools/test-verify-gki-release.py`: focused unit coverage for lock validation, digests, archive safety, and structural diffs.
- `runtime/android/kernel/tools/check-sdk-exports.py`: stream Clang's text AST so full SDK export regeneration stays memory-bounded.
- `runtime/android/kernel/tools/test-check-sdk-exports.py`: parser and all-profile manifest declaration coverage.
- `runtime/android/kernel/tools/gki-qemu-smoke-module.c`: deliberately zero-import module whose only purpose is loader init/exit offset execution.
- `runtime/android/kernel/tools/gki-qemu-init.c`: minimal static PID 1 that loads and unloads the smoke module and emits unambiguous markers.
- `runtime/android/kernel/tools/build-gki-initramfs.py`: deterministic `newc` writer, including a real `/dev/console` character-device record.
- `runtime/android/kernel/tools/run-gki-qemu-smoke.sh`: constructs an initramfs, boots a supplied GKI `Image`, and gates on guest markers.
- `runtime/android/kernel/tools/test-gki-qemu-smoke.py`: fake-tool/QEMU tests for all host-side success and failure paths.
- `.github/workflows/validate-gki-runtime.yml`: trusted artifact resolution, current-runtime validation, six-profile release/QEMU matrix, logs, and aggregate gate.
- `.github/workflows/build-linux-x64.yml`: invoke the reusable all-profile gate from the same push run after the compiler artifact is produced.
- `.github/workflows/build-gki-kernels.yml`: correct stale release wording that still calls 6.18 unverified.
- `runtime/android/kernel/README.md`: document the light release-validation path and distinguish it from full kernel rebuilding.

### Task 1: Pin release evidence and add a safe archive verifier

**Files:**
- Create: `runtime/android/kernel/arm64/gki-release.json`
- Create: `runtime/android/kernel/tools/verify-gki-release.py`
- Create: `runtime/android/kernel/tools/test-verify-gki-release.py`

- [ ] **Step 1: Write failing verifier unit tests**

  Cover a valid lock entry, a size mismatch, a SHA-256 mismatch, duplicate or
  missing profiles, absolute/`..` archive paths, symlinks/hardlinks/devices,
  duplicate normalized members, an uncompressed-size ceiling, and concise
  nested manifest differences.

- [ ] **Step 2: Run the focused tests and verify they fail**

  Run: `python3 runtime/android/kernel/tools/test-verify-gki-release.py -v`

  Expected: import or missing-function failures because the verifier does not
  exist yet.

- [ ] **Step 3: Add the pinned release lock**

  Record `NeverSight/NeverC`, `gki-build-20260701`, each profile's canonical
  kernel name and asset filename, the GitHub-reported byte size, its `sha256:`
  release digest, the full vermagic extracted from a packaged module, and an
  exact normalized archive member path for a `.ko` proven to contain
  `.gnu.linkonce.this_module` plus both init/cleanup relocations, and the exact
  KCFI type-id word immediately preceding each entry symbol (or explicit null
  when both symbols begin at section offset zero). Require exactly
  `510/515/601/606/612/618` and unique asset/member identities.

- [ ] **Step 4: Implement validation and safe extraction**

  Stream the digest, compare size before extraction, accept only regular files
  and directories below a normalized relative path, reject duplicate members
  and special/link types, cap total expanded bytes, and extract into a private
  temporary directory.

- [ ] **Step 5: Re-derive and compare kernel evidence**

  Locate exactly one `dist/vmlinux`, invoke `generate-gki-manifest.py` with the
  checked profile as `--base-manifest`; pass each real config and symvers input
  independently when it is packaged, inheriting only the absent input from the
  base manifest. Require a byte-for-byte `cmp` with the checked canonical JSON.
  Validate every packaged config/symvers occurrence, explicitly report each
  absent evidence input as inherited, and print recursive differences only
  after a failed comparison.

  Resolve only the lock-pinned `.ko` member, prove that it has the expected
  module section plus both relocations, verify its `.modinfo` vermagic and the
  `vmlinux` `linux_banner` release against the lock, and independently derive
  the uniform absent/present KCFI entry-prefix ABI and exact type IDs. Copy only
  that module into an isolated temporary directory, and invoke
  `utils/build/verify_gki_offsets.sh <profile> <isolated-directory>`. Assert and
  report that the verifier selected the isolated `.ko`, never a `.mod.o`.

- [ ] **Step 6: Run unit and real 6.18 fixture checks**

  Unit cases must cover malformed/mismatched vermagic, `linux_banner` mismatch,
  canonical byte-level manifest drift, config-only and symvers-only evidence,
  one bad occurrence among duplicate evidence files, a missing/non-qualifying
  pinned `.ko`, and proof that adjacent `.mod.o` files never enter the isolated
  relocation check.

  Run: `python3 runtime/android/kernel/tools/test-verify-gki-release.py -v`

  Run (when the local extracted fixture exists):
  `python3 runtime/android/kernel/tools/verify-gki-release.py --profile 618 --extracted-root local_docs/gki-android17-6.18-build-bin --skip-archive-identity`

  Expected: all unit tests pass; the release `vmlinux` exactly matches the
  checked 6.18 manifest and the direct module offsets pass where GNU readelf is
  available.

- [ ] **Step 7: Re-anchor stale manifests exposed by the release**

  Generate canonical manifests from all six archives. Review every structural
  difference, update 5.10–6.12 to the actual pinned config/build/layout evidence,
  and require byte identity with the derived files. Regenerate/check
  `nvk_compat_table.inc` so any runtime-consumed offset change is explicit.
  Keep 6.18 unchanged when it already matches. Replace the multi-gigabyte JSON
  AST capture in `check-sdk-exports.py` with a streaming text-AST reducer and
  assert current declarations equal every manifest's `sdk_exports` list.

### Task 2: Add an end-to-end QEMU module-load harness

**Files:**
- Create: `runtime/android/kernel/tools/gki-qemu-smoke-module.c`
- Create: `runtime/android/kernel/tools/gki-qemu-init.c`
- Create: `runtime/android/kernel/tools/build-gki-initramfs.py`
- Create: `runtime/android/kernel/tools/run-gki-qemu-smoke.sh`
- Create: `runtime/android/kernel/tools/test-gki-qemu-smoke.py`

- [ ] **Step 1: Add a genuinely zero-import module source**

  Define empty `init_module`/`cleanup_module` entry points and
  `NEVERC_KRT_DEFINE_MODULE("neverc_gki_smoke")` without calling bootstrap,
  printk, or any runtime helper. Compile it for each profile with only the
  locked vermagic overridden, require an empty `nm -u`, and assert the emitted
  `.modinfo` string before upload. For profiles whose official pinned module
  carries KCFI entry prefixes, write the locked four-byte type IDs into the
  verified zero padding immediately before the smoke entry symbols and read
  them back; require zero padding for non-KCFI profiles. Existing
  layout/linkage suites continue to use the unmodified presets and cover
  current runtime code.

- [ ] **Step 2: Add strict argument/tool/input preflight behavior**

  Make `--help` and missing-tool/input cases deterministic so the harness can
  be tested without booting a kernel. Require Python 3, an AArch64 static
  compiler, GNU `timeout`, and `qemu-system-aarch64`; the initramfs writer uses
  Python's gzip support and does not depend on host `cpio`/`mknod` behavior.

- [ ] **Step 3: Implement the guest init program**

  Open `/neverc-smoke.ko`, call `finit_module`, emit
  `NEVERC_GKI_LOAD_PASS`, call `delete_module` for `neverc_gki_smoke`, emit
  `NEVERC_GKI_UNLOAD_PASS`, sync, and power off. On failure print the syscall,
  errno number/text, and distinct `NEVERC_GKI_LOAD_FAIL` or
  `NEVERC_GKI_UNLOAD_FAIL` markers before powering off.

- [ ] **Step 4: Implement deterministic initramfs construction and boot**

  Cross-compile the init statically. Use a small standard-library-only Python
  writer to emit a sorted, timestamp-free `newc` archive containing directories,
  init, module, and a character-device `/dev/console` record with major/minor
  `5:1`; do not rely on unprivileged host `mknod` metadata. Boot the supplied
  raw `Image` on QEMU `virt` with explicit PL011 console and `rdinit=/init`
  arguments, capture the complete log, reject either failure marker even if
  pass text also appears, and require both pass markers. Bound the boot with a
  configurable timeout and print the diagnostic log tail on failure.

- [ ] **Step 5: Test host-side behavior with fake tools/QEMU**

  Use temporary fake cross-compiler and QEMU executables to cover both pass
  markers, load failure, unload failure after a load pass, timeout, and an early
  zero-status exit with no markers. Parse the generated gzip/newc archive and
  assert `/dev/console` is a character device with rdev `5:1`.

  Run: `python3 runtime/android/kernel/tools/test-gki-qemu-smoke.py -v`

  Expected: all scenarios pass.

- [ ] **Step 6: Check real host-side script behavior**

  Run: `bash runtime/android/kernel/tools/run-gki-qemu-smoke.sh --help`

  Run: `bash -n runtime/android/kernel/tools/run-gki-qemu-smoke.sh`

  Run: cross-compile `gki-qemu-init.c` with `-static -Wall -Wextra -Werror` in
  the Ubuntu CI environment and record compiler/QEMU versions.

### Task 3: Reuse the exact compiler artifact and validate the current runtime

**Files:**
- Create: `.github/workflows/validate-gki-runtime.yml`
- Modify: `.github/workflows/build-linux-x64.yml`

- [ ] **Step 1: Add trusted source/artifact resolution**

  Expose `workflow_call` plus manual dispatch; do not use `workflow_run` as a
  required gate. For automatic calls, require an internal non-tag `push`, the
  expected repository ID/full name, `compiler_run_id == github.run_id`, and
  `source_sha == github.sha`; query that still-running caller and verify its
  exact `.github/workflows/build-linux-x64.yml` path and head SHA. For manual
  dispatch, resolve the supplied run identically and require its head SHA to
  equal the explicitly checked-out `github.sha`. In both modes query artifacts
  and require exactly one unexpired `linux-x64-neverc-compiler` before checkout.
  Missing or mismatched artifacts are always errors.

- [ ] **Step 2: Generate the selected kernel matrix from the lock**

  Support `all` and each three-digit profile key through the dispatch UI without
  duplicating asset hashes in YAML. Export the release repository/tag and exact
  source SHA as job outputs.

- [ ] **Step 3: Run current-runtime source/compile/link gates once**

  Pin all third-party Actions to reviewed commit SHAs, set only `contents: read`
  and `actions: read`, checkout the explicit validated SHA with persisted
  credentials disabled, and download the compiler by the validated run ID.
  Extract it, overlay the
  current checkout's SDK files, run source-boundary and user-copy checks,
  `test-sdk-layouts.sh`, `generate-compat-table.py --check`,
  `test-runtime-linkage.sh --full`, and `test-all.sh`. Compile the dedicated
  zero-import smoke module for all six profiles with each locked vermagic and
  upload the six `.ko` files.

- [ ] **Step 4: Validate and boot each pinned GKI in parallel**

  Install `pyelftools==0.32`, the Ubuntu 22.04 Clang/binutils packages,
  `qemu-system-arm`, `gcc-aarch64-linux-gnu`, `libc6-dev-arm64-cross`, and the
  remaining archive utilities; print their resolved versions. Download only
  the matrix asset with `gh release download`, run the Python release verifier,
  download the matching smoke module, and run the boot/load/unload harness.
  Upload the verifier report and QEMU log even on failure.

- [ ] **Step 5: Bind the automatic gate to the source check suite**

  Add a reusable-workflow job to `build-linux-x64.yml`, dependent on the build
  job and guarded to trusted non-tag pushes. Pass `github.run_id`, `github.sha`,
  and `all`; use `always()` so a compiler artifact uploaded before a later build
  test failure can still be diagnosed, while an absent artifact remains red.
  Because this is a called job in the caller run, its aggregate check is bound
  to the same source commit rather than the default-branch tip.

  Inside the reusable workflow, an `always()` aggregate job must fail unless
  preparation, current-runtime validation, and every selected kernel job
  succeeded. Write the source SHA, compiler run, release tag, selected profiles,
  and results to the Actions step summary.

### Task 4: Document the new gate and correct historical wording

**Files:**
- Modify: `.github/workflows/build-gki-kernels.yml`
- Modify: `runtime/android/kernel/README.md`

- [ ] **Step 1: Correct the stale 6.18 release note**

  The current workflow compares 6.18 like every other profile, so remove the
  old statement that it is merely discovered/unverified.

- [ ] **Step 2: Document build-vs-validation workflows**

  Explain that `build-gki-kernels.yml` is the expensive manual producer while
  `validate-gki-runtime.yml` is the consumer/gate. Document the pinned release,
  the full-manifest/direct-relocation checks, exact-SHA compiler requirement,
  QEMU smoke semantics, manual inputs, and the OEM-kernel limitation.

### Task 5: Verify locally, review the workflow, and run it on GitHub

**Files:**
- Modify as needed from review: all files above

- [ ] **Step 1: Run focused local checks**

  Run: `python3 runtime/android/kernel/tools/test-verify-gki-release.py -v`

  Run: `python3 runtime/android/kernel/tools/test-gki-qemu-smoke.py -v`

  Run: `bash -n runtime/android/kernel/tools/run-gki-qemu-smoke.sh`

  Run: `python3 runtime/android/kernel/tools/generate-compat-table.py --check`

  Run: `sh runtime/android/kernel/tools/test-sdk-layouts.sh build-neverc/bin/neverc`

  Expected: all pass.

- [ ] **Step 2: Lint and structurally inspect the workflow**

  Run: `actionlint .github/workflows/validate-gki-runtime.yml` when available.
  Otherwise parse it with Ruby/Python YAML tooling and inspect all expressions
  and shell blocks. Run `git diff --check`.

- [ ] **Step 3: Push the manual-capable workflow first**

  Commit and push the reviewed implementation before adding the automatic
  caller job. This lets the existing Linux workflow produce an exact-SHA
  artifact while keeping the first live validation limited to 6.18; do not
  substitute an older release compiler.

- [ ] **Step 4: Prove the QEMU path on 6.18 first**

  Manually dispatch only profile `618`, wait for the exact-SHA compiler and GKI
  jobs, and inspect the full guest log. Do not start the all-profile validation
  until 6.18 has passed manifest, relocation, `finit_module`, and
  `delete_module` checks on a real GitHub runner.

- [ ] **Step 5: Enable the same-run automatic call and follow all six**

  After 6.18 passes, add the reusable-workflow caller job to
  `build-linux-x64.yml`, commit, and push it. Wait for that exact commit's Linux
  build and in-suite GKI validation jobs. Inspect failed-step logs and uploaded
  QEMU reports if any job is red, fix the root cause, and repeat. Completion
  requires one green aggregate validation covering all six profiles on the
  source commit's own check suite.
