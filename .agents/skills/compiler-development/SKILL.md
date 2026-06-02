---
name: compiler-development
description: Expertise in compiler development using LLVM infrastructure including frontend design, IR generation, optimization passes, and code generation. Use this skill when building custom programming languages, implementing DSL compilers, or working on compiler internals.
---

# Compiler Development Skill

This skill provides comprehensive knowledge of building compilers and language implementations using the LLVM infrastructure, with **NeverC-specific cross-platform development checklists** covering Windows x64/arm64, Linux x64/arm64, and macOS arm64.

---

## Cross-Platform Target Matrix

NeverC operates on a **three-dimensional matrix**: OS × Arch × ExecutionLevel.

```
                ┌──── arm64 ────┬──── x86_64 ────┐
     Darwin ────┤ User / Kernel │ User / Kernel  │  Mach-O
     Linux  ────┤ User / Kernel │ User / Kernel  │  ELF
     Android────┤ User / Kernel │ User / Kernel  │  ELF
     Windows────┤ User / Kernel │ User / Kernel  │  COFF
                └───────────────┴────────────────┘
```

All platform differences are encoded in `TargetDesc` (returned by `describeTriple(triple, Level)`). Passes **read from the table, never write `if (OS == Darwin)` branches**. Adding a new platform = filling one row in `describeTriple()` + adding one case in each extractor's switch.

### Supported Triples

| Platform | Triple | Object Format |
|----------|--------|---------------|
| macOS arm64 | `arm64-apple-macos` | Mach-O |
| macOS x86_64 | `x86_64-apple-macos` | Mach-O |
| Linux x86_64 | `x86_64-linux-gnu` | ELF |
| Linux arm64 | `aarch64-linux-gnu` | ELF |
| Windows x86_64 | `x86_64-pc-windows-msvc` | COFF |
| Windows arm64 | `aarch64-pc-windows-msvc` | COFF |
| Android arm64 | `aarch64-linux-android` | ELF |
| Android x86_64 | `x86_64-linux-android` | ELF |
| iOS arm64 | `arm64-apple-ios` | Mach-O |

---

## Platform-Specific Development Checklists

### Windows x64 Checklist

- [ ] **ABI**: Win64 calling convention — RCX, RDX, R8, R9 for first 4 integer args; 32-byte shadow space on stack
- [ ] **Codegen**: `createWinX86_64TargetCodeGenInfo()` in `ModuleEmitter.cpp`; AVX level selection via `Target.getABI()`
- [ ] **Object format**: COFF — section names use `.text`, `.data`, `.rdata`; no segment prefixes
- [ ] **Triple dispatch**: `Triple.getOS() == llvm::Triple::Win32` + `Triple.getArch() == llvm::Triple::x86_64`
- [ ] **Stack probe**: Windows requires `__chkstk` for stack allocations > 4KB; shellcode mode uses inline probe-stack shellcode to avoid CRT dependency
- [ ] **TLS/PEB access**: `movq %gs:0x60, $0` for PEB pointer (user mode)
- [ ] **Shellcode imports**: PEB walk (`WinPEBImportPass`) resolves `kernel32.dll` / `ntdll.dll` exports at runtime
- [ ] **Kernel mode**: `KernelImportABI::WindowsKernelResolverShim`; no PEB, no syscall stubs
- [ ] **Build host**: `_popen` / `_pclose` / `_chdir` / `FindFirstFileA` instead of POSIX equivalents
- [ ] **CMake**: detect MSVC via `_NEVERC_HOST_MSVC`; static CRT (`/MT`) via `CMAKE_MSVC_RUNTIME_LIBRARY`
- [ ] **LTO disabled on Windows hosts**: Full LTO miscompiles under Windows clang due to unspecified-evaluation-order UB; keep non-LTO until clean
- [ ] **Linker**: lld-link; dead-code elimination is on by default (no `--gc-sections`)
- [ ] **Cross-compilation**: bundled MSVC SDK in `runtime/`; no external SDK needed from macOS/Linux host
- [ ] **Shell redirects**: `>nul 2>&1` instead of `>/dev/null 2>&1`
- [ ] **Default shell**: `cmd.exe` (not `/bin/sh`)
- [ ] **Path separator**: use `llvm::sys::path::native()` for backslash normalization
- [ ] **Binary format**: `.exe` / `.dll` output; `NEVERC_STRIP_BINARY OFF` on Windows

### Windows arm64 Checklist

- [ ] **ABI**: Windows ARM64 uses its own calling convention (not pure AAPCS); X0–X7 for args, X18 reserved for TEB
- [ ] **Codegen**: `createWindowsAArch64TargetCodeGenInfo()` — separate from generic AArch64; `AArch64ABIKind::AAPCS` but with Win32 OS dispatch
- [ ] **TLS/TEB access**: `ldr $0, [x18, #0x60]` for PEB pointer (X18 = TEB on Windows ARM64)
- [ ] **Stack probe**: custom AArch64 probe-stack; different from x64 `__chkstk`
- [ ] **BTI/PAC**: Windows ARM64 supports branch target identification; check `LangOpts.BranchTargetEnforcement` module flags
- [ ] **Shellcode inject flags**: `TargetInjectFlags_Windows_AArch64.def` — distinct from Unix AArch64 flags
- [ ] **Kernel inject flags**: `TargetKernelFlags_Windows_AArch64.def`
- [ ] **Object format**: COFF (same as Windows x64)
- [ ] **Syscall ABI**: `SyscallABI::WindowsPEB` — no direct syscall instruction; all imports via PEB walk
- [ ] **Register convention**: same ArgRegs as Unix arm64 (`x0`–`x7`, 8 regs) but calling convention semantics differ

### Linux x64 Checklist

- [ ] **ABI**: System V AMD64 — RDI, RSI, RDX, R10, R8, R9 for syscall args; red zone present
- [ ] **Codegen**: `createX86_64TargetCodeGenInfo()` (non-Win32 default path)
- [ ] **Object format**: ELF — section `.text`; `isOSBinFormatELF()` gates ELF-specific module flags
- [ ] **Syscall**: `syscall` instruction; RAX = syscall number, RAX = return value
- [ ] **Shellcode imports**: `SyscallStubPass` generates inline syscall stubs
- [ ] **Kernel mode**: `KernelImportABI::LinuxKallsymsShim`; resolve kernel symbols via kallsyms
- [ ] **Stack probe**: no `__chkstk`; Linux typically has guard pages; but shellcode mode may still need inline probing for large allocations
- [ ] **Linker GC**: `--gc-sections` for dead code elimination
- [ ] **Build host**: standard POSIX — `popen`, `pclose`, `chdir`, `glob`
- [ ] **Exit code**: `WIFEXITED(Status) ? WEXITSTATUS(Status) : 1`
- [ ] **ELF module flags**: `getTriple().isOSBinFormatELF()` → emit ELF-specific metadata

### Linux arm64 Checklist

- [ ] **ABI**: AAPCS64 — X0–X7 for args (8 registers); no shadow space
- [ ] **Codegen**: `createAArch64TargetCodeGenInfo()` with `AArch64ABIKind::AAPCS`
- [ ] **Syscall**: `svc #0`; X8 = syscall number, X0 = return value
- [ ] **PAC/BTI**: module flags for `sign-return-address`, `branch-target-enforcement` emitted when `Arch == llvm::Triple::aarch64`
- [ ] **Shellcode inject flags**: `TargetInjectFlags_Unix_AArch64.def`
- [ ] **Kernel inject flags**: `TargetKernelFlags_Unix_AArch64.def`
- [ ] **Object format**: ELF
- [ ] **Indirect branches**: `AllBlrPass` (optional `-fshellcode-all-blr`) rewrites indirect branches to BLR for arm64

### macOS arm64 Checklist

- [ ] **ABI**: DarwinPCS — variant of AAPCS with differences in va_arg, struct passing, and alignment
- [ ] **Codegen**: `createAArch64TargetCodeGenInfo()` with `AArch64ABIKind::DarwinPCS` (gated by `Target.getABI() == "darwinpcs"`)
- [ ] **Object format**: Mach-O — section `__text` (prefixed with `__`); `isOSBinFormatMachO()` for Mach-O-specific paths
- [ ] **Syscall**: `svc #0x80`; X16 = syscall number (with `SyscallNumberMask = 0` for arm64, `= 0x2000000` for x86_64 Darwin), X0 = return value
- [ ] **Kernel mode**: `KernelImportABI::DarwinXNUKextShim` for kext symbol resolution
- [ ] **Build host**: Homebrew LLVM preferred; CMake auto-detects `/opt/homebrew/opt/llvm/bin`
- [ ] **Linker GC**: `-Wl,-dead_strip` (macOS-specific flag, not `--gc-sections`)
- [ ] **Code signing**: ad-hoc signed; users need `xattr -dr com.apple.quarantine` after download
- [ ] **llvm-ar**: auto-detected when host is non-Apple clang to avoid libtool incompatibility
- [ ] **PAC/BTI**: `BranchProtectionPAuthLR`, `sign-return-address-all`, `sign-return-address-with-bkey` module flags

---

## ABI & Calling Convention Reference

### x86_64 ABI Split

| | System V (Linux/macOS/Android) | Win64 (Windows) |
|---|---|---|
| Integer args | RDI, RSI, RDX, RCX, R8, R9 | RCX, RDX, R8, R9 |
| Syscall args | RDI, RSI, RDX, R10, R8, R9 | N/A (PEB walk) |
| Syscall # reg | RAX | N/A |
| Return reg | RAX | RAX |
| Shadow space | No (128-byte red zone) | Yes (32 bytes) |
| Callee-saved | RBX, RBP, R12–R15 | RBX, RBP, RDI, RSI, R12–R15 |
| Struct return | RAX:RDX or memory | RAX or memory |

### AArch64 ABI Split

| | AAPCS (Linux/Android) | DarwinPCS (macOS/iOS) | Windows ARM64 |
|---|---|---|---|
| Integer args | X0–X7 | X0–X7 | X0–X7 |
| Syscall # reg | X8 | X16 | N/A (PEB) |
| Syscall insn | `svc #0` | `svc #0x80` | N/A |
| TEB/TLS | N/A | N/A | X18 (reserved) |
| PEB access | N/A | N/A | `ldr $0, [x18, #0x60]` |
| va_arg | stack-based | slightly different alignment | Win64 rules |

---

## Build System Cross-Platform Checklist

### CMake Configuration (`NeverC.cmake`)

- [ ] **LLVM targets**: always `"AArch64;X86"` — only two backends
- [ ] **MSVC detection**: `_NEVERC_HOST_MSVC` flag; avoid `-O2`, `-march=native` on MSVC
- [ ] **Static CRT on Windows**: `CMAKE_MSVC_RUNTIME_LIBRARY = "MultiThreaded$<$<CONFIG:Debug>:Debug>"`
- [ ] **LLD selection**: auto-detect `lld-link` (Windows), `ld64.lld` (macOS), `ld.lld` (Linux)
- [ ] **LTO**: disabled on Windows hosts and MSVC; enabled only on macOS/Linux non-Debug non-cross-compile
- [ ] **PGO**: two-phase generate/use; `-fprofile-instr-generate` / `-fprofile-instr-use`
- [ ] **Section GC flags**: `-ffunction-sections -fdata-sections` + platform-specific linker flags
- [ ] **Native arch tuning**: `-march=native` when `NEVERC_NATIVE_ARCH=ON` and not cross-compiling
- [ ] **ccache/sccache**: auto-detected via `find_program`
- [ ] **llvm-ar**: auto-used on macOS when host compiler is non-Apple Clang (avoids libtool incompatibility)

### Platform Abstraction Layer (`Platform.cpp`)

All `#ifdef _WIN32` branches:

| Function | POSIX | Windows |
|----------|-------|---------|
| `shellExecute` | `popen` / `pclose` + `WEXITSTATUS` | `_popen` / `_pclose` + raw status |
| `shellExecuteNoCapture` | `>/dev/null 2>&1` | `>nul 2>&1` |
| `globFiles` | `glob()` / `globfree()` | `FindFirstFileA` / `FindNextFileA` / `FindClose` |
| `getDefaultShell` | `/bin/sh` | `cmd.exe` |
| `changeCwd` | `chdir` | `_chdir` |

Use `llvm::sys::fs::*` and `llvm::sys::path::*` whenever possible — they are already cross-platform.

---

## Shellcode Pipeline Cross-Platform Checklist

### TargetDesc Table Fields

Every platform must populate:

- [ ] `OS` — `ShellcodeOS` enum
- [ ] `Arch` — `ShellcodeArch` enum
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
  ⑫ AllBlrPass                  — optional (-fshellcode-all-blr), arm64 only
     (extensible hooks: RunAfterInlining, RunAfterStackify)
  ↓
MIR (TargetPassConfig.addMachinePasses):
  ⑬ ShellcodeMIRPrepPass        — per-target rewrite patterns/opcodes
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

## Module Flags & Platform-Specific Metadata

### AArch64-Only Module Flags

Only emitted when `Arch == llvm::Triple::aarch64`:

```cpp
"branch-target-enforcement"    // BTI
"branch-protection-pauth-lr"   // PAC for LR
"sign-return-address"          // PAC sign return addr
"sign-return-address-all"      // sign all functions
"sign-return-address-with-bkey"// use B-key
```

### Windows-Specific Module Flags

```cpp
"ms-kernel"    // kernel mode (-fms-kernel); affects AsmPrinter on both X86 and AArch64
"cfguard"      // Control Flow Guard (value 2 = full, 1 = no-checks)
"ehcontguard"  // Exception Handling Continuation Guard
```

### ELF-Specific Paths

```cpp
if (getTriple().isOSBinFormatELF()) { /* ELF metadata */ }
```

### Mach-O-Specific Paths

```cpp
const bool isMachO = getTriple().isOSBinFormatMachO();
```

---

## Testing Across Platforms

### Test Organization

- `ShellcodeTests.cpp` — core shellcode pipeline tests
- `ShellcodeCrossTargetTests.cpp` — cross-target compilation tests
- `ShellcodeStressTests.cpp` — stress / edge-case tests
- `BasicTests.cpp` — basic compiler functionality
- `DriverTests.cpp` — driver / CLI tests
- `BuildTests.cpp` — build system (make) tests
- Platform-specific loaders: `loader_windows.c`, `loader_linux.c`, `loader_arm64_macos.c`

### Cross-Platform Test Checklist

- [ ] Test all 5 target triples: macOS arm64, Linux x64, Linux arm64, Windows x64, Windows arm64
- [ ] Test both User and Kernel execution levels
- [ ] Verify shellcode extraction for each object format (MachO/ELF/COFF)
- [ ] Verify import resolution: PEB walk (Windows), syscall stubs (Linux/macOS), kernel shims
- [ ] Test string runtime in shellcode mode across all targets
- [ ] Verify stack probe behavior on Windows targets (both x64 and arm64)
- [ ] Test bad-byte auditing across different target encodings
- [ ] Cross-compile from macOS host to all other targets

---

## Compiler Architecture Overview

### Classic Three-Phase Design

```
Source Code → Frontend → Middle-End (Optimizer) → Backend → Machine Code
                ↓              ↓                      ↓
             AST/IR      LLVM IR Passes          Target Code
```

### NeverC-Specific Extensions

```
Source (.c/.nc) → Frontend → Shellcode IR Passes → MIR Passes → Backend → Extractor → .bin
                                    ↓                   ↓                      ↓
                             ZeroReloc, Import    MIRPrepPass         MachO/ELF/COFF extract
                             Syscall, String      Rewrite patterns
```

## Frontend Development

### Lexical Analysis

```cpp
enum class TokenKind {
    Identifier, Number, String, Keyword,
    Operator, Punctuation, EndOfFile
};

struct Token {
    TokenKind kind;
    std::string value;
    SourceLocation location;
};
```

### Parser Implementation

- Recursive Descent: Easy to implement, good error messages
- Operator Precedence Parsing: Efficient for expression parsing
- LALR/LR: Use tools like Bison for complex grammars

### AST Design

```cpp
class Expr {
public:
    virtual ~Expr() = default;
    virtual llvm::Value* codegen() = 0;
};

class BinaryExpr : public Expr {
    std::unique_ptr<Expr> LHS, RHS;
    char Op;
public:
    llvm::Value* codegen() override {
        llvm::Value* L = LHS->codegen();
        llvm::Value* R = RHS->codegen();
        switch (Op) {
            case '+': return Builder.CreateFAdd(L, R, "addtmp");
            case '-': return Builder.CreateFSub(L, R, "subtmp");
            case '*': return Builder.CreateFMul(L, R, "multmp");
            case '/': return Builder.CreateFDiv(L, R, "divtmp");
        }
    }
};
```

## LLVM IR Generation

### Module and Context Setup

```cpp
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"

class CodeGen {
    std::unique_ptr<llvm::LLVMContext> Context;
    std::unique_ptr<llvm::Module> Module;
    std::unique_ptr<llvm::IRBuilder<>> Builder;

public:
    CodeGen() {
        Context = std::make_unique<llvm::LLVMContext>();
        Module = std::make_unique<llvm::Module>("my_module", *Context);
        Builder = std::make_unique<llvm::IRBuilder<>>(*Context);
    }
};
```

### Target-Aware Codegen Dispatch

```cpp
std::unique_ptr<TargetCodeGenInfo> createTargetCodeGenInfo(ModuleEmitter &ME) {
    const llvm::Triple &Triple = ME.getTarget().getTriple();

    switch (Triple.getArch()) {
    case llvm::Triple::aarch64:
        switch (Triple.getOS()) {
        case llvm::Triple::Win32:
            return createWindowsAArch64TargetCodeGenInfo(ME, Kind);
        default:
            return createAArch64TargetCodeGenInfo(ME, Kind);
        }
    case llvm::Triple::x86_64:
        switch (Triple.getOS()) {
        case llvm::Triple::Win32:
            return createWinX86_64TargetCodeGenInfo(ME, AVXLevel);
        default:
            return createX86_64TargetCodeGenInfo(ME, AVXLevel);
        }
    }
}
```

## Optimization Pass Pipeline

### New Pass Manager

```cpp
#include "llvm/Passes/PassBuilder.h"

void optimizeModule(llvm::Module& M) {
    llvm::PassBuilder PB;
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O2);
    MPM.run(M, MAM);
}
```

## JIT Compilation

### LLVM ORC JIT

```cpp
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

auto JIT = llvm::orc::LLJITBuilder().create();
if (!JIT) handleError(JIT.takeError());

(*JIT)->addIRModule(llvm::orc::ThreadSafeModule(
    std::move(Module), std::move(Context)));

auto Sym = (*JIT)->lookup("main");
auto* MainFn = (int(*)())Sym->getAddress();
int result = MainFn();
```

## Language Implementation Patterns

### Memory-Safe Languages

- Use LLVM's memory sanitizer hooks
- Implement bounds checking with GEP introspection
- Reference counting or garbage collection integration

### Type Systems

- Implement type inference during AST construction
- Generate appropriate LLVM types (i32, float, struct, ptr)
- Handle generic types via monomorphization or boxing

### Error Handling

- Generate exception handling via LLVM's landingpad/invoke
- Implement Result/Option types as tagged unions
- Use LLVM's personality functions for unwinding

## Development Workflow

1. **Start Simple**: Begin with Kaleidoscope tutorial
2. **Incremental Features**: Add one language feature at a time
3. **Test Extensively**: Unit tests for each compiler phase
4. **Use LLVM Tools**: opt, llc, llvm-dis for debugging IR
5. **Profile and Optimize**: Focus on common code patterns
6. **Cross-Platform First**: Ensure every new feature works across all 5 target triples

## Cross-Platform Development Golden Rules

1. **Never hardcode OS checks in passes** — use `TargetDesc` table lookup
2. **Always use `llvm::sys::path` / `llvm::sys::fs`** — not raw POSIX or Win32 APIs
3. **Gate `#ifdef _WIN32` to Platform.cpp** — keep it out of compiler logic
4. **Test Windows ARM64 separately** — its ABI is a unique hybrid (AAPCS regs + Win64 semantics)
5. **Remember SyscallNumberMask** — macOS x86_64 uses `0x2000000` offset, everything else is `0`
6. **Mach-O section names have `__` prefix** — `__text` not `.text`
7. **Windows has no direct syscall in shellcode** — always PEB walk, even on ARM64
8. **LTO is not safe on Windows hosts** — known UB surfaces under LTO; build non-LTO on Windows CI

## Resources

### Official Tutorials

- LLVM Kaleidoscope: Building a language from scratch
- Clang internals: Frontend implementation patterns
- Writing an LLVM Backend: Target code generation

### Community Projects

See DIY Compiler section in README.md for 100+ example implementations across different language paradigms.

## Getting Detailed Information

When you need detailed and up-to-date resource links, tool lists, or project references, fetch the latest data from:

```
https://raw.githubusercontent.com/gmh5225/awesome-llvm-security/refs/heads/main/README.md
```

This README contains comprehensive curated lists of:
- 100+ DIY compiler implementations (DIY Compiler section)
- Toolchain configurations and IDE setup
- Compiler development tutorials and books
