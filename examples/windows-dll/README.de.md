**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Windows Ring3 DLL Beispiel

Eine Windows-Benutzermodus-DLL, cross-kompiliert mit NeverC.

## Build

```bash
cd examples/windows-dll
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
```

Das Makefile speichert `PROFILE`, sodass spätere `neverc make`-Aufrufe
dieselbe debug/release-Auswahl behalten. Release nutzt NeverCs integriertes
`--strip`: Debug-Metadaten und unnötige statische Symbolnamen entfallen,
benötigte Loader-/Dynamik-ABI-Namen bleiben. Siehe
[Release-Builds](../../docs/release-builds/README.de.md).

## Manueller Build

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## Funktionen

- Exportiert Cross-Prozess-Speicherzugriffs-Wrapper
- Prozess-/Modulauflistung
- XOR-Pufferverschlüsselungs-Helper

