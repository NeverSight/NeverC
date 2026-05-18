**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Conception des passes IR — Principes, pipeline et exemples avant/après

> Ce document explique le **pourquoi** de chaque passe dans le pipeline de compilation shellcode.

## 0. Idée centrale

Objectif en une phrase : **Éliminer tout dans le `.o` qui deviendrait une relocalisation, ne laissant qu'un flux d'instructions pur directement `mmap(RWX)` + `memcpy` + `blr`.**

## 1–13. Passes

| Passe | Fonction |
|-------|----------|
| ZeroRelocPass | Prep : unification linkage + alwaysinline. Stackify : globales mutables → alloca |
| IndirectBrPass | computed-goto → switch |
| SyscallStubPass | libc extern → traps inline pilotés par TargetDesc + compat POSIX + autofix K&R |
| WinPEBImportPass | Win32 extern → résolveur PEB walk (~190 APIs) + compat Windows POSIX |
| MemIntrinPass | mem*/str*/abs → helpers boucle-octet inline |
| CompilerRtPass | `__int128` div/mod → division longue inline |
| Data2TextPass | Phase 1+2 : GVs constants → immédiats/pile + split résiduel SROA |
| AllBlrPass | (optionnel) appels directs → indirects |
| KernelImportPass | (ring-0) extern → appels indirects via résolveur |
| StringRuntimePass | méthodes `string` intégrées → variantes arena pile |

11 hooks d'obfuscation. Philosophie de diagnostic : 1 erreur = 1 diagnostic actionnable. Voir [plugin-interface.md §6](../plugin-interface/README.fr.md#6-registration-position-selection--pic-coverage-matrix) et [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.fr.md).
