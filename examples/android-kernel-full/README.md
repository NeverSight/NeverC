**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Full SDK Demo

Full SDK integration — initializes all NVK subsystems and exposes them through a netlink command interface. Reference implementation for production modules. Exercises: interpose engine, credentials, module hiding, SELinux, process enumeration, VMA inspection, file I/O, environment detection, and statistics.

## Build

```bash
cd examples/android-kernel-full
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_full'
```
