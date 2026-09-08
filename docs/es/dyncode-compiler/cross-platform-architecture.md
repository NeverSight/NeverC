**Idiomas**: [English](../../dyncode-compiler/cross-platform-architecture.md) | [简体中文](../../zh-CN/dyncode-compiler/cross-platform-architecture.md) | [繁體中文](../../zh-TW/dyncode-compiler/cross-platform-architecture.md) | [日本語](../../ja/dyncode-compiler/cross-platform-architecture.md) | [한국어](../../ko/dyncode-compiler/cross-platform-architecture.md) | [Français](../../fr/dyncode-compiler/cross-platform-architecture.md) | [Deutsch](../../de/dyncode-compiler/cross-platform-architecture.md) | [Español](cross-platform-architecture.md) | [Italiano](../../it/dyncode-compiler/cross-platform-architecture.md) | [Русский](../../ru/dyncode-compiler/cross-platform-architecture.md) | [العربية](../../ar/dyncode-compiler/cross-platform-architecture.md)

[← Compilador de dyncode](README.md)

# Arquitectura multiplataforma de NeverC DynCode — Resumen

Este documento describe los principios de diseño detrás de "un conjunto de pasadas cubriendo macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Léalo antes de extender a una nueva plataforma.

Documentos relacionados:
- [README.md](README.md) — Resumen, opciones CLI, inicio rápido
- [ir-pass-design.md](ir-pass-design.md) — Responsabilidades de pasadas IR
- [mir-pass-design.md](mir-pass-design.md) — Capa MIR
- [kernel-mode-dyncode.md](kernel-mode-dyncode.md) — Contexto kernel
- [platform-extension-guide.md](platform-extension-guide.md) — Agregar plataformas

---

## 1. Matriz tridimensional: OS × Arch × ExecutionLevel

Todas las diferencias multiplataforma convergen en una **matriz 3D**: 8 (OS, arch) × 2 ExecutionLevel = **16 entradas de tabla** de `describeTriple()`.

**Principio central**: Las pasadas siempre leen de la tabla, nunca `if (OS == Darwin)`. Nueva plataforma = 1 fila + 1 case en extractor.

## 2–3. Pipeline y PIC

Orden fijo con 11 interposes de ofuscación. `isPICDefaultForced()` retorna **true** universalmente.

## 4. User / Kernel ortogonal

- **User**: Pipeline PEB walk / syscall stub.
- **Kernel**: SyscallStub/WinPEB cortocircuito; KernelImportPass activado.

## 5. Matriz de soporte "C normal" modo usuario

Arrays grandes, constantes FP, computed-goto, memcpy, `__int128`, atómicos, headers POSIX/Win32 — todo **soportado directamente** sin intervención del usuario.

## 6. Capa MIR: Pipeline de 3 etapas (Reparar / Fallback / Extraer)

1. Limpieza de pseudo-instrucciones multiplataforma
2. Reescritura de instrucciones dirigida por tablas
3. Auditoría de referencias externas / pool de constantes

## 7–8. Extractor y interposes de ofuscación

Extractor: "aceptar PC-rel intra-.text, rechazar todo lo demás". 11 interposes en todas las capas.

## 9–10. Extensión y no-objetivos

Costo: 1 fila TargetDesc + tabla syscall + case extractor + tests. No-objetivos: C++/ObjC, 32-bit, incrustación libc (asignación heap vía `HeapArenaPass`), direcciones absolutas.
