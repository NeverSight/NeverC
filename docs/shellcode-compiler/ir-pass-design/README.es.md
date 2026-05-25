**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Diseño de pasadas IR — Principios, pipeline y ejemplos antes/después

> Este documento explica el **porqué** de cada pasada en el pipeline de compilación de shellcode.

## 0. Idea central

Objetivo en una frase: **Eliminar todo en el `.o` que se convertiría en una relocalización, dejando solo un flujo de instrucciones puro que puede ser `mmap(RWX)` + `memcpy` + `blr` directamente.**

## 1–13. Pasadas

| Pasada | Función |
|--------|---------|
| ZeroRelocPass | Prep: unificación linkage + alwaysinline. Stackify: globales mutables → alloca |
| IndirectBrPass | computed-goto → switch |
| SyscallStubPass | libc extern → traps inline dirigidas por TargetDesc + compat POSIX + autofix K&R |
| WinPEBImportPass | Win32 extern → PEB walk resolver (~190 APIs) + caché de direcciones cifrada + compat Windows POSIX |
| MemIntrinPass | mem*/str*/abs → helpers byte-loop inline |
| CompilerRtPass | `__int128` div/mod → división larga inline |
| Data2TextPass | Phase 1+2: GVs constantes → inmediatos/stack + split SROA residual |
| AllBlrPass | (opcional) llamadas directas → indirectas |
| KernelImportPass | (ring-0) extern → llamadas indirectas vía resolver |
| StringRuntimePass | métodos `string` integrado → variantes arena stack |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → alloc arena + fallback OS para asignaciones grandes |

**Cifrado de caché de direcciones** (§4.1, compartido por WinPEBImportPass y KernelImportPass): las direcciones resueltas se cifran con XOR antes del almacenamiento. Tres funciones enchufables (`__sc_derive_key`, `__sc_ptr_encrypt`, `__sc_ptr_decrypt`) — por defecto XOR puro (`key = PEB ^ seed` en modo usuario, seed puro en modo kernel). Slots de caché por (DLL, API) en sección `.text`. Ruta rápida/lenta con `cmpxchg weak` thread-safe. El usuario puede proporcionar sus propias implementaciones (`always_inline`, inversas mutuas, sin llamadas externas). Ver [README.md §4.1–4.5](README.md#41-address-cache-encryption) para detalles completos.

11 hooks de ofuscación. Filosofía de diagnóstico: 1 error = 1 diagnóstico accionable. Ver [plugin-interface.md §6](../plugin-interface/README.es.md#6-registration-position-selection--pic-coverage-matrix) y [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.es.md).
