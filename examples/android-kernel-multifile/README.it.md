**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Multi-File Module

Modulo kernel NeverC multi-file. Punti chiave:

- **Bootstrap singolo**: `NEVERC_KRT_BOOTSTRAP()` viene chiamato solo una volta in `module_init`
- **Stato condiviso**: il compilatore promuove tutto lo stato `neverc_krt_*` a linkage `weak_odr`, tutti i `.c` condividono lo stesso resolver, cache e stato
- **Architettura divisa**: `main.c` (init/exit), `hooks.c` (logica hook), `utils.c` (helper)

## Compilazione

```bash
cd examples/android-kernel-multifile
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni del kernel.

## Deploy ed esecuzione

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Scaricamento

```bash
neverc make rmmod
```
