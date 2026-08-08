**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Linux Hello World Beispiel

Ein minimales C-Programm, das mit NeverC nach Linux ELF cross-kompiliert wird. Erstellung von macOS, Windows oder Linux aus — keine Ziel-Toolchain erforderlich.

NeverC bündelt ein Linux-Sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, sodass ein einziger Aufruf Vorverarbeitung, Kompilierung, Optimierung (auto-LTO) und Verlinkung über den integrierten Linker abwickelt.

## Erstellung

Aus dem Repository (Standard-Ziel: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
```

Das Makefile speichert `PROFILE`, sodass spätere `neverc make`-Aufrufe
dieselbe debug/release-Auswahl behalten. Release nutzt NeverCs integriertes
`--strip`: Debug-Metadaten und unnötige statische Symbolnamen entfallen,
benötigte Loader-/Dynamik-ABI-Namen bleiben. Siehe
[Release-Builds](../../docs/release-builds/README.de.md).


Für AArch64 erstellen:

```bash
neverc make TARGET=aarch64-linux-gnu
```

Mit einer eigenständigen NeverC-Version:

```bash
neverc make NEVERC=/path/to/neverc
```

## Manuelle Erstellung (ohne Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## Ausführung

Kopieren Sie `hello` auf eine Linux-Maschine (oder Docker-Container) und führen Sie aus:

```bash
chmod +x hello
./hello
```

## Funktionen

- Gibt eine Begrüßung mit Befehlszeilenargumenten aus
- Demonstriert `printf`, `strncpy`, `strlen`, `atoi` aus der gebündelten libc
- XOR-Transformation eines Strings zur Überprüfung grundlegender Integer/Zeichen-Operationen
