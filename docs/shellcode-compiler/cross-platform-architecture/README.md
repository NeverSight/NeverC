**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# NeverC Shellcode Cross-Platform Architecture Overview

This document describes the design principles behind "one set of passes covering macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Read this before extending to a new platform or context.

Related sub-system documents:
- [README.md](../README.md) — overview, CLI options, quick start
- [ir-pass-design.md](../ir-pass-design/README.md) — IR-layer pass responsibilities and examples
- [mir-pass-design.md](../mir-pass-design/README.md) — MIR-layer prep pass + obfuscation hooks
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.md) — kernel context design details
- [platform-extension-guide.md](../platform-extension-guide/README.md) — step-by-step guide for adding new platforms

---

## 1. Three-Dimensional Matrix: OS × Arch × ExecutionLevel

All cross-platform differences converge into a **three-dimensional matrix**, where each cell corresponds to a `TargetDesc` table entry:

```
                ┌──── arm64 ────┬──── x86_64 ────┐
     Darwin ────┤ User / Kernel │ User / Kernel  │  Mach-O
     Linux  ────┤ User / Kernel │ User / Kernel  │  ELF
     Android────┤ User / Kernel │ User / Kernel  │  ELF
     Windows────┤ User / Kernel │ User / Kernel  │  COFF
                └───────────────┴────────────────┘
```

8 (OS, arch) combinations × 2 ExecutionLevels = **16 table entries**, all returned by `describeTriple(triple, Level)`.

**Core design philosophy**: passes always read from the table, never write `if (OS == Darwin)` branches. Adding a new platform = filling one more row in `describeTriple()` + adding one case in each extractor's switch. Everything else stays untouched.

## 2. Pipeline Execution Order

When `-fshellcode` is active, the compiler follows this fixed order. **Every stage has a corresponding obfuscation hook** for third-party pass insertion:

```
  ┌──────── cc1 frontend (C → IR) ───────────┐
  │ All-platform default PIC                  │
  │ Driver injects CommonInjectFlags +        │
  │   perTargetInjectFlags (incl. Kernel)     │
  └──────────────┬────────────────────────────┘
                 │  LLVM IR
                 ▼
   PipelineStartEP:
    ① RunBeforePrep         ← extensible hook
    ② ZeroRelocPass (Prep)
    ③ RunAfterPrep          ← extensible hook
    ④ IndirectBrPass
    ⑤ MemIntrinPass         ← mem*/str*/bzero/abs inlining
    ⑥ StringRuntimePass     ← builtin string → stack arena
    ⑦ CompilerRtPass        ← __udivti3 family / i128 div inlining
    ⑧ SyscallStubPass       (User + non-Windows)
    ⑨ WinPEBImportPass      (User + Windows)
    ⑩ KernelImportPass      (Kernel, all OS)
    ⑪ Data2TextPass phase 1
    ⑫ RunBeforeInlining     ← extensible hook

   LLVM AlwaysInliner / SROA / SLPVectorize / InstCombine

   OptimizerLastEP:
    ⑫ RunAfterInlining      ← extensible hook (IR fully flattened)
    ⑬ Data2TextPass phase 2
    ⑭ ZeroRelocPass (Stackify)
    ⑮ RunAfterStackify      ← extensible hook
    ⑯ AllBlrPass            (optional -fshellcode-all-blr)

                 │  LLVM IR → MIR → Register Allocation
                 ▼
   TargetPassConfig.addMachinePasses, before addPreEmitPass:
    ⑰ RunBeforePreEmit      ← MIR hook, CFI pseudos still present
    ⑱ ShellcodeMIRPrepPass  ← includes MIRRewritePatterns/Opcodes tables
    ⑲ RunAfterPreEmit       ← MIR hook, closest to AsmPrinter

                 │  Mach-O / ELF / COFF object file
                 ▼
   ShellcodeExtractor dispatcher → MachO / ELF / COFF extractor
   → Patch intra-.text relocs, reject external relocs / data sections,
     verify entry at offset 0, output flat .bin
```

Two key invariants:

1. **Backend TableGen `.td` is the sole source of instruction descriptions**. Passes never assemble byte sequences manually; they always use `BuildMI(TII->get(Opc))` + `TII->getName(Opc)`.
2. **Shellcode has no external relocations and no data sections**. Each pass eliminates one class of "loader-required" references; the extractor performs byte-level fallback auditing.

## 3. Global PIC Strategy

`isPICDefaultForced()` returns **true unconditionally** across all three ToolChains (`Generic_GCC` / `MachO` / `MSVCToolChain`):

- Any `-fno-pic` / `-static` / `-mdynamic-no-pic` is overridden; `ParsePICArgs()` always resolves to `Reloc::PIC_`.
- Code generation always uses PC-relative addressing (AArch64 `adrp + add`, x86_64 `lea rip+`).
- Regular (non-shellcode) compilation also uses PIC, ensuring behavioral consistency.

## 4. User / Kernel Orthogonal Dimension

ExecutionLevel is orthogonal to (OS, arch):

- **User** (`-mshellcode-context=user`, default): classic ring-3 payload
  - Unix: `SyscallStubPass` rewrites `write`/`read`/`exit`/`mmap` to `svc`/`syscall` inline asm.
  - Windows: `WinPEBImportPass` reads TEB/PEB, queries DLL + API hash via `WinImportTables`.
  - `KernelImportPass` short-circuits.

- **Kernel** (`-mshellcode-context=kernel`): ring-0 payload
  - All platforms: `SyscallStubPass` / `WinPEBImportPass` both short-circuit.
  - `KernelInjectFlags` stacked per arch (x86_64: `-mno-red-zone`/`-mcmodel=kernel`/`-mno-{sse,sse2,mmx}`; AArch64: `-mgeneral-regs-only`).
  - `KernelImportPass` activated: auto-injects `(resolver, cookie)` prefix parameters, rewrites extern callsites to resolver-backed indirect calls, preserving variadic arguments.
  - Users still just write normal C. `<neverc/kernel.h>` provides `neverc_kern_resolve_t` and `neverc_kern_hash()`.

## 5. User-Mode "Normal C" Support Matrix

With `-fshellcode`, the following common C patterns are **directly supported** without user awareness:

| Category | Example | Internal lowering |
|----------|---------|-------------------|
| Large array/struct constants | `const unsigned char t[256] = {...};` | Data2TextPass stack-ifies |
| Floating-point constants | `double x = 3.14;` | Data2TextPass volatile alloca bit patterns |
| Computed-goto (GCC extension) | `goto *labels[op];` | IndirectBrPass → `switch` |
| memcpy / strlen / abs / struct assignment | Any `<string.h>` / `<stdlib.h>` usage | MemIntrinPass byte-loop wrappers |
| `__int128` division | `u128 q = a / b;` | CompilerRtPass shift-subtract helpers |
| Atomics | Any C11 atomic | Driver injects `-mno-outline-atomics` |
| `long double` (AArch64 binary128) | `long double x = 1.5L;` | Driver injects `-mlong-double-64` |
| POSIX headers | `<unistd.h>` / `<fcntl.h>` | Shellcode shim headers |
| Win32 headers | `<windows.h>` | Shellcode shim headers |
| Large stack frames | `int arr[4096];` | `-mno-stack-arg-probe` |

**Principle**: when users encounter unsupported C patterns, the compiler extends pass support internally rather than asking users to change code. Only patterns requiring a runtime (global constructors, `<stdio.h>`, libm transcendentals, heap allocation) trigger diagnostics, and every diagnostic includes the correct alternative.

## 6. MIR Layer: Fix / Fallback / Extract Three-Stage Pipeline

"Fix instruction-selection issues in MIR first; fall back to the extractor." This is the unified strategy. `MIRPrepPass` handles most work in three stages:

1. **Cross-platform pseudo cleanup**: strips CFI / EH_LABEL / STACKMAP / PATCHABLE_* / PSEUDO_PROBE / FENTRY_CALL / LOCAL_ESCAPE / JUMP_TABLE_DEBUG_INFO / SEH_* pseudos.
2. **Shellcode-friendly instruction rewriting**: `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def` table-driven (e.g., AArch64 CPI FP → FMOV imm; x86_64 MOVSS/MOVSD +0.0 → xorps).
3. **External reference / constant pool auditing**: actionable source-level diagnostics for any leaked CPI references or extern symbols.

The extractor is the last layer: even if MIR misses something, it rejects any external relocations or non-empty data sections at the byte level.

## 7. Extractor Layer — Intra-Text PC-Rel Patch, Reject Everything Else

The extractor dispatches by `ObjectFormat`, all sharing the same contract: "accept intra-`.text` PC-relative patches, reject everything else (GOT/TLS/absolute address/data sections)":

- **MachOExtractor**: arm64 `BRANCH26`/`PAGE21`/`PAGEOFF12`; x86_64 `SIGNED`/`SIGNED_1/2/4`/`BRANCH` (pcrel32)
- **ELFExtractor**: arm64 `CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/etc.; x86_64 `PC32`/`PLT32`
- **COFFExtractor**: arm64 `BRANCH26`/`PAGEBASE_REL21`/etc.; x86_64 `REL32`/`REL32_[1-5]`

Adding new arch support = add a case + write a reloc table. The patch algorithm itself is OS-independent.

## 8. Obfuscation Hook Points

11 `ObfuscationHooks` fields (6 IR + 3 MIR + 2 byte-level) cover every pipeline stage:

| Hook | Use case |
|------|----------|
| `RunBeforePrep` | Language-level rewrites (e.g., calling convention normalization) |
| `RunAfterPrep` | All non-entry functions are `internal` + `alwaysinline`; safe to clone |
| `RunBeforeInlining` | Last chance before AlwaysInliner |
| `RunAfterInlining` | IR flattened into one large function — ideal for CFF / bogus CF |
| `RunAfterStackify` | Final IR shape with no GVs — string encryption / constant obfuscation |
| `RunAfterFinalIR` | True last IR hook; your pass must be PIC-clean |
| `RunBeforePreEmit` | MIR with CFI pseudos still present |
| `RunAfterPreEmit` | Pseudos cleaned; closest to AsmPrinter — instruction substitution / register renaming |
| `RunAfterFinalMIR` | True last MIR hook; after all LLVM backend passes |
| `RunPostExtract` | Pure byte stream after reloc patching — whole-payload XOR/RC4, junk bytes, custom headers |
| `RunPostFinalize` | Last moment before disk write; no further NeverC auditing |

## 9. Adding a New (OS, Arch) Entry — Checklist

Total cost:
1. `TargetDesc.cpp::describeTriple` — one row (~15 lines)
2. `SyscallTables_<OS>_<arch>.def` — syscall numbers (skip for Windows)
3. Corresponding extractor arch switch — one case + reloc table
4. Test files — one case each in `tests/neverc/ShellcodeCrossTargetTests.cpp`

IR/MIR passes require zero changes. Kernel context is free — `KernelImportPass` uses a unified `__neverc_kern_resolve` interface.

## 10. Non-Goals

- **C++ / ObjC / Rust frontends**
- **32-bit / big-endian / niche ISAs** (RISC-V / PowerPC / SPARC / MIPS)
- **Embedding libc runtime in shellcode** (`<stdio.h>` / `<math.h>` / heap / setjmp / alloca / global constructors are explicitly rejected with actionable diagnostics)
- **Absolute address relocations** (all `_ABS*` / `_ADDR*` / GOT / TLS relocs hard fail)
