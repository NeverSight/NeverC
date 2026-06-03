**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Custom Calling Conventions

NeverC supports **data-driven custom calling conventions** — you can assign arbitrary physical registers to any function's arguments and return values, entirely from an out-of-tree plugin or source-level attributes, without modifying the compiler itself or any TableGen definitions.

## Overview

Traditional LLVM calling conventions are baked into the backend via `.td` / `.inc` files. Adding or modifying one requires editing compiler sources and re-running TableGen. NeverC replaces this with a **runtime data-driven** approach:

- A **register assignment spec** (a plain string) is attached to each function as a string attribute.
- The backend reads this spec and assigns parameters / return values to the specified physical registers.
- The spec can come from an **external plugin** (IR pass), **source-level attributes** (`__attribute__` / `__declspec`), or both.

This means calling conventions go from "compile-time hardcoded in the backend" to "runtime data-driven by external policy".

## Spec Format

The spec is a semicolon-delimited string. Each segment has a key and a comma-separated list of register names (case-insensitive, whitespace-tolerant):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segment | Aliases | Meaning |
|---|---|---|
| `args` | | **Positional mode**: each token is a register name or `stack`/`mem`, assigned to arguments by index |
| `gpr` | `arg_gpr` | **Pool mode**: integer/pointer argument registers, used in order; overflow spills to the stack |
| `xmm` | `arg_xmm` | **Pool mode**: float/vector argument registers |
| `fpr` | `arg_fpr` | AArch64 alias for `xmm` |
| `ret_gpr` | `ret` | Integer/pointer return registers |
| `ret_xmm` | | Float/vector return registers |
| `ret_fpr` | | AArch64 alias for `ret_xmm` |
| `csr` | | Custom callee-saved register set (default: standard ABI set) |

### Two Argument Modes

**Pool mode** (`gpr:` / `xmm:`): Integer arguments take registers from the `gpr` pool in order; float arguments take from `xmm`. When the pool is exhausted, remaining arguments spill to the stack.

**Positional mode** (`args:`): Argument *i* uses the *i*-th token. Each token is either a register name or `stack` / `mem` to force that argument onto the stack:

```
args:rcx,stack,r8;ret:rax   # arg0→rcx, arg1→stack, arg2→r8, return→rax
```

When `args` is present, it takes precedence over `gpr` / `xmm`. Type mismatches (e.g., an XMM name for an integer argument), out-of-range indices, and already-allocated registers all fall back to a stack slot.

### Supported Architectures

| Architecture | GPR names | SIMD names | Width selection |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32-bit sub-register, i64→64-bit |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vec→`q` |

### Constraints

- **Callee-saved**: Defaults to the standard ABI set. Use `csr:r12,r13` to declare a custom set (the function will only save/restore those registers). Supported on both x86-64 and AArch64.
- **Reserved registers**: The stack pointer (`rsp` / `sp`) and AArch64 `x29`/`x30` (FP/LR) are never assignable as argument/return registers — a spec naming them simply skips them.
- **csr conflicts**: If a register appears in both `csr` and an argument/return list, the bridge emits a warning (the callee would save/restore it, clobbering its value-passing role).
- **Variadic functions**: Not supported — the compiler emits a clear error instead of silently mis-passing arguments.
- **Indirect calls**: Function-pointer calls cannot carry a custom convention. The plugin warns when a custom-CC function has its address taken; indirect calls fall back to the standard CC.
- **Tail calls**: Automatically disabled for custom-CC functions (conservative safety).
- **AArch64 / GlobalISel**: The custom convention is implemented in the SelectionDAG path. Functions that define or call a custom-CC function automatically fall back from GlobalISel to SelectionDAG, so the behavior is identical regardless of the default ISel.

## Usage

### 1. Plugin-Driven (Recommended)

The reference plugin `CustomCallConvPlugin.c` ships under `pluginsdk/examples/`.

**Build the plugin:**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # or .so / .dll
```

**Attribute mode** (default) — only functions with `custom_attr` source annotations are affected:

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
```

**Global mode** — apply a spec to every defined function (requires explicit `cc-all=1`):

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**Filter by name prefix:**

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccprefix=secret_ \
       -fplugin-pass-arg=ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**Diversify** — each function gets a different layout (anti-reverse-engineering):

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccshuffle=1 \
       input.c -o output.o
```

### 2. Source-Level Attributes

Annotate functions directly in C source using the `custom_attr` attribute (GNU and Microsoft syntax):

```c
// GNU syntax
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Microsoft syntax
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` produces a clean function string attribute (`"key"="value"`) with **no** warnings and **no** `llvm.global.annotations`. It is a **general-purpose** mechanism — any key/value pair works, not just calling conventions. IR and MIR passes read it with `F.getFnAttribute("key")`.

### 3. Combined

Source attributes and plugin arguments work together. A function with a `custom_attr` is processed by the plugin's attribute-mode path; `cc-all=1` covers the rest. Each function is processed at most once.

## LTO Support

The plugin registers at both `NEVERC_HOOK_POST_OPT` (normal compilation) and `NEVERC_HOOK_LTO_POST_OPT` (after the LTO optimization pipeline). This ensures custom conventions are applied even when Link-Time Optimization merges translation units — enabling cross-TU call-site synchronization with full module visibility.

## Plugin API

The plugin uses a single API entry point added in API version 2:

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

This sets `CallingConv::NeverC_Custom` (CC 1000) on the function, writes the spec as a `"neverc-callconv"` string attribute, and **synchronizes all direct call sites** (each call instruction also gets the CC and attribute). Passing `NULL` or `""` clears the custom convention.

## Testing

GoogleTest suite in `tests/neverc/CustomCallConvTests.cpp` (22 tests, all PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

Coverage:

| Category | Tests |
|---|---|
| x86-64 pool/positional/stack/spill/i64/sret/byval/fallback | 9 |
| AArch64 GPR/FPR/stack/`csr`/mixed-spec cross-call | 5 |
| Frontend `custom_attr` (GNU / `__declspec` / end-to-end) | 3 |
| Hardening (`csr` / vararg / indirect / rsp rejection / csr-conflict warning) | 5 |

## Architecture

```
Source Code                Plugin
     │                       │
     ▼                       ▼
custom_attr("neverc-callconv", spec)
     │                       │
     └───────┬───────────────┘
             ▼
    "neverc-callconv" = spec   (function string attribute)
             │
             ▼
    ┌─────────────────────────────────┐
    │  Backend Executor (per-target)  │
    │  CC_X86_NeverC / RetCC_X86_..  │
    │  CC_AArch64_NeverC / RetCC_..  │
    │                                 │
    │  Reads spec → assigns regs     │
    │  Caller injects callee spec    │
    │  Tail calls disabled           │
    └─────────────────────────────────┘
             │
             ▼
    Machine code with custom register layout
```

The backend executor is a **one-time implementation** — all policy decisions live in the plugin. Adding new conventions never requires rebuilding NeverC.
