**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核完整 SDK 演示

完整 SDK 集成 —— 初始化所有 NVK 子系统，通过 netlink 命令接口暴露。生产模块的参考实现。涵盖：interpose 引擎、凭证操作、模块隐藏、SELinux、进程枚举、VMA 检查、文件 I/O、环境检测和统计。

## 构建

```bash
cd examples/android-kernel-full
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606`、`612` 或 `618` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod nvk_full'
```
