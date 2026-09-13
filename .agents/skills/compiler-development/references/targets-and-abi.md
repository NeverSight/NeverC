# Targets And Abi

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
- [ ] **Stack probe**: Windows requires `__chkstk` for stack allocations > 4KB; dyncode mode uses inline probe-stack dyncode to avoid CRT dependency
- [ ] **TLS/PEB access**: `movq %gs:0x60, $0` for PEB pointer (user mode)
- [ ] **DynCode imports**: PEB walk (`WinPEBImportPass`) resolves `kernel32.dll` / `ntdll.dll` exports at runtime
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
- [ ] **DynCode inject flags**: `TargetInjectFlags_Windows_AArch64.def` — distinct from Unix AArch64 flags
- [ ] **Kernel inject flags**: `TargetKernelFlags_Windows_AArch64.def`
- [ ] **Object format**: COFF (same as Windows x64)
- [ ] **Syscall ABI**: `SyscallABI::WindowsPEB` — no direct syscall instruction; all imports via PEB walk
- [ ] **Register convention**: same ArgRegs as Unix arm64 (`x0`–`x7`, 8 regs) but calling convention semantics differ

### Linux x64 Checklist

- [ ] **ABI**: System V AMD64 — RDI, RSI, RDX, R10, R8, R9 for syscall args; red zone present
- [ ] **Codegen**: `createX86_64TargetCodeGenInfo()` (non-Win32 default path)
- [ ] **Object format**: ELF — section `.text`; `isOSBinFormatELF()` gates ELF-specific module flags
- [ ] **Syscall**: `syscall` instruction; RAX = syscall number, RAX = return value
- [ ] **DynCode imports**: `SyscallStubPass` generates inline syscall stubs
- [ ] **Kernel mode**: `KernelImportABI::LinuxKallsymsShim`; resolve kernel symbols via kallsyms
- [ ] **Stack probe**: no `__chkstk`; Linux typically has guard pages; but dyncode mode may still need inline probing for large allocations
- [ ] **Linker GC**: `--gc-sections` for dead code elimination
- [ ] **Build host**: standard POSIX — `popen`, `pclose`, `chdir`, `glob`
- [ ] **Exit code**: `WIFEXITED(Status) ? WEXITSTATUS(Status) : 1`
- [ ] **ELF module flags**: `getTriple().isOSBinFormatELF()` → emit ELF-specific metadata

### Linux arm64 Checklist

- [ ] **ABI**: AAPCS64 — X0–X7 for args (8 registers); no shadow space
- [ ] **Codegen**: `createAArch64TargetCodeGenInfo()` with `AArch64ABIKind::AAPCS`
- [ ] **Syscall**: `svc #0`; X8 = syscall number, X0 = return value
- [ ] **PAC/BTI**: module flags for `sign-return-address`, `branch-target-enforcement` emitted when `Arch == llvm::Triple::aarch64`
- [ ] **DynCode inject flags**: `TargetInjectFlags_Unix_AArch64.def`
- [ ] **Kernel inject flags**: `TargetKernelFlags_Unix_AArch64.def`
- [ ] **Object format**: ELF
- [ ] **Indirect branches**: `AllBlrPass` (optional `-fdyncode-all-blr`) rewrites indirect branches to BLR for arm64

### macOS arm64 Checklist

- [ ] **ABI**: DarwinPCS — variant of AAPCS with differences in va_arg, struct passing, and alignment
- [ ] **Codegen**: `createAArch64TargetCodeGenInfo()` with `AArch64ABIKind::DarwinPCS` (gated by `Target.getABI() == "darwinpcs"`)
- [ ] **Object format**: Mach-O — section `__text` (prefixed with `__`); `isOSBinFormatMachO()` for Mach-O-specific paths
- [ ] **Syscall**: `svc #0x80`; X16 = syscall number (with `SyscallNumberMask = 0` for arm64, `= 0x2000000` for x86_64 Darwin), X0 = return value
- [ ] **Kernel mode**: `KernelImportABI::DarwinXNUKextShim` for kext symbol resolution
- [ ] **Build host**: Homebrew LLVM preferred; CMake auto-detects `/opt/homebrew/opt/llvm/bin`
- [ ] **Linker GC**: `-Wl,-dead_strip` (macOS-specific flag, not `--gc-sections`)
- [ ] **Code signing**: Developer ID signed + Apple notarized; no quarantine workaround needed
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
