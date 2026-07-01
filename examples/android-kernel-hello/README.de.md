**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Hello

Minimales NeverC Android-Kernelmodul (.ko). Bootstrapt `kallsyms_lookup_name` über kprobe, gibt eine Lademeldung aus und beendet sich sauber. Einfachste End-to-End-Validierung: Kompilierung → Linking → insmod.

## Kompilierung

```bash
cd examples/android-kernel-hello
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606`, `612` oder `618` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep nvk_hello'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_hello'
```
