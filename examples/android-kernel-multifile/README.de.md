**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Kernel Multi-File Module

Multi-Datei NeverC Kernelmodul Demo. Kernpunkte:

- **Einmaliger Bootstrap**: `NEVERC_KRT_BOOTSTRAP()` wird nur einmal in `module_init` aufgerufen
- **Geteilter Zustand**: Der Compiler hebt allen `neverc_krt_*` Zustand auf `weak_odr` Linkage, alle `.c` Dateien teilen denselben Resolver, Cache und Subsystem-Zustand
- **Aufgeteilte Architektur**: `main.c` (Init/Exit), `interposes.c` (Interpose-Logik), `utils.c` (Hilfsfunktionen)

## Kompilieren

```bash
cd examples/android-kernel-multifile
neverc make
```

`KERNEL` auf `515`, `601`, `606`, `612` oder `618` aendern fuer andere Kernelversionen.

## Deployment und Ausfuehrung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Entladen

```bash
neverc make rmmod
```
