**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Linux vollständig statisches Binary Beispiel

Eigenständige, statisch gelinkte Linux-Datei mit NeverC. Null Laufzeitabhängigkeiten.

NeverC bündelt ein Linux-Sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Erstellung

```bash
cd examples/linux-static
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
```

Das Makefile speichert `PROFILE`, sodass spätere `neverc make`-Aufrufe
dieselbe debug/release-Auswahl behalten. Release nutzt NeverCs integriertes
`--strip`: Debug-Metadaten und unnötige statische Symbolnamen entfallen,
benötigte Loader-/Dynamik-ABI-Namen bleiben. Siehe
[Release-Builds](../../docs/release-builds/README.de.md).


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
