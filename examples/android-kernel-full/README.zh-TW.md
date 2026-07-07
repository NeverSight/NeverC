**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心完整 SDK 展示

完整 SDK 整合 —— 初始化所有 NVK 子系統，透過 netlink 命令介面公開。生產模組的參考實現。涵蓋：interpose 引擎、憑證操作、模組隱藏、SELinux、程序列舉、VMA 檢查、檔案 I/O、環境偵測和統計。

## 建置

```bash
cd examples/android-kernel-full
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_full'
```
