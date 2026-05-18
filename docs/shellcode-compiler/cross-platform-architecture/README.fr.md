**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Architecture multiplateforme NeverC Shellcode — Vue d'ensemble

Ce document décrit les principes de conception derrière « un ensemble de passes couvrant macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel ». À lire avant d'étendre à une nouvelle plateforme.

Documents liés :
- [README.md](../README.fr.md) — Vue d'ensemble, options CLI, démarrage rapide
- [ir-pass-design.md](../ir-pass-design/README.fr.md) — Responsabilités des passes IR
- [mir-pass-design.md](../mir-pass-design/README.fr.md) — Couche MIR
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.fr.md) — Contexte noyau
- [platform-extension-guide.md](../platform-extension-guide/README.fr.md) — Ajout de plateformes

---

## 1. Matrice tridimensionnelle : OS × Arch × ExecutionLevel

Toutes les différences convergent dans une **matrice 3D** : 8 (OS, arch) × 2 ExecutionLevel = **16 entrées de table** de `describeTriple()`.

**Principe central** : Les passes lisent toujours de la table, jamais `if (OS == Darwin)`. Nouvelle plateforme = 1 ligne + 1 case extracteur.

## 2–3. Pipeline et PIC

Ordre fixe avec 11 hooks d'obfuscation. `isPICDefaultForced()` retourne **true** universellement.

## 4. User / Kernel orthogonal

- **User** : Pipeline PEB walk / syscall stub.
- **Kernel** : SyscallStub/WinPEB court-circuité ; KernelImportPass activé.

## 5. Matrice de support « C normal » mode utilisateur

Grands tableaux, constantes FP, computed-goto, memcpy, `__int128`, atomics, headers POSIX/Win32 — tout **directement supporté** sans intervention utilisateur.

## 6. Couche MIR : Pipeline 3 étapes (Réparer / Repli / Extraire)

1. Nettoyage pseudo-instructions multiplateforme
2. Réécriture d'instructions pilotée par table
3. Audit références externes / pool de constantes

## 7–8. Extracteur et hooks d'obfuscation

Extracteur : « accepter PC-rel intra-.text, rejeter tout le reste ». 11 hooks sur toutes les couches.

## 9–10. Extension et non-objectifs

Coût : 1 ligne TargetDesc + table syscall + case extracteur + tests. Non-objectifs : C++/ObjC, 32-bit, intégration libc, adresses absolues.
