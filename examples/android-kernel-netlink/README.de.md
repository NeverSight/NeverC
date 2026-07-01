**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Netlink

Bidirektionaler Netlink-IPC-Kanal. Erstellt einen Netlink-Socket für Benutzer↔Kernel-Kommunikation. Unterstützt PING (gibt PONG zurück), VERSION (Kernel-Versionsstring) und ECHO. Demonstriert `nvk_nl_open`, `nvk_nl_reply` und Dispatch-Callback-Muster.

## Kompilierung

```bash
cd examples/android-kernel-netlink
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606`, `612` oder `618` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_netlink'
```
