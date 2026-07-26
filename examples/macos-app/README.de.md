**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# macOS-Anwendungsbeispiel

Eine native macOS Mach-O-Programmdatei, die mit NeverC cross-kompiliert wurde. Demonstriert sysctl, uname und Mach-Kernel-APIs zur System- und Prozessabfrage. Kompilierung von macOS, Windows oder Linux — kein Xcode erforderlich.

## Kompilierung

Aus dem Repository (Standard-Ziel: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make
```

Für Intel kompilieren:

```bash
neverc make TARGET=x86_64-apple-macos
```

Mit einer eigenständigen NeverC-Version:

```bash
neverc make NEVERC=/path/to/neverc
```

## Manuelle Kompilierung (ohne Make)

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## Ausführung

```bash
./macos-app
```

## Funktionen

- Abfrage der Kernel-Informationen über `uname`
- Auslesen von Hardware-Details über `sysctl` (Modell, CPU-Anzahl, Arbeitsspeicher, Seitengröße)
- Anzeige der Prozessidentität (`getpid`, `getppid`, `getuid`)
- Abruf von Mach-Host-Informationen (`host_info`) und Task-Speicherstatistiken (`task_info`)
