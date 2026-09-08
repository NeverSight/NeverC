**Lingue**: [English](../builtins-strhash.md) | [简体中文](../zh-CN/builtins-strhash.md) | [繁體中文](../zh-TW/builtins-strhash.md) | [日本語](../ja/builtins-strhash.md) | [한국어](../ko/builtins-strhash.md) | [Français](../fr/builtins-strhash.md) | [Deutsch](../de/builtins-strhash.md) | [Español](../es/builtins-strhash.md) | [Italiano](builtins-strhash.md) | [Русский](../ru/builtins-strhash.md) | [العربية](../ar/builtins-strhash.md)

[← Sistema runtime integrato NeverC](builtins.md)

# Hash di stringhe a tempo di compilazione (`strhash`)

## Panoramica

NeverC fornisce hash di stringhe a tempo di compilazione e di esecuzione per C puro — utile per il dispatch rapido tramite uguaglianza di interi (nomi API, token di comando) senza tabelle di stringhe in chiaro nel binario.

- **Livello 1 — Macro esplicita**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` si riduce a una costante intera in Sema
- **Livello 2 — Runtime + fold IR opzionale**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`, con `-fstrhash-fold` per chiamate con argomenti letterali

Entrambi i livelli condividono l'algoritmo scelto con `-fstrhash-algo` (predefinito: FNV-1a 64-bit).

---

## Avvio rapido

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

## Algoritmi

| Valore | Descrizione | Predefinito |
|--------|-------------|-------------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **Sì** |
| `xxhash64` | XXHash64 (seed 0) | |

---

## Riferimento flag del compilatore

| Flag | Descrizione |
|------|-------------|
| `-fstrhash-algo=fnv32a` | Usa FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | Usa FNV-1a 64-bit (predefinito) |
| `-fstrhash-algo=xxhash64` | Usa XXHash64 (seed 0) |
| `-fstrhash-fold` | Piega le chiamate hash con argomenti stringa costanti |
| `-fno-strhash-fold` | Disabilita il fold IR |
