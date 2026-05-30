**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree Plugin-API

NeverC bietet ein **reines C-ABI** für Out-of-Tree-Pass-Plugins. Ein Plugin ist eine gemeinsam genutzte Bibliothek (`.dll` / `.so` / `.dylib`), die benutzerdefinierte Passes an vorgesehenen Stellen der Kompilierungspipeline registriert. Das Plugin wird gegen einen **einzigen Header** (`NevercPluginAPI.h`) kompiliert — **keine** LLVM- oder CRT-Abhängigkeiten. Alle Funktionalität wird über eine vom Host bereitgestellte vtable geroutet.

## 1. Schnellstart

### Minimales Plugin

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

### Kompilierung

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

make -C /path/to/pluginsdk/examples
```

### Ausführung

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. Architektur

**Kernmerkmale:**

- **Ein-Header-SDK**: Nur `NevercPluginAPI.h` wird zum Kompilieren eines Plugins benötigt.
- **Keine Abhängigkeiten**: Keine LLVM-Header, keine CRT-Verlinkung. Alle Operationen über die vtable.
- **Reines C-ABI**: Plugins können in C, C++, Zig, Rust (FFI) oder jeder Sprache geschrieben werden, die eine gemeinsam genutzte Bibliothek mit C-Verlinkung erzeugen kann.
- **Versionssicher**: `NEVERC_API_FN(api, Field)` prüft optionale vtable-Einträge vor dem Aufruf.

## 3. Plugin-Einstiegspunkt

Jedes Plugin muss eine Funktion exportieren:

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `APIVersion` | `uint32_t` | Muss `NEVERC_PLUGIN_API_VERSION` sein |
| `PluginName` | `const char *` | Lesbarer Name |
| `PluginVersion` | `const char *` | Semantischer Versionsstring |
| `RegisterPasses` | Funktionszeiger | Wird einmal aufgerufen, um alle Passes zu registrieren |
| `Destroy` | Funktionszeiger | Optionale Bereinigung, kann `NULL` sein |

## 4. Pass-Typen

- **Module Pass (IR)**: Arbeitet auf dem LLVM-IR-Modul. Kann IR lesen und modifizieren.
- **Machine Pass (MIR)**: Arbeitet auf maschinennahem IR (nach Instruktionsauswahl).
- **Binary Pass**: Arbeitet auf Roh-Bytes (Shellcode-Extraktion, Binär-Patches).
- **Linker Pass**: Arbeitet zur Link-Zeit mit Zugang zu Symbolen und Sektionen.

## 5. Hook-Punkte

### Normaler Ablauf

| Hook | Ebene | Beschreibung |
|------|-------|-------------|
| `NEVERC_HOOK_PRE_OPT` | IR | Vor LLVM-Optimierungspasses |
| `NEVERC_HOOK_POST_OPT` | IR | Nach LLVM-Optimierungspasses |
| `NEVERC_HOOK_PIPELINE_START` | IR | Anfang der Pipeline |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | Ende der IR-Pipeline |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | Vor Pre-Emit-Maschinenpasses |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | Nach allen Maschinenpasses |

### Shellcode / LTO / Linker-Ablauf

Shellcode-Hooks verwenden das Präfix `NEVERC_HOOK_SC_*`, LTO verwendet `NEVERC_HOOK_LTO_*`, Linker verwendet `NEVERC_HOOK_LINK_*`.

## 6. Opake Handle-Typen

Alle IR/MIR-Objekte werden über opake Handles zugegriffen. Handles sind nur **innerhalb des Scopes des Pass-Callbacks gültig**, der sie empfangen hat.

| Handle | Repräsentiert |
|--------|---------------|
| `NevercModuleRef` | LLVM Module |
| `NevercValueRef` | LLVM Value (Funktionen, Instruktionen, Globale) |
| `NevercBasicBlockRef` / `TypeRef` / `ContextRef` | BasicBlock / Type / Context |
| `NevercBuilderRef` | IR Builder |
| `NevercMetadataRef` / `NamedMDRef` / `ComdatRef` | Metadata / Named Metadata / COMDAT |
| `NevercMachineFuncRef` / `MBBRef` / `MInstrRef` | Machine Function / Block / Instruction |
| `NevercUseRef` | Use-def-Ketteneintrag |
| `NevercLinkerSymbolRef` / `SectionRef` | Linker-Symbol / -Sektion |

## 7. Datenstrukturen

Der Host stellt leistungsstarke Datenstrukturen über die vtable bereit: **Arena** (Bump-Pointer-Allokator), **StrMap** (String-Key-HashMap), **IntMap** (Integer-Key-HashMap), **StrBuilder** (inkrementelle String-Konstruktion), **ValueSet** (Value-Hash-Set).

## 8. Versionskompatibilität

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. Plugin-Argumente

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. Lebenszeitregeln

| Ressource | Lebensdauer | Bereinigung |
|-----------|------------|-------------|
| Opake Handles | Gültig im Pass-Callback | Nicht freigeben |
| `NevercBuilderRef` | Erstellt durch `BuilderCreate` | `BuilderDispose` |
| Heap-Strings | Gehören dem Aufrufer | `Free` |
| Arena-Allokationen | Gehören der Arena | `ArenaDestroy` |

## 11. Best Practices

1. **Arena bevorzugen**: Temporäre Daten mit `NEVERC_TRY_ARENA`. Ein `ArenaDestroy` ersetzt N `Free`-Aufrufe.
2. **Versionsschutz**: Neue vtable-Aufrufe immer mit `NEVERC_API_FN` umschließen.
3. **Callback-Iteration bevorzugen**: `ModuleForEachDefinedFunction` ist schneller als Makros.
4. **Keine CRT-Abhängigkeit**: Alle Operationen über die vtable.
5. **Saubere Rückgabe**: Alle Ressourcen vor dem Pass-Return freigeben.

## 12. Plugin-SDK-Inhalt

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # Der einzige benötigte Header
└── examples/
    ├── Makefile             # Eigenständige Build-Vorlage
    ├── ExamplePlugin.c      # Umfassende Demo
    ├── CrtShimPlugin.c      # Zero-CRT-Proof-of-Concept
    └── BenchPlugin.c        # HostAPI-Durchsatz-Mikrobenchmarks
```

## 13. Verwandte Dokumentation

- [Shellcode-Plugin-Schnittstelle](../shellcode-compiler/plugin-interface/README.de.md) — In-tree C++-Erweiterungspunkte für die Shellcode-Pipeline.
