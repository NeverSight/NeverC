**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Documentation](../README.md)

# NeverC Built-in Runtime System

NeverC extends standard C with opt-in built-in runtimes that are embedded directly into the compiler binary as LLVM bitcode. When enabled via compiler flags, the corresponding runtime is merged into the user's IR at compile time — no external headers, libraries, or link-time dependencies required.

## Available Built-ins


| Built-in                                    | Flag                 | Default | Description                                                                                       |
| ------------------------------------------- | -------------------- | ------- | ------------------------------------------------------------------------------------------------- |
| [**`string`**](string/README.md) | `-fbuiltin-string`   | Off     | Value-semantic string type with dot-call methods, automatic memory management, and native UTF-8   |
| [**`mimalloc`**](mimalloc/README.md) | `-fbuiltin-mimalloc` | **On**  | High-performance memory allocator that transparently overrides `malloc`/`free`/`calloc`/`realloc` |
| [**`xorstr`**](xorstr/README.md) | `-fencrypt-call-strings` | Off  | Compile-time string encryption with stack-allocated XOR decryption and anti-signature algorithm   |

The `string` built-in requires explicit opt-in; `mimalloc` is enabled by default for all hosted builds (automatically suppressed in kernel, shellcode, and freestanding modes). They can be combined:

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Architecture Overview

All built-ins share the same four-layer architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                     Compiler Build Time                         │
│                                                                 │
│  Source code ──→ neverc -c -emit-llvm ──→ .bc ──→ bin2c.py     │
│                                            │                    │
│                                   Embedded in compiler binary   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     User Compilation Time                       │
│                                                                 │
│  user.c ──→ Lexer/Parser/Sema/CodeGen ──→ IR Module            │
│                                              │                  │
│                          PipelineStartEP: RuntimeLinkerPass      │
│                             │                                   │
│                             ├─ Parse embedded bitcode           │
│                             ├─ Merge into user Module           │
│                             ├─ Internalize helper symbols       │
│                             └─ Clean llvm.used                  │
│                                              │                  │
│                          Optimization pipeline                  │
│                                              │                  │
│                                           Output .o             │
└─────────────────────────────────────────────────────────────────┘
```

### Layer 1: Language Options & Driver Flags

Each built-in is controlled by a `LangOption` defined in `LangOptions.def`:

```cpp
LANGOPT(BuiltinString,      1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc,    1, 1, "inject mimalloc allocator override")
LANGOPT(EncryptCallStrings, 1, 0, "auto-encrypt string literals in call arguments")
VALUE_LANGOPT(EncryptCallStringsMaxLen, 32, 1024,
              "maximum string length for auto-encryption (0 = no limit)")
```

Driver flags (`-fbuiltin-<name>` / `-fno-builtin-<name>`, `-fencrypt-call-strings` / `-fno-encrypt-call-strings`, `-fencrypt-call-strings-max-len=N`) are declared in `Options.td.h` with corresponding `LANG_OPTION_WITH_MARSHALLING` entries. The driver passes them to the frontend via `addNeverCFeatureFlags()`.

### Layer 2: Foundation API

Each built-in has a header + implementation pair in `neverc/Foundation/Builtin/`:


| Built-in   | Header                                          | Implementation                                    |
| ---------- | ----------------------------------------------- | ------------------------------------------------- |
| `string`   | `BuiltinString.h`                               | `BuiltinString.cpp`                               |
| `mimalloc` | `BuiltinMimalloc.h`                             | `BuiltinMimalloc.cpp`                             |
| `xorstr`   | `lib/Headers/neverc/xorstr.h` *(user-facing)*   | `lib/Transforms/XorStr/EncryptCallStringsPass.cpp` |


The API provides `getEmbeddedBitcode()` to retrieve the precompiled LLVM bitcode blob, and `isSupported()` to check platform availability.

> **Note:** `xorstr` does not use the embedded-bitcode model. The explicit macro [`NC_XORSTR(s)` / `NEVERC_XORSTR(s)`](xorstr/README.md) is lowered by the Sema layer (handler `semaBuiltinNeverCXorstr` in `SemaChecking.cpp`), and the optional `-fencrypt-call-strings` auto-encryption is performed by the IR transform pass `EncryptCallStringsPass` (with a `XorStrCleanupPass` companion that zeroes plaintext stack buffers). See [xorstr documentation](xorstr/README.md) for the full layered design.

### Layer 3: CMake Bootstrap Infrastructure

Bitcode generation follows a two-stage bootstrap:

```bash
ninja neverc                         # Stage 1: empty bitcode placeholders
ninja neverc-bootstrap-string-bc     # Compile string runtime with neverc
ninja neverc-bootstrap-mimalloc-bc   # Compile `mimalloc` for all target OSes
ninja neverc                         # Stage 2: embed real bitcode
```

The initial build uses empty placeholder headers (`static const unsigned char kXxxBitcode[] = {0};`) so compilation succeeds without bitcode. The bootstrap targets then use the freshly built neverc to compile the runtime sources into LLVM bitcode, convert them via `bin2c.py` into C header arrays, and trigger a recompile to embed the real data.

### Layer 4: IR Merge Pass (PipelineStartEP)

Each built-in registers a `ModulePass` at `PipelineStartEP` in `BackendUtil.cpp`:

```cpp
if (LangOpts.BuiltinString) {
    PB.registerPipelineStartEPCallback([](ModulePassManager &MPM, OptimizationLevel) {
        MPM.addPass(StringRuntimeLinkerPass());
    });
}
if (LangOpts.BuiltinMimalloc) {
    PB.registerPipelineStartEPCallback([](ModulePassManager &MPM, OptimizationLevel) {
        MPM.addPass(MimallocRuntimeLinkerPass());
    });
}
```

The pass parses the embedded bitcode, merges it into the user module via `llvm::Linker::linkModules()`, internalizes helper symbols (keeping only the public API external), and cleans up `llvm.used` / `llvm.compiler.used`.

`xorstr`'s obfuscation passes register at the **post-pass** position (after all optimizations) so that the optimizer cannot constant-fold or undo the encryption:

```cpp
if (LangOpts.EncryptCallStrings) {
    MPM.addPass(neverc::xorstr::EncryptCallStringsPass(
                    LangOpts.EncryptCallStringsMaxLen));
    MPM.addPass(createModuleToFunctionPassAdaptor(
                    neverc::xorstr::XorStrCleanupPass()));
}
```

---

## Design Differences Between Built-ins


| Aspect                 | `string`                                    | `mimalloc`                             |
| ---------------------- | ------------------------------------------- | -------------------------------------- |
| **Merge strategy**     | On-demand (BFS call graph, prune unused)    | Whole-archive (all symbols preserved)  |
| **Platform bitcode**   | Single (arch-neutral)                       | Per-OS (Linux / Darwin / Windows)      |
| **Symbol handling**    | All internalized                            | Override entries keep external linkage |
| **Preprocessor macro** | *(none)*                                    | `__NEVERC_MIMALLOC__`                  |
| **Shellcode mode**     | Auto-enabled, arena rewrite                 | Suppressed (HeapArenaPass handles heap)|
| **Optimization level** | `-O0` (bitcode compile)                     | `-O2` (performance-critical allocator) |
| **DCE**                | Pre-merge prune + post-merge mark-and-sweep | No DCE (whole-archive semantics)       |


---

## Safety Interlocks

Certain compilation modes are incompatible with built-in runtimes. The driver automatically suppresses them:


| Condition          | Effect              | Reason                                 |
| ------------------ | ------------------- | -------------------------------------- |
| `-fno-builtin`     | Suppresses `mimalloc` | No CRT override scenario               |
| `-mkernel`         | Suppresses `mimalloc` | Kernel has no userspace heap           |
| `-fshellcode-mode` | Suppresses `mimalloc` | Replaced by HeapArenaPass (arena-based)|
| `-ffreestanding`   | Suppresses `mimalloc` | No libc to override                    |


The `string` built-in has its own suppression logic within the shellcode pipeline (arena rewrite replaces heap allocation).

### HeapArenaPass (Shellcode Heap Allocation)

When `-fshellcode-mode` is active, `mimalloc` is suppressed but `malloc`/`free`/`calloc`/`realloc` calls are automatically rewritten by `HeapArenaPass` (enabled by default). The pass uses a hybrid strategy:

- **Small allocations (≤ 64 KB)**: Served from a stack-resident arena shared with the `string` built-in runtime (bump allocator + free list reuse).
- **Large allocations (> 64 KB) or arena OOM**: Fall back to the OS allocator:
  - **Windows**: `malloc`/`free` resolved from `msvcrt.dll` via PEB walk (`-mshellcode-win-peb-import`).
  - **Linux / macOS / Android**: `mmap`/`munmap` inlined as native syscalls (`-mshellcode-syscall`).
  - **No import pass enabled**: Arena-only; OOM returns `NULL`.

Control via driver flags:

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass ON (default)
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass OFF (original behaviour)
```

---

## Preprocessor Macros

When a built-in is active, a corresponding preprocessor macro is defined:

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc is active — malloc/free are transparently overridden
#endif
```

This allows user code to conditionally compile based on which built-ins are enabled.

---

## File Structure

```
neverc/
├── include/neverc/Foundation/
│   ├── LangOpts/LangOptions.def          # LANGOPT declarations
│   └── Builtin/
│       ├── BuiltinString.h               # string API
│       ├── BuiltinMimalloc.h             # mimalloc API
│       ├── Builtins.def                  # __builtin_neverc_xorstr registration
│       └── ...
│
├── include/neverc/Invoke/
│   └── Options.td.h                      # Driver flag declarations
│
├── include/neverc/Transforms/XorStr/     # xorstr IR pass headers
│   ├── EncryptCallStringsPass.h
│   └── XorStrCleanupPass.h
│
├── lib/Foundation/
│   ├── CMakeLists.txt                    # Bootstrap targets for all built-ins
│   └── Builtin/
│       ├── BuiltinString.cpp             # string bitcode embedding
│       ├── BuiltinMimalloc.cpp           # mimalloc per-OS bitcode embedding
│       ├── bin2c.py                      # .bc → C header converter (shared)
│       ├── gen_string_runtime.py         # string source generator
│       └── gen_mimalloc_source.py        # mimalloc source generator
│
├── lib/Headers/neverc/
│   ├── xorstr.h                          # NC_XORSTR / NEVERC_XORSTR macros
│   └── xorstr_impl.inc                   # __neverc_xorstr_decrypt helper
│
├── lib/Analyze/Checking/
│   └── SemaChecking.cpp                  # semaBuiltinNeverCXorstr handler
│
├── lib/Transforms/XorStr/                # xorstr IR transform passes
│   ├── EncryptCallStringsPass.cpp        # auto-encrypts call-string literals
│   └── XorStrCleanupPass.cpp             # zeroes plaintext stack buffers
│
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp                   # PipelineStartEP + post-pass registration
│   ├── StringRuntimeLinker.{h,cpp}       # string IR merge pass
│   └── MimallocRuntimeLinker.{h,cpp}     # mimalloc IR merge pass
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                        # addNeverCFeatureFlags()
│
└── lib/Compiler/Preprocessor/
    └── InitPredefinedMacros.cpp          # __NEVERC_MIMALLOC__ macro
```

---

## Adding a New Built-in

To add a new built-in runtime (e.g., a custom allocator, crypto library, or platform abstraction):

1. **LangOption**: Add `LANGOPT(BuiltinFoo, 1, 0, "description")` in `LangOptions.def`
2. **Driver flags**: Add `-fbuiltin-foo` / `-fno-builtin-foo` OPTION + MARSHALLING entries in `Options.td.h`; wire through `addNeverCFeatureFlags()` in `NeverC.cpp`
3. **Foundation API**: Create `BuiltinFoo.h` (with `getEmbeddedBitcode()` + `isSupported()`) and `BuiltinFoo.cpp`
4. **Source generator**: Create `gen_foo_source.py` to produce a standalone C compilation unit
5. **CMake**: Add placeholder headers, bootstrap targets, and the source file to `nevercFoundation` in `Foundation/CMakeLists.txt`
6. **IR Pass**: Create `FooRuntimeLinkerPass` in `Emit/Backend/`, register at `PipelineStartEP` in `BackendUtil.cpp`
7. **Preprocessor**: Define `__NEVERC_FOO__` in `InitPredefinedMacros.cpp`
8. **Safety**: Add suppression logic in `addNeverCFeatureFlags()` for incompatible modes
9. **Tests**: Add GTest cases + C test source files
10. **Documentation**: Add `docs/builtins/foo/README.md` with i18n translations

