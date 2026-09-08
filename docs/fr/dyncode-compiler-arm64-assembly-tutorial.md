**Langues**: [English](../dyncode-compiler-arm64-assembly-tutorial.md) | [简体中文](../zh-CN/dyncode-compiler-arm64-assembly-tutorial.md) | [繁體中文](../zh-TW/dyncode-compiler-arm64-assembly-tutorial.md) | [日本語](../ja/dyncode-compiler-arm64-assembly-tutorial.md) | [한국어](../ko/dyncode-compiler-arm64-assembly-tutorial.md) | [Français](dyncode-compiler-arm64-assembly-tutorial.md) | [Deutsch](../de/dyncode-compiler-arm64-assembly-tutorial.md) | [Español](../es/dyncode-compiler-arm64-assembly-tutorial.md) | [Italiano](../it/dyncode-compiler-arm64-assembly-tutorial.md) | [Русский](../ru/dyncode-compiler-arm64-assembly-tutorial.md) | [العربية](../ar/dyncode-compiler-arm64-assembly-tutorial.md)

[← Compilateur dyncode](dyncode-compiler.md)

# Tutoriel assemblage ARM64 (AArch64) — Perspective DynCode

> Pour les lecteurs non familiers avec ARM64, focalisé sur les instructions générées par le compilateur dyncode.

## 1–7. Registres, branches, adressage PC-relatif, chargement d'immédiats, accès mémoire, arithmétique, comparaisons

Registres généraux x0-x30 (64-bit) / w0-w30 (32-bit), sp, xzr/wzr. Convention AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **DynCode doit éviter `bl`** (relocalisation BRANCH26). `adr`/`adrp+add`. `mov+movk` pour 64-bit — **cœur de Data2TextPass**.

## 8. Séquences d'instructions typiques générées

Calcul pur, Fibonacci récursif, inlining de chaîne sur pile (Data2TextPass), syscall (SyscallStubPass svc directe). Flux d'instructions 100% dans `__TEXT,__text`.

## 9. Résumé clé

| Concept | x86_64 | ARM64 | DynCode |
|---------|--------|-------|-----------|
| Appel fonction | `call rel32` | `bl imm26` | Extracteur patche BRANCH26 |
| Chargement adresse | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 patchés |
| Immédiat 64-bit | `mov rax,imm64` | `mov+movk ×4` | Zéro relocalisations |
| Syscall | `syscall` | `svc #0x80` | Darwin : x16=nr |
