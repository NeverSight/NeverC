**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.de.md)

# NeverC Shellcode-Compiler

Kompiliert C-Quellcode direkt in **positionsunabhängigen, relocationsfreien, datensektionslosen** flachen Binär-Shellcode.

---

## Kernziele

1. **Normales C schreiben** — keine shellcode-spezifischen Tricks.
2. **Vollautomatische Pipeline** — `static int counter = 0`, `const char s[] = "..."`, Rekursion, `write/exit/read/...` und große Konstantenarrays werden intern ohne Änderungen am Benutzercode verarbeitet.
3. **Keine externen Abhängigkeiten** — die Ausgabe `.bin` ist reiner Instruktionsstrom ohne dyld, libSystem oder Datensektion.
4. **CLI-Optionen per TableGen** — jede `-fshellcode-*` in `neverc/include/neverc/Invoke/Options.td.h` (kein Hardcoding). Tippfehler → did-you-mean ; `--help` listet alle Optionen.
5. **Ausgabe-Constraints prüfbar** — `-fshellcode-bad-bytes=` / `-fshellcode-bad-byte-profile=` scannen die finale `.bin` nach post-extract und lehnen bei verbotenen Bytes ab (Offset, Byte, Kontext).
6. **Plattformübergreifende einzelne Pipeline** — gesteuert durch `TargetDesc`. Gleiche C-Quelle für macOS / Linux / Android / Windows. Neue Plattform = eine Tabellenzeile + ein Extraktor, nicht fünf Pass-Sätze.

---

## Unterstützte Ziele

| Triple | Format | User-Mode-Syscall | Ring-0-Resolver | Status |
|--------|--------|-------------------|-----------------|--------|
| `arm64-apple-macos*` | Mach-O | `svc #0x80` (Darwin BSD) | `DarwinXNUKextShim` | Nativer Loader Round-Trip + Kernel-Resolver abgedeckt |
| `x86_64-apple-macos*` | Mach-O | `syscall` (BSD-Maske `0x2000000`) | `DarwinXNUKextShim` | Kompilieren + Extraktion OK; x86_64 `__text` ohne Reloc-Erwartung |
| `aarch64-linux-gnu` | ELF | `svc #0` (x8 = nr) | `LinuxKallsymsShim` | Kompilieren + Extraktion + Kernel-Resolver OK |
| `x86_64-linux-gnu` | ELF | `syscall` (rax = nr) | `LinuxKallsymsShim` | Kompilieren + Extraktion + Kernel-Resolver OK |
| `aarch64-linux-android*` | ELF | Wie Linux arm64 | `LinuxKallsymsShim` (GKI) | Kompilieren + Extraktion OK |
| `x86_64-linux-android*` | ELF | Wie Linux x86_64 | `LinuxKallsymsShim` (GKI) | Kompilieren + Extraktion OK |
| `aarch64-pc-windows-msvc` | PE/COFF | **PEB-Walk** (`ldr xN, [x18, #0x60]`) | `WindowsKernelResolverShim` | User-Mode PEB-Byte `32 40 f9` validiert; Ring-0 nutzt Loader-Resolver |
| `x86_64-pc-windows-msvc` | PE/COFF | **PEB-Modul-Walk + PE-Exporttabelle** | `WindowsKernelResolverShim` | User-Mode = vollständiger IR-PEB-Walk; Ring-0 nutzt PEB nicht erneut |

Alle acht (OS, arch)-Triples nutzen **dieselbe Pass-Menge**. Unterschiede in `TargetDesc.cpp` und drei Extraktor-Zweigen. Neue Plattform = eine Zeile + ein Case pro Extraktor. `ExecutionLevel` orthogonal: `User` → Syscall/PEB ; `Kernel` deaktiviert beides und injiziert `KernelImportPass` für extern-Aufrufe über Resolver-Shims. Siehe [kernel-mode-shellcode.md](kernel-mode-shellcode/README.de.md).

---

## Schnellstart

```bash
# Always pass -target — output triple is independent of the compiler host.

# 1) Pure computation shellcode — no system calls
neverc -fshellcode -target arm64-apple-macos add.c -o add.bin

# 2) Darwin hello world — write/exit → svc #0x80
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# 3) Linux arm64: svc #0 + x8=nr
neverc -fshellcode -target aarch64-linux-gnu -mshellcode-syscall \
       hello.c -o hello_linux_arm64.bin

# 4) Linux x86_64: syscall + rax=nr
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall \
       hello.c -o hello_linux_x64.bin

# 5) Windows x86_64 (PEB walk for API calls)
neverc -fshellcode -target x86_64-pc-windows-msvc \
       -mshellcode-win-peb-import win.c -o win.bin

# 6) Custom entry symbol
neverc -fshellcode -target arm64-apple-macos -fshellcode-entry=shellcode_main kernel.c -o k.bin

# 7) Keep intermediate object for audit (otool / llvm-objdump / dumpbin)
neverc -fshellcode -target arm64-apple-macos -fshellcode-keep-obj=/tmp/dump.obj x.c -o x.bin

# 8) Reject forbidden bytes in final .bin
neverc -fshellcode -target arm64-apple-macos -fshellcode-bad-bytes=00,0a,0d x.c -o x.bin

# 9) Built-in bad-byte profile (same as forbidding 00/0a/0d)
neverc -fshellcode -target arm64-apple-macos -fshellcode-bad-byte-profile=http-newline x.c -o x.bin

# 10) Run on macOS (platform-specific loader)
./loader_arm64_macos add.bin 3 4   # exit code = 7

# 11) Verbose extractor summary
neverc -v -fshellcode -target arm64-apple-macos fib.c -o fib.bin
#   shellcode-extractor: wrote 64 bytes to 'fib.bin'
#   shellcode-extractor: target   = arm64-apple-macos (Mach-O)
#   shellcode-extractor: entry symbol = _main
#   shellcode-extractor: patched 1 BRANCH26, 0 PAGE21, 0 PAGEOFF12 intra-section reloc(s)
```

---

## CLI-Optionen (alle in `Options.td.h`)

| Option | Beschreibung |
|--------|-------------|
| `-fshellcode` | Shellcode-Kompilierungsmodus aktivieren. |
| `-fno-shellcode` | Vorheriges `-fshellcode` aufheben. |
| `-fshellcode-all-blr` | Aggressiv: direkte Aufrufe zu `blr xN` / `call *rax` indirektisieren, alle relativen Branch-Relocs entfernen. Normal nicht nötig. |
| `-mshellcode-syscall` | Syscall-Stubs explizit (Standard unter `-fshellcode` für Darwin/Linux/Android; Absicht/Skript-Kompat). |
| `-mshellcode-libsystem` | Darwin-Legacy-Alias für `-mshellcode-syscall`. |
| `-mshellcode-win-peb-import` | Windows-PEB-Import explizit (Standard mit `-fshellcode` + Windows-Triple). |
| `-fshellcode-keep-obj=<path>` | Zwischenobjekt nach `<path>` kopieren für natives Disassembler-Audit. |
| `-fshellcode-entry=<name>` | Standard-Einstieg überschreiben (`main`, `_main`, `shellcode_entry`, `_shellcode_entry`). |
| `-fshellcode-bad-bytes=<hex-list>` | Verbotene Bytes (kommagetrennt). Scan der finalen `.bin` nach post-extract; bei Treffer kein File. |
| `-fshellcode-bad-byte-profile=<name>` | Profile: `null`, `c-string`, `http-newline`, `line`, `whitespace`, `ascii-control`. Mit `-fshellcode-bad-bytes=` kombinierbar. |
| `-fshellcode-obfuscate=<spec>` | An **IR-Level**-Obfuskations-Hooks (`ObfuscationHooks`). No-op ohne Bibliothek. Siehe [ir-pass-design.md §9 — Obfuscation Hooks](ir-pass-design/README.de.md#9-obfuscation-hooks). |
| `-fshellcode-mir-obfuscate=<spec>` | An **MIR-Level**-Hooks (`RunBeforePreEmit` / `RunAfterPreEmit`). Fallback `-fshellcode-obfuscate=`. Siehe [mir-pass-design.md §3 — User Obfuscation Hooks](mir-pass-design/README.de.md#3-user-obfuscation-hooks). |

---

## Architekturüberblick

Die Pipeline teilt sich in **zielunabhängige IR-Passes + zielspezifische Extraktoren**:

```mermaid
flowchart TD
    Driver["neverc -fshellcode · OptTable + Options.td.h"]
    Frontend["C23 Frontend · PIC default"]
    Driver -->|describeTriple| Frontend
    Frontend -->|LLVM IR| ZRP

    subgraph IR["Target-Independent IR Passes"]
        direction TB
        ZRP["① ZeroRelocPass — Prep\ninternal + always_inline\nreject ctors / thread_local / extern_weak"]
        IBP["② IndirectBrPass\ncomputed-goto → switch"]
        SSP["③ SyscallStubPass\nlibc → svc #0x80 / svc #0 / syscall"]
        WPP["④ WinPEBImportPass\nextern Win32 API → PEB-walk thunk"]
        MIP["⑤ MemIntrinPass\nmemcpy/memset/str* → byte-loop"]
        CRP["⑥ CompilerRtPass\ni128 div/mod → inline long-division"]
        D2T1["⑦ Data2TextPass — Phase 1\nconst GV → stack stores"]
        ZRP --> IBP --> SSP --> WPP --> MIP --> CRP --> D2T1
    end

    Backend["AArch64 / X86 Backend\nSROA · InstCombine · AlwaysInliner · SLP"]
    D2T1 --> Backend

    Backend --> D2T2
    subgraph Post["Post-Backend IR"]
        direction TB
        D2T2["⑧ Data2TextPass — Phase 2\nvector const split"]
        ZRS["⑨ ZeroRelocPass — Stackify\nglobals → entry alloca"]
        ABP["⑩ AllBlrPass (optional)\ndirect call → indirect call"]
        D2T2 --> ZRS --> ABP
    end

    Codegen["Codegen · IR → MIR → Register Allocation"]
    ABP --> Codegen

    Codegen --> MH1
    subgraph MIR["MIR Layer"]
        direction TB
        MH1["⑪ RunBeforePreEmit hook"]
        MIRP["⑫ ShellcodeMIRPrepPass\nstrip CFI / EH_LABEL / XRay / StackMap"]
        MH2["⑬ RunAfterPreEmit hook\ninstruction-level obfuscation entry"]
        MH1 --> MIRP --> MH2
    end

    MH2 -->|"Mach-O / ELF / COFF .o"| Extractor

    subgraph Extract["Extractor Layer"]
        Extractor["ShellcodeExtractor\nMachO · ELF · COFF\npatch intra-.text relocs\nreject external reloc / data section\nbad-byte audit"]
    end

    Extractor --> Output(["flat .bin shellcode"])
```

## Tabellengesteuerte Plattformunterschiede

`neverc/include/neverc/Shellcode/Pipeline/TargetDesc.h` definiert `TargetDesc` pro (OS, arch):

- `TextSectionName`: Mach-O `__text` / ELF `.text` / COFF `.text`
- `SyscallABI`: enum value (`DarwinSvc80` / `LinuxSvc0` / `LinuxSyscall` / `WindowsPEB` / `None`)
- `AsmTemplate`: `svc #0x80` / `svc #0` / `syscall`
- `SyscallNumberReg`: x16 / x8 / rax
- `SyscallRetReg`: x0 / rax
- `ArgRegs`: ordered list of platform ABI argument registers + count
- `TCBReadAsm` / `TCBReadConstraint`: inline-asm single-instruction template for reading TEB/PEB pointer (Windows x86_64 = `movq %gs:0x60, $0`, Windows arm64 = `ldr $0, [x18, #0x60]`). `WinPEBImportPass` reads directly from the table.
- `DriverInjectFlags`: platform-specific driver flags as a null-terminated static array (x86_64 Unix gets `-fpic -mcmodel=small`; Windows gets `-mno-stack-arg-probe` / `/GS-`). `perTargetInjectFlags` reads from the table.

SyscallStubPass und WinPEBImportPass erzeugen InlineAsm aus TargetDesc. Das Backend nutzt TableGen-Muster. Neues Ziel = **eine Zeile** in `describeTriple` und **ein Case** pro Extraktor.

## Extraktorschicht

| Format | Implementierung | Patchbare Intra-Section-Relocations |
|--------|---------------|-------------------------------------|
| Mach-O | `MachOExtractor.cpp` | arm64: `ARM64_RELOC_BRANCH26` / `PAGE21` / `PAGEOFF12`; x86_64: `X86_64_RELOC_SIGNED` / `SIGNED_1/2/4` / `BRANCH` (intra-`__text` pcrel32); `UNSIGNED` / `GOT_LOAD` / `GOT` / `SUBTRACTOR` / `TLV` rejected |
| ELF | `ELFExtractor.cpp` | arm64: `R_AARCH64_CALL26` / `JUMP26` / `ADR_PREL_PG_HI21(_NC)` / `ADD_ABS_LO12_NC` / `LDST{8,16,32,64,128}_ABS_LO12_NC` / `PREL32`; x86_64: `R_X86_64_PC32` / `PLT32` (`GOTPCREL` rejected) |
| COFF | `COFFExtractor.cpp` | arm64: `IMAGE_REL_ARM64_BRANCH26` / `PAGEBASE_REL21` / `PAGEOFFSET_12A` / `PAGEOFFSET_12L` / `REL32`; x86_64: `IMAGE_REL_AMD64_REL32` / `REL32_[1-5]` |

Andere Typen oder Cross-Section-Relocations sind Hard-Fail mit Hinweisen (libc → Syscall-Stub / `_Complex` → manuelles Struct / Literal-Pool-Fallback usw.).

---

## Matrix der Benutzercode-Fähigkeiten

| Szenario | Benutzercode | Unterstützt | Mechanismus |
|----------|-----------|-----------|-----------|
| Integer arithmetic / bitwise | `int f(int a) { return a*3+1; }` | Ja | Pure instruction stream |
| Recursion / loops | `int fib(int n) { ... }` | Ja | `static` + always_inline |
| `switch / case` | `switch (op) { case 0: ... }` | Ja | Driver injects `-fno-jump-tables` |
| Struct by-value passing | `struct Vec3 v = {...}; dot(v);` | Ja | Stack-ified + always_inline |
| Floating-point | `double y = x * 3.14;` | Ja | Data2Text rewrites ConstantFP to volatile-loaded bit pattern |
| Small constant arrays | `const int t[4] = {1,2,3,4};` | Ja | Data2Text stack-ifies |
| Large constant arrays (256B+) | `const unsigned char tbl[256] = {...}` | Ja | Data2Text, no size limit |
| String literals | `const char s[] = "hi\n";` | Ja | Data2Text stack-ifies |
| `memcpy` / `memset` / `memmove` / `memcmp` | `memcpy(dst, src, n);` | Ja | MemIntrinPass byte-loop wrappers |
| `strlen` / `strcpy` / `strcmp` / etc. | `strlen(buf);` | Ja | MemIntrinPass byte-loop wrappers |
| `__int128` division / modulo | `u128 q = a / b;` | Ja | CompilerRtPass inline long-division |
| `_Atomic` / `__atomic_*` / `__sync_*` | `__atomic_fetch_add(&c, 1, ...)` | Ja | Inline LDXR/STXR (arm64) / LOCK (x86_64) |
| `__builtin_*` family | `__builtin_popcount(x)` | Ja | Backend single-instruction selection |
| VLA / flexible array / compound literal | Normal C99/C11 | Ja | `-fno-jump-tables` + Data2Text |
| Mutable globals | `static int counter = 0;` | Ja | ZeroReloc stack-ifies |
| libc write/exit | `write(1, s, 3);` | Yes (with `-mshellcode-syscall`) | Syscall wrapper |
| POSIX includes | `#include <unistd.h>` | Yes (shellcode mode auto-switches to shim) | Driver injects `__NEVERC_SHELLCODE__` |
| Win32 API | `WriteFile(h, buf, n, &w, 0);` | Yes (with `-mshellcode-win-peb-import`) | PEB-walk thunk |
| Windows SDK includes | `#include <windows.h>` | Yes (shellcode mode auto-switches to shim) | Lightweight shim headers |
| Custom entry name | `int shellcode_main(...)` | Yes (with `-fshellcode-entry=...`) | Driver pass-through |
| Global constructors | `__attribute__((constructor))` | Nein | No runtime to trigger them |
| TLS / thread_local | `thread_local int x;` | Auto-demoted to static | ZeroRelocPass.Prep silently demotes |
| C++ / ObjC | — | Nein | Projektumfang nur C |

---

## Verzeichnisstruktur

```
neverc/
├── include/neverc/Invoke/Options.td.h           # -fshellcode-* TableGen definitions
├── include/neverc/Shellcode/                  # Headers (organized by subsystem)
│   ├── Pipeline/                              # Pipeline / driver integration
│   │   ├── Pipeline.h                         # IR + MIR hook registration
│   │   ├── Plugin.h                           # Plugin SDK (bad-byte / charset)
│   │   ├── DriverIntegration.h
│   │   ├── TargetDesc.h                       # Platform table / descriptors
│   │   ├── ShellcodeOptions.h                 # Cross-subsystem config
│   │   ├── Diagnostics.h                      # Cross-subsystem diagnostics
│   │   └── SymbolNames.h                      # Cross-subsystem symbol utilities
│   ├── Extractor/
│   │   └── ShellcodeExtractor.h
│   ├── IR/                                    # IR-level passes and ABIs
│   │   ├── ZeroRelocPass.h / ZeroRelocABI.h
│   │   ├── Data2TextPass.h
│   │   ├── AllBlrPass.h / IndirectBrPass.h
│   │   ├── MemIntrinPass.h                    # memcpy/memset/str* inlining
│   │   ├── StringRuntimePass.h / StringRuntimeABI.h
│   │   └── CompilerRtPass.h                   # __int128 division inline
│   ├── MIR/
│   │   └── MIRPrepPass.h                      # Catch-all MachineFunctionPass
│   ├── Import/                                # User-mode + kernel-mode import resolution
│   │   ├── SyscallStub.h / SyscallTables.h
│   │   ├── WinPEBImport.h / WinImportTables.h
│   │   ├── KernelImportPass.h / KernelImportABI.h
│   └── Tables/                                # User-extensible .def tables
├── lib/Shellcode/                             # Implementation (mirrors header structure)
│   ├── Pipeline/ Extractor/ IR/ MIR/ Import/
└── lib/Invoke/Core/Driver.cpp

tests/neverc/                                   # Tests (GTest)
├── ShellcodeTests.cpp                         # Core shellcode round-trip tests
├── ShellcodeStressTests.cpp                   # Stress tests (VLA, __sync_*, __int128, etc.)
├── ShellcodeCrossTargetTests.cpp              # Cross-target compile-only smoke tests
├── shellcode/
│   ├── loader_arm64_macos.c / loader_linux.c / loader_windows.c
│   └── test_shellcode_*.c

docs/shellcode-compiler/
├── README.md                                  ← Englisch
├── README.de.md                               ← Deutsch
├── arm64-assembly-tutorial/README.md
├── cross-platform-architecture/README.md
├── ir-pass-design/README.md
├── kernel-mode-shellcode/README.md
├── mir-pass-design/README.md
├── pipeline-and-pic/README.md
├── platform-extension-guide/README.md
├── plugin-interface/README.md
├── progress/README.md
└── roadmap/README.md
```

---

## Voraussetzungen (plattformübergreifend)

1. Ladeadresse 4 KB ausgerichtet — natürliches Verhalten von `mmap` / `VirtualAlloc` ; Loader erfüllen dies bereits.
2. Aufrufkonventionen folgen der nativen ABI des Ziel-OS:
   - Darwin / Linux / Android: System V AMD64 or AAPCS64
   - Windows: Win64 (rcx/rdx/r8/r9)
3. Loader verantwortlich für i-cache-Flush (arm64) / FlushInstructionCache (Windows).

## Obfuskations-Pass-Erweiterung (reservierte Schnittstelle)

Die Shellcode-Pipeline stellt nur sicher, dass „der Code korrekt läuft“. Obfuskation (CFF, bogus CF, opaque predicates, Stringverschlüsselung, Instruktionsersatz, Registerumbenennung usw.) ist separate Arbeit. `Pipeline.h` exponiert `ObfuscationHooks` mit **11 Hooks** auf drei Ebenen:

**IR-Ebene (6 Hooks, `ModulePassManager &`)**:
- `RunBeforePrep` — Before any shellcode pass
- `RunAfterPrep` — Linkage unified (internal + always_inline)
- `RunBeforeInlining` — Last chance before AlwaysInliner
- `RunAfterInlining` — IR fully compressed into one large function
- `RunAfterStackify` — Final IR shape, next step is codegen
- `RunAfterFinalIR` — After AllBlrPass, the true last IR hook

**MIR-Ebene (3 Hooks, `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Registers allocated, **CFI/EH pseudos still present**
- `RunAfterPreEmit` — **Built-in MIRPrepPass has stripped pseudos**, closest to the byte form AsmPrinter will see; ideal for instruction-level obfuscation/register renaming
- `RunAfterFinalMIR` — True last MIR hook, after LLVM `addPreEmitPass2()`, just before AsmPrinter

**Byte-Stream-Ebene (3 Hooks, `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — After extractor completes intra-text relocation patching and data-section audit; before `.bin` is written. Use for whole-payload encryption, junk byte insertion, or custom headers.
- `RunPostFinalize` — After all finalize steps; NeverC performs no further auditing.

`-fshellcode-obfuscate=<spec>` and `-fshellcode-mir-obfuscate=<spec>` pass strings through to `ShellcodeOptions::ObfuscateSpec` / `MirObfuscateSpec`. MIR spec defaults to the IR spec. The pipeline does not parse the content — the obfuscation library defines its own DSL. Details:

- IR-level: [ir-pass-design.md §9 — Obfuscation Hooks](ir-pass-design/README.de.md#9-obfuscation-hooks).
- MIR-level: [mir-pass-design.md §3 — User Obfuscation Hooks](mir-pass-design/README.de.md#3-user-obfuscation-hooks)
---

## Aktuelle Einschränkungen

- **Supports 8 (OS, arch) combinations** (see matrix above). Other triples (RISC-V, PowerPC, 32-bit x86, big-endian ARM, etc.) are rejected at `describeTriple()` with the full supported set listed as a hint. Each (OS, arch) row has independent `User` / `Kernel` contexts, yielding 16 (OS, arch, level) variants.
- **Windows PEB walk is fully implemented with multi-DLL dispatch**. `__neverc_win_resolve` accepts `(dll_hash, api_hash)` pairs. The current whitelist covers kernel32.dll (~110 APIs), ntdll.dll (~26), user32.dll (~13), ws2_32.dll (~23), advapi32.dll (~16), shell32.dll (~6). Adding an API = one row in `WinImportTables.cpp` + one declaration in `lib/Headers/windows.h`.
- **External function whitelist** only covers Darwin BSD / Linux / Android common syscalls (~80+) + Win32 APIs (~190). stdio and similar runtime-heavy interfaces are not included — shellcode cannot embed the full stdio state machine.
- Kein C++ / ObjC / CUDA — NeverC ist bewusst nur C.
