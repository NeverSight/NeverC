**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Eingebautes Laufzeitsystem](../README.de.md)

# Kompilierzeit-Zeichenketten-Hashing (`strhash`)

## Überblick

NeverC bietet Kompilierzeit- und Laufzeit-Zeichenketten-Hashing für reines C — für schnellen Dispatch über Integer-Hash-Vergleiche (API-Namen, Befehlstoken), ohne Klartext-Vergleichstabellen im Binary.

- **Stufe 1 — Explizites Makro**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` wird in Sema zu einer Ganzzahlkonstante gefaltet
- **Stufe 2 — Runtime + optionaler IR-Fold**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`, mit `-fstrhash-fold` für Aufrufe mit String-Literal-Argumenten

Beide Stufen teilen den über `-fstrhash-algo` gewählten Algorithmus (Standard: FNV-1a 64-bit).

---

## Schnellstart

```c
#include <neverc/strhash/strhash.h>
static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");
int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## Algorithmen

| Wert | Beschreibung | Standard |
|------|--------------|----------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **Ja** |
| `xxhash64` | XXHash64 (Seed 0) | |

---

## Compiler-Flags-Referenz

| Flag | Beschreibung |
|------|--------------|
| `-fstrhash-algo=fnv32a` | FNV-1a 32-bit verwenden |
| `-fstrhash-algo=fnv64a` | FNV-1a 64-bit verwenden (Standard) |
| `-fstrhash-algo=xxhash64` | XXHash64 (Seed 0) verwenden |
| `-fstrhash-fold` | Laufzeit-Hash-Aufrufe mit konstanten String-Args falten |
| `-fno-strhash-fold` | IR-Folding deaktivieren |
