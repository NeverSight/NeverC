**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Android Kernel Netlink

Bidirectional netlink IPC channel. Creates a netlink socket for user↔kernel communication. Supports PING (returns PONG), VERSION (kernel version string), and ECHO (payload echo). Demonstrates `nvk_nl_open`, `nvk_nl_reply`, and dispatch-callback pattern.

## Build

```bash
cd examples/android-kernel-netlink
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_netlink'
```
