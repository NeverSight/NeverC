**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema de ejecución integrado de NeverC](../README.es.md)

# Hash de cadenas en tiempo de compilación (`strhash`)

## Descripción general

NeverC proporciona hash de cadenas en tiempo de compilación y de ejecución para C puro — útil para despacho rápido por igualdad de enteros (nombres de API, tokens de comando) sin tablas de cadenas en claro en el binario.

- **Capa 1 — Macro explícita**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` se reduce a una constante entera en Sema
- **Capa 2 — Runtime + pliegue IR opcional**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`, con `-fstrhash-fold` para llamadas con argumentos literales

Ambas capas comparten el algoritmo elegido con `-fstrhash-algo` (predeterminado: FNV-1a 64-bit).

---

## Inicio rápido

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

## Algoritmos

| Valor | Descripción | Predeterminado |
|-------|-------------|----------------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **Sí** |
| `xxhash64` | XXHash64 (seed 0) | |

---

## Referencia de flags del compilador

| Flag | Descripción |
|------|-------------|
| `-fstrhash-algo=fnv32a` | Usar FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | Usar FNV-1a 64-bit (predeterminado) |
| `-fstrhash-algo=xxhash64` | Usar XXHash64 (seed 0) |
| `-fstrhash-fold` | Plegar llamadas de hash con argumentos de cadena constantes |
| `-fno-strhash-fold` | Deshabilitar el pliegue IR |
