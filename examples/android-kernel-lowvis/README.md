**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Android Kernel Lowvis

Module visibility management demo. Compile-time flags: none=basic list visibility, `-DNVK_LOWVIS_FILTER`=full visibility filter (list+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=extended (dmesg+PID+mount+maps), `-DNVK_LOWVIS_CRED`=credential wrapper demo (`struct cred`), `-DNVK_LOWVIS_SELINUX`=SELinux enforcement-state demo (permissive).

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
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
```

## Kernel log (live)

On the device, `cat /proc/kmsg` streams the kernel ring buffer in real time — similar to **DbgView** on Windows. Use it when `insmod` fails with a vague error or you need the exact kernel rejection reason (vermagic, modversions, section size, and so on).

Terminal 1 (leave running):

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
```

New lines appear in terminal 1 as the kernel handles the load. Press Ctrl+C to stop.

Note: stock `dmesg -w` is missing on some Android builds; `/proc/kmsg` needs root but follows live kernel output reliably.

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
