**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Встроенная среда выполнения NeverC](../README.ru.md)

# Хеширование строк на этапе компиляции (`strhash`)

## Обзор

NeverC предоставляет хеширование строк на этапе компиляции и во время выполнения для чистого C — для быстрой диспетчеризации по равенству целых (имена API, токены команд) без таблиц открытых строк в бинарнике.

- **Уровень 1 — Явный макрос**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` сворачивается в целочисленную константу на этапе Sema
- **Уровень 2 — Runtime + опциональный IR-fold**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`, с `-fstrhash-fold` для вызовов с литеральными аргументами

Оба уровня используют алгоритм, выбранный через `-fstrhash-algo` (по умолчанию: FNV-1a 64-bit).

---

## Быстрый старт

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

## Алгоритмы

| Значение | Описание | По умолчанию |
|----------|----------|--------------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **Да** |
| `xxhash64` | XXHash64 (seed 0) | |

---

## Справка по флагам компилятора

| Флаг | Описание |
|------|----------|
| `-fstrhash-algo=fnv32a` | Использовать FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | Использовать FNV-1a 64-bit (по умолчанию) |
| `-fstrhash-algo=xxhash64` | Использовать XXHash64 (seed 0) |
| `-fstrhash-fold` | Сворачивать runtime-вызовы хеша с константными строковыми аргументами |
| `-fno-strhash-fold` | Отключить IR-fold |
