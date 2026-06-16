**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Stealth-Modul

Modul-Verbergungs-Demo. Flags: keine=grundlegendes Listen-Verstecken, `-DNVK_STEALTH_HIDE`=vollständig (Liste+sysfs+proc), `-DNVK_STEALTH_FULL_HIDE`=erweitert (dmesg+PID+Mount+Maps), `-DNVK_STEALTH_ROOT`=Root gewähren, `-DNVK_STEALTH_SELINUX`=permissiv setzen.

## Kompilierung

```bash
cd examples/android-kernel-stealth
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606` oder `612` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_stealth'
```
