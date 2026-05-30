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
| WinPEBImportPass | Win32 extern → PEB walk resolver (~210 APIs) + caché de direcciones cifrada + compat Windows POSIX |
| MemIntrinPass | mem*/str*/abs → helpers byte-loop inline |
| CompilerRtPass | `__int128` div/mod → división larga inline |
| Data2TextPass | Phase 1+2: GVs constantes → inmediatos/stack + split SROA residual |
| AllBlrPass | (opcional) llamadas directas → indirectas |
| KernelImportPass | (ring-0) extern → llamadas indirectas vía resolver |
| StringRuntimePass | métodos `string` integrado → variantes arena stack |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → alloc arena + fallback OS para asignaciones grandes |

**Cifrado de caché de direcciones** (§4.1, compartido por WinPEBImportPass y KernelImportPass): las direcciones resueltas se cifran antes del almacenamiento mediante descomposición aritmética sin XOR `(a + b) - 2*(a & b)` + intermediarios `volatile`. Tres funciones enchufables (`__sc_derive_key`, `__sc_ptr_encrypt`, `__sc_ptr_decrypt`). Slots de caché por (DLL, API) en sección `.text`. Ruta rápida/lenta con `cmpxchg weak` thread-safe. El usuario puede proporcionar sus propias implementaciones (`always_inline`, inversas mutuas, sin llamadas externas). Ver [README.md §4.1–4.5](README.md#41-address-cache-encryption) para detalles completos.

11 hooks de ofuscación (`NEVERC_HOOK_SC_*`). Filosofía de diagnóstico: 1 error = 1 diagnóstico accionable. Ver [Plugin API — Puntos de enganche](../../plugin-api/README.es.md#5-puntos-de-enganche) y [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.es.md).
