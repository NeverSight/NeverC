**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Soporte de shellcode modo kernel (Ring-0)

`-fshellcode` originalmente cubría solo payloads ring-3. Los payloads ring-0 no pueden reutilizar la ABI ring-3: no existe TEB/PEB, las instrucciones syscall son traps usuario→kernel, x86_64 necesita modelo de código diferente y deshabilitar red zone.

## 1. `-mshellcode-context={user,kernel}`
- **User** (predeterminado): Pipeline PEB/syscall.
- **Kernel**: SyscallStub/WinPEB deshabilitados, flags kernel inyectados, KernelImportPass activado.

## 2–3. Campos TargetDesc y flags de driver
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`; AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
Reescritura automática de llamadas extern no resueltas a llamadas indirectas vía resolver. Hash FNV-1a 64-bit. Defensa de tres capas.

## 5–7. Kernel Android, headers, escribir código Ring-0
`<neverc/kernel.h>` proporciona `neverc_kern_resolve_t` y `neverc_kern_hash()`. Payloads de cálculo puro o basados en resolver.

## 8. Hoja de ruta
Cambio de contexto kernel, reescritura resolver, ambos tipos de payload — todo completado. Headers SDK kernel planificados.
