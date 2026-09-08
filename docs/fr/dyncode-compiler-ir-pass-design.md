**Langues**: [English](../dyncode-compiler-ir-pass-design.md) | [简体中文](../zh-CN/dyncode-compiler-ir-pass-design.md) | [繁體中文](../zh-TW/dyncode-compiler-ir-pass-design.md) | [日本語](../ja/dyncode-compiler-ir-pass-design.md) | [한국어](../ko/dyncode-compiler-ir-pass-design.md) | [Français](dyncode-compiler-ir-pass-design.md) | [Deutsch](../de/dyncode-compiler-ir-pass-design.md) | [Español](../es/dyncode-compiler-ir-pass-design.md) | [Italiano](../it/dyncode-compiler-ir-pass-design.md) | [Русский](../ru/dyncode-compiler-ir-pass-design.md) | [العربية](../ar/dyncode-compiler-ir-pass-design.md)

[← Compilateur dyncode](dyncode-compiler.md)

# Conception des passes IR — Principes, pipeline et exemples avant/après

> Ce document explique le **pourquoi** de chaque passe dans le pipeline de compilation dyncode.

## 0. Idée centrale

Objectif en une phrase : **Éliminer tout dans le `.o` qui deviendrait une relocalisation, ne laissant qu'un flux d'instructions pur directement `mmap(RWX)` + `memcpy` + `blr`.**

## 1–13. Passes

| Passe | Fonction |
|-------|----------|
| ZeroRelocPass | Prep : unification linkage + alwaysinline. Stackify : globales mutables → alloca |
| IndirectBrPass | computed-goto → switch |
| SyscallStubPass | libc extern → traps inline pilotés par TargetDesc + compat POSIX + autofix K&R |
| WinPEBImportPass | Win32 extern → résolveur PEB walk (~210 APIs) + cache d'adresses chiffrée (XOR) + compat Windows POSIX |
| MemIntrinPass | mem*/str*/abs → helpers boucle-octet inline |
| CompilerRtPass | `__int128` div/mod → division longue inline |
| Data2TextPass | Phase 1+2 : GVs constants → immédiats/pile + split résiduel SROA |
| AllBlrPass | (optionnel) appels directs → indirects |
| KernelImportPass | (ring-0) extern → appels indirects via résolveur |
| StringRuntimePass | méthodes `string` intégrées → variantes arena pile |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → alloc arena + fallback OS pour grandes allocations |

**Chiffrement du cache d'adresses** (§4.1, partagé par WinPEBImportPass et KernelImportPass) : les adresses résolues sont chiffrées avant stockage via décomposition arithmétique sans XOR `(a + b) - 2*(a & b)` + intermédiaires `volatile`. Trois fonctions enfichables (`__sc_derive_key`, `__sc_ptr_encrypt`, `__sc_ptr_decrypt`). Slots de cache par (DLL, API) en section `.text`. Chemin rapide/lent avec `cmpxchg weak` thread-safe. L'utilisateur peut fournir ses propres implémentations (`always_inline`, inverses mutuelles, sans appels externes). Voir [README.md §4.1–4.5](../dyncode-compiler-ir-pass-design.md#41-address-cache-encryption) pour les détails complets.

11 interposes d'obfuscation. Philosophie de diagnostic : 1 erreur = 1 diagnostic actionnable. Voir [mir-pass-design.md §3](dyncode-compiler-mir-pass-design.md#3-interposes-dobfuscation-utilisateur) et [kernel-mode-dyncode.md](dyncode-compiler-kernel-mode-dyncode.md).
