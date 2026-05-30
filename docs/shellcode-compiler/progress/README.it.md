**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Compilatore shellcode — Tracciamento progressi

## Fase 0 — macOS arm64 MVP (consegnato)

- [x] Scheletro directory e CMake (libreria `nevercShellcode`)
- [x] `ZeroRelocPass`: due fasi (Prep + Stackify), impilamento automatico dei globali mutabili
- [x] `Data2TextPass`: due fasi (array costanti → store a blocchi sullo stack; splitting costanti vettoriali post-SROA; ConstantFP → pattern di bit caricati con volatile)
- [x] `SyscallStubPass`: whitelist guidata da tabella per Darwin BSD / Linux arm64 / Linux x86_64 / Android syscall
- [x] `AllBlrPass`: riscrittura aggressiva opzionale delle chiamate indirette
- [x] `ShellcodeExtractor`: Mach-O `.o` → `.bin` piatto con patching delle rilocazioni intra-sezione
- [x] Opzioni CLI via `neverc/include/neverc/Invoke/Options.td.h` generato: `-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`
- [x] PIC predefinito su tutte le piattaforme (`isPICDefault()` restituisce `true` universalmente)
- [x] Impilamento ricorsivo generico (tabelle di puntatori a funzione, tabelle di puntatori a stringa, tabelle di strutture annidate, inizializzatori ConstantExpr GEP/BitCast)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch, inclusa condivisione tabelle multi-sito di dispatch
- [x] Inlining costanti vettoriali SIMD (`inlineVectorConstants`)
- [x] Retrocessione automatica di `_Thread_local` a static
- [x] Loader nativo macOS arm64 (MAP_JIT + i-cache flush)

**Test**: 108/108 asserzioni shellcode superate. Dimensioni binarie: `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## Fase 1 — Linux / Android / Windows multipiattaforma (consegnato)

- [x] Astrazione `TargetDesc`: differenze di piattaforma guidate da tabella
- [x] Semantica `-mshellcode-syscall` multipiattaforma (sostituisce `-mshellcode-libsystem` solo Darwin)
- [x] Tabelle numeri di syscall Linux / Android (Darwin BSD 100+, Linux arm64 130+, Linux x86_64 150+)
- [x] `ShellcodeExtractor` rifattorizzato in `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] Estrattore ELF (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/etc.; x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] Estrattore COFF (arm64: `IMAGE_REL_ARM64_BRANCH26`/etc.; x86_64: `IMAGE_REL_AMD64_REL32`/etc.)
- [x] Pass di importazione PEB Windows (`WinPEBImportPass`) con resolver PEB walk reale
- [x] Whitelist Win32 API multi-DLL (~210 API su kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/etc. → helper di ciclo byte inline
- [x] `CompilerRtPass`: divisione/modulo `__int128` → helper di divisione lunga inline
- [x] Supporto frontend Windows `aarch64-pc-windows-msvc`
- [x] `MIRPrepPass`: rimozione pseudo-istruzioni multipiattaforma (CFI/EH/XRay/StackMap/SEH/FENTRY/etc.)
- [x] Hook di offuscamento MIR + livello byte (11 hook su livelli IR/MIR/flusso byte)
- [x] Downgrade automatico AArch64 non-Darwin `long double` a binary64
- [x] Header shim shellcode: `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Livello di compatibilità POSIX Windows (13 ponti POSIX→Win32: write→WriteFile, mmap→VirtualAlloc, etc.)
- [x] Correzione automatica dichiarazioni implicite K&R (50+ firme POSIX canoniche)
- [x] Purificazione guidata da tabella (codifica rigida rami architettura → zero)
- [x] `KernelImportPass`: riscrittura automatica dei callsite ring-0 con resolver
- [x] Diagnostica guidata da tabella nomi helper del kernel (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` per le convenzioni di punto d'ingresso ring-0
- [x] Imposizione offset zero del punto d'ingresso (`placeEntryFirst`)
- [x] Pipeline di finalizzazione: SDK rewriter byte proibiti + SDK encoder charset + vincoli dimensionali
- [x] API Plugin C fuori dall'albero (`NevercPluginAPI.h`): 11 hook point shellcode (`NEVERC_HOOK_SC_*`)
- [x] Iniezione `-mno-implicit-float` per x86_64 (previene spill del pool costanti SSE del backend)
- [x] Loader multipiattaforma (macOS/Linux/Windows)

**Test**: 743+ asserzioni shellcode, tutte superate su 8 triple. Suite NeverC completa: 1000+ test superati.

## Fase 2 — Encoder stampabile / alfanumerico (pianificato)

- [ ] Encoder shellcode stampabile ARM64 (sottoinsieme istruzioni 0x20–0x7e)
- [ ] Encoder alfanumerico x86_64
- [ ] Generazione stub auto-decodificante (decoder stub)
- [ ] Statistiche dimensione / entropia post-codifica

## Fase 3 — Polimorfismo / auto-modifica (pianificato)

- [ ] Motore polimorfo: stesso sorgente → sequenze byte equivalenti diverse per compilazione
- [ ] Codice auto-modificante: decrittazione / decompressione del corpo payload a runtime
- [ ] Anti-rilevamento: evitare pattern di firma shellcode noti

## Estensioni future

- [ ] iOS arm64 (firma del codice + scenari jailbreak JIT)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
