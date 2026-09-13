# Dyncode And Testing

## DynCode Pipeline Cross-Platform Checklist

### TargetDesc Table Fields

Every platform must populate:

- [ ] `OS` — `DynCodeOS` enum
- [ ] `Arch` — `DynCodeArch` enum
- [ ] `Format` — `ObjectFormat` enum (MachO/ELF/COFF)
- [ ] `Syscall` — `SyscallABI` (which syscall mechanism)
- [ ] `TextSectionName` — `.text` (ELF/COFF) or `__text` (Mach-O)
- [ ] `AsmTemplate` — inline asm for syscall instruction
- [ ] `SyscallNumberReg` / `SyscallRetReg` — register names
- [ ] `SyscallNumberMask` — `0x2000000` for macOS x86_64, `0` elsewhere
- [ ] `ArgRegs` / `NumArgRegs` — argument register array
- [ ] `TCBReadAsm` / `TCBReadConstraint` — PEB/TEB access (Windows only)
- [ ] `DriverInjectFlags` — from `.def` tables per (OS, Arch) pair
- [ ] `KernelImport` — `KernelImportABI` enum for kernel mode
- [ ] `KernelInjectFlags` — from `.def` tables per (OS, Arch) pair

### Inject Flags `.def` Tables

Each (OS, Arch, Level) combination has its own `.def` file:

```
TargetInjectFlags_Unix_X86_64.def
TargetInjectFlags_Unix_AArch64.def
TargetInjectFlags_Windows_X86_64.def
TargetInjectFlags_Windows_AArch64.def
TargetKernelFlags_Unix_X86_64.def
TargetKernelFlags_Unix_AArch64.def
TargetKernelFlags_Windows_X86_64.def
TargetKernelFlags_Windows_AArch64.def
```

Plus `UserExtra_*` variants for extension. When adding a new platform, create matching `.def` files.

### Pipeline Execution Order (Cross-Platform)

```
cc1 frontend (C → IR) → PIC default
  ↓
PipelineStartEP:
  ① ZeroRelocPass (Prep)        — all platforms
  ② IndirectBrPass              — all platforms
  ③ MemIntrinPass               — mem*/str*/bzero inlining (all)
  ④ StringRuntimePass           — builtin string → stack arena (all)
  ⑤ CompilerRtPass              — __udivti3 / i128 div inlining (all)
  ⑥ SyscallStubPass             — User + non-Windows only
  ⑦ WinPEBImportPass            — User + Windows only
  ⑧ KernelImportPass            — Kernel, all OS
  ⑨ Data2TextPass phase 1       — all platforms
     (extensible hooks: RunBefore/AfterPrep, RunBeforeInlining)
  ↓
LLVM optimizer (AlwaysInliner, SROA, SLPVectorize, InstCombine)
  ↓
OptimizerLastEP:
  ⑩ Data2TextPass phase 2       — all platforms
  ⑪ ZeroRelocPass (Stackify)    — all platforms
  ⑫ AllBlrPass                  — optional (-fdyncode-all-blr), arm64 only
     (extensible hooks: RunAfterInlining, RunAfterStackify)
  ↓
MIR (TargetPassConfig.addMachinePasses):
  ⑬ DynCodeMIRPrepPass        — per-target rewrite patterns/opcodes
     (extensible hooks: RunBeforePreEmit, RunAfterPreEmit)
  ↓
Extractor: MachO / ELF / COFF dispatcher
  → patch intra-.text relocs, reject external relocs/data, output flat .bin
```

### Object Extractor Differences

| | Mach-O | ELF | COFF |
|---|---|---|---|
| Extractor | `MachOExtractor` | `ELFExtractor` | `COFFExtractor` |
| Text section | `__text` | `.text` | `.text` |
| Reloc patching | intra-segment | intra-section | intra-section |
| External reloc | rejected | rejected | rejected |
| Data sections | rejected | rejected | rejected |

---

## Testing Across Platforms

### Test Organization

- `DynCodeTests.cpp` — core dyncode pipeline tests
- `DynCodeCrossTargetTests.cpp` — cross-target compilation tests
- `DynCodeStressTests.cpp` — stress / edge-case tests
- `BasicTests.cpp` — basic compiler functionality
- `DriverTests.cpp` — driver / CLI tests
- `BuildTests.cpp` — build system (make) tests
- Platform-specific loaders: `loader_windows.c`, `loader_linux.c`, `loader_arm64_macos.c`

### Cross-Platform Test Checklist

- [ ] Test all 5 target triples: macOS arm64, Linux x64, Linux arm64, Windows x64, Windows arm64
- [ ] Test both User and Kernel execution levels
- [ ] Verify dyncode extraction for each object format (MachO/ELF/COFF)
- [ ] Verify import resolution: PEB walk (Windows), syscall stubs (Linux/macOS), kernel shims
- [ ] Test string runtime in dyncode mode across all targets
- [ ] Verify stack probe behavior on Windows targets (both x64 and arm64)
- [ ] Test bad-byte auditing across different target encodings
- [ ] Cross-compile from macOS host to all other targets

---
