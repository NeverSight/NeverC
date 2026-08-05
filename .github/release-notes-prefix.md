## Quick install

Linux x64/arm64 and macOS arm64:

```sh
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/@RELEASE_TAG@/install.sh | NEVERC_VERSION=@RELEASE_TAG@ sh
```

The command is pinned to this release tag. The installer verifies the downloaded archive against the release's `SHA256SUMS` before changing the install prefix.

Windows x64 and arm64 packages are available as release assets for manual installation.

## Choosing a download

Choose the compiler package for the machine that will run NeverC. A runtime package describes the target platform that NeverC will generate code for, not the host platform running the compiler.

| Host platform | Recommended package |
|---|---|
| Linux x86_64 | `neverc-linux-x64.zip` |
| Linux arm64 | `neverc-linux-arm64.zip` |
| macOS Apple Silicon | `neverc-macos-arm64.zip` |
| Windows x86_64 | `windows-x64-neverc-release.zip` |
| Windows arm64 | `windows-arm64-neverc-release.zip` |

### Package types

| Asset pattern | Description |
|---|---|
| `neverc-<os>-<arch>.zip` | Recommended lightweight package used by `install.sh`. It contains the compiler, required headers and libraries, and the NeverC Plugin SDK. Target runtimes are installed on demand. |
| `<os>-<arch>-neverc.zip` | Full offline package for Linux or macOS. It contains the complete CMake installation tree, including bundled runtime resources. |
| `windows-<arch>-neverc-release.zip` | Complete Windows distribution for manual installation. |
| `neverc-runtime-<target>.zip` | Cross-compilation runtime only. It does not contain the NeverC compiler. Install it through `neverc runtime install` instead of extracting it manually. |
| `SHA256SUMS` | SHA-256 checksums for all custom release ZIP files. |
| Source code archives | GitHub-generated source snapshots for users who want to build NeverC from source. They do not contain prebuilt NeverC binaries. |

The larger `linux-<arch>-neverc.zip` and `macos-arm64-neverc.zip` archives are intended for complete offline installations. For a normal online installation, use `install.sh` or the smaller `neverc-<os>-<arch>.zip` package.

## macOS

The macOS arm64 binaries are signed with an Apple Developer ID and notarized by Apple.

## Cross-compilation runtimes

Runtime sysroots are installed on demand:

```sh
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

| Runtime target | Purpose |
|---|---|
| `windows-x64` | Windows x86_64 SDK, MSVC CRT headers, and libraries |
| `windows-arm64` | Windows arm64 SDK, MSVC CRT headers, and libraries |
| `linux-x64` | Ubuntu 22.04 Linux x86_64 sysroot |
| `linux-arm64` | Linux arm64 sysroot |
| `macos-arm64` | macOS 14 arm64 sysroot |
| `android-arm64` | Android arm64 user-space sysroot based on NDK r26c, API 21 or later |
| `android-kernel-arm64` | Minimal Android kernel module headers and NeverC kernel SDK for kernel driver mode |

For example, to cross-compile from macOS arm64 to Windows x86_64, install the macOS arm64 compiler package and then run:

```sh
neverc runtime install windows-x64
```
