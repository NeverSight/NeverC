# NeverC First Release Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the current NeverC compiler as the first complete, installable GitHub Release without exposing users to partial assets or unrelated GKI releases.

**Architecture:** Keep each platform workflow responsible only for building, testing, packaging, and uploading CI artifacts. A single coordinator waits for every platform and runtime job, verifies the complete asset set, generates checksums, creates a draft release, uploads everything, and only then publishes it as latest. The POSIX installer resolves the newest stable release that actually contains its platform asset and verifies that asset against the published checksum manifest before changing the install prefix.

**Tech Stack:** POSIX shell, GitHub Actions reusable workflows, CMake/LLVM version metadata, GitHub CLI, actionlint, shellcheck

---

### Task 1: Capture Installer Failures as Integration Tests

**Files:**
- Create: `utils/release/test-install.sh`
- Modify: `install.sh`

- [ ] **Step 1: Add one public-interface test for mixed release types**

Run `install.sh` with boundary fakes for `curl`, `uname`, and `unzip`. Return a newer GKI release before a compiler release and assert that the installer downloads the compiler tag containing `neverc-linux-x64.zip`.

- [ ] **Step 2: Run the test and verify RED**

Run: `sh utils/release/test-install.sh latest-selects-platform-asset`

Expected: FAIL because the current installer calls `/releases/latest` and selects `gki-build-20260701`.

- [ ] **Step 3: Implement asset-aware stable release resolution**

Pass the detected platform asset to `resolve_version`, query `releases?per_page=100`, ignore drafts/prereleases, and select the first release block containing the exact asset name. Normalize explicit bare semantic versions to `v<version>` and reject malformed versions.

- [ ] **Step 4: Run the test and verify GREEN**

Run: `sh utils/release/test-install.sh latest-selects-platform-asset`

Expected: PASS.

- [ ] **Step 5: Add and implement checksum-before-install behavior**

Add one test that supplies a mismatched `SHA256SUMS`, verify RED, then download the manifest beside the archive and compare with `sha256sum` or `shasum -a 256`. Extract only after verification and validate `bin/neverc` in staging before copying into the prefix.

- [ ] **Step 6: Add and implement the supported-platform guard**

Add one test for Darwin x86_64, verify RED, then reject combinations without a matching release workflow before any network request. Keep Linux x64/arm64 and macOS arm64 supported.

- [ ] **Step 7: Run the complete installer test**

Run: `sh utils/release/test-install.sh`

Expected: PASS for latest resolution, explicit versions, checksum success/failure, installation layout, cleanup, and unsupported platform diagnostics.

### Task 2: Enforce the CMake Version and Tag Contract

**Files:**
- Create: `utils/release/check-version.sh`
- Create: `utils/release/test-version.sh`
- Modify: `llvm/CMakeLists.txt`

- [ ] **Step 1: Add a failing tag-contract test**

Exercise `check-version.sh` against a fixture CMake file and assert that `v3389.1.2` matches while `v3389.1.1` fails.

- [ ] **Step 2: Run the test and verify RED**

Run: `sh utils/release/test-version.sh`

Expected: FAIL because the checker does not exist.

- [ ] **Step 3: Implement the checker**

Read the first default values of `LLVM_VERSION_MAJOR`, `LLVM_VERSION_MINOR`, and `LLVM_VERSION_PATCH`, require numeric values, print the version, and compare an optional tag exactly with `v<major>.<minor>.<patch>`.

- [ ] **Step 4: Bump the patch version safely**

Change `LLVM_VERSION_PATCH` from `1` to `2`; existing `v3389.1.1` points to an older commit and must not be moved.

- [ ] **Step 5: Run the checker against the repository**

Run: `sh utils/release/check-version.sh llvm/CMakeLists.txt v3389.1.2`

Expected: output `3389.1.2` and exit 0.

### Task 3: Make Platform Workflows Reusable Build Producers

**Files:**
- Create: `.github/actionlint.yaml`
- Modify: `.github/workflows/release-linux-x64.yml`
- Modify: `.github/workflows/release-linux-arm64.yml`
- Modify: `.github/workflows/release-windows-x64.yml`
- Modify: `.github/workflows/release-windows-arm64.yml`
- Modify: `.github/workflows/release-macos-arm64.yml`
- Modify: `.github/workflows/release-runtime.yml`

- [ ] **Step 1: Replace independent tag triggers**

Add `workflow_call` and retain `workflow_dispatch` for artifact-only rehearsals. Remove each workflow's `push.tags` trigger so six concurrent runs cannot create or publish the same GitHub Release independently.

- [ ] **Step 2: Reduce permissions**

Set build producer permissions to `contents: read`; only the coordinator publish job receives `contents: write`.

- [ ] **Step 3: Remove direct release mutation**

Delete every producer's `softprops/action-gh-release` step. Preserve all build, plugin SDK, signing, notarization, package verification, and artifact upload steps.

- [ ] **Step 4: Pin release-critical actions**

Resolve and pin checkout, artifact, Xcode, MSVC, and sccache actions to reviewed commit SHAs with version comments.

- [ ] **Step 5: Configure the real hosted ARM runner label**

Add `windows-11-arm` to `.github/actionlint.yaml`; actionlint 1.7.7 predates this GitHub-hosted label even though the repository's successful ARM64 release run proves it is valid.

### Task 4: Add One Draft-Then-Publish Coordinator

**Files:**
- Create: `.github/workflows/release.yml`
- Create: `.github/release-notes-prefix.md`
- Modify: `.github/workflows/build-gki-kernels.yml`

- [ ] **Step 1: Add validation, concurrency, and reusable build jobs**

Trigger on `v*` tags and manual dispatch. Serialize each ref with `compiler-release-${{ github.ref }}` and never cancel a run in progress. Run shell tests, shellcheck, actionlint, and CMake version validation first. On tag pushes only, additionally require the exact stable tag `v<major>.<minor>.<patch>`; manual runs on `dev` validate the numeric CMake version but skip tag comparison so they remain artifact-only rehearsals. Call all five platform producers plus the runtime producer, inheriting repository secrets only for the macOS call.

- [ ] **Step 2: Add the complete asset gate**

After every producer succeeds, download all artifacts into one directory and require the five full archives, three curl-installer archives, seven runtime archives, and no missing installer platform asset. Test every zip archive with `unzip -t`.

- [ ] **Step 3: Generate a deterministic checksum manifest**

Sort the exact expected asset list and run `sha256sum` to produce `SHA256SUMS`; include it in the release assets consumed by `install.sh`.

- [ ] **Step 4: Create safely and retryably with GitHub CLI**

Run the publish job only when `github.event_name == 'push'` and `github.ref` is a tag; `workflow_dispatch` is an artifact-only rehearsal and can never create a release. For a new tag, use `gh release create "$GITHUB_REF_NAME" --verify-tag --draft --latest=false --generate-notes --notes-file .github/release-notes-prefix.md <assets>`. If the same tag already has a draft from a failed upload, recover it with `gh release upload "$GITHUB_REF_NAME" <assets> --clobber`; if it is already published, verify its complete asset contract and do not mutate it. Only after every local/remote asset gate succeeds should `gh release edit "$GITHUB_REF_NAME" --draft=false --latest` publish the stable release.

- [ ] **Step 5: Remove future latest collisions**

Add `--latest=false` when the GKI workflow creates its date-based release so kernel artifacts cannot replace the compiler distribution behind installer/runtime latest URLs.

### Task 5: Close the Nightly Merge-Fuzzer Release Blocker

**Files:**
- Modify: `neverc/include/neverc/Merge/Merger.h`
- Modify: `neverc/lib/Merge/ELF/MergerELF.cpp`
- Modify: `neverc/lib/Merge/Verify/MergerVerify.cpp`
- Modify: `tests/neverc/MergeTests.cpp`

- [ ] **Step 1: Pin the exact nightly crash as a regression**

Replay `crash-c01ac293a62ba717cec7f4670eddf4b6a0a01e6f` with the same length-prefix carving as `MergeFuzzer.cpp`. Add a valid compressed-output test whose `ch_size` is patched to the exact `0xffffffff1100` value that caused ASan's allocation-size-too-big failure.

- [ ] **Step 2: Validate typed ELF parser inputs**

Before calling `ELFObjectFile<ELF64LE>::create`, require ELF magic, 64-bit class, little-endian data, and the current ident version. The typed parser assumes callers already identified the container and otherwise accepts any header-sized byte buffer.

- [ ] **Step 3: Refuse pre-compressed merge inputs**

Reject retained `SHF_COMPRESSED` input sections even when verification is disabled. Concatenating their on-disk headers and independent frames is not a valid logical section; return false so the caller falls back to the regular linker. Preserve final output compression after uncompressed contributions are merged.

- [ ] **Step 4: Bound verifier decompression before allocation**

Give the raw ELF verifier a cumulative decompression budget equal to the actual input bytes supplied by the caller. Reject unsupported codecs or any declared logical size that exceeds the remaining budget before calling `compression::decompress`.

- [ ] **Step 5: Run regression, differential, and fuzzer replay checks**

Build `neverc-merge-tests` and `neverc-merge-fuzzer`, run all merger tests, and replay the saved artifact once. After pushing, dispatch the sanitizer-enabled `merge-fuzz` workflow and require success.

### Task 6: Verify and Publish `v3389.1.2`

**Files:**
- Test: all files above

- [ ] **Step 1: Run local release checks**

Run: `sh utils/release/test-version.sh && sh utils/release/test-install.sh && shellcheck install.sh utils/release/*.sh`

Expected: all tests pass with no shellcheck findings.

- [ ] **Step 2: Validate workflow syntax and references**

Run: `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.7 .github/workflows/release.yml .github/workflows/release-linux-x64.yml .github/workflows/release-linux-arm64.yml .github/workflows/release-windows-x64.yml .github/workflows/release-windows-arm64.yml .github/workflows/release-macos-arm64.yml .github/workflows/release-runtime.yml`

Expected: exit 0 for all workflows, including local reusable workflow calls and required secrets.

- [ ] **Step 3: Re-run Apple notarization preflight**

Dispatch `check-notarization.yml` on `dev`, wait for completion, and require success. The prior July 26 failure was Apple HTTP 403 for an expired/missing agreement; the July 28 preflight succeeded after account remediation.

- [ ] **Step 4: Commit and push release preparation**

Commit only the release-preparation files and push `dev` to `origin/dev`. Do not move or delete `v3389.1.0` or `v3389.1.1`.

- [ ] **Step 5: Run the complete artifact-only release rehearsal**

Dispatch `.github/workflows/release.yml` on `dev` and wait for validation plus all five platform producers and the runtime producer. Require success with the publish job skipped; a manual dispatch must not create or mutate a GitHub Release.

- [ ] **Step 6: Create and push the immutable version tag**

Create annotated tag `v3389.1.2` at the verified release-preparation commit, confirm the tag's CMake contract, then push the tag to origin.

- [ ] **Step 7: Monitor the coordinator to terminal success**

Wait for every validation/build/runtime/sign/notarize/publish job. Confirm the resulting Release is non-draft, non-prerelease, marked latest, contains the exact expected assets plus `SHA256SUMS`, and has no partial assets.

- [ ] **Step 8: Test the public installer against the published release**

Use a temporary install prefix with `NEVERC_NO_MODIFY_PATH=1`, run the raw `HEAD/install.sh`, execute the installed `neverc --version`, and require `3389.1.2`. Remove only the explicit temporary prefix afterward.
