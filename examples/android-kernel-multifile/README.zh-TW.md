**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 核心多檔案模組

演示多檔案 NeverC 核心模組。要點：

- **單次引導**：`NEVERC_KRT_BOOTSTRAP()` 只需在 `module_init` 中呼叫一次
- **共享狀態**：編譯器將所有 `neverc_krt_*` 狀態提升為 `weak_odr` 連結，所有 `.c` 檔案共享同一符號解析器、快取和子系統狀態
- **分檔架構**：`main.c`（初始化/退出）、`interposes.c`（interpose 邏輯）、`utils.c`（輔助函式）

## 建置

```bash
cd examples/android-kernel-multifile
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

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

## 查看核心日誌（即時）

在裝置上執行 `cat /proc/kmsg` 可持續讀取核心 ring buffer，效果類似 Windows 上的 **DbgView**。當 `insmod` 只回傳含糊錯誤、或需要看清 vermagic、modversions、section 大小等真實拒絕原因時，應優先用這種方式。

終端機 1（保持執行）：

```bash
adb shell
su
cat /proc/kmsg
```

終端機 2：

```bash
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

載入當下的新日誌會出現在終端機 1。按 Ctrl+C 停止。

說明：部分 Android 內建的 `dmesg` 不支援 `-w`；`/proc/kmsg` 需要 root，但對模組載入除錯更可靠。

## 卸載模組

```bash
neverc make rmmod
```
