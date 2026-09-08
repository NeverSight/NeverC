**Lingue**: [English](../dyncode-compiler-arm64-assembly-tutorial.md) | [简体中文](../zh-CN/dyncode-compiler-arm64-assembly-tutorial.md) | [繁體中文](../zh-TW/dyncode-compiler-arm64-assembly-tutorial.md) | [日本語](../ja/dyncode-compiler-arm64-assembly-tutorial.md) | [한국어](../ko/dyncode-compiler-arm64-assembly-tutorial.md) | [Français](../fr/dyncode-compiler-arm64-assembly-tutorial.md) | [Deutsch](../de/dyncode-compiler-arm64-assembly-tutorial.md) | [Español](../es/dyncode-compiler-arm64-assembly-tutorial.md) | [Italiano](dyncode-compiler-arm64-assembly-tutorial.md) | [Русский](../ru/dyncode-compiler-arm64-assembly-tutorial.md) | [العربية](../ar/dyncode-compiler-arm64-assembly-tutorial.md)

[← Compilatore dyncode](dyncode-compiler.md)

# Tutorial assembly ARM64 (AArch64) — Prospettiva DynCode

> Per lettori non familiari con ARM64, focalizzato sulle istruzioni generate dal compilatore dyncode.

## 1–7. Registri, rami, indirizzamento PC-relativo, caricamento immediati, accesso memoria, aritmetica, confronti

Registri generali x0-x30 (64-bit) / w0-w30 (32-bit), sp, xzr/wzr. Convenzione AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **DynCode deve evitare `bl`** (rilocazione BRANCH26). `adr`/`adrp+add`. `mov+movk` per 64-bit — **nucleo di Data2TextPass**.

## 8. Sequenze di istruzioni tipiche generate

Calcolo puro, Fibonacci ricorsivo, inlining stringa su stack (Data2TextPass), syscall (SyscallStubPass svc diretto). Flusso istruzioni 100% in `__TEXT,__text`.

## 9. Riepilogo chiave

| Concetto | x86_64 | ARM64 | DynCode |
|----------|--------|-------|-----------|
| Chiamata funzione | `call rel32` | `bl imm26` | Estrattore patcha BRANCH26 |
| Caricamento indirizzo | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 patchati |
| Immediato 64-bit | `mov rax,imm64` | `mov+movk ×4` | Zero rilocazioni |
| Syscall | `syscall` | `svc #0x80` | Darwin: x16=nr |
