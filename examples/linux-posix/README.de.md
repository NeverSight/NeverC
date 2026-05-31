**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux POSIX API Beispiel

POSIX-Systemprogrammierung cross-kompiliert nach Linux mit NeverC: pthreads, mmap, pipe und Signalbehandlung.

NeverC bündelt ein Linux-Sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Erstellung

```bash
cd examples/linux-posix
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Manuelle Erstellung

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## Ausführung

```bash
chmod +x posix-demo
./posix-demo
```

## Funktionen

- **pthreads**: 4 Worker-Threads erstellen
- **mmap**: Anonyme Speicherseite zuweisen
- **pipe**: Nachricht über Unix-Pipe senden
- **signals**: `SIGUSR1`-Handler verifizieren
