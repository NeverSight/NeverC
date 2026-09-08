**Lingue**: [English](../dyncode-compiler-cross-platform-architecture.md) | [简体中文](../zh-CN/dyncode-compiler-cross-platform-architecture.md) | [繁體中文](../zh-TW/dyncode-compiler-cross-platform-architecture.md) | [日本語](../ja/dyncode-compiler-cross-platform-architecture.md) | [한국어](../ko/dyncode-compiler-cross-platform-architecture.md) | [Français](../fr/dyncode-compiler-cross-platform-architecture.md) | [Deutsch](../de/dyncode-compiler-cross-platform-architecture.md) | [Español](../es/dyncode-compiler-cross-platform-architecture.md) | [Italiano](dyncode-compiler-cross-platform-architecture.md) | [Русский](../ru/dyncode-compiler-cross-platform-architecture.md) | [العربية](../ar/dyncode-compiler-cross-platform-architecture.md)

[← Compilatore dyncode](dyncode-compiler.md)

# Architettura multipiattaforma NeverC DynCode — Panoramica

Questo documento descrive i principi di progettazione dietro "un set di pass che copre macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Da leggere prima di estendere a una nuova piattaforma.

Documenti correlati:
- [README.md](dyncode-compiler.md) — Panoramica, opzioni CLI, avvio rapido
- [ir-pass-design.md](dyncode-compiler-ir-pass-design.md) — Responsabilità pass IR
- [mir-pass-design.md](dyncode-compiler-mir-pass-design.md) — Livello MIR
- [kernel-mode-dyncode.md](dyncode-compiler-kernel-mode-dyncode.md) — Contesto kernel
- [platform-extension-guide.md](dyncode-compiler-platform-extension-guide.md) — Aggiungere piattaforme

---

## 1. Matrice tridimensionale: OS × Arch × ExecutionLevel

Tutte le differenze convergono in una **matrice 3D**: 8 (OS, arch) × 2 ExecutionLevel = **16 voci di tabella** da `describeTriple()`.

**Principio centrale**: I pass leggono sempre dalla tabella, mai `if (OS == Darwin)`. Nuova piattaforma = 1 riga + 1 case nell'estrattore.

## 2–10. Pipeline, PIC, User/Kernel, MIR, Estrattore, Interpose, Estensione

Ordine fisso con 11 interpose di offuscamento. `isPICDefaultForced()` restituisce **true** universalmente. Costo nuova piattaforma: 1 riga TargetDesc + tabella syscall + case estrattore + test. Non-obiettivi: C++/ObjC, 32-bit, incorporamento libc (allocazione heap via `HeapArenaPass`), indirizzi assoluti.
