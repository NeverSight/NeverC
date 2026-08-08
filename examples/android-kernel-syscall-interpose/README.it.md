**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Android Kernel Syscall Interpose

Interpose di `openat` sostituendo il puntatore in `sys_call_table`. Dimostra l'intercettazione classica di syscall su kernel ARM64 GKI con `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Compilazione

```bash
cd examples/android-kernel-syscall-interpose
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606`, `612` o `618` per altre versioni del kernel.

## Deploy ed esecuzione

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Log del kernel (live)

Sul dispositivo, `cat /proc/kmsg` trasmette il ring buffer del kernel in tempo reale — simile a **DbgView** su Windows. Usarlo quando `insmod` fallisce con un errore generico o serve vedere il vero motivo del rifiuto (vermagic, modversions, dimensione section, ecc.).

Terminale 1 (lasciare in esecuzione):

```bash
adb shell
su
cat /proc/kmsg
```

Terminale 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
```

Le nuove righe compaiono nel terminale 1 al momento del caricamento. Ctrl+C per fermare.

Nota: su alcune build Android manca `dmesg -w`; `/proc/kmsg` richiede root ma segue l'output kernel live in modo affidabile.

## Scaricamento

```bash
neverc make rmmod
```
