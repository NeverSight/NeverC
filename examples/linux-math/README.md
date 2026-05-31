**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Math + zlib Example

Demonstrates math library functions and zlib compression cross-compiled to Linux using NeverC. Uses both `-lm` and `-lz` from the bundled sysroot.

NeverC bundles a Linux sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, so a single invocation handles preprocessing, compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo (default target: `x86_64-linux-gnu`):

```bash
cd examples/linux-math
make
```

Build for AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Manual build (without Make)

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -lm -lz -o math-demo main.c
```

## Run

Copy `math-demo` to a Linux machine (or Docker container) and execute:

```bash
chmod +x math-demo
./math-demo
```

## What it does

- **Trigonometry**: sin/cos/tan for 0° through 360°
- **Special functions**: `exp`, `log`, `tgamma` (factorial), `erf`, `cbrt`, `hypot`
- **zlib compression**: Compresses a string with `compress2` (best compression), decompresses with `uncompress`, verifies roundtrip, computes CRC32
