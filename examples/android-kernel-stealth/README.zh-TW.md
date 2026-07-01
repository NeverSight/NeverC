**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心隱身模組

模組隱藏展示。編譯旗標：無=基本清單隱藏，`-DNVK_STEALTH_HIDE`=完整隱藏（清單+sysfs+proc），`-DNVK_STEALTH_FULL_HIDE`=擴展隱藏（dmesg+PID+掛載+maps），`-DNVK_STEALTH_ROOT`=授予 root，`-DNVK_STEALTH_SELINUX`=設定寬容模式。

## 建置

```bash
cd examples/android-kernel-stealth
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_stealth'
```
