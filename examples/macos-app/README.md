**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# macOS Application Example

A native macOS Mach-O executable cross-compiled using NeverC. Demonstrates sysctl, uname, and Mach kernel APIs for system and process introspection. Builds from macOS, Windows, or Linux — no Xcode required.

## Build

From the repo (default target: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
```

The Makefile persists `PROFILE`, so later `neverc make` keeps the same
debug/release selection. Release uses NeverC's integrated `--strip`:
debug metadata and unneeded static symbol names are removed while
loader/dynamic ABI names that the binary still needs are preserved.
See [Release builds](../../docs/release-builds/README.md).


Build for Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

From a standalone NeverC release:

```bash
neverc make NEVERC=/path/to/neverc
```

## Manual build (without Make)

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## Run

```bash
./macos-app
```

## What it does

- Queries kernel info via `uname`
- Reads hardware details through `sysctl` (model, CPU count, memory size, page size)
- Reports process identity (`getpid`, `getppid`, `getuid`)
- Retrieves Mach host info (`host_info`) and task memory statistics (`task_info`)
