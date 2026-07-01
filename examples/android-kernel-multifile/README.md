**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Multi-File Module

Demonstrates a multi-file NeverC kernel module. Key points:

- **Single bootstrap**: `NEVERC_KRT_BOOTSTRAP()` only needs to be called once in `module_init`
- **Shared state**: the compiler promotes all `neverc_krt_*` state to `weak_odr` linkage, so all `.c` files share the same resolver, cache, and subsystem state
- **Split architecture**: `main.c` (init/exit), `hooks.c` (hook logic), `utils.c` (helpers)

## Build

```bash
cd examples/android-kernel-multifile
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Unload

```bash
neverc make rmmod
```
