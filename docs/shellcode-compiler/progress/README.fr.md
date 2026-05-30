**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Compilateur shellcode — Suivi de progression

## Phase 0 — macOS arm64 MVP (livré)

- [x] Squelette répertoire & CMake (bibliothèque `nevercShellcode`)
- [x] `ZeroRelocPass` : deux phases (Prep + Stackify), empilage automatique des globaux mutables
- [x] `Data2TextPass` : deux phases (tableaux constants → stockage en pile par morceaux ; fractionnement de constantes vectorielles post-SROA ; ConstantFP → motifs de bits chargés avec volatile)
- [x] `SyscallStubPass` : liste blanche pilotée par table couvrant Darwin BSD / Linux arm64 / Linux x86_64 / Android syscalls
- [x] `AllBlrPass` : réécriture agressive optionnelle des appels indirects
- [x] `ShellcodeExtractor` : Mach-O `.o` → `.bin` plat avec correctifs de relocalisations intra-section
- [x] Options CLI via `neverc/include/neverc/Invoke/Options.td.h` généré : `-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`
- [x] PIC par défaut sur toutes les plateformes (`isPICDefault()` retourne `true` universellement)
- [x] Empilage récursif générique (tables de pointeurs de fonction, tables de pointeurs de chaîne, tables de structures imbriquées, initialiseurs ConstantExpr GEP/BitCast)
- [x] `IndirectBrPass` : GCC computed-goto (`&&label`) → switch, y compris partage de tables multi-sites de dispatch
- [x] Inlining de constantes vectorielles SIMD (`inlineVectorConstants`)
- [x] Rétrogradation automatique de `_Thread_local` en static
- [x] Chargeur natif macOS arm64 (MAP_JIT + i-cache flush)

**Tests** : 108/108 assertions shellcode réussies. Tailles binaires : `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## Phase 1 — Linux / Android / Windows multiplateforme (livré)

- [x] Abstraction `TargetDesc` : différences de plateforme pilotées par table
- [x] Sémantique `-mshellcode-syscall` multiplateforme (remplace `-mshellcode-libsystem` Darwin-only)
- [x] Tables de numéros de syscall Linux / Android (Darwin BSD 100+, Linux arm64 130+, Linux x86_64 150+)
- [x] `ShellcodeExtractor` refactorisé en `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] Extracteur ELF (arm64 : `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/etc. ; x86_64 : `R_X86_64_PC32`/`PLT32`)
- [x] Extracteur COFF (arm64 : `IMAGE_REL_ARM64_BRANCH26`/etc. ; x86_64 : `IMAGE_REL_AMD64_REL32`/etc.)
- [x] Passe d'import PEB Windows (`WinPEBImportPass`) avec résolveur PEB walk réel
- [x] Liste blanche Win32 API multi-DLL (~210 API sur kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass` : memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/etc. → helpers de boucle d'octets inline
- [x] `CompilerRtPass` : division/modulo `__int128` → helpers de division longue inline
- [x] Support frontend Windows `aarch64-pc-windows-msvc`
- [x] `MIRPrepPass` : suppression de pseudo-instructions multiplateforme (CFI/EH/XRay/StackMap/SEH/FENTRY/etc.)
- [x] Hooks d'obfuscation MIR + niveau octet (11 hooks sur les couches IR/MIR/flux d'octets)
- [x] Rétrogradation automatique AArch64 non-Darwin `long double` en binary64
- [x] En-têtes shim shellcode : `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Couche de compatibilité POSIX Windows (13 ponts POSIX→Win32 : write→WriteFile, mmap→VirtualAlloc, etc.)
- [x] Correction automatique des déclarations implicites K&R (50+ signatures POSIX canoniques)
- [x] Purification pilotée par table (codage en dur des branches d'architecture → zéro)
- [x] `KernelImportPass` : réécriture automatique des sites d'appel ring-0 avec résolveur
- [x] Diagnostics pilotés par table de noms d'helpers du noyau (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` pour les conventions de point d'entrée ring-0
- [x] Forçage du décalage zéro du point d'entrée (`placeEntryFirst`)
- [x] Pipeline de finalisation : SDK de réécriture d'octets interdits + SDK d'encodeur de charset + contraintes de taille
- [x] API Plugin C hors arbre (`NevercPluginAPI.h`) : 11 points d'accroche shellcode (`NEVERC_HOOK_SC_*`)
- [x] Injection `-mno-implicit-float` pour x86_64 (empêche le déversement du pool de constantes SSE du backend)
- [x] Chargeurs multiplateformes (macOS/Linux/Windows)

**Tests** : 743+ assertions shellcode, toutes réussies sur 8 triples. Suite NeverC complète : 1000+ tests réussis.

## Phase 2 — Encodeur imprimable / alphanumérique (planifié)

- [ ] Encodeur shellcode imprimable ARM64 (sous-ensemble d'instructions 0x20–0x7e)
- [ ] Encodeur alphanumérique x86_64
- [ ] Génération de stub auto-décodant (decoder stub)
- [ ] Statistiques de taille / entropie post-encodage

## Phase 3 — Polymorphisme / auto-modification (planifié)

- [ ] Moteur polymorphe : même source → séquences d'octets équivalentes différentes par compilation
- [ ] Code auto-modifiant : déchiffrement / décompression du corps de charge utile à l'exécution
- [ ] Anti-détection : éviter les motifs de signature shellcode connus

## Extensions futures

- [ ] iOS arm64 (signature de code + scénarios jailbreak JIT)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
