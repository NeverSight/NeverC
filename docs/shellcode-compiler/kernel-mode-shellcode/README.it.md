**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Supporto shellcode modalità kernel (Ring-0)

`-fshellcode` copriva inizialmente solo payload ring-3. I payload ring-0 non possono riutilizzare l'ABI ring-3: niente TEB/PEB, istruzioni syscall = trap utente→kernel, x86_64 richiede modello di codice diverso e disabilitazione red zone.

## 1. `-mshellcode-context={user,kernel}`
- **User** (default): Pipeline PEB/syscall.
- **Kernel**: SyscallStub/WinPEB disabilitati, flag kernel iniettati, KernelImportPass attivato.

## 2–3. Campi TargetDesc e flag driver
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`; AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
Riscrittura automatica di chiamate extern non risolte in chiamate indirette via resolver. Hash FNV-1a 64-bit. Difesa a tre livelli.

## 5–7. Kernel Android, header, scrivere codice Ring-0
`<neverc/kernel.h>` fornisce `neverc_kern_resolve_t` e `neverc_kern_hash()`. Payload calcolo puro o basati su resolver.

## 8. Roadmap
Cambio contesto kernel, riscrittura resolver, entrambi i tipi di payload — tutto completato. Header SDK kernel pianificati.
