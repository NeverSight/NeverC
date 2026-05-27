**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation NeverC](../README.fr.md)

# Système de Runtime Intégré NeverC

NeverC étend le C standard avec des runtimes intégrés optionnels, embarqués directement dans le binaire du compilateur sous forme de bitcode LLVM. Une fois activés via des drapeaux du compilateur, les runtimes correspondants sont fusionnés dans l'IR de l'utilisateur au moment de la compilation — sans en-têtes externes, bibliothèques ou dépendances de liaison.

## Fonctionnalités Intégrées Disponibles

| Intégré | Drapeau | Défaut | Description |
|---------|---------|--------|-------------|
| [**`string`**](string/README.fr.md) | `-fbuiltin-string` | Désactivé | Type chaîne à sémantique de valeur avec méthodes par appel pointé, gestion automatique de la mémoire et UTF-8 natif |
| [**`mimalloc`**](mimalloc/README.fr.md) | `-fbuiltin-mimalloc` | **Activé** | Allocateur mémoire haute performance remplaçant de manière transparente `malloc`/`free`/`calloc`/`realloc` |
| [**`xorstr`**](xorstr/README.fr.md) | `-fencrypt-call-strings` | Désactivé | Chiffrement de chaînes à la compilation, déchiffrement XOR sur pile, algorithme anti-signature |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Vue d'Ensemble de l'Architecture

`string` et `mimalloc` partagent la même architecture à quatre couches :

1. **Options de langage et drapeaux du pilote** — `LangOption` défini dans `LangOptions.def`
2. **API Foundation** — fournit `getEmbeddedBitcode()` et `isSupported()`
3. **Infrastructure CMake Bootstrap** — génération de bitcode en deux étapes
4. **Passe de fusion IR** — fusion du bitcode dans le module utilisateur à `PipelineStartEP`

Exemple d'enregistrement dans `LangOptions.def` :

```cpp
LANGOPT(BuiltinString,      1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc,    1, 1, "inject mimalloc allocator override")
LANGOPT(EncryptCallStrings, 1, 0, "auto-encrypt string literals in call arguments")
VALUE_LANGOPT(EncryptCallStringsMaxLen, 32, 1024,
              "maximum string length for auto-encryption (0 = no limit)")
```

> **Remarque :** `xorstr` n'utilise pas le modèle de bitcode embarqué. La macro explicite [`NC_XORSTR(s)` / `NEVERC_XORSTR(s)`](xorstr/README.fr.md) est abaissée par la couche Sema (gestionnaire `semaBuiltinNeverCXorstr` dans `SemaChecking.cpp`), et le chiffrement automatique optionnel `-fencrypt-call-strings` est effectué par la passe de transformation IR `EncryptCallStringsPass` enregistrée en **OptimizerLast** (avec `XorStrCleanupPass` qui met à zéro les tampons de pile en clair via `memset` volatile). Voir la [documentation xorstr](xorstr/README.fr.md) pour le détail.

---

## Différences de Conception entre les Intégrés

| Aspect | `string` | `mimalloc` |
|--------|----------|------------|
| **Stratégie de fusion** | À la demande (BFS graphe d'appels) | Archive complète (tous les symboles) |
| **Bitcode plateforme** | Unique (indépendant de l'architecture) | Par OS (Linux / Darwin / Windows) |
| **Traitement des symboles** | Tous internalisés | Points d'entrée d'override restent externes |
| **Macro préprocesseur** | *(aucune)* | `__NEVERC_MIMALLOC__` |
| **Mode shellcode** | Auto-activé, réécriture arena | Supprimé (HeapArenaPass gère le tas) |
| **Niveau d'optimisation** | `-O0` (compilation bitcode) | `-O2` (allocateur critique en performance) |
| **DCE** | Élagage pré-fusion + mark-and-sweep post-fusion | Pas de DCE (sémantique archive complète) |

---

## Verrouillages de Sécurité

| Condition | Effet | Raison |
|-----------|-------|--------|
| `-fno-builtin` | Supprime mimalloc | Pas de scénario d'override CRT |
| `-mkernel` | Supprime mimalloc | Pas de tas en espace utilisateur dans le noyau |
| `-fshellcode-mode` | Supprime mimalloc | Remplacé par HeapArenaPass (basé sur l'arène) |
| `-ffreestanding` | Supprime mimalloc | Pas de libc à remplacer |

Le built-in `string` a sa propre logique de suppression (la réécriture d'arène dans le pipeline shellcode remplace l'allocation de tas).

### HeapArenaPass (Allocation de tas Shellcode)

Lorsque `-fshellcode-mode` est actif, `mimalloc` est supprimé mais les appels `malloc`/`free`/`calloc`/`realloc` sont automatiquement réécrits par `HeapArenaPass` (activé par défaut). La passe utilise une stratégie hybride :

- **Petites allocations (≤ 64 Ko)** : servies depuis une arène résidente sur la pile partagée avec le runtime built-in `string` (allocateur bump + réutilisation de liste libre).
- **Grandes allocations (> 64 Ko) ou arène OOM** : repli vers l'allocateur OS :
  - **Windows** : `malloc`/`free` résolus depuis `msvcrt.dll` via PEB walk (`-mshellcode-win-peb-import`).
  - **Linux / macOS / Android** : `mmap`/`munmap` inlinés en appels système natifs (`-mshellcode-syscall`).
  - **Aucune passe d'import activée** : arène uniquement ; OOM retourne `NULL`.

Contrôle via les flags du driver :

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass activé (défaut)
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass désactivé (comportement original)
```

---

## Macros Préprocesseur

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc est actif — malloc/free sont remplacés de manière transparente
#endif
```

---

## Structure des Fichiers

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h / BuiltinMimalloc.h
│   └── Builtins.def                      # __builtin_neverc_xorstr
├── include/neverc/Transforms/XorStr/
│   └── EncryptCallStringsPass.h / XorStrCleanupPass.h
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   └── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Headers/neverc/
│   └── xorstr.h / xorstr_impl.inc        # macros NC_XORSTR / NEVERC_XORSTR
├── lib/Analyze/Checking/SemaChecking.cpp # semaBuiltinNeverCXorstr
├── lib/Transforms/XorStr/
│   └── EncryptCallStringsPass.cpp / XorStrCleanupPass.cpp
├── lib/Emit/Backend/
│   └── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## Ajout d'un Nouveau Runtime Intégré

1. Ajouter `LANGOPT` dans `LangOptions.def`
2. Ajouter les drapeaux du pilote dans `Options.td.h`
3. Créer l'API Foundation (`BuiltinFoo.h` + `.cpp`)
4. Créer le générateur de source
5. Ajouter les cibles CMake bootstrap
6. Créer la passe IR et l'enregistrer à `PipelineStartEP`
7. Définir la macro préprocesseur
8. Ajouter les vérifications de sécurité
9. Ajouter les tests
10. Ajouter la documentation et les traductions i18n
