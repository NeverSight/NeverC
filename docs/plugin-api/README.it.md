**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# API per plugin out-of-tree di NeverC

NeverC fornisce un'**ABI C pura** per plugin di pass out-of-tree. Un plugin è una libreria condivisa (`.dll` / `.so` / `.dylib`) che registra pass personalizzati in punti designati della pipeline di compilazione. Il plugin si compila con un **solo header** (`NevercPluginAPI.h`) con **zero** dipendenze da LLVM o CRT — tutta la funzionalità viene instradata attraverso una vtable fornita dall'host.

## 1. Avvio rapido

### Plugin minimo

```c
#include "NevercPluginAPI.h"

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

### Compilazione

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include
```

### Esecuzione

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. Architettura

- **SDK a header singolo**: Solo `NevercPluginAPI.h` è necessario.
- **Zero dipendenze**: Nessun header LLVM, nessun collegamento CRT. Tutte le operazioni tramite vtable.
- **ABI C pura**: I plugin possono essere scritti in C, C++, Zig, Rust (FFI) o qualsiasi linguaggio in grado di produrre una libreria condivisa con collegamento C.
- **Sicurezza versione**: Usare `NEVERC_API_FN(api, Field)` per verificare le voci vtable opzionali.

## 3. Punto di ingresso

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| Campo | Tipo | Descrizione |
|-------|------|-------------|
| `APIVersion` | `uint32_t` | Deve essere `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | Nome leggibile |
| `PluginVersion` | `const char *` | Stringa di versione semantica |
| `RegisterPasses` | puntatore a funzione | Chiamato una volta per registrare tutti i pass |
| `Destroy` | puntatore a funzione | Pulizia opzionale, può essere `NULL` |

## 4. Tipi di pass

- **Module Pass (IR)**: Opera sul modulo LLVM IR.
- **Machine Pass (MIR)**: Opera sull'IR a livello macchina.
- **Binary Pass**: Opera su byte grezzi.
- **Linker Pass**: Opera in fase di collegamento.

## 5. Punti di aggancio

| Hook | Livello | Descrizione |
|------|---------|-------------|
| `NEVERC_HOOK_PRE_OPT` | IR | Prima dell'ottimizzazione LLVM |
| `NEVERC_HOOK_POST_OPT` | IR | Dopo l'ottimizzazione LLVM |
| `NEVERC_HOOK_PIPELINE_START` | IR | Inizio della pipeline |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | Fine della pipeline IR |
| `NEVERC_HOOK_SC_*` | IR/MIR/Binario | Flusso shellcode |
| `NEVERC_HOOK_LTO_*` | IR | Flusso LTO |
| `NEVERC_HOOK_LINK_*` | Linker | Flusso del linker |

## 6. Strutture dati

**Arena** (allocatore bump-pointer), **StrMap** (tabella hash con chiave stringa), **IntMap** (tabella hash con chiave intera), **StrBuilder** (costruzione incrementale di stringhe), **ValueSet** (insieme hash di valori).

## 7. Compatibilità versioni

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 8. Argomenti del plugin

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 9. Migliori pratiche

1. **Arena prima**: Usare `NEVERC_TRY_ARENA` per i dati temporanei.
2. **Protezione versione**: Avvolgere le nuove chiamate vtable con `NEVERC_API_FN`.
3. **Iterazione callback**: `ModuleForEachDefinedFunction` è più veloce delle macro.
4. **Nessuna dipendenza CRT**: Tutte le operazioni tramite vtable.
5. **Ritorno pulito**: Rilasciare tutte le risorse prima del ritorno del pass.

## 10. Contenuto del Plugin SDK

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h
└── examples/
    ├── CMakeLists.txt
    ├── ExamplePlugin.c
    ├── CrtShimPlugin.c
    └── BenchPlugin.c
```

## 11. Documentazione correlata

- [Interfaccia plugin Shellcode](../shellcode-compiler/plugin-interface/README.it.md) — Punti di estensione C++ in-tree per la pipeline shellcode.
