**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Inline Hook

Inline hook on `do_faccessat`. Default: simple replacement with trampoline. With `-DNVK_CONTEXT_HOOK`: context hook receiving full `nvk_reg_ctx` register state. Demonstrates BTI/PAC-safe patching, PC-relative relocation, and D-cache→I-cache coherent trampoline.

## Build

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, or `612` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
