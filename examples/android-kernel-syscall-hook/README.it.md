**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Hook

Hook di `openat` sostituendo il puntatore in `sys_call_table`. Dimostra l'intercettazione classica di syscall su kernel ARM64 GKI con `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Compilazione

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni del kernel.

## Deploy ed esecuzione

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Scaricamento

```bash
neverc make rmmod
```
