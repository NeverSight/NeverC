**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation NeverC](../README.fr.md)

# Système de Runtime Intégré NeverC

NeverC étend le C standard avec des runtimes intégrés optionnels, embarqués directement dans le binaire du compilateur sous forme de bitcode LLVM. Une fois activés via des drapeaux du compilateur, les runtimes correspondants sont fusionnés dans l'IR de l'utilisateur au moment de la compilation — sans en-têtes externes, bibliothèques ou dépendances de liaison.

## Fonctionnalités Intégrées Disponibles

| Intégré | Drapeau | Défaut | Description |
|---------|---------|--------|-------------|
| [**`string`**](string/README.fr.md) | `-fbuiltin-string` | Désactivé | Type chaîne à sémantique de valeur avec méthodes par appel pointé, gestion automatique de la mémoire et UTF-8 natif |
| [**mimalloc**](mimalloc/README.fr.md) | `-fbuiltin-mimalloc` | **Activé** | Allocateur mémoire haute performance remplaçant de manière transparente `malloc`/`free`/`calloc`/`realloc` |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Vue d'Ensemble de l'Architecture

Tous les intégrés partagent la même architecture à quatre couches :

1. **Options de langage et drapeaux du pilote** — `LangOption` défini dans `LangOptions.def`
2. **API Foundation** — fournit `getEmbeddedBitcode()` et `isSupported()`
3. **Infrastructure CMake Bootstrap** — génération de bitcode en deux étapes
4. **Passe de fusion IR** — fusion du bitcode dans le module utilisateur à `PipelineStartEP`

---

## Différences de Conception entre les Intégrés

| Aspect | `string` | `mimalloc` |
|--------|----------|------------|
| **Stratégie de fusion** | À la demande (BFS graphe d'appels) | Archive complète (tous les symboles) |
| **Bitcode plateforme** | Unique (indépendant de l'architecture) | Par OS (Linux / Darwin / Windows) |
| **Traitement des symboles** | Tous internalisés | Points d'entrée d'override restent externes |
| **Macro préprocesseur** | *(aucune)* | `__NEVERC_MIMALLOC__` |
| **Mode shellcode** | Auto-activé, réécriture arena | Supprimé (pas de tas en shellcode) |
| **Niveau d'optimisation** | `-O0` (compilation bitcode) | `-O2` (allocateur critique en performance) |
| **DCE** | Élagage pré-fusion + mark-and-sweep post-fusion | Pas de DCE (sémantique archive complète) |

---

## Verrouillages de Sécurité

| Condition | Effet | Raison |
|-----------|-------|--------|
| `-fno-builtin` | Supprime mimalloc | Pas de scénario d'override CRT |
| `-mkernel` | Supprime mimalloc | Pas de tas en espace utilisateur dans le noyau |
| `-fshellcode-mode` | Supprime mimalloc | Pas de tas en shellcode |
| `-ffreestanding` | Supprime mimalloc | Pas de libc à remplacer |

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
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   ├── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
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
