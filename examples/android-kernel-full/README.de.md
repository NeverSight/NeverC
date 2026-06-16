**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Voll-SDK-Demo

Vollständige SDK-Integration — initialisiert alle NVK-Subsysteme und stellt sie über eine Netlink-Befehlsschnittstelle bereit. Referenzimplementierung für Produktionsmodule. Umfasst: Hook-Engine, Anmeldeinformationen, Modul-Verbergung, SELinux, Prozessauflistung, VMA-Inspektion, Datei-I/O, Umgebungserkennung und Statistiken.

## Kompilierung

```bash
cd examples/android-kernel-full
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606` oder `612` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_full'
```
