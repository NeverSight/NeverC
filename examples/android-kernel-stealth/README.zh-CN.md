**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核隐身模块

模块隐藏演示。编译时标志：无=基本列表隐藏，`-DNVK_STEALTH_HIDE`=完整隐藏（列表+sysfs+proc），`-DNVK_STEALTH_FULL_HIDE`=扩展隐藏（dmesg+PID+挂载+maps），`-DNVK_STEALTH_ROOT`=授予 root，`-DNVK_STEALTH_SELINUX`=设置宽容模式。

## 构建

```bash
cd examples/android-kernel-stealth
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606`、`612` 或 `618` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod nvk_stealth'
```
