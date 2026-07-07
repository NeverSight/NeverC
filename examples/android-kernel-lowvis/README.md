**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Lowvis

Module concealment demo. Compile-time flags: none=basic list hide, `-DNVK_LOWVIS_HIDE`=full hide (list+sysfs+proc), `-DNVK_LOWVIS_FULL_HIDE`=extended (dmesg+PID+mount+maps), `-DNVK_LOWVIS_ROOT`=grant root, `-DNVK_LOWVIS_SELINUX`=set permissive.

## Build

```bash
cd examples/android-kernel-lowvis
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
