**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核 Inline Hook

对 `do_faccessat` 的 inline hook。默认：简单替换 + trampoline。加 `-DNVK_CONTEXT_HOOK`：上下文 hook 接收完整 `nvk_reg_ctx` 寄存器状态。演示 BTI/PAC 安全补丁、PC 相对重定位和 D-cache→I-cache 一致的 trampoline。

## 构建

```bash
cd examples/android-kernel-inline-hook
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606` 或 `612` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
