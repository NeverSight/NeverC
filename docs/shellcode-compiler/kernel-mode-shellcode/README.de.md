**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# Kernel-Modus (Ring-0) Shellcode-Unterstützung

`-fshellcode` deckte ursprünglich nur Ring-3-Payloads ab. Ring-0-Payloads können die Ring-3-ABI nicht wiederverwenden: kein TEB/PEB, Syscall-Befehle sind User-zu-Kernel-Traps, x86_64 benötigt anderes Codemodell und Red-Zone-Deaktivierung.

## 1. `-mshellcode-context={user,kernel}`
- **User** (Standard): PEB/Syscall-Pipeline.
- **Kernel**: SyscallStub/WinPEB deaktiviert, Kernel-Flags injiziert, KernelImportPass aktiviert.

## 2–3. TargetDesc-Felder und Treiber-Flags
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`; AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
Automatisches Umschreiben ungelöster extern-Aufrufe in Resolver-gestützte indirekte Aufrufe. FNV-1a-64-Hash. Drei-Schicht-Verteidigung.

## 5–7. Android Kernel, Header, Ring-0 Code schreiben
`<neverc/kernel.h>` bietet `neverc_kern_resolve_t` und `neverc_kern_hash()`. Reine Berechnung oder Resolver-basierte Payloads.

## 8. Roadmap
Kernel-Kontextwechsel, Resolver-Umschreibung, beide Payload-Typen — alles erledigt. Kernel-SDK-Header geplant.
