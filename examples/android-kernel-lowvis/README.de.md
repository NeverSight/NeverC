**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Kernel Lowvis-Modul

Modul-Sichtbarkeitsverwaltungs-Demo. Flags: keine=grundlegende Listen-Sichtbarkeit, `-DNVK_LOWVIS_FILTER`=vollständiger Sichtbarkeitsfilter (Liste+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=erweitert (dmesg+PID+Mount+Maps), `-DNVK_LOWVIS_CRED`=Credential-Wrapper-Demo (`struct cred`), `-DNVK_LOWVIS_SELINUX`=SELinux-Enforcement-State-Demo (permissive).

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
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
