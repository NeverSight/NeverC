**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index documentation](../README.fr.md) · [← Projet NeverC](../../README.fr.md)

# NeverC Compilateur shellcode

Compilez du C directement en shellcode binaire plat **indépendant de la position, sans relocalisation, sans section de données**.

---

## Objectifs principaux

1. **Écrivez du C normal** — aucune astuce spécifique au shellcode.
2. **Pipeline entièrement automatique** — `static int counter = 0`, `const char s[] = "..."`, récursion, `write/exit/read/...` et grands tableaux constants sont gérés en interne sans modifier le code utilisateur.
3. **Zéro dépendance externe** — le `.bin` est un flux d'instructions pur, sans référence à dyld, libSystem ni section de données.
4. **Options CLI via TableGen** — chaque `-fshellcode-*` est enregistré dans `neverc/include/neverc/Invoke/Options.td.h` (pas de correspondance de chaînes en dur). Typos → did-you-mean ; `--help` liste tout.
5. **Contraintes de sortie vérifiables** — `-fshellcode-bad-bytes=` / `-fshellcode-bad-byte-profile=` analysent le `.bin` final après le hook post-extract et rejettent la sortie en cas d'octet interdit, avec offset, octet et contexte.
6. **Pipeline unique multiplateforme** — piloté par la table `TargetDesc`. Une même source C pour macOS / Linux / Android / Windows. Nouvelle plateforme = une ligne de table + un extracteur, pas cinq jeux de passes.

---

## Cibles supportées

| Triple | Format | Syscall mode utilisateur | Résolveur Ring-0 | État |
|--------|--------|-------------------|-----------------|--------|
| `arm64-apple-macos*` | Mach-O | `svc #0x80` (Darwin BSD) | `DarwinXNUKextShim` | Round-trip loader natif + résolveur noyau couvert |
| `x86_64-apple-macos*` | Mach-O | `syscall` (masque BSD `0x2000000`) | `DarwinXNUKextShim` | Compilation + extraction OK ; x86_64 `__text` sans reloc attendue |
| `aarch64-linux-gnu` | ELF | `svc #0` (x8 = nr) | `LinuxKallsymsShim` | Compilation + extraction + résolveur noyau OK |
| `x86_64-linux-gnu` | ELF | `syscall` (rax = nr) | `LinuxKallsymsShim` | Compilation + extraction + résolveur noyau OK |
| `aarch64-linux-android*` | ELF | Identique Linux arm64 | `LinuxKallsymsShim` (GKI) | Compilation + extraction OK |
| `x86_64-linux-android*` | ELF | Identique Linux x86_64 | `LinuxKallsymsShim` (GKI) | Compilation + extraction OK |
| `aarch64-pc-windows-msvc` | PE/COFF | **Parcours PEB** (`ldr xN, [x18, #0x60]`) | `WindowsKernelResolverShim` | Sentinelle PEB `32 40 f9` validée ; ring-0 utilise le résolveur du loader |
| `x86_64-pc-windows-msvc` | PE/COFF | **Parcours modules PEB + table d'export PE** | `WindowsKernelResolverShim` | Résolveur mode utilisateur = parcours PEB IR complet ; ring-0 ne réutilise pas le PEB |

Les huit triples (OS, arch) partagent **le même ensemble de passes**. Les différences sont dans `TargetDesc.cpp` et trois branches d'extracteurs. Nouvelle plateforme = une ligne + un case par extracteur. `ExecutionLevel` est orthogonal : `User` → syscall / PEB ; `Kernel` désactive les deux et injecte `KernelImportPass` pour réécrire les appels externes via des shims. Voir [kernel-mode-shellcode.md](kernel-mode-shellcode/README.fr.md).

---

## Démarrage rapide

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

## Options CLI (toutes dans `Options.td.h`)

| Option | Description |
|--------|-------------|
| `-fshellcode` | Active le mode compilation shellcode. |
| `-fno-shellcode` | Annule un `-fshellcode` précédent. |
| `-fshellcode-all-blr` | Mode agressif : appels directs indirectisés en `blr xN` / `call *rax`, supprime toutes les relocs de branche relative. Inutile en usage normal. |
| `-mshellcode-syscall` | Active explicitement les stubs syscall (défaut avec `-fshellcode` sur Darwin/Linux/Android ; intention ou compat scripts). |
| `-mshellcode-libsystem` | Alias legacy Darwin de `-mshellcode-syscall`. |
| `-mshellcode-win-peb-import` | Active explicitement l'import PEB Windows (défaut avec `-fshellcode` + triple Windows). |
| `-fshellcode-keep-obj=<path>` | Copie l'objet intermédiaire vers `<path>` pour audit désassembleur natif. |
| `-fshellcode-entry=<name>` | Remplace le symbole d'entrée par défaut (`main`, `_main`, `shellcode_entry`, `_shellcode_entry`). |
| `-fshellcode-bad-bytes=<hex-list>` | Liste d'octets interdits séparés par des virgules. Scan du `.bin` final après post-extract ; échec sans fichier écrit si hit. |
| `-fshellcode-bad-byte-profile=<name>` | Profils intégrés : `null`, `c-string`, `http-newline`, `line`, `whitespace`, `ascii-control`. Combinable avec `-fshellcode-bad-bytes=`. |
| `-fshellcode-obfuscate=<spec>` | Vers les hooks d'obfuscation **niveau IR** (`ObfuscationHooks`). No-op sans bibliothèque liée. Voir [ir-pass-design.md §9 — Obfuscation Hooks](ir-pass-design/README.fr.md#9-obfuscation-hooks). |
| `-fshellcode-mir-obfuscate=<spec>` | Vers les hooks **niveau MIR** (`RunBeforePreEmit` / `RunAfterPreEmit`). Par défaut `-fshellcode-obfuscate=`. Voir [mir-pass-design.md §3 — User Obfuscation Hooks](mir-pass-design/README.fr.md#3-user-obfuscation-hooks). |

---

## Vue d'architecture

Le pipeline se divise en **passes IR indépendantes de la cible + extracteurs spécifiques** :

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

## Différences plateforme pilotées par table

`neverc/include/neverc/Shellcode/Pipeline/TargetDesc.h` définit `TargetDesc` pour chaque combinaison (OS, arch) :

- `TextSectionName`: Mach-O `__text` / ELF `.text` / COFF `.text`
- `SyscallABI`: enum value (`DarwinSvc80` / `LinuxSvc0` / `LinuxSyscall` / `WindowsPEB` / `None`)
- `AsmTemplate`: `svc #0x80` / `svc #0` / `syscall`
- `SyscallNumberReg`: x16 / x8 / rax
- `SyscallRetReg`: x0 / rax
- `ArgRegs`: ordered list of platform ABI argument registers + count
- `TCBReadAsm` / `TCBReadConstraint`: inline-asm single-instruction template for reading TEB/PEB pointer (Windows x86_64 = `movq %gs:0x60, $0`, Windows arm64 = `ldr $0, [x18, #0x60]`). `WinPEBImportPass` reads directly from the table.
- `DriverInjectFlags`: platform-specific driver flags as a null-terminated static array (x86_64 Unix gets `-fpic -mcmodel=small`; Windows gets `-mno-stack-arg-probe` / `/GS-`). `perTargetInjectFlags` reads from the table.

SyscallStubPass et WinPEBImportPass génèrent de l'InlineAsm depuis TargetDesc. Le backend utilise des motifs TableGen. Nouvelle cible = **une ligne** dans `describeTriple` et **un case** par extracteur.

## Couche extracteur

| Format | Implémentation | Relocations intra-section patchables |
|--------|---------------|-------------------------------------|
| Mach-O | `MachOExtractor.cpp` | arm64: `ARM64_RELOC_BRANCH26` / `PAGE21` / `PAGEOFF12`; x86_64: `X86_64_RELOC_SIGNED` / `SIGNED_1/2/4` / `BRANCH` (intra-`__text` pcrel32); `UNSIGNED` / `GOT_LOAD` / `GOT` / `SUBTRACTOR` / `TLV` rejected |
| ELF | `ELFExtractor.cpp` | arm64: `R_AARCH64_CALL26` / `JUMP26` / `ADR_PREL_PG_HI21(_NC)` / `ADD_ABS_LO12_NC` / `LDST{8,16,32,64,128}_ABS_LO12_NC` / `PREL32`; x86_64: `R_X86_64_PC32` / `PLT32` (`GOTPCREL` rejected) |
| COFF | `COFFExtractor.cpp` | arm64: `IMAGE_REL_ARM64_BRANCH26` / `PAGEBASE_REL21` / `PAGEOFFSET_12A` / `PAGEOFFSET_12L` / `REL32`; x86_64: `IMAGE_REL_AMD64_REL32` / `REL32_[1-5]` |

Tout autre type ou relocation inter-section échoue avec des indications (libc → stub syscall / `_Complex` → struct manuel / repli backend pool littéral, etc.).

---

## Matrice des capacités du code utilisateur

| Scénario | Code utilisateur | Pris en charge | Mécanisme |
|----------|-----------|-----------|-----------|
| Integer arithmetic / bitwise | `int f(int a) { return a*3+1; }` | Oui | Pure instruction stream |
| Recursion / loops | `int fib(int n) { ... }` | Oui | `static` + always_inline |
| `switch / case` | `switch (op) { case 0: ... }` | Oui | Driver injects `-fno-jump-tables` |
| Struct by-value passing | `struct Vec3 v = {...}; dot(v);` | Oui | Stack-ified + always_inline |
| Floating-point | `double y = x * 3.14;` | Oui | Data2Text rewrites ConstantFP to volatile-loaded bit pattern |
| Small constant arrays | `const int t[4] = {1,2,3,4};` | Oui | Data2Text stack-ifies |
| Large constant arrays (256B+) | `const unsigned char tbl[256] = {...}` | Oui | Data2Text, no size limit |
| String literals | `const char s[] = "hi\n";` | Oui | Data2Text stack-ifies |
| `memcpy` / `memset` / `memmove` / `memcmp` | `memcpy(dst, src, n);` | Oui | MemIntrinPass byte-loop wrappers |
| `strlen` / `strcpy` / `strcmp` / etc. | `strlen(buf);` | Oui | MemIntrinPass byte-loop wrappers |
| `__int128` division / modulo | `u128 q = a / b;` | Oui | CompilerRtPass inline long-division |
| `_Atomic` / `__atomic_*` / `__sync_*` | `__atomic_fetch_add(&c, 1, ...)` | Oui | Inline LDXR/STXR (arm64) / LOCK (x86_64) |
| `__builtin_*` family | `__builtin_popcount(x)` | Oui | Backend single-instruction selection |
| VLA / flexible array / compound literal | Normal C99/C11 | Oui | `-fno-jump-tables` + Data2Text |
| Mutable globals | `static int counter = 0;` | Oui | ZeroReloc stack-ifies |
| libc write/exit | `write(1, s, 3);` | Yes (with `-mshellcode-syscall`) | Syscall wrapper |
| POSIX includes | `#include <unistd.h>` | Yes (shellcode mode auto-switches to shim) | Driver injects `__NEVERC_SHELLCODE__` |
| Win32 API | `WriteFile(h, buf, n, &w, 0);` | Yes (with `-mshellcode-win-peb-import`) | PEB-walk thunk |
| Windows SDK includes | `#include <windows.h>` | Yes (shellcode mode auto-switches to shim) | Lightweight shim headers |
| Custom entry name | `int shellcode_main(...)` | Yes (with `-fshellcode-entry=...`) | Driver pass-through |
| Global constructors | `__attribute__((constructor))` | Non | No runtime to trigger them |
| TLS / thread_local | `thread_local int x;` | Auto-demoted to static | ZeroRelocPass.Prep silently demotes |
| C++ / ObjC | — | Non | Le projet est C uniquement |

---

## Structure des répertoires

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
├── README.md                                  ← Anglais
├── README.fr.md                               ← Français
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

## Prérequis (multiplateforme)

1. L'adresse de chargement doit être alignée sur 4 Ko — comportement naturel de `mmap` / `VirtualAlloc` ; les loaders respectent déjà cela.
2. Les conventions d'appel suivent l'ABI native de l'OS cible :
   - Darwin / Linux / Android: System V AMD64 or AAPCS64
   - Windows: Win64 (rcx/rdx/r8/r9)
3. Le loader gère le flush i-cache (arm64) / FlushInstructionCache (Windows).

## Extension de passes d'obfuscation (interface réservée)

Le pipeline shellcode garantit seulement que « le code s'exécute correctement ». L'obfuscation (CFF, faux flux, prédicats opaques, chiffrement de chaînes, substitution d'instructions, renommage de registres, etc.) est un travail séparé. `Pipeline.h` expose `ObfuscationHooks` avec **11 points d'accroche** sur trois couches :

**Niveau IR (6 hooks, `ModulePassManager &`)** :
- `RunBeforePrep` — Before any shellcode pass
- `RunAfterPrep` — Linkage unified (internal + always_inline)
- `RunBeforeInlining` — Last chance before AlwaysInliner
- `RunAfterInlining` — IR fully compressed into one large function
- `RunAfterStackify` — Final IR shape, next step is codegen
- `RunAfterFinalIR` — After AllBlrPass, the true last IR hook

**Niveau MIR (3 hooks, `TargetPassConfig &`)** :
- `RunBeforePreEmit` — Registers allocated, **CFI/EH pseudos still present**
- `RunAfterPreEmit` — **Built-in MIRPrepPass has stripped pseudos**, closest to the byte form AsmPrinter will see; ideal for instruction-level obfuscation/register renaming
- `RunAfterFinalMIR` — True last MIR hook, after LLVM `addPreEmitPass2()`, just before AsmPrinter

**Niveau flux d'octets (3 hooks, `SmallVectorImpl<uint8_t> &`)** :
- `RunPostExtract` — After extractor completes intra-text relocation patching and data-section audit; before `.bin` is written. Use for whole-payload encryption, junk byte insertion, or custom headers.
- `RunPostFinalize` — After all finalize steps; NeverC performs no further auditing.

`-fshellcode-obfuscate=<spec>` and `-fshellcode-mir-obfuscate=<spec>` pass strings through to `ShellcodeOptions::ObfuscateSpec` / `MirObfuscateSpec`. MIR spec defaults to the IR spec. The pipeline does not parse the content — the obfuscation library defines its own DSL. Details:

- IR-level: [ir-pass-design.md §9 — Obfuscation Hooks](ir-pass-design/README.fr.md#9-obfuscation-hooks).
- MIR-level: [mir-pass-design.md §3 — User Obfuscation Hooks](mir-pass-design/README.fr.md#3-user-obfuscation-hooks)
---

## Limitations actuelles

- **Supports 8 (OS, arch) combinations** (see matrix above). Other triples (RISC-V, PowerPC, 32-bit x86, big-endian ARM, etc.) are rejected at `describeTriple()` with the full supported set listed as a hint. Each (OS, arch) row has independent `User` / `Kernel` contexts, yielding 16 (OS, arch, level) variants.
- **Windows PEB walk is fully implemented with multi-DLL dispatch**. `__neverc_win_resolve` accepts `(dll_hash, api_hash)` pairs. The current whitelist covers kernel32.dll (~110 APIs), ntdll.dll (~26), user32.dll (~13), ws2_32.dll (~23), advapi32.dll (~16), shell32.dll (~6). Adding an API = one row in `WinImportTables.cpp` + one declaration in `lib/Headers/windows.h`.
- **External function whitelist** only covers Darwin BSD / Linux / Android common syscalls (~80+) + Win32 APIs (~190). stdio and similar runtime-heavy interfaces are not included — shellcode cannot embed the full stdio state machine.
- Pas de C++ / ObjC / CUDA — NeverC est C uniquement par conception.
