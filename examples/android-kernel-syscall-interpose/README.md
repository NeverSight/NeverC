**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Interpose

Interposes `openat` by replacing its pointer in `sys_call_table`. Demonstrates `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore` for classic syscall interception on ARM64 GKI kernels.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Build

```bash
cd examples/android-kernel-syscall-interpose
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, `612`, or `618` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Unload

```bash
neverc make rmmod
```
