**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心多檔案模組

演示多檔案 NeverC 核心模組。要點：

- **單次引導**：`NEVERC_KRT_BOOTSTRAP()` 只需在 `module_init` 中呼叫一次
- **共享狀態**：編譯器將所有 `neverc_krt_*` 狀態提升為 `weak_odr` 連結，所有 `.c` 檔案共享同一符號解析器、快取和子系統狀態
- **分檔架構**：`main.c`（初始化/退出）、`hooks.c`（hook 邏輯）、`utils.c`（輔助函式）

## 建置

```bash
cd examples/android-kernel-multifile
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606` 或 `612` 以適配其他核心版本。

## 部署和執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## 卸載模組

```bash
neverc make rmmod
```
