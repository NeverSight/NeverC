**Languages**: [English](../../builtins/mimalloc.md) | [简体中文](../../zh-CN/builtins/mimalloc.md) | [繁體中文](../../zh-TW/builtins/mimalloc.md) | [日本語](../../ja/builtins/mimalloc.md) | [한국어](../../ko/builtins/mimalloc.md) | [Français](mimalloc.md) | [Deutsch](../../de/builtins/mimalloc.md) | [Español](../../es/builtins/mimalloc.md) | [Italiano](../../it/builtins/mimalloc.md) | [Русский](../../ru/builtins/mimalloc.md) | [العربية](../../ar/builtins/mimalloc.md)

[← Système de Runtime Intégré NeverC](README.md)

# Allocateur `mimalloc` Intégré

## Vue d'ensemble

NeverC intègre [mimalloc](https://github.com/microsoft/mimalloc) — l'allocateur mémoire haute performance de Microsoft — directement dans les binaires compilés via la fusion de bitcode LLVM. `malloc`, `free`, `calloc` et `realloc` sont remplacés de manière transparente par les implémentations de mimalloc au moment de la compilation.

**Activé par défaut** partout où il existe un tas libc à remplacer : une compilation ordinaire alloue déjà via mimalloc. Les cibles noyau et freestanding sont exclues automatiquement ; sur une cible hôte, `-fno-builtin-mimalloc` permet de s'en passer.

```bash
neverc main.c -o main
```

---

## Utilisation

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # basique
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # combiné avec `string`
neverc -fno-builtin-mimalloc main.c -o main                    # désactiver
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("Utilisation de l'allocateur mimalloc\n");
#endif
```

---

## Support des Plateformes

| Plateforme | Triple | Statut |
|------------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Supporté |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Supporté |
| Android | `aarch64-linux-android` | Supporté |
| macOS x86_64 | `x86_64-apple-macosx` | Supporté |
| macOS AArch64 | `arm64-apple-macosx` | Supporté |
| iOS | `arm64-apple-ios` | Supporté |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Supporté |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Supporté |

---

## Suppression Automatique

| Drapeau / Mode | Raison |
|----------------|--------|
| `-fno-builtin` | Pas de scénario d'override CRT |
| `-mkernel` | Pas de tas en espace utilisateur dans le noyau |
| `-fms-kernel` | Pilote noyau Windows ; idem, et n'implique pas `-fno-builtin` |
| `-fandroid-kernel-driver-mode` | Module noyau Android ; idem |
| `-shared` / `-dynamiclib` | Remplacer `malloc` relève du programme, pas d'une bibliothèque ; le tas thread-local exige aussi le modèle TLS initial-exec, inutilisable dans un objet partagé |
| `-fdyncode-mode` | Remplacé par HeapArenaPass (arène + fallback OS) |
| `-ffreestanding` | Pas de libc à remplacer |

---

## Processus de Bootstrap

```bash
ninja neverc                         # Étape 1 : placeholders bitcode vides
ninja neverc-bootstrap-mimalloc-bc   # Étape 2 : compiler le bitcode par OS
ninja neverc                         # Étape 3 : intégrer le vrai bitcode
```

---

## Architecture

mimalloc est intégré comme bitcode LLVM dans le binaire du compilateur. Lors de la compilation utilisateur, un Module Pass fusionne le bitcode dans l'IR avant le pipeline d'optimisation. Compilé séparément par OS (Linux `mmap`, macOS `vm_allocate`, Windows `VirtualAlloc`), sélectionné via le target triple. Sémantique **archive complète** — toutes les fonctions sont liées.

---

## Structure des Fichiers

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPredefinedMacros.cpp
```

---

## Référence des Drapeaux du Compilateur

| Drapeau | Description |
|---------|-------------|
| `-fbuiltin-mimalloc` | Activer l'injection de l'override `mimalloc` (activé par défaut pour les builds hébergés) |
| `-fno-builtin-mimalloc` | Désactiver explicitement l'injection `mimalloc` |

| Macro | Valeur | Quand définie |
|-------|--------|---------------|
| `__NEVERC_MIMALLOC__` | `1` | Quand `-fbuiltin-mimalloc` est actif |
