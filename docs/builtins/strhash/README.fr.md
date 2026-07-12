**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Système d'exécution intégré NeverC](../README.fr.md)

# Hachage de chaînes à la compilation (`strhash`)

## Vue d'ensemble

NeverC fournit un hachage de chaînes à la compilation et à l'exécution pour le C pur — utile pour le dispatch rapide par égalité d'entiers (noms d'API, jetons de commande) sans table de chaînes en clair dans le binaire.

- **Niveau 1 — Macro explicite** : `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` se réduit à une constante entière en Sema
- **Niveau 2 — Runtime + pliage IR optionnel** : `neverc_strhash_rt` / `NC_STRHASH_AUTO`, avec `-fstrhash-fold` pour plier les appels à arguments littéraux

Les deux niveaux partagent l'algorithme choisi par `-fstrhash-algo` (défaut : FNV-1a 64-bit).

---

## Démarrage rapide

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

## Algorithmes

| Valeur | Description | Défaut |
|--------|-------------|--------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **Oui** |
| `xxhash64` | XXHash64 (seed 0) | |

---

## Référence des drapeaux du compilateur

| Drapeau | Description |
|---------|-------------|
| `-fstrhash-algo=fnv32a` | Utiliser FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | Utiliser FNV-1a 64-bit (défaut) |
| `-fstrhash-algo=xxhash64` | Utiliser XXHash64 (seed 0) |
| `-fstrhash-fold` | Plier les appels de hachage à arguments chaîne constants |
| `-fno-strhash-fold` | Désactiver le pliage IR |
