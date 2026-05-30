**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# API de plugins out-of-tree de NeverC

NeverC proporciona una **ABI de C puro** para plugins de pases out-of-tree. Un plugin es una biblioteca compartida (`.dll` / `.so` / `.dylib`) que registra pases personalizados en puntos designados del pipeline de compilación. El plugin se compila contra un **único encabezado** (`NevercPluginAPI.h`) con **cero** dependencias de LLVM o CRT — toda la funcionalidad se enruta a través de una vtable proporcionada por el host.

## 1. Inicio rápido

### Plugin mínimo

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

### Compilación

```bash
# Recomendado: compilar con neverc (ABI consistente, multiplataforma):
neverc --target=x86_64-pc-windows-msvc -shared -o MyPlugin.dll MyPlugin.c \
       -I/path/to/pluginsdk/include

# O con Make (usa neverc por defecto):
make -C /path/to/pluginsdk/examples
```

> **Nota:** Se recomienda encarecidamente usar **neverc** para consistencia ABI y soporte multiplataforma.

### Ejecución

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. Arquitectura

- **SDK de un solo encabezado**: Solo se necesita `NevercPluginAPI.h`.
- **Cero dependencias**: Sin encabezados LLVM, sin enlace CRT. Todas las operaciones a través de la vtable.
- **ABI de C puro**: Los plugins se pueden escribir en C, C++, Zig, Rust (FFI) o cualquier lenguaje que pueda producir una biblioteca compartida con enlace C.
- **Seguro en versiones**: Use `NEVERC_API_FN(api, Field)` para verificar entradas opcionales de la vtable.

## 3. Punto de entrada

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| Campo | Tipo | Descripción |
|-------|------|-------------|
| `APIVersion` | `uint32_t` | Debe ser `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | Nombre legible |
| `PluginVersion` | `const char *` | Cadena de versión semántica |
| `RegisterPasses` | puntero a función | Se llama una vez para registrar todos los pases |
| `Destroy` | puntero a función | Limpieza opcional, puede ser `NULL` |

## 4. Tipos de pases

- **Module Pass (IR)**: Opera sobre el módulo LLVM IR.
- **Machine Pass (MIR)**: Opera sobre IR a nivel de máquina.
- **Binary Pass**: Opera sobre bytes sin procesar.
- **Linker Pass**: Opera en tiempo de enlace.

## 5. Puntos de enganche

| Hook | Nivel | Descripción |
|------|-------|-------------|
| `NEVERC_HOOK_PRE_OPT` | IR | Antes de la optimización LLVM |
| `NEVERC_HOOK_POST_OPT` | IR | Después de la optimización LLVM |
| `NEVERC_HOOK_PIPELINE_START` | IR | Inicio del pipeline |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | Final del pipeline IR |
| `NEVERC_HOOK_SC_*` | IR/MIR/Binario | Flujo shellcode |
| `NEVERC_HOOK_LTO_*` | IR | Flujo LTO |
| `NEVERC_HOOK_LINK_*` | Enlazador | Flujo del enlazador |

## 6. Tipos de handles opacos

Todos los objetos IR/MIR se acceden a través de handles opacos: `NevercModuleRef`, `NevercValueRef`, `NevercBasicBlockRef`, `NevercTypeRef`, `NevercBuilderRef`, `NevercContextRef`, `NevercMetadataRef`, `NevercNamedMDRef`, `NevercComdatRef`, `NevercMachineFuncRef`, `NevercMachineBBRef`, `NevercMachineInstrRef`, `NevercUseRef`, `NevercLinkerSymbolRef`, `NevercLinkerSectionRef`. Los handles son válidos **solo dentro del ámbito del callback de paso** que los recibió.

## 7. Estructuras de datos

**Arena** (asignador bump-pointer), **StrMap** (tabla hash con clave de cadena), **IntMap** (tabla hash con clave entera), **StrBuilder** (construcción incremental de cadenas), **ValueSet** (conjunto hash de valores).

## 8. Compatibilidad de versiones

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. Argumentos del plugin

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. Mejores prácticas

1. **Arena primero**: Use `NEVERC_TRY_ARENA` para datos temporales.
2. **Protección de versión**: Envuelva nuevas llamadas vtable con `NEVERC_API_FN`.
3. **Iteración por callback**: `ModuleForEachDefinedFunction` es más rápido que las macros.
4. **Sin dependencia CRT**: Todas las operaciones a través de la vtable.
5. **Retorno limpio**: Libere todos los recursos antes del retorno del pase.

## 11. Contenido del Plugin SDK

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h
└── examples/
    ├── Makefile
    ├── ExamplePlugin.c
    ├── CrtShimPlugin.c
    └── BenchPlugin.c
```

## 12. Documentación relacionada
