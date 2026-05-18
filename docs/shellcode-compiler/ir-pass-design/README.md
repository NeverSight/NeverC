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
| CompilerRtPass | `__udivti3` family + IR-level i128 div/rem → internal 128-bit long-division | PipelineStart |
| SyscallStubPass | libc extern → target OS kernel trap wrapper, table-driven via `TargetDesc` | PipelineStart |
| WinPEBImportPass | Win32 extern → PEB module walk + PE export resolver | PipelineStart |
| KernelImportPass | Ring-0 extern → resolver-backed indirect call (kernel mode only) | PipelineStart |
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

**Solution**: for each extern matching the `WinImportTables` whitelist (~190 APIs across 6 DLLs):
1. Generate an `always_inline` wrapper that calls `__neverc_win_resolve(dll_hash, api_hash)`
2. The resolver is a real PEB → Ldr → InMemoryOrderModuleList walker with ROR-13 hash matching
3. Only one inline asm instruction per platform: `movq %gs:0x60, $0` (x86_64) / `ldr $0, [x18, #0x60]` (arm64)
4. All list traversal, PE header parsing, and hash comparison is pure LLVM IR

**Windows POSIX compat**: `WinPEBImportPass` bridges 13 POSIX function groups (write→WriteFile, mmap→VirtualAlloc, exit→ExitProcess, etc.) via `Win32PosixCompat.def`, enabling the same C source to compile across all 8 triples without `#ifdef`.

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

`Pipeline.h::ObfuscationHooks` provides 11 hook points for third-party obfuscation passes. See [plugin-interface.md §6](../plugin-interface/README.md#6-registration-position-selection--pic-coverage-matrix) for the full PIC coverage matrix and recommended registration positions.

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

Activated when `-mshellcode-context=kernel`. Automatically rewrites unresolved extern direct calls to resolver-backed indirect calls:

1. When the module contains extern direct calls needing resolution, the entry signature is prepended with `(resolver, cookie)` implicit parameters
2. Each callsite: `fn_ptr = resolver(FNV1a_hash(name), cookie); ((T*)fn_ptr)(args...)`
3. Preserves variadic arguments (critical for `printk`)
4. Address-taken externs are not wrapped; diagnostics direct users to pass pre-resolved function pointers

See [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.md) for details.

---

## 12. StringRuntimePass

Handles NeverC's built-in `string` type methods. When `string` values are used in shellcode mode, this pass rewrites heap-allocating runtime calls (`neverc_string_from_*`, `neverc_string_concat`, etc.) to stack-allocated arena variants, ensuring zero external dependencies.

---

## 13. Error Diagnostics Philosophy

Every hard error produces exactly **one actionable diagnostic**. The module-level `__neverc_shellcode_hard_error` metadata sentinel prevents cascade noise: once `reportError` sets the flag, subsequent phases (ZeroReloc Stackify validation, extractor audit) silently early-return.

Diagnostic sources are table-driven:
- `Tables/KernelHelperNames.def`: ring-0 helper recognition
- `ExtractorCommon::printExternHint`: libc / Win32 / compiler-rt hint system
- `MIRPrepPass::hintForExternalSymbol`: MIR-level extern audit

Users see one clear error with the fix, never three cascading messages where only one is the root cause.
