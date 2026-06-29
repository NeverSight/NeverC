**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Hook

ربط `openat` عبر استبدال مؤشره في `sys_call_table`. يوضح اعتراض syscall الكلاسيكي على نوى ARM64 GKI باستخدام `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## البناء

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات نواة أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدوياً:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## إلغاء التحميل

```bash
neverc make rmmod
```
