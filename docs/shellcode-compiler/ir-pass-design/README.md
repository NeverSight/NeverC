**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# IR Pass Design — Principles, Pipeline, and Before/After Examples

> This document explains the **why** behind each pass in the shellcode compilation pipeline. Implementation details are in the `.cpp` source files with English comments.

---

## 0. Core Idea

Shellcode's goal in one sentence: **eliminate everything in the `.o` that would become a relocation, leaving only a pure instruction stream that can be `mmap(RWX)` + `memcpy` + `blr`'d directly.**

We do not want to leak this constraint to users — users should write normal C, and the pipeline handles relocation-producing IR constructs internally.

This leads to the following pass division:

| Pass | What it does | Runs at |
|------|-------------|---------|
| ZeroRelocPass (Prep) | Unify linkage / force always_inline / reject hard blockers | PipelineStart |
| IndirectBrPass | Computed goto `indirectbr` → `switch` | PipelineStart |
| MemIntrinPass | `@llvm.mem*` + explicit mem*/str*/abs calls → internal byte-loop helpers | PipelineStart |
| StringRuntimePass | Built-in `string` type runtime → stack-allocated arena variants | PipelineStart |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → arena alloc + OS fallback for large allocations | PipelineStart |
| CompilerRtPass | `__udivti3` family + IR-level i128 div/rem → internal 128-bit long-division | PipelineStart |
| SyscallStubPass | libc extern → target OS kernel trap wrapper, table-driven via `TargetDesc` | PipelineStart |
| WinPEBImportPass | Win32 extern → PEB module walk + PE export resolver + encrypted address cache | PipelineStart |
| KernelImportPass | Ring-0 extern → resolver-backed indirect call + encrypted address cache (kernel mode only) | PipelineStart |
| Data2TextPass (Phase 1) | Constant GV → immediates / stack chunk stores | PipelineStart |
| *(LLVM standard optimizations)* | AlwaysInliner / SROA / InstCombine / SLP / ... | O-level |
| Data2TextPass (Phase 2) | Split SROA-residual `store <N x i8> <const>`, consume late constants | OptimizerLast |
| ZeroRelocPass (Stackify) | Mutable global → entry's alloca + final validation | OptimizerLast |
| AllBlrPass (optional) | Direct call → indirect call | OptimizerLast |
| MIRPrepPass | MIR catch-all: strip CFI/EH/XRay/SEH/etc. pseudos | Before addPreEmitPass |
| ShellcodeExtractor | Scan `.o` for final audit + output flat `.bin` | Post-processing |

**Why does a pass run twice?** Because IR shape changes significantly before and after AlwaysInliner / SROA / vectorizer, each phase must clean the IR to a "no relocation-producing uses" state.

---

## 1. ZeroRelocPass

### 1.1 Prep Stage — Linkage Unification

**Goal**: make the module contain only one user-visible symbol (entry), with everything else `internal` + `alwaysinline`. This eliminates cross-function direct calls, preventing `ARM64_RELOC_BRANCH26` residuals on AArch64.

**Actions**:
- All non-entry functions → `internal` linkage + `alwaysinline` attribute
- Reject hard blockers: `__attribute__((constructor))`, `external_weak`, `extern_weak`
- `_Thread_local` → silently demoted to static (shellcode runs on the host's calling thread)
- Clear `@llvm.used` / `@llvm.compiler.used` (prevent GVs from being kept alive)

### 1.2 Stackify Stage — Global Variable Elimination

**Goal**: move all remaining mutable globals into the entry function's stack frame.

**Actions**:
- For each mutable GV: create an `alloca` in entry, initialize from GV's initializer, replace all uses
- Call `GV->removeDeadConstantUsers()` before `use_empty()` check (handles orphan ConstantExpr GEPs)
- Final validation: reject any remaining user GVs with actionable diagnostics

### 1.3 `placeEntryFirst`

After all rewrites, moves the entry function to the front of `Module::getFunctionList()`, ensuring it appears at offset 0 in the output `.bin`. This handles recursive functions and `noinline` helpers that cannot be inlined.

---

## 2. IndirectBrPass

**Problem**: GCC computed-goto (`goto *labels[op]`) produces `indirectbr` instructions referencing a `BlockAddress` table, which the backend places in a data section with `ARM64_RELOC_UNSIGNED` relocations.

**Solution**: pattern-match the `indirectbr` dispatch (including multi-dispatch-site phi convergence under `-O0`), rewrite to an index-based `switch`. Combined with `-fno-jump-tables`, the switch becomes a compare-branch ladder with zero relocations and zero data sections.

---

## 3. SyscallStubPass

**Problem**: extern libc calls (`write`, `read`, `exit`, `mmap`, ...) produce unresolvable relocations.

**Solution**: for each extern matching the syscall table, generate an `always_inline` wrapper that emits the platform's inline assembly trap:
- Darwin arm64: `svc #0x80` (x16 = nr)
- Darwin x86_64: `syscall` with BSD class mask `0x2000000`
- Linux arm64: `svc #0` (x8 = nr)
- Linux x86_64: `syscall` (rax = nr)

All templates and register constraints come from `TargetDesc` — the pass has zero arch-specific branches.

**POSIX compat layer** (arm64 Linux/Android): classic calls like `open` that lack a direct syscall number on arm64 are auto-translated to `*at` equivalents (e.g., `open(path, flags)` → `openat(AT_FDCWD, path, flags, 0)`).

**K&R auto-fix**: when 0-parameter implicit declarations are detected, the pass substitutes canonical POSIX signatures from a 50+ function table.

---

## 4. WinPEBImportPass

**Problem**: Windows has no stable syscall ABI; Win32 APIs must be resolved via PEB walk.

**Solution**: for each extern matching the `WinImportTables` whitelist (~210 APIs across 6 DLLs):
1. Generate an `always_inline` wrapper with fast/slow path and encrypted address cache
2. **Fast path** (cache hit): `atomic_load(cache) → decrypt → indirect call` (~10 instructions)
3. **Slow path** (cache miss): full PEB → Ldr → InMemoryOrderModuleList walk with ROR-13 hash matching → `encrypt → cmpxchg` → call
4. Only one inline asm instruction per platform: `movq %gs:0x60, $0` (x86_64) / `ldr $0, [x18, #0x60]` (arm64)
5. All list traversal, PE header parsing, and hash comparison is pure LLVM IR
6. Resolved addresses are XOR-encrypted with `PEB_BASE ^ COMPILE_TIME_SEED` before caching (anti-memory-scan)
7. Cache slots are per-(DLL, API) global variables in `.text` section; thread-safe via `cmpxchg` (lock-free)

**Windows POSIX compat**: `WinPEBImportPass` bridges 13 POSIX function groups (write→WriteFile, mmap→VirtualAlloc, exit→ExitProcess, etc.) via `Win32PosixCompat.def`, enabling the same C source to compile across all 8 triples without `#ifdef`.

### 4.1 Address Cache Encryption

Resolved API addresses are encrypted before being stored in cache global variables. This prevents simple memory scanning from discovering resolved function pointers in the shellcode's address space.

The encryption infrastructure is implemented in `PtrCacheHelpers.h` and shared by both `WinPEBImportPass` (user mode) and `KernelImportPass` (ring-0).

**Three pluggable helper functions** (all generated as `internal alwaysinline`):

| Function | Signature | Purpose |
|----------|-----------|---------|
| `__sc_derive_key` | `() → i64` | Derive the encryption key at runtime |
| `__sc_ptr_encrypt` | `(ptr) → i64` | Encrypt a function pointer for cache storage |
| `__sc_ptr_decrypt` | `(i64) → ptr` | Decrypt a cached value back to a function pointer |

**Default implementation** — XOR-free arithmetic decomposition:

The default uses the mathematical identity `a ^ b = (a + b) - 2*(a & b)` instead of a literal `xor` instruction. All intermediate values pass through `volatile` stack slots, preventing LLVM's InstCombine from recognizing and re-optimizing the pattern back to `xor`. This makes the encryption invisible to simple disassembly pattern matching.

```
__sc_derive_key():
  [PEB mode]   key = (PEB_int + seed) - (PEB_int & seed) - (seed & PEB_int)
  [Seed mode]  key = COMPILE_TIME_SEED

__sc_ptr_encrypt(ptr):
  key = __sc_derive_key()
  plain = PtrToInt(ptr)
  return (plain + key) - (plain & key) - (key & plain)

__sc_ptr_decrypt(enc_i64):
  key = __sc_derive_key()
  return IntToPtr((enc + key) - (enc & key) - (key & enc))
```

The same XOR-free decomposition is also applied to the built-in string encryption's `NEVERC_STRING_DECRYPT_BYTE` macro via the `__neverc_xfree_dec` helper function. This can be further strengthened with MBA (Mixed Boolean-Arithmetic) obfuscation passes applied independently.

### 4.2 Key Derivation Modes

| Mode | When used | Key source | Security property |
|------|-----------|------------|------------------|
| `PEB` | Windows user-mode | `PEB_base ^ seed` | PEB base varies per process (ASLR), making the key process-unique |
| `SeedOnly` | Kernel mode, non-Windows | Pure compile-time seed constant | Static key; user should override `__sc_derive_key` for dynamic keys (e.g. from KPCR) |

`COMPILE_TIME_SEED` is a random `uint64_t` generated once per compilation unit. PEB access uses a single inline asm instruction per platform: `movq %gs:0x60, $0` (x86_64) / `ldr $0, [x18, #0x60]` (arm64).

The `__sc_derive_key` function has `MemoryEffects::none()` since the PEB read is modeled as side-effect-free (`hasSideEffects=false`). This allows LLVM to CSE (common subexpression eliminate) redundant `__sc_derive_key()` calls within the same function.

### 4.3 Cache Slot Layout

Each (DLL, API) pair gets its own global variable:

- **Name**: `@__sc_cache_<dll_prefix>_<api_name>` (e.g. `@__sc_cache_kernel32_VirtualAlloc`)
- **Type**: `i64`, initialized to `0`
- **Linkage**: `internal` (not visible to linker)
- **Section**: `.text` (colocated with code to avoid data section creation)
- **Alignment**: 8 bytes
- **Semantics**: `0` = unresolved (cache miss); non-zero = XOR-encrypted function pointer

### 4.4 Fast/Slow Path Pattern

Every API callsite is replaced by an `always_inline` wrapper with this structure:

```
entry:
  %cached = atomic load monotonic @__sc_cache_<dll>_<api>
  br (%cached == 0) → slow [weight 1], fast [weight 2000]

fast:                                    ; ~10 instructions
  %fn.ptr = call __sc_ptr_decrypt(%cached)
  br → merge

slow:                                    ; full PEB walk / resolver call
  %raw.fn = <resolve API address>
  br (%raw.fn == null) → merge [weight 1], store [weight 2000]

store:
  %enc.fn = call __sc_ptr_encrypt(%raw.fn)
  cmpxchg weak @__sc_cache_<dll>_<api>, 0, %enc.fn  (release / monotonic)
  br → merge

merge:
  %fn = phi [fast: %fn.ptr, slow: %raw.fn, store: %raw.fn]
  call %fn(original args...)
```

**Thread safety**: `cmpxchg weak` ensures only the first resolver wins; subsequent threads that resolve concurrently will simply discard their result (weak CAS failure is harmless since the value is the same). The `monotonic` load on the fast path and `release` store on the slow path guarantee correct visibility ordering.

**Branch weights**: `br_weights(1, 2000)` hint the backend to lay out the fast path fall-through for optimal branch prediction.

### 4.5 Overriding the Default Encryption

Users can provide their own implementations by defining functions with matching names in their source code. The pass uses a "get or create" pattern — `M.getFunction("__sc_ptr_encrypt")` is checked first; if a user definition already exists, the default is not generated.

Example — replacing XOR with a rotate+XOR scheme:

```c
#include <stdint.h>

static inline __attribute__((always_inline))
uint64_t __sc_derive_key(void) {
    uint64_t peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    return (peb ^ 0xDEAD1337CAFE4242ULL);
}

static inline __attribute__((always_inline))
uint64_t __sc_ptr_encrypt(void *ptr) {
    uint64_t k = __sc_derive_key();
    uint64_t v = (uint64_t)ptr;
    v ^= k;
    v = (v << 13) | (v >> 51);  // rotate left 13
    return v;
}

static inline __attribute__((always_inline))
void *__sc_ptr_decrypt(uint64_t enc) {
    uint64_t k = __sc_derive_key();
    enc = (enc >> 13) | (enc << 51);  // rotate right 13
    enc ^= k;
    return (void *)enc;
}
```

**Constraints for custom implementations**:
- Must be `always_inline` (the pass expects inlining to eliminate the call overhead)
- `__sc_ptr_encrypt` and `__sc_ptr_decrypt` must be mathematical inverses: `decrypt(encrypt(ptr)) == ptr`
- Should have no side effects beyond the encryption itself (to allow CSE optimizations)
- Must not call any external functions (would re-introduce unresolvable relocations)

---

## 5. MemIntrinPass

**Problem**: `memcpy`, `memset`, `strlen`, `strcpy`, etc. — both explicit calls and implicit `@llvm.mem*` intrinsics — produce unresolvable extern calls.

**Solution**: generate `internal alwaysinline` byte-loop helpers for each function:
- `__sc_memcpy`: forward byte copy loop
- `__sc_memset`: constant byte store loop
- `__sc_memmove`: runtime direction check, forward/backward paths
- `__sc_memcmp`: loop until first mismatch, return `(int)a[i] - (int)b[i]`
- `__sc_strlen`, `__sc_strcpy`, `__sc_strcmp`, `__sc_strchr`, `__sc_strrchr`, etc.
- `__sc_abs` / `__sc_labs` / `__sc_llabs`: `select (x slt 0) (sub 0 x) x`

Per-callsite ABI reconciliation handles Windows LLP64 `size_t == i32` vs POSIX `i64` differences.

---

## 6. CompilerRtPass

**Problem**: `__int128` division/modulo produces calls to `__udivti3` / `__divti3` / `__umodti3` / `__modti3` / `__udivmodti4`.

**Solution**: generate `internal alwaysinline` shift-subtract long-division helpers. These only use constant i128 shifts and i64 variable shifts, avoiding `__ashlti3` / `__lshrti3` recursion.

---

## 7. Data2TextPass

**Problem**: constant data (string literals, const arrays, floating-point constants) in `.data` / `.rodata` / `.cstring` / `__literal*` sections produce relocations.

### Phase 1 (PipelineStart)

**Actions**:
- Scalar constant GVs → `ConstantInt` (direct substitution)
- Constant arrays → stack chunk stores (recursive `writeInto` + `getOrMaterialize` handles nested structs, function pointer tables, string pointer tables, self-referential structs)
- `ConstantFP` → volatile-loaded integer bit pattern + bitcast (prevents backend literal pool spilling)
- Function pointer entries → store function pointers directly (backend uses `adrp+add+str`, intra-section PAGE21/PAGEOFF12 patched by extractor)

### Phase 2 (OptimizerLast)

**Actions**:
- Split SROA/vectorizer-generated `store <N x i8> <const>` back to individual volatile chunk stores (prevents x86_64 `MergeConsecutiveStores` from re-aggregating into `.rodata.cst16`)
- Inline vector constants (`inlineVectorConstants`): scan non-store instructions for vector-typed constant operands, expand via per-function i64 volatile slot + `insertelement` chain

---

## 8. AllBlrPass (Optional)

Activated by `-fshellcode-all-blr`. Converts remaining intra-module direct calls to indirect calls via volatile slot + `blr xN` / `call *rax`, eliminating all relative branch relocations. Not needed for normal use since the extractor patches intra-section branches.

---

## 9. Obfuscation Hooks

The out-of-tree C plugin API provides 11 shellcode hook points (`NEVERC_HOOK_SC_*`) for third-party obfuscation passes. See [Plugin API — Hook Points](../../plugin-api/README.md#5-hook-points) for the full list.

Key design principles:
- **Registration position is a contract**: earlier hooks have broader built-in fixup coverage
- **Hooks do not interpret the spec string**: `-fshellcode-obfuscate=<spec>` is passed through to `ShellcodeOptions::ObfuscateSpec`; the obfuscation library defines its own DSL
- **Multiple libraries coexist** via get/modify/set pattern on `ObfuscationHooks`

---

## 10. Two-Phase Design Rationale

Why does Data2TextPass / ZeroRelocPass each run twice?

| Timing | IR shape | What needs cleaning |
|--------|----------|-------------------|
| Phase 1 (PipelineStart) | User's original IR with GVs, const arrays, extern calls | Original constant GVs, explicit mem* calls, libc externs |
| *(LLVM optimizations run)* | AlwaysInliner folds helpers; SROA splits allocas; SLP/vectorizer creates vector constants | New vector constant stores, re-materialized FP constants, merged consecutive stores |
| Phase 2 (OptimizerLast) | Post-optimization IR | SROA residual vector stores, late constant GVs proven write-never by GlobalOpt/IPSCCP, mutable globals to stack-ify |

This ensures both "original code" and "optimizer-introduced code" are cleaned to zero-relocation state.

---

## 11. KernelImportPass (ring-0 only)

Activated when `-mshellcode-context=kernel`. Automatically rewrites unresolved extern direct calls to resolver-backed indirect calls with encrypted address caching:

1. When the module contains extern direct calls needing resolution, the entry signature is prepended with `(resolver, cookie)` implicit parameters
2. Each API gets an `always_inline` wrapper with fast/slow path:
   - **Fast path**: `atomic_load(cache) → decrypt → indirect call`
   - **Slow path**: `resolver(FNV1a_hash(name), cookie) → encrypt → cmpxchg → call`
3. Preserves variadic arguments (critical for `printk`)
4. Address-taken externs are not wrapped; diagnostics direct users to pass pre-resolved function pointers
5. Default encryption: XOR with compile-time seed (no PEB in kernel mode); user can override `__sc_derive_key` for KPCR-based keys (see [§4.1–4.5](#41-address-cache-encryption) for the shared encryption infrastructure)

See [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.md) for details.

---

## 12. StringRuntimePass

Handles NeverC's built-in `string` type methods. When `string` values are used in shellcode mode, this pass rewrites heap-allocating runtime calls (`neverc_string_from_*`, `neverc_string_concat`, etc.) to stack-allocated arena variants, ensuring zero external dependencies.

---

## 13. HeapArenaPass

Rewrites `malloc`/`free`/`calloc`/`realloc` calls in shellcode into a hybrid allocation strategy (enabled by default, controlled via `-fshellcode-heap-arena` / `-fno-shellcode-heap-arena`):

- **Small allocations (≤ 64 KB)**: Served from the `StringRuntimePass` stack-resident arena (bump allocator + free-list reuse). Shares the same `__sc_string_arena` global to avoid doubling stack usage.
- **Large allocations (> 64 KB) or arena OOM**: Fall back to the OS allocator:
  - **Windows**: `malloc`/`free` kept as extern symbols, resolved by `WinPEBImportPass` to `msvcrt.dll` via PEB walk.
  - **Linux / macOS / Android**: `mmap`/`munmap` calls emitted, resolved by `SyscallStubPass` to inline syscalls.
  - **No import pass enabled**: Arena-only; OOM returns `NULL`.

**Pipeline position**: Must run **after** `StringRuntimePass` (shares the arena infrastructure) and **before** `SyscallStubPass` / `WinPEBImportPass` (fallback symbols need resolution).

**Function classification**: Driven by `Tables/HeapArenaRewriteTargets.def`, covering both standard names (`malloc`, `free`, `calloc`, `realloc`) and GCC builtins (`__builtin_malloc`, etc.).

**Safety features**:
- `free(NULL)` is a no-op (null check before dispatch)
- `calloc` checks for `count * size` overflow via `llvm.umul.with.overflow` and returns `NULL` on overflow
- `realloc(NULL, n)` behaves as `malloc(n)`; `realloc(p, n)` with `p != NULL` reads the old block size from the appropriate header (arena header or fallback header) before copying
- Fallback allocations prepend a size header for correct `realloc` copy-length computation

---

## 14. Error Diagnostics Philosophy

Every hard error produces exactly **one actionable diagnostic**. The module-level `__neverc_shellcode_hard_error` metadata sentinel prevents cascade noise: once `reportError` sets the flag, subsequent phases (ZeroReloc Stackify validation, extractor audit) silently early-return.

Diagnostic sources are table-driven:
- `Tables/KernelHelperNames.def`: ring-0 helper recognition
- `ExtractorCommon::printExternHint`: libc / Win32 / compiler-rt hint system
- `MIRPrepPass::hintForExternalSymbol`: MIR-level extern audit

Users see one clear error with the fix, never three cascading messages where only one is the root cause.
