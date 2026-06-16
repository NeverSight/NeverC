**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心 Inline Hook

對 `do_faccessat` 的 inline hook。預設：簡單替換 + trampoline。加 `-DNVK_CONTEXT_HOOK`：上下文 hook 接收完整 `nvk_reg_ctx` 暫存器狀態。展示 BTI/PAC 安全補丁、PC 相對重定位和 D-cache→I-cache 一致的 trampoline。

## 建置

```bash
cd examples/android-kernel-inline-hook
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606` 或 `612` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
