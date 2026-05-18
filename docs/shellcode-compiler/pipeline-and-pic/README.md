**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# Shellcode Pipeline, MIR & PIC Strategy (Design Notes)

This document describes the design tradeoffs in NeverC's shellcode mode across the **IR → LLVM optimization → backend MIR → object file → extraction/patching** chain, and its relationship with the **compiler-wide default PIC** policy. Implementation details are authoritative in the source code and English comments.

## 1. Why Force PIC by Default (including non-shellcode compilation)

The shellcode extractor assumes that references to external symbols in the executable fragment land on **PC-relative** or intra-`.text` resolvable relocations, rather than hardcoded absolute addresses or constant pools requiring a loader to fill `.data`.

NeverC returns **true** from `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()`, and `MSVCToolChain::isPICDefaultForced()`, distinguishing from upstream Clang's "optional PIC by default" behavior: **all-platform compilation always uses PIC as the only model**. This means:

- Regular C compilation and `-fshellcode` compilation share the same relocation habits, reducing the "works normally, breaks under shellcode" cognitive burden.
- Linux / Android / macOS / Windows backends share the same assumptions under table-driven descriptors (`TargetDesc` + `Options.td.h`), avoiding `if (linux)` style hardcoding in the driver.

This policy does not distinguish whether `-fshellcode` is enabled or whether the context is user/kernel. Even if the user passes `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`, `ParsePICArgs()` maintains `Reloc::PIC_`, using the same PC-relative assumptions for regular compilation, user-mode shellcode, and kernel-mode shellcode.

## 2. IR and MIR Two-Stage Division of Labor

### 2.1 IR Layer (`registerShellcodePasses`)

Responsible for compressing "normal C" semantics into a **single-entry, no independent data section, no problematic globals** shape: `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass` (kernel only), `Data2TextPass`, etc.

**Principle**: Problems solvable in IR using structured approaches should be fixed in IR first (constant pools, BlockAddress, `memcpy` falling through to libc, `__int128 /` falling through to `__udivti3`, etc.), making the byte stream seen by the backend and extractor simpler. For scenarios with high user cognitive burden that can be safely internalized, the driver proactively injects rules (e.g., AArch64 Linux / Android / Windows `long double` is downgraded to binary64 in shellcode mode). Only constructs that cannot be supported without a runtime trigger MIR / extractor diagnostics.

### 2.2 MIR Layer (`registerShellcodeMachinePasses`)

Registers callbacks into LLVM's legacy `TargetPassConfig` **after register allocation, before `addPreEmitPass`**, in this order:

1. User/obfuscation library: `RunBeforePreEmit` (CFI / EH pseudos still present; useful for metadata-dependent transforms).
2. **`ShellcodeMIRPrepPass`**: removes pseudos that would generate `.eh_frame` / `.pdata` / `.xray_*` side sections, making the instruction stream as close to "pure code" as possible before AsmPrinter.
3. User/obfuscation library: `RunAfterPreEmit` (suitable for instruction substitution, register renaming, and similar "final machine code form" obfuscation).

**Principle**: If native instruction sequences still have issues, fix them in MIR (especially around `ShellcodeMIRPrepPass`); **extraction and patching are the last-resort safety net**, avoiding duplicating the same logic across COFF/ELF/Mach-O layers.

MIR opcode names are not scattered in pass control flow; `ShellcodeMIRPrepPass` uses `Tables/MIRRewriteOpcodes.def`'s `(pattern, role, opcode)` table to look up backend instruction names via `TargetInstrInfo::getName()`. When adding shellcode-friendly instruction substitutions, prefer adding table entries and small MIR rewrites; only fall back to backend `.td` instruction selection changes when necessary, and use extractor-level object format fallback as the last resort.

> Note: `ShellcodeMIRPrepPass` is only registered when `-fshellcode` is enabled. Regular programs must not globally strip CFI/EH, as this would break normal exception handling and debug info.

Both IR and MIR global callbacks use a **register-once, read-current-`ShellcodeOptions`-snapshot-at-runtime** pattern. This supports longer-lived embedded compiler processes: when the same process first compiles shellcode then regular C, the regular C compilation does not inherit the previous IR/MIR passes; when compiling multiple shellcode TUs consecutively, duplicate global callback registrations do not stack the same pass set multiple times.

## 3. Table-Driven Platform Differences

- **Triple → behavior**: centralized in `TargetDesc.cpp`'s `describeTriple()` and `TargetDesc` fields (section name, syscall ABI, inline assembly template, driver injection flags, etc.). When adding a new OS/Arch, prefer **adding table entries** rather than writing long branches in extractors or passes.
- **CLI options**: defined in `neverc/include/neverc/Invoke/Options.td.h`; consumed by `DriverIntegration.cpp` using `OPT_*` enums, avoiding string magic.

## 4. Windows MSVC Toolchain and SDK Layout

When cross-compiling for Windows targets, NeverC supports two SDK sources with **no hardcoded absolute paths**:

1. **Bundled SDK with the build tree** (recommended): users and test scripts treat `build-neverc/sdk` as the SDK root. NeverC auto-detects `sdk/msvc/` within the installation directory and injects include/lib paths in `MSVCToolChain::AddClangSystemIncludeArgs` / `Linker::ConstructJob`. Typical layout:

   ```
   build-neverc/bin/neverc
   build-neverc/sdk/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **Real VS-style sysroot** (optional): if you have a `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...` directory tree, point to it via `-winsysroot=<path>` or the `NEVERC_WIN_SYSROOT` environment variable.

Both sources work without registry or OS-provided VS environment variables, enabling Windows shellcode cross-compilation from macOS / Linux.

## 5. Obfuscation and Extension Points

- **IR obfuscation**: via `setShellcodeObfuscationHooks` with multiple IR-stage hooks; `-fshellcode-obfuscate=` passes the spec string to the external library. Each layer provides **pre** (before optimization) and **post** (after optimization) hooks. `RunAfterFinalIR` is the true last IR-dimension injectable point — obfuscation passes registered here have no subsequent passes that can modify their output. 11 hook points total (6 IR + 3 MIR + 2 byte-stream).
- **MIR obfuscation**: `RunBeforePreEmit` / `RunAfterPreEmit` are pre/mid-granularity MIR hooks; `RunAfterFinalMIR` is the **true last** MIR hook (fork-extension adds `RegisterTargetPassConfigPostPreEmitCallbackFn` invoked after `addPreEmitPass2()`). `-fshellcode-mir-obfuscate=` specifies MIR spec separately; defaults to IR spec when unset.
- **Byte-stream hooks**: `RunPostExtract` is the finalize **pre** hook; `RunPostFinalize` is the finalize **post** hook (last moment before disk write, no further NeverC auditing).
- **Finalize plugin SDK**: `Plugin.h` exposes `registerBadByteRewriteStrategy` (chains instruction-level bad-byte rewrite strategies) and `registerCharsetEncoder` (named charset registration). See [plugin-interface.md §2–§3](../plugin-interface/README.md#2-bad-byte-rewriter-badbyterewritestrategy).
- **Size / alignment / padding**: `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` execute at the end of finalize; the driver rejects contradictory configurations.
- **Design choice**: obfuscation, polymorphism, staged encoders, indirect syscalls, and similar strategy-layer features are **intentionally not built-in**, and are only available as optional plugins.

## 6. Kernel-Mode (Ring-0) Dimension

Shellcode mode introduces `-mshellcode-context=user|kernel` as the pipeline's second dimension, layered on top of the triple:

- **User mode**: PEB walk / syscall stub pipeline.
- **Kernel mode**:
  - `SyscallStubPass` / `WinPEBImportPass` early-return at the pass level.
  - `TargetDesc::KernelInjectFlags` appends OS/arch-appropriate backend flags (Unix x86_64: `-mno-red-zone -mcmodel=kernel`, Windows: `/kernel`, AArch64: `-mgeneral-regs-only`).
  - `KernelImportPass` rewrites unresolved extern direct calls to resolver-backed indirect calls, injecting `(resolver, cookie)` implicit prefix parameters when needed.
  - `<neverc/kernel.h>` exposes `neverc_kern_resolve_t`, `neverc_kern_hash()`, and related kernel-side signatures; user-mode shims (`<windows.h>`, `<unistd.h>`, etc.) reject via `#error` in kernel mode.

See [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.md) for details.

## 7. Windows POSIX Compatibility Layer

### 7.1 Problem

Cross-platform C code commonly uses `write(fd, buf, n)`, `read(fd, buf, n)`, `exit(code)`, etc. On Unix platforms, `SyscallStubPass` replaces these with inline syscalls. On Windows, these POSIX names have no corresponding Win32 API, causing "unresolved relocation" errors.

### 7.2 Design Goal

**Zero user awareness**: the same C source compiles across all 8 target triples without `#ifdef _WIN32` or manual Win32 API calls.

### 7.3 Implementation

`WinPEBImportPass` implements three-phase processing:

1. **Phase 1 — POSIX scan**: scans unmatched extern declarations against a POSIX compat table.
2. **Phase 2 — Bridge wrapper generation**: `Win32PosixCompat.def` dispatches POSIX names to wrapper builders generating `always_inline` wrappers (e.g., `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc` with prot mapping, `exit` → `ExitProcess`, etc.). 13 POSIX function groups covered.
3. **Phase 3 — PEB resolution**: Win32 APIs referenced by wrappers are resolved through the normal PEB walk resolver.

### 7.4 Extensibility

Adding new POSIX compat functions: alias-only additions just change `Win32PosixCompat.def`; new semantics require a small IR builder + one table entry. Stateful operations like `open→CreateFileA` that need fd/handle lifetime tables are intentionally not emulated.

## 8. K&R Implicit Declaration Auto-Fix

When users call POSIX functions without `#include`, C89 generates K&R implicit declarations with 0 formal parameters. `SyscallStubPass` now maintains a `getCanonicalSyscallType()` table with canonical LLVM IR function types for 50+ common POSIX functions. When a 0-parameter K&R declaration is detected, the canonical signature is automatically substituted.

## 9. Summary

| Topic | Approach |
|-------|----------|
| Default PIC | All toolchains `isPICDefaultForced()==true`, aligned with shellcode assumptions |
| Fix IR first | Constants, indirect jumps, memory intrinsics eliminated in IR whenever possible |
| MIR safety net | `ShellcodeMIRPrepPass` + pre/post hooks, then object extraction/patching as last resort |
| Minimize hardcoding | `TargetDesc` + `Options.td.h` table-driven |
| User/kernel two dimensions | `-fshellcode` × `-mshellcode-context={user,kernel}`; each (OS, arch, level) is one row in `describeTriple()` |
| Windows POSIX compat | `WinPEBImportPass` bridges 13 POSIX function groups (write→WriteFile, mmap→VirtualAlloc, etc.) |
| K&R auto-fix | `SyscallStubPass` falls back to canonical POSIX signatures on 0-parameter declarations |

## 10. Shim Header Cross-Platform Constants

Shim headers (`sys/mman.h`, `fcntl.h`, etc.) expose constants that must match the target kernel ABI, since shellcode syscall stubs pass these values directly to the kernel without libc translation.

Key differences:

| Constant | Darwin | Linux/Android |
|----------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

Implementation: `#if defined(__APPLE__)` guards in shim headers. `SyscallTables.cpp` POSIX compat table uses Linux values (`AT_FDCWD = -100`), only active on `SyscallABI::LinuxSvc0` / `LinuxSyscall` paths. Windows targets do not use these POSIX headers; POSIX→Win32 bridging is handled by `WinPEBImportPass` compat wrappers.
