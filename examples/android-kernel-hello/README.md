**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Android Kernel Hello

Minimal NeverC Android kernel module (.ko). Bootstraps `kallsyms_lookup_name` via kprobe, prints a load message, and exits cleanly. The simplest end-to-end validation that compile → link → insmod works.

## Build

```bash
cd examples/android-kernel-hello
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep nvk_hello'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_hello'
```
