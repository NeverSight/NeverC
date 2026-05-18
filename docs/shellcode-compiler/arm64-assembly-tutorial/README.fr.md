**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Tutoriel assemblage ARM64 (AArch64) — Perspective Shellcode

> Pour les lecteurs non familiers avec ARM64, focalisé sur les instructions générées par le compilateur shellcode.

## 1–7. Registres, branches, adressage PC-relatif, chargement d'immédiats, accès mémoire, arithmétique, comparaisons

Registres généraux x0-x30 (64-bit) / w0-w30 (32-bit), sp, xzr/wzr. Convention AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **Shellcode doit éviter `bl`** (relocalisation BRANCH26). `adr`/`adrp+add`. `mov+movk` pour 64-bit — **cœur de Data2TextPass**.

## 8. Séquences d'instructions typiques générées

Calcul pur, Fibonacci récursif, inlining de chaîne sur pile (Data2TextPass), syscall (SyscallStubPass svc directe). Flux d'instructions 100% dans `__TEXT,__text`.

## 9. Résumé clé

| Concept | x86_64 | ARM64 | Shellcode |
|---------|--------|-------|-----------|
| Appel fonction | `call rel32` | `bl imm26` | Extracteur patche BRANCH26 |
| Chargement adresse | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 patchés |
| Immédiat 64-bit | `mov rax,imm64` | `mov+movk ×4` | Zéro relocalisations |
| Syscall | `syscall` | `svc #0x80` | Darwin : x16=nr |
