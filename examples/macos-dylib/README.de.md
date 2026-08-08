**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# macOS Dynamische Bibliothek — Beispiel

Eine native macOS `.dylib`, die mit NeverC cross-kompiliert wurde. Kapselt Mach-Kernel-Schnittstellen für Task-Informationen und virtuelle Speicheroperationen — für die Sicherheitsforschung konzipiert. Kompilierung von macOS, Windows oder Linux — kein Xcode erforderlich.

## Kompilierung

Aus dem Repository (Standard-Ziel: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
```

Das Makefile speichert `PROFILE`, sodass spätere `neverc make`-Aufrufe
dieselbe debug/release-Auswahl behalten. Release nutzt NeverCs integriertes
`--strip`: Debug-Metadaten und unnötige statische Symbolnamen entfallen,
benötigte Loader-/Dynamik-ABI-Namen bleiben. Siehe
[Release-Builds](../../docs/release-builds/README.de.md).


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
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## Funktionen

- Exportiert `nc_task_basic_info`-Wrapper für Mach `task_info`-Abfragen
- Bietet `nc_vm_read`/`nc_vm_write` für Mach-VM-Lese-/Schreiboperationen
- `nc_vm_alloc`/`nc_vm_dealloc` für Mach-VM-Speicherallokation und -freigabe
- XOR-Puffer-Verschlüsselungshelfer und PID-/Task-Abfragefunktionen
