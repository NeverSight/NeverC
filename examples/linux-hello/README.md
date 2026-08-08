**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Linux Hello World Example

A minimal C program cross-compiled to Linux ELF using NeverC. Builds from macOS, Windows, or Linux — no target system toolchain required.

NeverC bundles a Linux sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, so a single invocation handles preprocessing, compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo (default target: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
```

The Makefile persists `PROFILE`, so later `neverc make` keeps the same
debug/release selection. Release uses NeverC's integrated `--strip`:
debug metadata and unneeded static symbol names are removed while
loader/dynamic ABI names that the binary still needs are preserved.
See [Release builds](../../docs/release-builds/README.md).


Build for AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

From a standalone NeverC release:

```bash
neverc make NEVERC=/path/to/neverc
```

## Manual build (without Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## Run

Copy `hello` to a Linux machine (or Docker container) and execute:

```bash
chmod +x hello
./hello
```

## What it does

- Prints a greeting with command-line arguments
- Demonstrates `printf`, `strncpy`, `strlen`, `atoi` from the bundled libc
- XOR-transforms a string to verify basic integer/character operations
