**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Tutorial assembly ARM64 (AArch64) — Prospettiva Shellcode

> Per lettori non familiari con ARM64, focalizzato sulle istruzioni generate dal compilatore shellcode.

## 1–7. Registri, rami, indirizzamento PC-relativo, caricamento immediati, accesso memoria, aritmetica, confronti

Registri generali x0-x30 (64-bit) / w0-w30 (32-bit), sp, xzr/wzr. Convenzione AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **Shellcode deve evitare `bl`** (rilocazione BRANCH26). `adr`/`adrp+add`. `mov+movk` per 64-bit — **nucleo di Data2TextPass**.

## 8. Sequenze di istruzioni tipiche generate

Calcolo puro, Fibonacci ricorsivo, inlining stringa su stack (Data2TextPass), syscall (SyscallStubPass svc diretto). Flusso istruzioni 100% in `__TEXT,__text`.

## 9. Riepilogo chiave

| Concetto | x86_64 | ARM64 | Shellcode |
|----------|--------|-------|-----------|
| Chiamata funzione | `call rel32` | `bl imm26` | Estrattore patcha BRANCH26 |
| Caricamento indirizzo | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 patchati |
| Immediato 64-bit | `mov rax,imm64` | `mov+movk ×4` | Zero rilocazioni |
| Syscall | `syscall` | `svc #0x80` | Darwin: x16=nr |
