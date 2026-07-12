**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Built-in Runtime System](../README.md)

# Compile-Time String Hashing (`strhash`)

## Overview

NeverC provides compile-time and runtime string hashing for plain C, designed for fast string dispatch without storing plaintext comparison tables in the binary — for example matching API names, item IDs, or command tokens via integer hash equality.

- **Layer 1 — Explicit compile-time macro**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` folds to an integer constant during Sema
- **Layer 2 — Runtime + optional IR fold**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`, with `-fstrhash-fold` to constant-fold runtime calls whose arguments are string literals

Both layers share the same algorithm selected by `-fstrhash-algo` (default: FNV-1a 64-bit), so compile-time and runtime hashes always match.

---

## Quick Start

### Layer 1: Compile-Time Macro

```c
#include <neverc/strhash/strhash.h>

static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");

int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

### Layer 2: Auto Dispatch + Fold

```c
#include <neverc/strhash/strhash.h>

// Variable → runtime call; literal + -fstrhash-fold → constant
uint64_t h = NC_STRHASH_AUTO(name);
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## Layer 1: `NC_STRHASH` / `NEVERC_STRHASH` Macro

### Usage

```c
#include <neverc/strhash/strhash.h>

uint64_t h = NC_STRHASH("hello");     // short form
uint64_t h = NEVERC_STRHASH("hello"); // long form (alias)

// Usable in static initializers and _Static_assert
static const uint64_t kHello = NC_STRHASH("hello");
_Static_assert(NC_STRHASH("a") != NC_STRHASH("b"), "hashes differ");
```

The macro accepts any string literal kind:

| Literal | Example | Support |
|---------|---------|---------|
| Ordinary | `NC_STRHASH("hello")` | Supported |
| UTF-8 | `NC_STRHASH(u8"hello 世界")` | Supported (folded to UTF-8) |
| Wide | `NC_STRHASH(L"hello")` | Supported (folded to UTF-8) |
| UTF-16 | `NC_STRHASH(u"hello")` | Supported (folded to UTF-8) |
| UTF-32 | `NC_STRHASH(U"hello")` | Supported (folded to UTF-8) |

Non-string-literal arguments produce a compile-time error:

```c
const char *s = get_string();
NC_STRHASH(s);  // error: expression is not a string literal
```

For variables, use `NC_STRHASH_AUTO(s)` or `neverc_strhash_rt(s, len)`.

### How It Works

1. **Sema (compile time)**: `__builtin_neverc_strhash("hello")` hashes the string bytes with the algorithm from `-fstrhash-algo`
2. **Rewrite**: The builtin call is replaced with an `IntegerLiteral` (`unsigned long long`) — no runtime call remains
3. **Result**: A pure compile-time constant usable in initializers, `switch`-style `if` chains, and `_Static_assert`

---

## Hash Algorithms

| Algo ID | Flag value | Function | Width | Default |
|---------|------------|----------|-------|---------|
| 1 | `fnv32a` | FNV-1a 32-bit (zero-extended to `uint64_t`) | 32 → 64 | |
| 2 | `fnv64a` | FNV-1a 64-bit | 64 | **Yes** |
| 3 | `xxhash64` | XXHash64 (seed `0`) | 64 | |

```bash
neverc -fstrhash-algo=fnv32a main.c
neverc -fstrhash-algo=fnv64a main.c
neverc -fstrhash-algo=xxhash64 main.c
```

The selected algorithm is also exposed as the preprocessor macro `__NEVERC_STRHASH_ALGO__` (`1` / `2` / `3`), which drives the runtime dispatch in `strhash_impl.inc`.

---

## Runtime Hash: `neverc_strhash_rt`

```c
uint64_t neverc_strhash_rt(const void *data, size_t len);
```

Inline helper that calls the NeverC std hash implementation matching the selected algo:

| `__NEVERC_STRHASH_ALGO__` | Runtime callee |
|---------------------------|----------------|
| 1 (`fnv32a`) | `neverc_fnv_sum32a` |
| 2 (`fnv64a`) | `neverc_fnv_sum64a` |
| 3 (`xxhash64`) | `neverc_xxhash64(..., 0)` |

These symbols live in NeverC std (`std/src/hash/`). Link the std hash objects (or the full NeverC std) when using runtime hashing.

### Typical Pattern: Compile-Time Table + Runtime Lookup

```c
static const uint64_t valuable_items[] = {
    NC_STRHASH("apple"),
    NC_STRHASH("banana"),
    NC_STRHASH("grape"),
};

int is_valuable(const char *name) {
    uint64_t h = neverc_strhash_rt(name, strlen(name));
    for (size_t i = 0; i < sizeof(valuable_items) / sizeof(valuable_items[0]); i++)
        if (h == valuable_items[i])
            return 1;
    return 0;
}
```

---

## `NC_STRHASH_AUTO` / `NEVERC_STRHASH_AUTO`

```c
#define NC_STRHASH_AUTO(s) neverc_strhash_rt((s), __builtin_strlen(s))
```

Accepts both literals and variables:

| Argument | Without `-fstrhash-fold` | With `-fstrhash-fold` |
|----------|--------------------------|------------------------|
| String literal | Runtime call | Folded to integer constant by `StrHashFoldPass` |
| Variable / pointer | Runtime call | Runtime call |

```c
uint64_t auto_literal(void) {
    return NC_STRHASH_AUTO("hello");  // foldable with -fstrhash-fold
}

int match_item(const char *name) {
    return NC_STRHASH_AUTO(name) == NC_STRHASH("apple");
}
```

---

## Layer 2: `-fstrhash-fold` (IR Constant Folding)

### Usage

```bash
neverc -fstrhash-fold main.c -o main
```

`StrHashFoldPass` runs in the **post-pass** phase (after the optimization pipeline) when `LangOpts.StrHashFold` is set. It scans for calls to `neverc_fnv_sum32a`, `neverc_fnv_sum64a`, and `neverc_xxhash64` whose data argument is a constant string global and whose length (and XXHash seed) are constant, then replaces the call with a constant integer.

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `-fstrhash-fold` | Enable IR folding of constant-arg runtime hash calls | Off |
| `-fno-strhash-fold` | Disable folding | — |
| `-fstrhash-algo=<algo>` | Select algorithm for Sema builtin + fold pass | `fnv64a` |

### What Gets Folded

| Condition | Required |
|-----------|----------|
| Callee is `neverc_fnv_sum32a` / `neverc_fnv_sum64a` / `neverc_xxhash64` | Yes |
| Data arg is a constant string global | Yes |
| Length arg is a constant integer ≤ string length | Yes |
| For XXHash64: seed is constant `0` | Yes |

Non-constant data or length arguments are left as runtime calls.

---

## Custom Runtime Hash

Override only the **runtime** path by defining `NC_STRHASH_HASH_FN` before including the header:

```c
#define NC_STRHASH_HASH_FN(data, len) my_hash(data, len)
#include <neverc/strhash/strhash.h>

// neverc_strhash_rt / NC_STRHASH_AUTO use my_hash
// NC_STRHASH() still uses the builtin / -fstrhash-algo
```

---

## Architecture

```
┌─── Layer 1: NC_STRHASH (Explicit) ────────────────────────────┐
│                                                                 │
│  NC_STRHASH("GetPid")                                           │
│       │                                                         │
│       ▼ Sema: computeStrHash(bytes, algo)                       │
│       │                                                         │
│  IntegerLiteral (uint64_t) — no runtime call                    │
└─────────────────────────────────────────────────────────────────┘

┌─── Runtime + AUTO ──────────────────────────────────────────────┐
│                                                                 │
│  neverc_strhash_rt(ptr, len) / NC_STRHASH_AUTO(s)               │
│       │                                                         │
│       ▼ always_inline dispatch via __NEVERC_STRHASH_ALGO__      │
│       │                                                         │
│  neverc_fnv_sum32a / neverc_fnv_sum64a / neverc_xxhash64        │
└─────────────────────────────────────────────────────────────────┘

┌─── Layer 2: -fstrhash-fold (Optional) ──────────────────────────┐
│                                                                 │
│  call @neverc_fnv_sum64a(ptr @.str, i64 5)                      │
│       │                                                         │
│       ▼ StrHashFoldPass                                         │
│       │                                                         │
│  i64 <constant hash>                                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## File Structure

```
neverc/
├── lib/Headers/neverc/strhash/
│   ├── strhash.h                    # NC_STRHASH / NC_STRHASH_AUTO macros
│   └── strhash_impl.inc             # neverc_strhash_rt inline dispatch
│
├── include/neverc/
│   ├── Foundation/
│   │   ├── Builtin/Builtins.def     # __builtin_neverc_strhash registration
│   │   └── LangOpts/LangOptions.def # StrHashAlgo / StrHashFold options
│   ├── Invoke/Options.td.h          # CLI flags + marshalling
│   └── Transforms/StrHash/
│       ├── StrHashCompute.h         # Shared FNV / XXHash compute
│       └── StrHashFoldPass.h        # IR fold pass header
│
├── lib/Analyze/Checking/
│   └── SemaCheckingBuiltinNeverC.cpp # semaBuiltinNeverCStrHash
│
├── lib/Transforms/StrHash/
│   ├── StrHashFoldPass.cpp          # Constant-arg hash call folding
│   └── CMakeLists.txt
│
├── lib/Emit/Backend/
│   └── BackendUtil.cpp              # Fold pass registration
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                   # Driver flag forwarding
│
└── std/src/hash/                    # Runtime hash implementations
    ├── fnv/fnv.c                    # neverc_fnv_sum32a / sum64a
    └── xxhash/xxhash.c              # neverc_xxhash64
```

---

## Compiler Flags Reference

| Flag | Description |
|------|-------------|
| `-fstrhash-algo=fnv32a` | Use FNV-1a 32-bit for Sema + fold + runtime dispatch |
| `-fstrhash-algo=fnv64a` | Use FNV-1a 64-bit (default) |
| `-fstrhash-algo=xxhash64` | Use XXHash64 with seed 0 |
| `-fstrhash-fold` | Fold runtime hash calls with constant string args to integers |
| `-fno-strhash-fold` | Disable IR folding |
