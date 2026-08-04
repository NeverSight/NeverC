## Quick install

Linux x64/arm64 and macOS arm64:

```sh
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/@RELEASE_TAG@/install.sh | NEVERC_VERSION=@RELEASE_TAG@ sh
```

The command is pinned to this release tag. The installer verifies the downloaded archive against the release's `SHA256SUMS` before changing the install prefix.

Windows x64 and arm64 packages are available as release assets for manual installation.

## macOS

The macOS arm64 binaries are signed with an Apple Developer ID and notarized by Apple.

## Cross-compilation runtimes

Runtime sysroots are installed on demand:

```sh
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```
