**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# API de plugins out-of-tree NeverC

NeverC fournit une **ABI C pure** pour les plugins de passes out-of-tree. Un plugin est une bibliothèque partagée (`.dll` / `.so` / `.dylib`) qui enregistre des passes personnalisées à des points désignés du pipeline de compilation. Le plugin se compile contre un **seul en-tête** (`NevercPluginAPI.h`) avec **zéro** dépendance LLVM ou CRT — toute la fonctionnalité passe par une vtable fournie par l'hôte.

## 1. Démarrage rapide

### Plugin minimal

```c
#include "neverc/Plugin/NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### Compilation

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

make -C /path/to/pluginsdk/examples
```

### Exécution

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. Architecture

**Propriétés clés :**

- **SDK à en-tête unique** : Seul `NevercPluginAPI.h` est nécessaire pour compiler un plugin.
- **Zéro dépendance** : Pas d'en-têtes LLVM, pas de liaison CRT. Toutes les opérations passent par la vtable.
- **ABI C pure** : Les plugins peuvent être écrits en C, C++, Zig, Rust (FFI), ou tout langage capable de produire une bibliothèque partagée avec liaison C.
- **Sûr en termes de version** : Utilisez `NEVERC_API_FN(api, Field)` pour vérifier les entrées optionnelles de la vtable avant de les appeler.

## 3. Point d'entrée du plugin

Chaque plugin doit exporter une fonction :

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| Champ | Type | Description |
|-------|------|-------------|
| `APIVersion` | `uint32_t` | Doit être `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | Nom lisible |
| `PluginVersion` | `const char *` | Chaîne de version sémantique |
| `RegisterPasses` | pointeur de fonction | Appelé une fois pour enregistrer toutes les passes |
| `Destroy` | pointeur de fonction | Nettoyage optionnel, peut être `NULL` |

## 4. Types de passes

- **Module Pass (IR)** : Opère sur le module LLVM IR. Peut lire et modifier l'IR.
- **Machine Pass (MIR)** : Opère sur l'IR au niveau machine (après la sélection d'instructions).
- **Binary Pass** : Opère sur des octets bruts (extraction de shellcode, correctifs binaires).
- **Linker Pass** : Opère au moment de l'édition des liens avec accès aux symboles et sections.

## 5. Points d'accrochage

### Flux normal

| Hook | Niveau | Description |
|------|--------|-------------|
| `NEVERC_HOOK_PRE_OPT` | IR | Avant les passes d'optimisation LLVM |
| `NEVERC_HOOK_POST_OPT` | IR | Après les passes d'optimisation LLVM |
| `NEVERC_HOOK_PIPELINE_START` | IR | Tout début du pipeline |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | Fin du pipeline IR |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | Avant les passes machine pre-emit |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | Après toutes les passes machine |

### Flux shellcode / LTO / éditeur de liens

Les hooks shellcode utilisent le préfixe `NEVERC_HOOK_SC_*`, LTO utilise `NEVERC_HOOK_LTO_*`, et l'éditeur de liens utilise `NEVERC_HOOK_LINK_*`.

## 6. Types de handles opaques

Tous les objets IR/MIR sont accessibles via des handles opaques. Les handles ne sont valides que **dans la portée du callback de passe** qui les a reçus.

| Handle | Représente |
|--------|------------|
| `NevercModuleRef` | LLVM Module |
| `NevercValueRef` | LLVM Value (fonctions, instructions, globaux) |
| `NevercBasicBlockRef` / `TypeRef` / `ContextRef` | BasicBlock / Type / Context |
| `NevercBuilderRef` | IR Builder |
| `NevercMetadataRef` / `NamedMDRef` / `ComdatRef` | Metadata / Named Metadata / COMDAT |
| `NevercMachineFuncRef` / `MBBRef` / `MInstrRef` | Fonction machine / Bloc / Instruction |
| `NevercUseRef` | Entrée chaîne use-def |
| `NevercLinkerSymbolRef` / `SectionRef` | Symbole / Section de l'éditeur de liens |

## 7. Structures de données

L'hôte fournit des structures de données haute performance via la vtable : **Arena** (allocateur bump-pointer), **StrMap** (table de hachage à clé chaîne), **IntMap** (table de hachage à clé entière), **StrBuilder** (construction incrémentale de chaînes), **ValueSet** (ensemble de hachage de valeurs).

## 8. Compatibilité des versions

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. Arguments du plugin

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. Règles de durée de vie

| Ressource | Durée de vie | Nettoyage |
|-----------|-------------|-----------|
| Handles opaques | Valides dans le callback | Ne pas libérer |
| `NevercBuilderRef` | Créé par `BuilderCreate` | `BuilderDispose` |
| Chaînes heap | Appartiennent à l'appelant | `Free` |
| Allocations Arena | Appartiennent à l'Arena | `ArenaDestroy` |

## 11. Bonnes pratiques

1. **Arena en priorité** : Utilisez `NEVERC_TRY_ARENA` pour les données temporaires.
2. **Garde de version** : Enveloppez toujours les nouveaux appels vtable avec `NEVERC_API_FN`.
3. **Itération par callback** : `ModuleForEachDefinedFunction` est plus rapide que les macros.
4. **Aucune dépendance CRT** : Toutes les opérations passent par la vtable.
5. **Retour propre** : Libérez toutes les ressources avant le retour de la passe.

## 12. Contenu du Plugin SDK

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # Le seul en-tête nécessaire
└── examples/
    ├── Makefile             # Modèle de compilation autonome
    ├── ExamplePlugin.c      # Démonstration complète
    ├── CrtShimPlugin.c      # Preuve de concept zéro CRT
    └── BenchPlugin.c        # Micro-benchmarks de débit HostAPI
```

## 13. Documentation associée

- [Interface de plugin Shellcode](../shellcode-compiler/plugin-interface/README.fr.md) — Points d'extension C++ in-tree pour le pipeline shellcode.
