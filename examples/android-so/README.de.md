**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Shared Library Beispiel

Eine native ARM64 `.so` Shared Library, cross-kompiliert für Android mit NeverC. Von macOS, Windows oder Linux aus baubar.

## Build

```bash
cd examples/android-so
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
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## Funktionen

- Hilfsfunktionen für Game-Security-Forschung: PID-Abfrage, `/proc/self/maps`-Lesen, RWX-Speicherzuweisung, XOR-Pufferverschlüsselung
- Dynamisches Laden von `liblog.so` über `dlopen`
- Demo der Zuweisung von ausführbarem Speicher mit `mmap` + `PROT_EXEC`

