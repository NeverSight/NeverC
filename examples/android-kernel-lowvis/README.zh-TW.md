**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心低可見性模組

模組可見性管理展示。編譯旗標：無=基本清單可見性，`-DNVK_LOWVIS_HIDE`=完整可見性過濾（清單+sysfs+proc），`-DNVK_LOWVIS_FULL_HIDE`=擴展（dmesg+PID+掛載+maps），`-DNVK_LOWVIS_ROOT`=憑證包裝展示（`struct cred`），`-DNVK_LOWVIS_SELINUX`=SELinux 強制狀態展示（permissive）。

## 建置

```bash
cd examples/android-kernel-lowvis
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
