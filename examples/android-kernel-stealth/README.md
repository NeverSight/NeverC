**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Stealth

Module concealment demo. Compile-time flags: none=basic list hide, `-DNVK_STEALTH_HIDE`=full hide (list+sysfs+proc), `-DNVK_STEALTH_FULL_HIDE`=extended (dmesg+PID+mount+maps), `-DNVK_STEALTH_ROOT`=grant root, `-DNVK_STEALTH_SELINUX`=set permissive.

## Build

```bash
cd examples/android-kernel-stealth
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_stealth'
```
