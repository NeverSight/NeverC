**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心驅動模板

透過 `kallsyms_lookup_name` 動態解析符號的驅動模板。僅匯入 `register_kprobe`/`unregister_kprobe`（GKI 穩定 ABI）。單一原始碼相容所有 GKI 核心 5.10–6.12。

## 建置

```bash
cd examples/android-kernel-driver
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_driver.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
adb shell su -c 'dmesg | grep nvk_driver'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_driver'
```
