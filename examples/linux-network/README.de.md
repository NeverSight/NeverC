**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Netzwerk-Socket Beispiel

TCP Client/Server-Demo cross-kompiliert mit NeverC.

NeverC bündelt ein Linux-Sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Erstellung

```bash
cd examples/linux-network
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Manuelle Erstellung

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -o network-demo main.c
```

## Ausführung

```bash
chmod +x network-demo
./network-demo
```

## Funktionen

- TCP-Server (127.0.0.1)
- Client-Verbindung
- 3 Nachrichten senden
- Demo von `socket`, `bind`, `listen`, `accept`
