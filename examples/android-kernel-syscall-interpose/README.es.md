**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Interpose

Interpose de `openat` reemplazando su puntero en `sys_call_table`. Demuestra la intercepción clásica de syscall en kernels ARM64 GKI con `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Compilar

```bash
cd examples/android-kernel-syscall-interpose
neverc make
```

Cambiar `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones del kernel.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Descargar

```bash
neverc make rmmod
```
