**Sprachen**: [English](../../dyncode-compiler/kernel-mode-dyncode.md) | [简体中文](../../zh-CN/dyncode-compiler/kernel-mode-dyncode.md) | [繁體中文](../../zh-TW/dyncode-compiler/kernel-mode-dyncode.md) | [日本語](../../ja/dyncode-compiler/kernel-mode-dyncode.md) | [한국어](../../ko/dyncode-compiler/kernel-mode-dyncode.md) | [Français](../../fr/dyncode-compiler/kernel-mode-dyncode.md) | [Deutsch](kernel-mode-dyncode.md) | [Español](../../es/dyncode-compiler/kernel-mode-dyncode.md) | [Italiano](../../it/dyncode-compiler/kernel-mode-dyncode.md) | [Русский](../../ru/dyncode-compiler/kernel-mode-dyncode.md) | [العربية](../../ar/dyncode-compiler/kernel-mode-dyncode.md)

[← DynCode-Compiler](README.md)

# Kernel-Modus (Ring-0) DynCode-Unterstützung

`-fdyncode` deckte ursprünglich nur Ring-3-Payloads ab. Ring-0-Payloads können die Ring-3-ABI nicht wiederverwenden: kein TEB/PEB, Syscall-Befehle sind User-zu-Kernel-Traps, x86_64 benötigt anderes Codemodell und Red-Zone-Deaktivierung.

## 1. `-mdyncode-context={user,kernel}`
- **User** (Standard): PEB/Syscall-Pipeline.
- **Kernel**: SyscallStub/WinPEB deaktiviert, Kernel-Flags injiziert, KernelImportPass aktiviert.

## 2–3. TargetDesc-Felder und Treiber-Flags
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`; AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
Automatisches Umschreiben ungelöster extern-Aufrufe in Resolver-gestützte indirekte Aufrufe. FNV-1a-64-Hash. Drei-Schicht-Verteidigung.

## 5–7. Android Kernel, Header, Ring-0 Code schreiben
`<neverc/dyncode/kernel.h>` bietet `neverc_kern_resolve_t` und `neverc_kern_hash()`. Reine Berechnung oder Resolver-basierte Payloads.

## 8. Roadmap
Kernel-Kontextwechsel, Resolver-Umschreibung, beide Payload-Typen — alles erledigt. Kernel-SDK-Header geplant.
