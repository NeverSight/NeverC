**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android ELF Example

A native ARM64 ELF binary cross-compiled to Android using NeverC. Designed to run directly on rooted Android devices via `adb shell`. Builds from macOS, Windows, or Linux — no Android NDK or CMake required.

NeverC bundles an Android sysroot (NDK r26c, API 21+) in `runtime/android/`, so a single invocation handles preprocessing, compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo:

```bash
cd examples/android-elf
make
```

From a standalone NeverC release:

```bash
make NEVERC=/path/to/neverc
```

## Manual build (without Make)

```bash
neverc --target=aarch64-linux-android21 -O2 -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## Deploy & Run

Push to device and run via adb:

```bash
make run
```

Or manually:

```bash
adb push android-elf /data/local/tmp/
adb shell chmod 755 /data/local/tmp/android-elf
adb shell /data/local/tmp/android-elf
```

## What it does

- Prints device info (`uname`) and kernel version
- Checks root/privilege status (`uid`/`euid`, `su` paths)
- Dynamically loads `liblog.so` and calls `__android_log_print`
- Reads `/proc/self/maps` to display memory layout
- Demonstrates `dlopen`/`dlsym`, `readlink`, `fopen` on Android
