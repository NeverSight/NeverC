**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Interpose

Interpose von `openat` durch Ersetzen des Zeigers in `sys_call_table`. Demonstriert klassische Syscall-Interception auf ARM64 GKI Kerneln mit `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Kompilieren

```bash
cd examples/android-kernel-syscall-interpose
neverc make
```

`KERNEL` auf `515`, `601`, `606`, `612` oder `618` ändern für andere Kernelversionen.

## Deployment und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Entladen

```bash
neverc make rmmod
```
