# NeverC Release Guide

This document describes how to publish a NeverC compiler release. Run all commands from the repository root.

NeverC uses a tag-driven central release pipeline:

- Manually dispatching `.github/workflows/release.yml` builds and validates every release artifact without publishing a GitHub Release.
- Pushing a `v*` tag that exactly matches the CMake version builds and publishes the release.
- Do not create the GitHub Release or upload its assets manually.
- Do not use `git push --tags`. Push only the exact tag for the current release.

## 1. Prepare the release branch

Releases are built from the remote `dev` branch. Before starting, verify that GitHub CLI is authenticated and synchronize the working tree:

```sh
gh auth status
git switch dev
git pull --ff-only origin dev
git status --short
```

`git status --short` should produce no output.

Set the release version. A normal patch release usually increments only the patch component, for example from `3389.1.2` to `3389.1.3`:

```sh
NEVERC_RELEASE_VERSION=3389.1.3
NEVERC_RELEASE_TAG="v${NEVERC_RELEASE_VERSION}"
```

The tag must use the format `v<major>.<minor>.<patch>`.

## 2. Update the version and release notes

Update the following version fields in `llvm/CMakeLists.txt`:

```cmake
set(LLVM_VERSION_MAJOR 3389)
set(LLVM_VERSION_MINOR 1)
set(LLVM_VERSION_PATCH 3)
```

A normal patch release only requires changing `LLVM_VERSION_PATCH`.

Update the version tests at the same time:

- Change the current version, current tag, and expected error message in `utils/release/test-version.sh` to the new version.
- Use the previous version for the mismatch case in `utils/release/test-version.sh`.
- It is recommended to update the fake release fixture and explicit-version assertions in `utils/release/test-install.sh` so the examples continue to represent the current release.

To change the static text included in every GitHub Release, edit `.github/release-notes-prefix.md`. Keep the `@RELEASE_TAG@` placeholder because the release workflow replaces it with the current tag.

## 3. Regenerate the target schemas

The target schema `producer_build_id` includes the LLVM/CMake version. After changing the version, regenerate the schemas with a generator built from the new version:

```sh
cmake --build build-neverc \
  --target neverc-plugin-target-schema-gen

python3 utils/plugin-api/check-target-schema.py \
  --update \
  --generator build-neverc/bin/neverc-plugin-target-schema-gen

python3 utils/plugin-api/check-target-schema.py \
  --check \
  --generator build-neverc/bin/neverc-plugin-target-schema-gen
```

Confirm that both schemas use the new version:

```sh
jq -r '.producer_build_id' \
  pluginsdk/schemas/targets/x86_64.json \
  pluginsdk/schemas/targets/aarch64.json
```

If no target register, instruction, or feature table changed, the schemas should normally differ only in `producer_build_id` and `digest`. Do not accept other large changes without reviewing them.

## 4. Run local pre-release checks

Run the installer, version, shell, and generated-file checks:

```sh
sh utils/release/test-install.sh
sh utils/release/test-version.sh

sh utils/release/check-version.sh \
  llvm/CMakeLists.txt \
  "$NEVERC_RELEASE_TAG"

shellcheck install.sh utils/release/*.sh

python3 utils/plugin-api/check-target-schema.py \
  --check \
  --generator build-neverc/bin/neverc-plugin-target-schema-gen

git diff --check
```

Run the complete NeverC test suite:

```sh
cmake --build build-neverc --target check-neverc
```

If a release workflow changed, also run Actionlint locally:

```sh
go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.7 \
  .github/workflows/release.yml \
  .github/workflows/release-linux-x64.yml \
  .github/workflows/release-linux-arm64.yml \
  .github/workflows/release-windows-x64.yml \
  .github/workflows/release-windows-arm64.yml \
  .github/workflows/release-macos-arm64.yml \
  .github/workflows/release-runtime.yml
```

## 5. Commit and wait for normal CI

Review the changes, commit them, and push `dev`:

```sh
git diff --stat
git status --short

git add llvm/CMakeLists.txt \
  utils/release \
  pluginsdk/schemas/targets \
  .github/release-notes-prefix.md

git commit -m "Prepare NeverC ${NEVERC_RELEASE_VERSION} release"
git push origin dev
```

If one of these paths did not change, it does not need to be staged. Wait for all normal Linux, Windows, macOS, documentation, and security checks triggered by the commit to succeed.

## 6. Run a non-publishing release rehearsal

Before pushing the tag, manually dispatch the central release workflow:

```sh
gh workflow run release.yml \
  --repo NeverSight/NeverC \
  --ref dev
```

The command returns the workflow run URL. You can also list the latest rehearsal:

```sh
gh run list \
  --repo NeverSight/NeverC \
  --workflow release.yml \
  --event workflow_dispatch \
  --limit 1
```

After finding the run ID, monitor it until completion:

```sh
gh run watch <RUN_ID> \
  --repo NeverSight/NeverC \
  --compact \
  --exit-status
```

The rehearsal must satisfy all of the following conditions:

- `Validate release contract` succeeds.
- Linux x64, Linux arm64, Windows x64, Windows arm64, and macOS arm64 all succeed.
- `Cross-compilation runtimes` succeeds.
- `Publish complete release` is skipped.
- The rehearsal `headSha` exactly matches the local commit being prepared for release.

Inspect the rehearsal commit with:

```sh
gh run view <RUN_ID> \
  --repo NeverSight/NeverC \
  --json headSha,status,conclusion,url
```

## 7. Create and push the release tag

After the rehearsal and normal CI are green, run the final checks:

```sh
git fetch origin dev

test -z "$(git status --short)"
test "$(git rev-parse HEAD)" = "$(git rev-parse origin/dev)"

sh utils/release/check-version.sh \
  llvm/CMakeLists.txt \
  "$NEVERC_RELEASE_TAG"

git ls-remote --tags origin "refs/tags/$NEVERC_RELEASE_TAG"
```

The last command should produce no output, confirming that the remote does not already contain that tag.

Create an annotated tag and push only that tag:

```sh
git tag -a "$NEVERC_RELEASE_TAG" \
  -m "NeverC $NEVERC_RELEASE_VERSION"

git push origin "refs/tags/$NEVERC_RELEASE_TAG"
```

Never run:

```sh
git push --tags
```

Pushing the exact tag is the only action that starts a public release.

## 8. Monitor the public release workflow

Find the workflow run triggered by the tag:

```sh
gh run list \
  --repo NeverSight/NeverC \
  --workflow release.yml \
  --branch "$NEVERC_RELEASE_TAG" \
  --event push \
  --limit 1
```

Monitor it until completion:

```sh
gh run watch <RUN_ID> \
  --repo NeverSight/NeverC \
  --compact \
  --exit-status
```

The central workflow automatically:

1. Verifies that the tag exactly matches the version in `llvm/CMakeLists.txt`.
2. Builds five supported host distributions and seven target runtime archives in parallel.
3. Verifies the exact set of 15 ZIP asset names.
4. Runs a complete extraction test on every ZIP archive.
5. Verifies that the three curl installer archives contain `bin/neverc`.
6. Generates `SHA256SUMS`.
7. Creates a draft GitHub Release and uploads the assets.
8. Verifies that the remote and local asset sets are identical.
9. Publishes the GitHub Release and marks it as latest.

The workflow does not publish an incomplete GitHub Release before every build and asset check succeeds.

## 9. Perform post-release verification

Inspect the GitHub Release status and assets:

```sh
gh release view "$NEVERC_RELEASE_TAG" \
  --repo NeverSight/NeverC \
  --json tagName,isDraft,isPrerelease,url,assets

gh release view "$NEVERC_RELEASE_TAG" \
  --repo NeverSight/NeverC \
  --json assets \
  --jq '.assets | length'
```

The asset count should be `16`: 15 ZIP archives plus `SHA256SUMS`.

Confirm that the release is the latest compiler release:

```sh
gh api repos/NeverSight/NeverC/releases/latest \
  --jq .tag_name
```

The output must equal `$NEVERC_RELEASE_TAG`.

On Linux or macOS, perform a clean installation from the public release:

```sh
NEVERC_RELEASE_TEST_ROOT=$(mktemp -d /tmp/neverc-release-install.XXXXXX)

curl -fsSL \
  "https://raw.githubusercontent.com/NeverSight/NeverC/${NEVERC_RELEASE_TAG}/install.sh" |
  env NEVERC_VERSION="$NEVERC_RELEASE_TAG" \
      NEVERC_INSTALL_DIR="$NEVERC_RELEASE_TEST_ROOT/prefix" \
      NEVERC_NO_MODIFY_PATH=1 \
      sh

"$NEVERC_RELEASE_TEST_ROOT/prefix/bin/neverc" --version
```

Optionally compile and run a minimal program:

```sh
printf 'int main(void) { return 0; }\n' |
  "$NEVERC_RELEASE_TEST_ROOT/prefix/bin/neverc" \
    -x c - \
    -o "$NEVERC_RELEASE_TEST_ROOT/hello"

"$NEVERC_RELEASE_TEST_ROOT/hello"
```

## 10. Handle failures

### Rehearsal failure

Do not push the tag. Fix the problem, commit and push `dev`, wait for normal CI, and run the rehearsal again. The version can remain unchanged because it has not been published.

### Transient public release failure

If the code at the tag is correct and the failure was caused by a runner, network, Apple service, or another transient dependency, rerun only the failed jobs:

```sh
gh run rerun <RUN_ID> \
  --repo NeverSight/NeverC \
  --failed
```

The release workflow can recover an existing draft for the same tag.

### Code problem discovered after pushing the tag

Do not delete, move, or force-push a tag that has already been pushed. Fix the code, increment the patch version, repeat the complete release process, and publish a new tag.

For example, if `v3389.1.3` contains a code problem, publish the fix as `v3389.1.4` instead of moving `v3389.1.3`.

### Problem discovered after publication

Keep the published release and tag unchanged. Publish the fix as a new patch release. Do not replace published assets or rewrite release history.
