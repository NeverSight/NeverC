**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree Plugin API

NeverC provides a **pure C ABI** for out-of-tree pass plugins. A plugin is a shared library (`.dll` / `.so` / `.dylib`) that registers custom passes to run at designated points in the compilation pipeline. The plugin compiles against a **single header** (`NevercPluginAPI.h`) with **zero** LLVM or CRT dependencies — all functionality is routed through a host-provided vtable.

## 1. Quick Start

### Minimal Plugin

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

### Build

```bash
# Single-command build (any C compiler):
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

# Or with Make (using the shipped Makefile):
make -C /path/to/pluginsdk/examples
```

### Run

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    neverc (host)                          │
│                                                          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              NevercHostAPI vtable                    │ │
│  │  ModuleGetFirstFunction, BuilderCreate, DiagNoteF,  │ │
│  │  ArenaCreate, StrMapCreate, Sort, ...  (200+ fns)   │ │
│  └──────────────────────┬──────────────────────────────┘ │
│                         │ passed to plugin                │
│                         ▼                                │
│  ┌─────────────────────────────────────────────────────┐ │
│  │            Plugin (.dll / .so / .dylib)              │ │
│  │                                                     │ │
│  │  nevercGetPluginInfo() → NevercPluginInfo            │ │
│  │    ├─ RegisterPasses(API, Registrar)                 │ │
│  │    │    └─ API->RegisterModulePass(...)              │ │
│  │    │    └─ API->RegisterMachinePass(...)             │ │
│  │    │    └─ API->RegisterBinaryPass(...)              │ │
│  │    │    └─ API->RegisterLinkerPass(...)              │ │
│  │    └─ Destroy() (optional cleanup)                  │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

**Key properties:**

- **Single-header SDK**: Only `NevercPluginAPI.h` is needed to compile a plugin.
- **Zero dependencies**: No LLVM headers, no CRT linkage. All operations go through the vtable.
- **Pure C ABI**: Plugins can be written in C, C++, Zig, Rust (via FFI), or any language that can produce a shared library with C linkage.
- **Version-safe**: Use `NEVERC_API_FN(api, Field)` to check for optional vtable entries before calling them. Plugins compiled against older headers remain compatible with newer hosts.

## 3. Plugin Entry Point

Every plugin must export one function:

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

The returned `NevercPluginInfo` struct contains:

| Field | Type | Description |
|-------|------|-------------|
| `APIVersion` | `uint32_t` | Must be `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | Human-readable name |
| `PluginVersion` | `const char *` | Semantic version string |
| `RegisterPasses` | function pointer | Called once to register all passes |
| `Destroy` | function pointer | Optional cleanup; may be `NULL` |

## 4. Pass Types

### 4.1 Module Pass (IR)

Operates on the LLVM IR module. Can read and mutate IR.

```c
typedef int (*NevercModulePassFn)(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData);
```

Returns non-zero if the module was modified.

Register with:
```c
API->RegisterModulePass(Registrar, hook, callback, userData, "pass-name");
```

### 4.2 Machine Pass (MIR)

Operates on machine-level IR (after instruction selection).

```c
typedef int (*NevercMachinePassFn)(NevercMachineFuncRef MF,
                                   const NevercHostAPI *API,
                                   void *UserData);
```

Register with:
```c
API->RegisterMachinePass(Registrar, hook, callback, userData, "pass-name");
```

### 4.3 Binary Pass

Operates on raw bytes (shellcode extraction, binary patching).

```c
typedef int (*NevercBinaryPassFn)(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity,
                                  const NevercHostAPI *API,
                                  void *UserData);
```

Can resize the buffer via `API->BinaryResize()`.

Register with:
```c
API->RegisterBinaryPass(Registrar, hook, callback, userData, "pass-name");
```

### 4.4 Linker Pass

Operates at link time with access to symbols and sections.

```c
typedef int (*NevercLinkerPassFn)(const NevercHostAPI *API, void *UserData);
```

Register with:
```c
API->RegisterLinkerPass(Registrar, hook, callback, userData, "pass-name");
```

## 5. Hook Points

Hooks determine **when** a pass runs in the pipeline. Each hook fires exactly once at its designated moment.

### Normal Flow

| Hook | Level | Description |
|------|-------|-------------|
| `NEVERC_HOOK_PRE_OPT` | IR | Before LLVM optimization passes |
| `NEVERC_HOOK_POST_OPT` | IR | After LLVM optimization passes |
| `NEVERC_HOOK_PIPELINE_START` | IR | Very beginning of the pipeline |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | Very end of the IR pipeline |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | Before pre-emit machine passes |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | After all machine passes |

### Shellcode Flow

| Hook | Level | Description |
|------|-------|-------------|
| `NEVERC_HOOK_SC_BEFORE_PREP` | IR | Before shellcode IR preparation |
| `NEVERC_HOOK_SC_AFTER_PREP` | IR | After PIC preparation |
| `NEVERC_HOOK_SC_BEFORE_INLINING` | IR | Before always-inliner |
| `NEVERC_HOOK_SC_AFTER_INLINING` | IR | After inlining + stackify |
| `NEVERC_HOOK_SC_AFTER_STACKIFY` | IR | After stack transformation |
| `NEVERC_HOOK_SC_AFTER_FINAL_IR` | IR | Final IR before code generation |
| `NEVERC_HOOK_SC_BEFORE_PREEMIT` | MIR | Before MIR preparation |
| `NEVERC_HOOK_SC_AFTER_PREEMIT` | MIR | After MIR preparation |
| `NEVERC_HOOK_SC_AFTER_FINAL_MIR` | MIR | Final MIR before emission |
| `NEVERC_HOOK_SC_POST_EXTRACT` | Binary | After byte extraction |
| `NEVERC_HOOK_SC_POST_FINALIZE` | Binary | After all finalization |

### LTO Flow

| Hook | Level | Description |
|------|-------|-------------|
| `NEVERC_HOOK_LTO_PRE_OPT` | IR | Before LTO optimization |
| `NEVERC_HOOK_LTO_POST_OPT` | IR | After LTO optimization |

### Linker Flow

| Hook | Level | Description |
|------|-------|-------------|
| `NEVERC_HOOK_LINK_PRE_LAYOUT` | Linker | Before section layout |
| `NEVERC_HOOK_LINK_POST_LAYOUT` | Linker | After section layout |
| `NEVERC_HOOK_LINK_POST_EMIT` | Linker | After binary emission |

## 6. Opaque Handle Types

All IR/MIR objects are accessed through opaque handles. Handles are valid **only within the scope of the pass callback** that received them.

| Handle | Represents |
|--------|------------|
| `NevercModuleRef` | LLVM Module |
| `NevercValueRef` | LLVM Value (functions, instructions, globals) |
| `NevercBasicBlockRef` | LLVM BasicBlock |
| `NevercTypeRef` | LLVM Type |
| `NevercBuilderRef` | IR Builder (create with `BuilderCreate`, free with `BuilderDispose`) |
| `NevercContextRef` | LLVM Context |
| `NevercMetadataRef` | LLVM Metadata |
| `NevercNamedMDRef` | Named Metadata Node |
| `NevercComdatRef` | COMDAT Group |
| `NevercMachineFuncRef` | Machine function |
| `NevercMachineBBRef` | Machine basic block |
| `NevercMachineInstrRef` | Machine instruction |
| `NevercUseRef` | Use-def chain entry |
| `NevercLinkerSymbolRef` | Linker symbol |
| `NevercLinkerSectionRef` | Linker section |

## 7. Data Structures

The host provides several high-performance data structures through the vtable. All are opaque and must be freed before the pass returns.

### Arena (Bump-Pointer Allocator)

```c
NevercArenaRef A = NEVERC_TRY_ARENA(API);  // NULL on old hosts
// ... allocate with ArenaAllocArray, ArenaStrDup, etc.
API->ArenaDestroy(A);  // frees ALL arena allocations at once
```

Best for collect-then-process workflows. Replaces N individual `Free` calls with one `ArenaDestroy`.

### StrMap (String-Keyed Hash Map)

```c
NevercStrMapRef Map = NEVERC_STRMAP_NEW(API, 64);
API->StrMapPut(Map, "key", 42);
uint64_t Val;
if (API->StrMapGet(Map, "key", &Val)) { /* found */ }
API->StrMapDestroy(Map);
```

### IntMap (Integer-Keyed Hash Map)

```c
NevercIntMapRef Map = NEVERC_INTMAP_NEW(API, 128);
API->IntMapIncrement(Map, opcode, 1);
API->IntMapDestroy(Map);
```

### StrBuilder (Incremental String Construction)

```c
NevercStrBuilderRef SB = API->StrBuilderCreate();
API->StrBuilderAppendF(SB, "count=%u", 42);
NEVERC_STRBUILDER_DIAG(API, SB, DiagNote);  // emit + no alloc
API->StrBuilderDestroy(SB);
```

### ValueSet (Hash Set for Values)

```c
NevercValueSetRef Set = API->ValueSetCreate();
API->ValueSetInsert(Set, someValue);
if (API->ValueSetContains(Set, someValue)) { /* in set */ }
API->ValueSetDestroy(Set);
```

## 8. Version Compatibility

The vtable grows monotonically. Use these macros to check for newer entries:

```c
// Check if a field exists in the vtable (layout check only).
if (NEVERC_API_HAS(API, SomeNewFunction)) { ... }

// Check if a field exists AND is non-NULL (safe to call).
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

Plugins compiled against an older header remain compatible with newer hosts. Plugins calling newer APIs on older hosts must guard with `NEVERC_API_FN`.

## 9. Plugin Arguments

Pass arguments via the CLI:

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       -fplugin-pass-arg=max-fns=100 \
       input.c -o output.obj
```

Read them in the plugin:

```c
if (NEVERC_API_FN(API, PluginGetArg)) {
    const char *val = API->PluginGetArg("verbose");  // "1" or NULL
}

// Typed accessors (newer hosts):
int verbose   = API->PluginGetArgBool("verbose", 0);     // default 0
int64_t limit = API->PluginGetArgInt64("max-fns", -1);   // default -1
```

## 10. Lifetime Rules

| Resource | Lifetime | Cleanup |
|----------|----------|---------|
| Opaque handles (`NevercModuleRef`, `NevercValueRef`, ...) | Valid within the pass callback | Do not free |
| `const char*` from `ValueGetName`, `ModuleGetTargetTriple` | Valid while owning object exists | Do not free |
| `NevercBuilderRef` | Created by `BuilderCreate` | `BuilderDispose` before return |
| Heap strings from `StrDup`, `StrFormat`, `ValuePrintToString` | Caller-owned | `Free` |
| Arena allocations (`ArenaAlloc`, `ArenaStrDup`, ...) | Owned by the Arena | `ArenaDestroy` (bulk free) |
| `NevercDynArrayRef` / `NevercStrMapRef` / `NevercIntMapRef` / `NevercStrBuilderRef` | Created by `*Create` | Corresponding `*Destroy` |
| `NevercDomTreeRef` / `NevercLoopInfoRef` / `NevercSCEVInfoRef` / `NevercCallGraphRef` | Created by `FunctionBuild*` / `ModuleBuild*` | Corresponding `*Destroy` |
| Diagnostic strings from `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | Consumed by the call | Do not free |

## 11. Convenience Macros

| Macro | Purpose |
|-------|---------|
| `NEVERC_FOR_EACH_FUNCTION(api, m, var)` | Iterate all functions |
| `NEVERC_FOR_EACH_DEFINED_FUNCTION(api, m, var)` | Iterate defined (non-declaration) functions |
| `NEVERC_FOR_EACH_GLOBAL(api, m, var)` | Iterate global variables |
| `NEVERC_FOR_EACH_ALIAS(api, m, var)` | Iterate aliases |
| `NEVERC_FOR_EACH_BB(api, fn, var)` | Iterate basic blocks |
| `NEVERC_FOR_EACH_INST(api, bb, var)` | Iterate instructions |
| `NEVERC_FOR_EACH_USE(api, val, var)` | Traverse use-def chain |
| `NEVERC_FOR_EACH_MBB(api, mf, var)` | Iterate machine basic blocks |
| `NEVERC_FOR_EACH_MI(api, mbb, var)` | Iterate machine instructions |
| `NEVERC_FOR_EACH_SYMBOL(api, var)` | Iterate linker symbols |
| `NEVERC_FOR_EACH_SECTION(api, var)` | Iterate linker sections |
| `NEVERC_ALLOC_ARRAY(api, type, count)` | Typed heap allocation |
| `NEVERC_CALLOC_ARRAY(api, type, count)` | Typed zero-initialized heap allocation |
| `NEVERC_REALLOC_ARRAY(api, ptr, type, count)` | Typed heap reallocation |
| `NEVERC_COLLECT_FUNCTIONS(api, m, count)` | Batch-collect all functions into array |
| `NEVERC_COLLECT_DEFINED_FUNCTIONS(api, m, count)` | Batch-collect defined functions |
| `NEVERC_COLLECT_GLOBALS(api, m, count)` | Batch-collect global variables |
| `NEVERC_COLLECT_INSTRUCTIONS(api, m, count)` | Batch-collect all instructions |
| `NEVERC_COLLECT_OPCODES(api, m, count)` | Batch-collect opcode histogram |
| `NEVERC_TRY_ARENA(api)` | Create an arena (or `NULL` on old hosts) |
| `NEVERC_ARENA_ALLOC_ARRAY(api, arena, type, count)` | Typed arena allocation |
| `NEVERC_ARENA_CALLOC_ARRAY(api, arena, type, count)` | Typed zero-initialized arena allocation |
| `NEVERC_ARENA_COLLECT_*(api, arena, ...)` | Arena variants of batch-collect macros |
| `NEVERC_AUTO_COLLECT_*(api, arena, ...)` | Arena-preferred batch collect with heap fallback |
| `NEVERC_FREE_IF_HEAP(api, ptr, arena)` | Free only if not arena-owned |
| `NEVERC_ARENA_DESTROY(api, arena)` | Destroy arena if non-NULL |
| `NEVERC_STRBUILDER_DIAG(api, sb, diagFn)` | Emit StrBuilder content as diagnostic |
| `NEVERC_STRMAP_NEW(api, cap)` | Create StrMap with initial capacity |
| `NEVERC_INTMAP_NEW(api, cap)` | Create IntMap with initial capacity |
| `NEVERC_VALUESET_NEW(api, cap)` | Create ValueSet with initial capacity |
| `NEVERC_API_HAS(api, field)` | Check vtable field existence (layout only) |
| `NEVERC_API_FN(api, field)` | Check vtable entry existence + non-NULL |
| `NEVERC_HOOK_UD(hook)` | Cast hook enum to `void*` UserData |
| `NEVERC_HOOK_NAME(api, ud)` | Resolve hook name from UserData |
| `NEVERC_STR_OR(s, def)` | Return `s` if non-NULL and non-empty, else `def` |
| `NEVERC_NPOS` | Sentinel value for "not found" (`(uint64_t)-1`) |
| `NEVERC_MIN(a, b)` / `NEVERC_MAX(a, b)` | Compile-time min/max |
| `NEVERC_CLAMP(v, lo, hi)` | Clamp value to `[lo, hi]` range |

## 12. Best Practices

1. **Arena-first allocation**: Use `NEVERC_TRY_ARENA` + `NEVERC_ARENA_ALLOC_ARRAY` for temporary data. One `ArenaDestroy` replaces N `Free` calls.
2. **Version-guard new APIs**: Always wrap newer vtable calls with `NEVERC_API_FN`.
3. **Prefer callback iteration**: `ModuleForEachDefinedFunction` is faster than `NEVERC_FOR_EACH_DEFINED_FUNCTION` (one vtable call vs N).
4. **Tiered fallbacks**: Support multiple host versions by trying batch APIs first, then callback iteration, then per-element loops. See `ExamplePlugin.c` for the canonical pattern.
5. **No CRT dependency**: The vtable provides `Alloc`, `Free`, `MemSet`, `MemCopy`, `StrDup`, `StrFormat`, `Sort`, etc. Never call `malloc`, `printf`, or `qsort` directly — this ensures cross-DLL CRT safety.
6. **Clean return**: Free all Builders, destroy all data structures, and destroy all Arenas before the pass callback returns.

## 13. Plugin SDK Contents

The `pluginsdk/` directory distributed with NeverC contains:

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # The only header you need
└── examples/
    ├── Makefile             # Standalone build template
    ├── ExamplePlugin.c      # Comprehensive demo (IR + MIR + Binary + LTO + Linker)
    ├── CrtShimPlugin.c      # Zero-CRT-dependency proof of concept
    └── BenchPlugin.c        # HostAPI throughput micro-benchmarks
```

## 14. Related Documentation

- [Shellcode Plugin Interface](../shellcode-compiler/plugin-interface/README.md) — In-tree C++ extension points for the shellcode pipeline (separate from this out-of-tree C API).
