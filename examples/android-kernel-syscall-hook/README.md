**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Hook

Syscall table replacement on `openat`. Default: table entry swap. With `-DNVK_SYSCALL_INLINE_HOOK`: patches the handler function prologue instead. Demonstrates `nvk_syscall_replace`/`nvk_syscall_restore` and arm64 syscall number definitions.

## Build

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, or `612` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
