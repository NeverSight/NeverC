**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# Shellcode Compiler — Progress Tracker

## Stage 0 — macOS arm64 MVP (delivered)

- [x] Directory & CMake skeleton (`nevercShellcode` library)
- [x] `ZeroRelocPass`: two-phase (Prep + Stackify), automatic stack-ification of mutable globals
- [x] `Data2TextPass`: two-phase (constant arrays → stack chunk stores; post-SROA vector constant splitting; ConstantFP → volatile-loaded bit patterns)
- [x] `SyscallStubPass`: table-driven whitelist covering Darwin BSD / Linux arm64 / Linux x86_64 / Android syscalls
- [x] `AllBlrPass`: optional aggressive indirect call rewriting
- [x] `ShellcodeExtractor`: Mach-O `.o` → flat `.bin` with intra-section relocation patching
- [x] CLI options via generated `neverc/include/neverc/Invoke/Options.td.h` (`-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`)
- [x] All-platform default PIC (`isPICDefault()` returns `true` universally)
- [x] Generic recursive stack-ification (function pointer tables, string pointer tables, nested struct tables, ConstantExpr GEP/BitCast initializers)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch, including multi-dispatch-site table sharing
- [x] SIMD vector constant inlining (`inlineVectorConstants`)
- [x] `_Thread_local` auto-demotion to static
- [x] Native macOS arm64 loader (MAP_JIT + i-cache flush)

**Tests**: 108/108 shellcode assertions passing. Binary sizes: `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## Stage 1 — Linux / Android / Windows cross-platform (delivered)

- [x] `TargetDesc` abstraction: table-driven platform differences
- [x] Cross-platform `-mshellcode-syscall` semantics (replaces Darwin-only `-mshellcode-libsystem`)
- [x] Linux / Android syscall number tables (Darwin BSD 80+, Linux arm64 60+, Linux x86_64 90+)
- [x] `ShellcodeExtractor` refactored into `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] ELF extractor (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/etc.; x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] COFF extractor (arm64: `IMAGE_REL_ARM64_BRANCH26`/etc.; x86_64: `IMAGE_REL_AMD64_REL32`/etc.)
- [x] Windows PEB import pass (`WinPEBImportPass`) with real PEB walk resolver
- [x] Multi-DLL Win32 API whitelist (~190 APIs across kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/etc. → inline byte-loop helpers
- [x] `CompilerRtPass`: `__int128` division/modulo → inline long-division helpers
- [x] Windows `aarch64-pc-windows-msvc` frontend support
- [x] `MIRPrepPass`: cross-platform pseudo stripping (CFI/EH/XRay/StackMap/SEH/FENTRY/etc.)
- [x] MIR + byte-level obfuscation hooks (11 hooks across IR/MIR/byte-stream layers)
- [x] AArch64 non-Darwin `long double` auto-downgrade to binary64
- [x] Shellcode shim headers: `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Windows POSIX compatibility layer (13 POSIX→Win32 bridges: write→WriteFile, mmap→VirtualAlloc, etc.)
- [x] K&R implicit declaration auto-fix (50+ canonical POSIX signatures)
- [x] Table-driven purification (arch branch hardcoding → zero)
- [x] `KernelImportPass`: ring-0 automatic resolver-backed callsite rewriting
- [x] Kernel helper name table-driven diagnostics (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` for ring-0 entry conventions
- [x] Entry offset zero enforcement (`placeEntryFirst`)
- [x] Finalize pipeline: bad-byte rewriter SDK + charset encoder SDK + sizing constraints
- [x] Plugin SDK (`Plugin.h`): `registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] x86_64 `-mno-implicit-float` injection (prevents backend SSE constant pool spills)
- [x] Cross-platform loaders (macOS/Linux/Windows)

**Tests**: 743+ shellcode assertions, all passing across 8 triples. Overall neverc suite: 1000+ tests passing.

## Stage 2 — Printable / Alphanumeric Encoder (planned)

- [ ] ARM64 printable shellcode encoder (0x20–0x7e instruction subset)
- [ ] x86_64 alphanumeric encoder
- [ ] Self-decoding stub (decoder stub) generation
- [ ] Post-encoding size / entropy statistics

## Stage 3 — Polymorphism / Self-Modifying (planned)

- [ ] Polymorphic engine: same source → different equivalent byte sequences per compilation
- [ ] Self-modifying code: runtime decryption / decompression of payload body
- [ ] Anti-detection: avoid known shellcode signature patterns

## Future Extensions

- [ ] iOS arm64 (code signing + JIT jailbreak scenarios)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
