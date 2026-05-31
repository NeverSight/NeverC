**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Fully-Static Binary Example

A self-contained, statically-linked Linux executable built with NeverC. The output binary has zero runtime dependencies — runs on any Linux system without shared libraries.

NeverC bundles a Linux sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, so a single invocation handles preprocessing, compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo (default target: `x86_64-linux-gnu`):

```bash
cd examples/linux-static
make
```

Build for AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Manual build (without Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## Verify static linking

```bash
file static-demo           # "statically linked"
ldd static-demo            # "not a dynamic executable"
```

## What it does

- Reports system info (pointer size, architecture)
- Math functions: `sqrt`, `sin`, `cos`, `pow`, `log`, `exp`
- String operations: `snprintf`, `strdup`, sorting
- Dynamic memory: `malloc`/`free` across 10 sizes
