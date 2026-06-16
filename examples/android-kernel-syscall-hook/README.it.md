**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook syscall kernel Android

Sostituzione tabella syscall su `openat`. Default: scambio voce tabella. Con `-DNVK_SYSCALL_INLINE_HOOK`: patch del prologo del handler. Dimostra `nvk_syscall_replace`/`nvk_syscall_restore` e numeri syscall arm64.

## Compilazione

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni.

## Distribuzione ed esecuzione

```bash
neverc make run
```

Oppure manualmente:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## Scaricamento

```bash
neverc make rmmod
```

Oppure manualmente:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
