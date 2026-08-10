**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Built-in Runtime System](../README.md)

# Compile-Time String Encryption (`xorstr`)

## Overview

NeverC provides two-layer compile-time string encryption for C code, designed for security-sensitive scenarios where plaintext strings (API names, registry paths, debug messages) must not be visible in the compiled binary.

- **Layer 1 — Explicit macro**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` for precise, per-string control
- **Layer 2 — Automatic IR pass**: `-fencrypt-call-strings` to auto-encrypt all string arguments in function calls

Both layers use stack-allocated buffers (no heap allocation), per-instance key streams, and volatile cleanup before function return. At a native machine-code boundary, explicit `NC_XORSTR` decoder calls are rekeyed and expanded into their individual call sites; the final object does not retain a shared decoder function.

---

## Quick Start

### Layer 1: Explicit Macro

```c
#include <neverc/xorstr/xorstr.h>

// String is encrypted at compile time, decrypted on stack at runtime
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### Layer 2: Automatic Encryption

```bash
neverc -fencrypt-call-strings main.c -o main
```

All string literal arguments in function calls are automatically encrypted — no source code changes required.

---

## Layer 1: `NC_XORSTR` / `NEVERC_XORSTR` Macro

### Usage

```c
#include <neverc/xorstr/xorstr.h>

const char *api = NC_XORSTR("GetProcAddress");    // short form
const char *api = NEVERC_XORSTR("GetProcAddress"); // long form (alias)
```

The macro accepts any string literal kind:

| Literal | Example | Support |
|---------|---------|---------|
| Ordinary | `NC_XORSTR("hello")` | Supported |
| UTF-8 | `NC_XORSTR(u8"hello 世界")` | Supported (folded to UTF-8) |
| Wide | `NC_XORSTR(L"hello")` | Supported (folded to UTF-8) |
| UTF-16 | `NC_XORSTR(u"hello")` | Supported (folded to UTF-8) |
| UTF-32 | `NC_XORSTR(U"hello")` | Supported (folded to UTF-8) |

Non-string-literal arguments produce a compile-time error:

```c
const char *s = get_string();
NC_XORSTR(s);  // error: expression is not a string literal
```

### How It Works

1. **Sema**: `__builtin_neverc_xorstr("hello")` encrypts the bytes with a per-instance key. Seed `0` obtains fresh operating-system entropy in the compiler; `-fstring-encrypt-key=` selects deterministic 64-bit output.
2. **Intermediate IR / LTO input**: the builtin becomes a call to an opaque, non-specializable decoder. Keeping this boundary intact prevents ordinary and LTO optimization from folding the plaintext back into IR.
3. **Final machine-code boundary**: the finalizer decrypts and rekeys the compiler-side ciphertext, chooses a per-call loop shape, and emits the decoder directly at every call site. It then removes the decoder, its helper graph, ABI anchor, route state, and semantic names.
4. **Cleanup**: volatile zeroing is inserted both before optimization/provider handoff and again at the final tail. The second pass is idempotent and repairs placement after CFG changes.

### Decoder Diversity

The finalizer can express each byte combination in several equivalent forms. One form is:

```
dec(a, b) = a + b − 2 × (a & b)
```

The state schedule, constants, ciphertext, and expression choices vary with the seed and call site. Volatile state/ciphertext loads resist constant folding, and `nooutline` prevents the machine outliner from reconstructing a shared decoder after IR finalization. This removes a stable, standalone routine for IDA to identify or emulate; it does not claim that plaintext needed by a running program is impossible to recover with dynamic instrumentation.

---

## Layer 2: `-fencrypt-call-strings` (Automatic)

### Usage

```bash
neverc -fencrypt-call-strings main.c -o main
```

The transform runs before IPO, after ordinary optimization, and once more after every ordinary or plugin-provided late IR phase. LTO applies the same mandatory seal after provider and pre-codegen hooks. This protects literal provenance before it can be propagated away and catches literals introduced later.

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `-fencrypt-call-strings` | Enable automatic encryption | Off |
| `-fno-encrypt-call-strings` | Disable (overrides `-fencrypt-call-strings`) | — |
| `-fencrypt-call-strings-max-len=N` | Skip strings longer than N bytes | 1024 |

### What Gets Encrypted

The pass processes direct and indirect `CallBase` arguments that resolve to compiler-owned, private `unnamed_addr` literal storage with `i8`, `i16`, or `i32` elements. Constant GEPs, dynamic GEPs, casts, freezes, selects, PHIs, and promotable local pointer slots are rebuilt around the decrypted buffer. Base/interior pointer relationships and pointer identity for one source literal within a function invocation are preserved.

### What Gets Skipped

| Condition | Reason |
|-----------|--------|
| LLVM intrinsics (`llvm.memcpy`, `llvm.dbg.*`, etc.) | Compiler primitives, not user code |
| Inline asm | Its operand and control-flow contracts cannot be rewritten safely |
| Strings exceeding `-fencrypt-call-strings-max-len` | Avoid excessive stack usage for large strings |
| Externally visible or user-defined constant arrays | Their symbol, storage, and pointer identity are part of the program ABI |
| A protected literal passed by `musttail` | Compilation fails closed because a stack buffer cannot outlive a valid `musttail` transfer |

### IR Transformation

For each eligible string argument, the pass:

1. Creates an anonymous private encrypted copy of the global variable
2. Allocates one stack buffer per source literal and function invocation (`%xorstr.buf`)
3. Inserts one stateful decrypt loop in the function entry path
4. Rebuilds the argument pointer flow over the stack buffer
5. Marks the alloca with `!neverc.xorstr` metadata for the cleanup pass
6. Removes the original global variable if it has no remaining uses

---

## Stack Cleanup (`XorStrCleanupPass`)

After decryption, the plaintext resides on the stack. `XorStrCleanupPass` (a FunctionPass) ensures it does not survive any normal or exceptional exit:

1. Finds protected allocas from metadata and any explicit decoder calls that still remain
2. Removes lifetime markers so optimizers cannot reuse the plaintext storage before its wipe
3. Before every reachable `ret`, `resume`, unwind-to-caller `cleanupret`, or unmatched `catchswitch` unwind, inserts `llvm.memset(buf, 0, complete_size, volatile=true)`; a direct `catchswitch` unwind is routed through a cleanup funclet
4. Rejects non-stack or incompletely traceable decoder outputs and dynamic, scalable, overflowing, or non-dominating protected storage instead of guessing an unsafe wipe
5. The `volatile` flag prevents the optimizer from eliminating the zeroing as a dead store

The cleanup is installed early, repeated after late passes, and run once more by finalization. Existing wipes are recognized and moved rather than duplicated.

---

## Architecture

```
┌─── Layer 1: NC_XORSTR (Explicit) ─────────────────────────────┐
│                                                                 │
│  NC_XORSTR("GetPid")                                          │
│       │                                                        │
│       ▼ Sema: compile-time encryption                         │
│       │                                                        │
│  __neverc_xorstr_decrypt(enc, len, key)                        │
│       │                                                        │
│       ▼ final boundary: per-call rekey + inline loop           │
│       │                                                        │
│  returns const char* (stack-allocated, auto-zeroed)            │
└────────────────────────────────────────────────────────────────┘

┌─── Layer 2: -fencrypt-call-strings (Automatic) ───────────────┐
│                                                                 │
│  call @GetProcAddress(ptr @.str)                               │
│       │                                                        │
│       ▼ EncryptCallStringsPass (pre-IPO + mandatory late seal) │
│       │                                                        │
│       ├─ Create anonymous private encrypted global             │
│       ├─ Emit %xorstr.buf = alloca                             │
│       ├─ Emit decrypt loop (anti-XOR signature)                │
│       └─ Replace @.str operand with %xorstr.buf                │
│                                                                 │
│       ▼ XorStrCleanupPass                                      │
│       │                                                        │
│  volatile memset(%xorstr.buf, 0, size) before every ret        │
└────────────────────────────────────────────────────────────────┘
```

---

## Comparison with `.encrypt()` String Method

| Aspect | `NC_XORSTR()` | `.encrypt()` |
|--------|---------------|--------------|
| **Availability** | Plain C (via header) | NeverC syntax extension only |
| **Memory** | Stack (`alloca`) | Heap (`NEVERC_STRING_ALLOC`) |
| **Return type** | `const char*` | `string` (value type) |
| **Lifetime** | Current function scope | Managed by string runtime |
| **Cleanup** | `memset` before `ret` | Garbage collected by `string` runtime |
| **Use case** | Win32 API calls, FFI | General string manipulation |

Both mechanisms share the 64-bit seed control and fresh-default-entropy policy, but use representations appropriate to their different runtime lifetimes.

---

## File Structure

```
neverc/
├── lib/Headers/neverc/
│   └── xorstr/
│       ├── xorstr.h                 # NC_XORSTR / NEVERC_XORSTR macros
│       └── xorstr_impl.inc          # opaque intermediate decoder
│
├── include/neverc/
│   ├── Foundation/
│   │   ├── Builtin/Builtins.def    # __builtin_neverc_xorstr registration
│   │   └── LangOpts/LangOptions.def # EncryptCallStrings option
│   ├── Invoke/Options.td.h          # CLI flag declarations + marshalling
│   └── Transforms/XorStr/
│       ├── EncryptCallStringsPass.h  # IR pass header
│       └── XorStrCleanupPass.h      # Cleanup pass header
│
├── lib/Analyze/Checking/
│   └── SemaCheckingBuiltinNeverC.cpp # Sema handler for the builtin
│
├── lib/Transforms/XorStr/
│   ├── EncryptCallStringsPass.cpp  # Automatic string encryption IR pass
│   ├── XorStrCleanupPass.cpp       # Stack zeroing pass
│   └── CMakeLists.txt
│
├── lib/Emit/Backend/
│   └── BackendUtil.cpp             # frontend boundary pipeline registration
│
└── lib/Invoke/ToolChains/
    └── NeverC.cpp                  # Driver flag forwarding
```

---

## Compiler Flags Reference

| Flag | Description |
|------|-------------|
| `-fencrypt-call-strings` | Enable automatic string encryption for call arguments |
| `-fno-encrypt-call-strings` | Disable automatic encryption |
| `-fencrypt-call-strings-max-len=N` | Maximum byte length for auto-encryption (default: 1024, 0 = unlimited) |
| `-fstring-encrypt-key=0xHEX` | Override the full 64-bit seed (shared with `.encrypt()`); default `0` uses fresh entropy |

## Output Boundaries and Reproducibility

- `-fno-lto` finalizes at frontend native code generation.
- Auto-LTO and full LTO retain the opaque explicit decoder in pre-link bitcode, then rekey and expand it after whole-program and plugin IR optimization.
- Provider-replaced pipelines and late plugin passes are followed by mandatory encryption, cleanup, and finalization tails.
- With the default seed, independent native builds differ. Whole-link and partition LTO caches are bypassed whenever replay could suppress either a fresh explicit-xorstr final rekey or automatic encryption of a literal exposed only after LTO.
- A nonzero `-fstring-encrypt-key` is intentionally deterministic and remains cacheable; identical input plus the same full 64-bit seed produces identical protected code.
- `-emit-llvm` and pre-link bitcode are intermediate artifacts, so they intentionally retain the opaque decoder ABI. The no-shared-decoder guarantee applies to successful final machine-code outputs.
