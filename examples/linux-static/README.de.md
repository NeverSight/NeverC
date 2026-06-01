**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux vollständig statisches Binary Beispiel

Eigenständige, statisch gelinkte Linux-Datei mit NeverC. Null Laufzeitabhängigkeiten.

NeverC bündelt ein Linux-Sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Erstellung

```bash
cd examples/linux-static
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Manuelle Erstellung

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## Ausführung

```bash
chmod +x static-demo
./static-demo
```

## Funktionen

- Systeminformationen
- Mathematische Funktionen: `sqrt`, `sin`, `pow`, `log`
- String-Operationen, dynamische Speicherverwaltung
