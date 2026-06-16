**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核 Netlink

双向 Netlink IPC 通道。创建 netlink socket 用于用户态↔内核态通信。支持 PING（返回 PONG）、VERSION（内核版本字符串）和 ECHO（负载回显）。演示 `nvk_nl_open`、`nvk_nl_reply` 和分发回调模式。

## 构建

```bash
cd examples/android-kernel-netlink
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606` 或 `612` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod nvk_netlink'
```
