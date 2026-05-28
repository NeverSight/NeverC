**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Built-in Runtime System](../README.md)

# Built-in `mimalloc` Allocator

## Overview

NeverC can embed [mimalloc](https://github.com/microsoft/mimalloc) — Microsoft's high-performance general-purpose allocator — directly into compiled binaries via LLVM bitcode merging. When enabled, `malloc`, `free`, `calloc`, and `realloc` are transparently replaced by mimalloc's implementations at compile time, requiring no external library linkage.

**Enabling:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

The resulting binary uses mimalloc for all heap allocations without any source code changes.

---

## Why mimalloc?

mimalloc consistently outperforms system allocators across workloads:

- **2× faster** than glibc malloc on allocation-heavy benchmarks
- **Reduced fragmentation** via size-class segregated free lists
- **Thread-local heaps** eliminate lock contention in multi-threaded programs
- **Secure mode** with guard pages and randomized allocation

By embedding mimalloc at the IR level, NeverC provides these benefits as a single compiler flag — no build system changes, no library management, no runtime dependencies.

---

## Usage

### Basic

```bash
neverc -fbuiltin-mimalloc hello.c -o hello
```

### Combined with Built-in String

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

When both are enabled, the `string` runtime's `__builtin_malloc`/`__builtin_free` calls are served by `mimalloc`.

### Disabling

```bash
neverc -fno-builtin-mimalloc main.c -o main    # explicit disable
```

### Compile-Time Detection

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef __NEVERC_MIMALLOC__
    printf("Using mimalloc allocator\n");
#else
    printf("Using system allocator\n");
#endif

    void *p = malloc(1024);
    free(p);
    return 0;
}
```

---

## Platform Support

| Platform | Triple | Status |
|----------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Supported |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Supported |
| Android | `aarch64-linux-android` | Supported (OSType = Linux) |
| macOS x86_64 | `x86_64-apple-macosx` | Supported |
| macOS AArch64 | `arm64-apple-macosx` | Supported |
| iOS | `arm64-apple-ios` | Supported |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Supported |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Supported |

Unsupported platforms (FreeBSD, etc.) silently skip `mimalloc` injection — no error is emitted, the binary simply uses the system allocator.

### OS-Specific Override Mechanisms

| OS | Override Strategy |
|----|-------------------|
| **Linux** | `MI_OVERRIDE` replaces glibc `malloc`/`free` via symbol interposition |
| **macOS/iOS** | `malloc_zone` registration overrides the default zone |
| **Windows** | CRT `malloc`/`free` replacement via `MI_OVERRIDE` |

---

## Automatic Suppression

The driver automatically suppresses `-fbuiltin-mimalloc` in these scenarios:

| Flag / Mode | Reason |
|-------------|--------|
| `-fno-builtin` | No CRT function override makes sense |
| `-mkernel` | Kernel mode has no userspace heap; drivers use `ExAllocatePool2` |
| `-fshellcode-mode` | Replaced by HeapArenaPass (arena + OS fallback) |
| `-ffreestanding` | No libc to override |

If both `-fbuiltin-mimalloc` and a suppression flag are present, the suppression wins silently (no warning emitted). The intent is fail-safe: the user's explicit mode choice takes priority.

---

## Architecture

### IR Bitcode Embedding

Unlike traditional library linking, mimalloc is embedded as LLVM bitcode inside the compiler binary itself. At user compile time, a Module Pass merges the bitcode into the user's IR before the optimization pipeline runs.

```
┌─── Compiler Build Time ────────────────────────────────────────┐
│                                                                 │
│  mimalloc source (FetchContent)                                │
│       │                                                        │
│       ▼ gen_mimalloc_source.py                                 │
│       │                                                        │
│  MimallocRuntime_{linux,darwin,win}.c                          │
│       │                                                        │
│       ▼ neverc -c -emit-llvm -O2 --target=<triple>            │
│       │                                                        │
│  neverc_mimalloc_{linux,darwin,win}.bc                         │
│       │                                                        │
│       ▼ bin2c.py                                               │
│       │                                                        │
│  BuiltinMimallocBitcode_{linux,darwin,win}.h                   │
│       │                                                        │
│  Embedded in neverc binary                                     │
└────────────────────────────────────────────────────────────────┘

┌─── User Compilation Time ──────────────────────────────────────┐
│                                                                 │
│  user.c ──→ CodeGen ──→ IR Module                              │
│                            │                                    │
│              MimallocRuntimeLinkerPass (PipelineStartEP)        │
│                            │                                    │
│               1. Extract OS from Module triple                  │
│               2. Parse embedded bitcode for that OS             │
│               3. Strip host target-cpu/target-features          │
│               4. Whole-archive merge (linkModules)              │
│               5. Internalize helpers, keep override entries     │
│               6. Clean llvm.used / llvm.compiler.used           │
│                            │                                    │
│              Optimization pipeline ──→ Output .o                │
└────────────────────────────────────────────────────────────────┘
```

### Per-OS Bitcode

mimalloc uses OS-specific system calls (`mmap` on Linux, `vm_allocate` on macOS, `VirtualAlloc` on Windows), so the bitcode is compiled separately for each target OS. At merge time, the pass selects the correct blob based on the user module's target triple.

### Whole-Archive Semantics

Unlike the `string` built-in (which prunes unused functions via call-graph BFS), `mimalloc` uses **whole-archive** merge — all functions are linked in. This is necessary because:

1. mimalloc's override mechanism requires the complete set of `malloc`/`free`/`calloc`/`realloc` entry points
2. Internal helper functions are tightly interconnected
3. The system linker resolves standard allocation calls to mimalloc's implementations

### Symbol Internalization

After merging, the pass processes symbols:

- **Override entry points** (`malloc`, `free`, `calloc`, `realloc`, `posix_memalign`, `aligned_alloc`, `memalign`, `valloc`, `pvalloc`, `reallocf`, `malloc_size`, `malloc_usable_size`, `malloc_good_size`, `_malloc_default_zone`, `mi_*`) → **keep external linkage**
- **Internal helper functions** → **internalize** (set to `internal` linkage)
- **Global variables** → **internalize**

This prevents symbol conflicts with other libraries while ensuring the system linker sees the override entry points.

---

## Bootstrap Build Process

mimalloc bitcode is generated via a two-stage bootstrap:

```bash
# Stage 1: Build neverc with empty bitcode placeholders
ninja neverc

# Stage 2: Use neverc to compile mimalloc into per-OS bitcode
ninja neverc-bootstrap-mimalloc-bc

# Stage 3: Rebuild neverc with real embedded bitcode
ninja neverc
```

### Bootstrap Targets

| Target | Description |
|--------|-------------|
| `neverc-bootstrap-mimalloc-bc` | Umbrella target — builds all per-OS bitcode |
| `neverc-bootstrap-mimalloc-bc-linux` | Linux bitcode (`x86_64-unknown-linux-gnu`) |
| `neverc-bootstrap-mimalloc-bc-darwin` | macOS bitcode (`arm64-apple-macosx11.0`) |
| `neverc-bootstrap-mimalloc-bc-win` | Windows bitcode (`x86_64-pc-windows-msvc`) |

### Compilation Flags for Bitcode

```bash
neverc -c -emit-llvm -O2 \
    -fno-builtin-mimalloc -fno-lto \
    -ffreestanding -std=gnu11 \
    --target=<triple> \
    -I <mimalloc>/include -I <mimalloc>/src \
    -w \
    MimallocRuntime_<os>.c -o neverc_mimalloc_<os>.bc
```

Key choices:
- **`-O2`**: mimalloc is a performance-critical allocator; bitcode benefits from optimization
- **`-fno-builtin-mimalloc`**: prevent recursive self-injection
- **`-ffreestanding`**: avoid implicit libc assumptions during bitcode compilation
- **`-w`**: suppress mimalloc source warnings

### Source Generator

`gen_mimalloc_source.py` produces a per-OS wrapper C file that includes mimalloc's `static.c` (the single-file compilation unit) with appropriate defines:

```c
#define MI_OVERRIDE        1    // Replace malloc/free
#define MI_BUILD_SHARED    0    // Static build
#define MI_MALLOC_OVERRIDE 1    // Enable override
#define MI_SKIP_COLLECT_ON_EXIT 1  // Skip cleanup at process exit

// OS-specific defines
#define _GNU_SOURCE              // Linux
#define _DEFAULT_SOURCE          // Linux
// or
#define _WIN32                   // Windows
#define WIN32                    // Windows

#include "static.c"
```

---

## Interaction with Other Features

### LTO Mode

mimalloc works correctly with LTO. The bitcode is merged at `PipelineStartEP` (before the optimization pipeline), so LTO's cross-module optimization and DCE apply normally. Internal mimalloc helpers that the linker proves dead are eliminated.

### Address Sanitizer

NeverC currently rejects sanitizer flags entirely. If ASan support is added in the future, mimalloc will need to be automatically suppressed (ASan replaces `malloc`/`free` with its own instrumented versions).

### Custom Allocators

If user code defines its own `malloc`/`free` (e.g., a Windows kernel driver with `ExAllocatePool2`-backed allocation), the `Linker::Flags::OverrideFromSrc` flag ensures mimalloc's definitions take precedence. Users in such scenarios should use `-fno-builtin-mimalloc`.

---

## File Structure

```
neverc/
├── include/neverc/Foundation/Builtin/
│   └── BuiltinMimalloc.h              # API: isSupported() + getEmbeddedBitcode()
│
├── lib/Foundation/
│   ├── CMakeLists.txt                  # Per-OS placeholder headers + bootstrap targets
│   └── Builtin/
│       ├── BuiltinMimalloc.cpp         # Per-OS bitcode dispatch (Linux/Darwin/iOS/Win32)
│       ├── gen_mimalloc_source.py      # Generate single-file wrapper for each OS
│       └── bin2c.py                    # .bc → C header (shared with `string`)
│
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.h         # Pass declaration
│   ├── MimallocRuntimeLinker.cpp       # IR merge pass (whole-archive + internalize)
│   └── BackendUtil.cpp                 # PipelineStartEP registration
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                      # Safety interlocks in addNeverCFeatureFlags()
│
├── lib/Compiler/Preprocessor/
│   └── InitPredefinedMacros.cpp        # __NEVERC_MIMALLOC__ macro definition
│
└── include/neverc/
    ├── Foundation/LangOpts/
    │   └── LangOptions.def             # LANGOPT(BuiltinMimalloc, ...)
    └── Invoke/
        └── Options.td.h                # -fbuiltin-mimalloc / -fno-builtin-mimalloc
```

---

## Compiler Flags Reference

| Flag | Description |
|------|-------------|
| `-fbuiltin-mimalloc` | Enable `mimalloc` override injection (on by default for hosted builds) |
| `-fno-builtin-mimalloc` | Explicitly disable `mimalloc` injection |

### Preprocessor Macro

| Macro | Value | When Defined |
|-------|-------|-------------|
| `__NEVERC_MIMALLOC__` | `1` | When `-fbuiltin-mimalloc` is active |
