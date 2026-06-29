**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心 Syscall Hook

透過替換 `sys_call_table` 中的指標來 hook `openat`。演示 ARM64 GKI 核心上使用 `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore` 進行經典 syscall 攔截。

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## 建置

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606` 或 `612` 以適配其他核心版本。

## 部署和執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## 卸載模組

```bash
neverc make rmmod
```
