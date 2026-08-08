**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Linux POSIX API Example

Demonstrates POSIX system programming cross-compiled to Linux using NeverC: pthreads, mmap, pipe, and signal handling.

NeverC bundles a Linux sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, so a single invocation handles preprocessing, compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo (default target: `x86_64-linux-gnu`):

```bash
cd examples/linux-posix
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

## Manual build (without Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## Run

Copy `posix-demo` to a Linux machine (or Docker container) and execute:

```bash
chmod +x posix-demo
./posix-demo
```

## What it does

- **pthreads**: Creates 4 worker threads, each computing a sum, then joins them
- **mmap**: Allocates an anonymous memory page, writes to it, and unmaps
- **pipe**: Sends a message through a Unix pipe and reads it back
- **signals**: Installs a `SIGUSR1` handler and verifies it fires correctly
