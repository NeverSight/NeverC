**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Built-in Runtime System](../README.md)

# Compile-Time String Encryption (`xorstr`)

## Overview

NeverC provides two-layer compile-time string encryption for C code, designed for security-sensitive scenarios where plaintext strings (API names, registry paths, debug messages) must not be visible in the compiled binary.

- **Layer 1 — Explicit macro**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` for precise, per-string control
- **Layer 2 — Automatic IR pass**: `-fencrypt-call-strings` to auto-encrypt all string arguments in function calls

Both layers use stack-allocated buffers (no heap allocation), XOR-based decryption without using the XOR instruction (anti-signature), and volatile `memset` cleanup before function return.

---

## Quick Start

### Layer 1: Explicit Macro

```c
#include <neverc/xorstr.h>

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
#include <neverc/xorstr.h>

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

1. **Sema (compile time)**: `__builtin_neverc_xorstr("hello")` encrypts the string bytes using per-instance XOR keys derived from compilation time + a counter
2. **Rewrite**: The builtin call is replaced with `__neverc_xorstr_decrypt(encrypted_literal, len, key)`
3. **Runtime**: The `always_inline` helper allocates a stack buffer via `__builtin_alloca`, decrypts byte-by-byte using an anti-XOR-signature algorithm (`a + b - 2*(a & b)`), and returns `const char*`
4. **Cleanup**: `XorStrCleanupPass` inserts `volatile memset(buf, 0, size)` before every `ret` instruction to erase plaintext from the stack

### Anti-Signature Decryption

The decrypt operation avoids the XOR instruction entirely. Instead of `a ^ b`, it computes the mathematically equivalent:

```
dec(a, b) = a + b − 2 × (a & b)
```

Combined with `volatile` qualifiers on key variables, this prevents:
- Pattern matching on XOR decrypt loops
- Constant folding by the optimizer (key stays opaque)
- YARA/signature-based detection of XOR decryption routines

---

## Layer 2: `-fencrypt-call-strings` (Automatic)

### Usage

```bash
neverc -fencrypt-call-strings main.c -o main
```

This IR pass runs after all optimizations (Post pass phase) and automatically encrypts every string literal argument in non-intrinsic function calls.

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `-fencrypt-call-strings` | Enable automatic encryption | Off |
| `-fno-encrypt-call-strings` | Disable (overrides `-fencrypt-call-strings`) | — |
| `-fencrypt-call-strings-max-len=N` | Skip strings longer than N bytes | 1024 |

### What Gets Encrypted

The pass processes all `CallInst` / `InvokeInst` arguments that reference:
- `ConstantDataArray` global variables with `i8` (char), `i16` (wchar_t/char16_t), or `i32` (char32_t) element types

### What Gets Skipped

| Condition | Reason |
|-----------|--------|
| LLVM intrinsics (`llvm.memcpy`, `llvm.dbg.*`, etc.) | Compiler primitives, not user code |
| Indirect calls / inline asm | No `getCalledFunction()` — cannot determine callee |
| EH pad blocks (`catchpad`, `cleanuppad`) | Exception handling blocks must not be restructured |
| Strings exceeding `-fencrypt-call-strings-max-len` | Avoid excessive stack usage for large strings |
| Strings already marked with `!neverc.xorstr` metadata | Prevent double encryption |

### IR Transformation

For each eligible string argument, the pass:

1. Creates an encrypted copy of the global variable (`@.str.xorstr.enc`)
2. Allocates a stack buffer in the function's entry block (`%xorstr.buf`)
3. Inserts a decrypt loop before the call (using the anti-XOR-signature algorithm)
4. Replaces the original string operand with the stack buffer pointer
5. Marks the alloca with `!neverc.xorstr` metadata for the cleanup pass
6. Removes the original global variable if it has no remaining uses

---

## Stack Cleanup (`XorStrCleanupPass`)

After decryption, the plaintext resides on the stack. `XorStrCleanupPass` (a FunctionPass) ensures it doesn't survive past the function return:

1. Scans for all `AllocaInst` with `!neverc.xorstr` metadata
2. Before every `ReturnInst`, inserts `llvm.memset(buf, 0, size, volatile=true)`
3. The `volatile` flag prevents the optimizer from eliminating the zeroing as dead store

This pass runs immediately after `EncryptCallStringsPass` in the Post pass phase.

---

## Architecture

```
┌─── Layer 1: NC_XORSTR (Explicit) ─────────────────────────────┐
│                                                                 │
│  NC_XORSTR("GetPid")                                          │
│       │                                                        │
│       ▼ Sema: compile-time XOR encrypt                         │
│       │                                                        │
│  __neverc_xorstr_decrypt(enc, len, key)                        │
│       │                                                        │
│       ▼ always_inline: alloca + anti-XOR decrypt loop          │
│       │                                                        │
│  returns const char* (stack-allocated, auto-zeroed)            │
└────────────────────────────────────────────────────────────────┘

┌─── Layer 2: -fencrypt-call-strings (Automatic) ───────────────┐
│                                                                 │
│  call @GetProcAddress(ptr @.str)                               │
│       │                                                        │
│       ▼ EncryptCallStringsPass (Post pass, after optimization) │
│       │                                                        │
│       ├─ Create @.str.xorstr.enc (encrypted global)            │
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

Both mechanisms share the same compile-time XOR encryption logic and anti-signature decryption algorithm.

---

## File Structure

```
neverc/
├── lib/Headers/neverc/
│   ├── xorstr.h                     # NC_XORSTR / NEVERC_XORSTR macros
│   └── xorstr_impl.inc             # __neverc_xorstr_decrypt inline helper
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
│   └── SemaChecking.cpp            # Sema handler for __builtin_neverc_xorstr
│
├── lib/Transforms/XorStr/
│   ├── EncryptCallStringsPass.cpp  # Automatic string encryption IR pass
│   ├── XorStrCleanupPass.cpp       # Stack zeroing pass
│   └── CMakeLists.txt
│
├── lib/Emit/Backend/
│   └── BackendUtil.cpp             # Post pass registration
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
| `-fstring-encrypt-key=0xHEX` | Override the XOR key seed (shared with `.encrypt()`, default: time-derived) |
