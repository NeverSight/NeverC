**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Kerneltreiber-Vorlage

Treibervorlage mit dynamischer Symbolauflösung über `kallsyms_lookup_name`. Importiert nur `register_kprobe`/`unregister_kprobe` (GKI-stabile ABI). Ein einziger Quellcode für alle GKI-Kernel 5.10–6.12.

## Kompilierung

```bash
cd examples/android-kernel-driver
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606`, `612` oder `618` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_driver.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
adb shell su -c 'dmesg | grep nvk_driver'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_driver'
```
