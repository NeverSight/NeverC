**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Character Device

Misc-Zeichengerät mit ioctl-Schnittstelle und `/proc`-Statusseite. Demonstriert `misc_register`, ioctl-Befehlsweiterleitung und `seq_file`-basierte proc-Einträge — das Standard-IPC-Muster Benutzer↔Kernel auf Android.

## Kompilierung

```bash
cd examples/android-kernel-chardev
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606`, `612` oder `618` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_chardev.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_chardev.ko'
adb shell su -c 'dmesg | grep nvk_chardev'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_chardev'
```
