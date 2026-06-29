**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Hook

Перехват `openat` заменой указателя в `sys_call_table`. Демонстрирует классический перехват системных вызовов на ARM64 GKI ядрах с помощью `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Сборка

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий ядра.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Выгрузка

```bash
neverc make rmmod
```
