**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# Shellcode-Compiler — Fortschrittsverfolgung

## Phase 0 — macOS arm64 MVP (ausgeliefert)

- [x] Verzeichnis- & CMake-Skelett (`nevercShellcode`-Bibliothek)
- [x] `ZeroRelocPass`: Zwei-Phasen (Prep + Stackify), automatische Stack-Übernahme veränderlicher Globaler
- [x] `Data2TextPass`: Zwei-Phasen (konstante Arrays → Stack-Chunk-Stores; Post-SROA-Vektorkonstanten-Splitting; ConstantFP → volatile-geladene Bitmuster)
- [x] `SyscallStubPass`: Tabellengesteuerte Whitelist für Darwin BSD / Linux arm64 / Linux x86_64 / Android-Syscalls
- [x] `AllBlrPass`: Optionales aggressives Umschreiben indirekter Aufrufe
- [x] `ShellcodeExtractor`: Mach-O `.o` → flache `.bin` mit Intra-Section-Relokationspatching
- [x] CLI-Optionen via generierte `neverc/include/neverc/Invoke/Options.td.h`: `-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`
- [x] Plattformübergreifendes Standard-PIC (`isPICDefault()` gibt einheitlich `true` zurück)
- [x] Generische rekursive Stack-Übernahme (Funktionszeigertabellen, Stringzeigertabellen, verschachtelte Strukturtabellen, ConstantExpr GEP/BitCast-Initialisierer)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch, einschließlich Multi-Dispatch-Site-Tabellenfreigabe
- [x] SIMD-Vektorkonstanten-Inlining (`inlineVectorConstants`)
- [x] `_Thread_local` automatische Herabstufung zu static
- [x] Nativer macOS arm64 Loader (MAP_JIT + i-cache flush)

**Tests**: 108/108 Shellcode-Assertions bestanden. Binärgrößen: `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## Phase 1 — Linux / Android / Windows plattformübergreifend (ausgeliefert)

- [x] `TargetDesc`-Abstraktion: Tabellengesteuerte Plattformunterschiede
- [x] Plattformübergreifende `-mshellcode-syscall`-Semantik (ersetzt Darwin-only `-mshellcode-libsystem`)
- [x] Linux / Android Syscall-Nummerntabellen (Darwin BSD 100+, Linux arm64 130+, Linux x86_64 150+)
- [x] `ShellcodeExtractor` refaktoriert in `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] ELF-Extraktor (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/etc.; x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] COFF-Extraktor (arm64: `IMAGE_REL_ARM64_BRANCH26`/etc.; x86_64: `IMAGE_REL_AMD64_REL32`/etc.)
- [x] Windows PEB Import Pass (`WinPEBImportPass`) mit echtem PEB-Walk-Resolver
- [x] Multi-DLL Win32 API Whitelist (~210 APIs über kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/etc. → Inline-Byteschleifenhelfer
- [x] `CompilerRtPass`: `__int128` Division/Modulo → Inline-Langdivisionshelfer
- [x] Windows `aarch64-pc-windows-msvc` Frontend-Unterstützung
- [x] `MIRPrepPass`: Plattformübergreifendes Pseudo-Stripping (CFI/EH/XRay/StackMap/SEH/FENTRY/etc.)
- [x] MIR + Byte-Level-Obfuskationshooks (11 Hooks über IR/MIR/Byte-Stream-Schichten)
- [x] AArch64 Nicht-Darwin `long double` automatisches Downgrade auf binary64
- [x] Shellcode-Shim-Header: `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Windows POSIX-Kompatibilitätsschicht (13 POSIX→Win32-Brücken: write→WriteFile, mmap→VirtualAlloc, etc.)
- [x] K&R implizite Deklaration Auto-Fix (50+ kanonische POSIX-Signaturen)
- [x] Tabellengesteuerte Bereinigung (Architekturzweig-Hardcodierung → null)
- [x] `KernelImportPass`: Ring-0 automatisches resolvergestütztes Callsite-Umschreiben
- [x] Kernel-Helpername tabellengesteuerte Diagnostik (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` für Ring-0-Eintrittspunktkonventionen
- [x] Einstiegspunkt-Offset-Null-Erzwingung (`placeEntryFirst`)
- [x] Finalize-Pipeline: Bad-Byte-Rewriter SDK + Charset-Encoder SDK + Größenbeschränkungen
- [x] Plugin SDK (`Plugin.h`): `registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] x86_64 `-mno-implicit-float`-Injektion (verhindert Backend-SSE-Konstantenpoolauslagerung)
- [x] Plattformübergreifende Loader (macOS/Linux/Windows)

**Tests**: 743+ Shellcode-Assertions, alle bestanden über 8 Triples. Gesamte NeverC-Suite: 1000+ Tests bestanden.

## Phase 2 — Druckbare / Alphanumerische Encoder (geplant)

- [ ] ARM64 druckbarer Shellcode-Encoder (0x20–0x7e Instruktionssubset)
- [ ] x86_64 alphanumerischer Encoder
- [ ] Selbstdekodierender Stub (Decoder Stub) Erzeugung
- [ ] Post-Encoding Größen- / Entropiestatistiken

## Phase 3 — Polymorphismus / Selbstmodifikation (geplant)

- [ ] Polymorphe Engine: gleicher Quellcode → unterschiedliche äquivalente Bytefolgen pro Kompilierung
- [ ] Selbstmodifizierender Code: Laufzeitentschlüsselung / -dekompression des Payload-Bodys
- [ ] Anti-Erkennung: Vermeidung bekannter Shellcode-Signaturmuster

## Zukünftige Erweiterungen

- [ ] iOS arm64 (Code-Signierung + JIT Jailbreak-Szenarien)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
