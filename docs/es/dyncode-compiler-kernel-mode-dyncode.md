**Idiomas**: [English](../dyncode-compiler-kernel-mode-dyncode.md) | [简体中文](../zh-CN/dyncode-compiler-kernel-mode-dyncode.md) | [繁體中文](../zh-TW/dyncode-compiler-kernel-mode-dyncode.md) | [日本語](../ja/dyncode-compiler-kernel-mode-dyncode.md) | [한국어](../ko/dyncode-compiler-kernel-mode-dyncode.md) | [Français](../fr/dyncode-compiler-kernel-mode-dyncode.md) | [Deutsch](../de/dyncode-compiler-kernel-mode-dyncode.md) | [Español](dyncode-compiler-kernel-mode-dyncode.md) | [Italiano](../it/dyncode-compiler-kernel-mode-dyncode.md) | [Русский](../ru/dyncode-compiler-kernel-mode-dyncode.md) | [العربية](../ar/dyncode-compiler-kernel-mode-dyncode.md)

[← Compilador de dyncode](dyncode-compiler.md)

# Soporte de dyncode modo kernel (Ring-0)

`-fdyncode` originalmente cubría solo payloads ring-3. Los payloads ring-0 no pueden reutilizar la ABI ring-3: no existe TEB/PEB, las instrucciones syscall son traps usuario→kernel, x86_64 necesita modelo de código diferente y deshabilitar red zone.

## 1. `-mdyncode-context={user,kernel}`
- **User** (predeterminado): Pipeline PEB/syscall.
- **Kernel**: SyscallStub/WinPEB deshabilitados, flags kernel inyectados, KernelImportPass activado.

## 2–3. Campos TargetDesc y flags de driver
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`; AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
Reescritura automática de llamadas extern no resueltas a llamadas indirectas vía resolver. Hash FNV-1a 64-bit. Defensa de tres capas.

## 5–7. Kernel Android, headers, escribir código Ring-0
`<neverc/dyncode/kernel.h>` proporciona `neverc_kern_resolve_t` y `neverc_kern_hash()`. Payloads de cálculo puro o basados en resolver.

## 8. Hoja de ruta
Cambio de contexto kernel, reescritura resolver, ambos tipos de payload — todo completado. Headers SDK kernel planificados.
