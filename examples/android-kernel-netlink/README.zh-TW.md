**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心 Netlink

雙向 Netlink IPC 通道。建立 netlink socket 用於用戶態↔核心態通訊。支援 PING（回傳 PONG）、VERSION（核心版本字串）和 ECHO（負載回顯）。展示 `nvk_nl_open`、`nvk_nl_reply` 和分派回呼模式。

## 建置

```bash
cd examples/android-kernel-netlink
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_netlink'
```
