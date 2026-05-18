**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Architettura multipiattaforma NeverC Shellcode — Panoramica

Questo documento descrive i principi di progettazione dietro "un set di pass che copre macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Da leggere prima di estendere a una nuova piattaforma.

Documenti correlati:
- [README.md](../README.it.md) — Panoramica, opzioni CLI, avvio rapido
- [ir-pass-design.md](../ir-pass-design/README.it.md) — Responsabilità pass IR
- [mir-pass-design.md](../mir-pass-design/README.it.md) — Livello MIR
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.it.md) — Contesto kernel
- [platform-extension-guide.md](../platform-extension-guide/README.it.md) — Aggiungere piattaforme

---

## 1. Matrice tridimensionale: OS × Arch × ExecutionLevel

Tutte le differenze convergono in una **matrice 3D**: 8 (OS, arch) × 2 ExecutionLevel = **16 voci di tabella** da `describeTriple()`.

**Principio centrale**: I pass leggono sempre dalla tabella, mai `if (OS == Darwin)`. Nuova piattaforma = 1 riga + 1 case nell'estrattore.

## 2–10. Pipeline, PIC, User/Kernel, MIR, Estrattore, Hook, Estensione

Ordine fisso con 11 hook di offuscamento. `isPICDefaultForced()` restituisce **true** universalmente. Costo nuova piattaforma: 1 riga TargetDesc + tabella syscall + case estrattore + test. Non-obiettivi: C++/ObjC, 32-bit, incorporamento libc, indirizzi assoluti.
