**Langues**: [English](../dyncode-compiler-cross-platform-architecture.md) | [简体中文](../zh-CN/dyncode-compiler-cross-platform-architecture.md) | [繁體中文](../zh-TW/dyncode-compiler-cross-platform-architecture.md) | [日本語](../ja/dyncode-compiler-cross-platform-architecture.md) | [한국어](../ko/dyncode-compiler-cross-platform-architecture.md) | [Français](dyncode-compiler-cross-platform-architecture.md) | [Deutsch](../de/dyncode-compiler-cross-platform-architecture.md) | [Español](../es/dyncode-compiler-cross-platform-architecture.md) | [Italiano](../it/dyncode-compiler-cross-platform-architecture.md) | [Русский](../ru/dyncode-compiler-cross-platform-architecture.md) | [العربية](../ar/dyncode-compiler-cross-platform-architecture.md)

[← Compilateur dyncode](dyncode-compiler.md)

# Architecture multiplateforme NeverC DynCode — Vue d'ensemble

Ce document décrit les principes de conception derrière « un ensemble de passes couvrant macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel ». À lire avant d'étendre à une nouvelle plateforme.

Documents liés :
- [README.md](dyncode-compiler.md) — Vue d'ensemble, options CLI, démarrage rapide
- [ir-pass-design.md](dyncode-compiler-ir-pass-design.md) — Responsabilités des passes IR
- [mir-pass-design.md](dyncode-compiler-mir-pass-design.md) — Couche MIR
- [kernel-mode-dyncode.md](dyncode-compiler-kernel-mode-dyncode.md) — Contexte noyau
- [platform-extension-guide.md](dyncode-compiler-platform-extension-guide.md) — Ajout de plateformes

---

## 1. Matrice tridimensionnelle : OS × Arch × ExecutionLevel

Toutes les différences convergent dans une **matrice 3D** : 8 (OS, arch) × 2 ExecutionLevel = **16 entrées de table** de `describeTriple()`.

**Principe central** : Les passes lisent toujours de la table, jamais `if (OS == Darwin)`. Nouvelle plateforme = 1 ligne + 1 case extracteur.

## 2–3. Pipeline et PIC

Ordre fixe avec 11 interposes d'obfuscation. `isPICDefaultForced()` retourne **true** universellement.

## 4. User / Kernel orthogonal

- **User** : Pipeline PEB walk / syscall stub.
- **Kernel** : SyscallStub/WinPEB court-circuité ; KernelImportPass activé.

## 5. Matrice de support « C normal » mode utilisateur

Grands tableaux, constantes FP, computed-goto, memcpy, `__int128`, atomics, headers POSIX/Win32 — tout **directement supporté** sans intervention utilisateur.

## 6. Couche MIR : Pipeline 3 étapes (Réparer / Repli / Extraire)

1. Nettoyage pseudo-instructions multiplateforme
2. Réécriture d'instructions pilotée par table
3. Audit références externes / pool de constantes

## 7–8. Extracteur et interposes d'obfuscation

Extracteur : « accepter PC-rel intra-.text, rejeter tout le reste ». 11 interposes sur toutes les couches.

## 9–10. Extension et non-objectifs

Coût : 1 ligne TargetDesc + table syscall + case extracteur + tests. Non-objectifs : C++/ObjC, 32-bit, intégration libc (allocation heap via `HeapArenaPass`), adresses absolues.
