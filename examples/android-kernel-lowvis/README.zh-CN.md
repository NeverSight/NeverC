**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核低可见性模块

模块可见性管理演示。编译时标志：无=基本列表可见性，`-DNVK_LOWVIS_HIDE`=完整可见性过滤（列表+sysfs+proc），`-DNVK_LOWVIS_FULL_HIDE`=扩展（dmesg+PID+挂载+maps），`-DNVK_LOWVIS_ROOT`=凭证包装演示（`struct cred`），`-DNVK_LOWVIS_SELINUX`=SELinux 强制状态演示（permissive）。

## 构建

```bash
cd examples/android-kernel-lowvis
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606`、`612` 或 `618` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
