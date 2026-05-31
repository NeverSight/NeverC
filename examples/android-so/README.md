**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Shared Library Example

A native ARM64 `.so` shared library cross-compiled to Android using NeverC. Designed to be loaded via `dlopen` or linked at build time on rooted devices. Builds from macOS, Windows, or Linux — no Android NDK or CMake required.

## Build

```bash
cd examples/android-so
make
```

## Manual build (without Make)

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## What it does

- Provides helper functions for game security research: PID query, `/proc/self/maps` reading, RWX memory allocation, XOR buffer encryption
- Uses `dlopen` to dynamically load `liblog.so` for Android logcat output
- Demonstrates `mmap` with `PROT_EXEC` for executable memory allocation

