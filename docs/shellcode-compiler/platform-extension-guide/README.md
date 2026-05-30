**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# Platform Extension Guide

This document explains how to extend the shellcode compiler to new target platforms. Currently supported: **arm64 / x86_64 on macOS / Linux / Android / Windows** (8 triples), each with independent **User** / **Kernel** contexts (16 variants total). Adding a new platform typically requires a few hundred lines of code.

## Design Philosophy: Table-Driven, Not Branch-Driven

Every pass is target-independent. Platform differences are concentrated in **two places**:

1. `TargetDesc.cpp`'s `describeTriple()` table entries
2. Three extractors (Mach-O / ELF / COFF) arch switches

Adding a new platform = one more row in (1), one more case in (2).

## Steps

### 1. Add a Row in `TargetDesc`

Add the corresponding OS branch in `describeTriple()`:

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**Required fields** (all defined in `TargetDesc.h`):

| Field | Purpose | If missing |
|-------|---------|-----------|
| `OS` / `Arch` / `Format` | Dispatch key | `describeTriple` returns Unknown → driver rejects early |
| `TextSectionName` | Extractor finds entry section | Extractor cannot find `.text` → reject |
| `Syscall` | SyscallStubPass replacement decision | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | SyscallStubPass inline-asm generation | Any empty → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | WinPEBImportPass TEB read inline-asm | Empty → PEB walk generates empty InlineAsm (Windows: required) |
| `DriverInjectFlags` | Platform-specific flags injected in shellcode mode | null → none injected |

### 2. Extend `SyscallStub` / `SyscallTables` (if OS has kernel traps)

- Add an enum value to `SyscallABI` in `TargetDesc.h`
- Add a `kXxxTable` in `SyscallTables.cpp`
- Add a case in `lookupSyscall`'s switch
- `SyscallStubPass` requires no changes — InlineAsm templates/constraints are read from `TargetDesc`

### 2.5 Extend Windows Win32 API Whitelist

Windows has no stable syscall ABI; user calls to `WriteFile` / `CreateThread` / `VirtualAlloc` go through `WinPEBImportPass`. The whitelist is a multi-DLL table:

- Defined in `Tables/Win32Apis.def`
- Each row: `NEVERC_WIN32_API(ApiName, "dll.dll")`
- The resolver already supports arbitrary DLLs via the two-parameter `__neverc_win_resolve(dll_hash, api_hash)`

**Adding a new API** (e.g., `DeviceIoControl`):
1. Add one row to `Win32Apis.def`
2. Mirror the declaration in `lib/Headers/windows.h`'s shellcode branch
3. No pass changes needed

**Adding a new DLL bucket** (e.g., `winhttp.dll`):
- Just add rows with the new DLL name in `Win32Apis.def`

### 3. Extend the Corresponding Extractor

Three things to handle:
1. Identify reloc types → patch bytes or reject
2. Update forbidden data section name list (new OS may have its own sections)
3. Update entry-at-offset-0 relocation target range validation

For an entirely new object format (e.g., WASM modules):
1. Add an `ObjectFormat` enum value
2. Add a case in `ShellcodeExtractor.cpp`'s dispatch switch
3. Write `<Format>Extractor.cpp` (following `ELFExtractor.cpp`'s structure)

### 4. Add a Loader (test tooling only)

- Reference `tests/neverc/shellcode/loader_linux.c` and `loader_windows.c`
- Typically: `mmap(RWX) → memcpy → icache flush → call`

### 5. Update Tests

- Add a cross-compile check in `tests/neverc/ShellcodeCrossTargetTests.cpp`
- If CI can execute on the platform, add a loader round-trip test

---

## Known Cross-Platform Gotchas

- **Endianness**: NeverC only supports little-endian (LE), covering all mainstream targets.
- **ABI differences**: Win64 (rcx/rdx/r8/r9) vs System V AMD64 (rdi/rsi/rdx/rcx/r8/r9) have completely different argument registers. This is handled at the NeverC frontend layer; the shellcode pipeline does not need to care.
- **Syscall numbers**: differ per architecture on Linux, Android matches Linux, Darwin has its own BSD numbers, Windows has no stable numbers (hence PEB walk). Indexed by (OS, arch) in the table.
- **Cache coherency**: ARM requires explicit i-cache flush; x86 does not. macOS arm64 JIT also needs `pthread_jit_write_protect_np`; Linux arm64 uses `__builtin___clear_cache`; Windows uses `FlushInstructionCache` (no-op on x86).
- **SELinux / W^X**: Android is constrained by SELinux `execmem`; iOS on non-jailbroken devices completely rejects `mmap(RWX)`, requiring `MAP_JIT` + code signing.

## Future Extension Roadmap

| Target | Estimated effort | Dependencies |
|--------|-----------------|--------------|
| **iOS arm64** (jailbreak / `MAP_JIT`) | 1 day | Reuse Mach-O extractor, modify loader |
| **FreeBSD / OpenBSD x86_64** | Half day | Reuse ELF extractor + new syscall table |
| **RISC-V64 Linux** | 2 days | Needs RISC-V TargetDesc + new AllBlr variant + RISC-V reloc patching |

## Obfuscation Pass Extension Interface

The shellcode pipeline exposes 11 hook points via `Pipeline.h::ObfuscationHooks` for third-party obfuscation libraries:

```
PipelineStartEP:
  RunBeforePrep → [ZeroReloc Prep] → RunAfterPrep →
  [IndirectBr → MemIntrin → CompilerRt → SyscallStub →
   WinPEBImport → KernelImport → Data2Text phase 1] →
  RunBeforeInlining

OptimizerLastEP:
  RunAfterInlining → [Data2Text phase 2 → ZeroReloc Stackify] →
  RunAfterStackify → [AllBlrPass] → RunAfterFinalIR

MIR: RunBeforePreEmit → [MIRPrepPass] → RunAfterPreEmit →
     [LLVM addPreEmitPass/addPreEmitPass2] → RunAfterFinalMIR

Byte-stream: RunPostExtract → [finalize chain] → RunPostFinalize
```

IR-level usage:
```cpp
neverc::shellcode::ObfuscationHooks H;
H.RunAfterInlining = [](llvm::ModulePassManager &MPM,
                        const neverc::shellcode::ShellcodeOptions &Opts) {
  MPM.addPass(MyCFFPass(Opts.ObfuscateSpec));
};
// Register via Plugin API: NEVERC_HOOK_SC_* hooks (see plugin-api docs)
```

MIR-level usage:
```cpp
H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
};
```

Built-in MIR patching is also table-driven: `Tables/MIRRewritePatterns.def` records pattern diagnostic names, arch filters, and helper names; `Tables/MIRRewriteOpcodes.def` records backend opcode names. When adding new shellcode-friendly backend forms, prefer adding table entries and narrow helpers over scattering target-specific branches in the pass body.
