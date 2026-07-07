**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Lowvis-Modul

Modul-Verbergungs-Demo. Flags: keine=grundlegendes Listen-Verstecken, `-DNVK_LOWVIS_HIDE`=vollständig (Liste+sysfs+proc), `-DNVK_LOWVIS_FULL_HIDE`=erweitert (dmesg+PID+Mount+Maps), `-DNVK_LOWVIS_ROOT`=Root gewähren, `-DNVK_LOWVIS_SELINUX`=permissiv setzen.

## Kompilierung

```bash
cd examples/android-kernel-lowvis
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606`, `612` oder `618` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
