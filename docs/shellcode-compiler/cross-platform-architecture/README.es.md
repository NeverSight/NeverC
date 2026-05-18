**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Arquitectura multiplataforma de NeverC Shellcode — Resumen

Este documento describe los principios de diseño detrás de "un conjunto de pasadas cubriendo macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Léalo antes de extender a una nueva plataforma.

Documentos relacionados:
- [README.md](../README.es.md) — Resumen, opciones CLI, inicio rápido
- [ir-pass-design.md](../ir-pass-design/README.es.md) — Responsabilidades de pasadas IR
- [mir-pass-design.md](../mir-pass-design/README.es.md) — Capa MIR
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.es.md) — Contexto kernel
- [platform-extension-guide.md](../platform-extension-guide/README.es.md) — Agregar plataformas

---

## 1. Matriz tridimensional: OS × Arch × ExecutionLevel

Todas las diferencias multiplataforma convergen en una **matriz 3D**: 8 (OS, arch) × 2 ExecutionLevel = **16 entradas de tabla** de `describeTriple()`.

**Principio central**: Las pasadas siempre leen de la tabla, nunca `if (OS == Darwin)`. Nueva plataforma = 1 fila + 1 case en extractor.

## 2–3. Pipeline y PIC

Orden fijo con 11 hooks de ofuscación. `isPICDefaultForced()` retorna **true** universalmente.

## 4. User / Kernel ortogonal

- **User**: Pipeline PEB walk / syscall stub.
- **Kernel**: SyscallStub/WinPEB cortocircuito; KernelImportPass activado.

## 5. Matriz de soporte "C normal" modo usuario

Arrays grandes, constantes FP, computed-goto, memcpy, `__int128`, atómicos, headers POSIX/Win32 — todo **soportado directamente** sin intervención del usuario.

## 6. Capa MIR: Pipeline de 3 etapas (Reparar / Fallback / Extraer)

1. Limpieza de pseudo-instrucciones multiplataforma
2. Reescritura de instrucciones dirigida por tablas
3. Auditoría de referencias externas / pool de constantes

## 7–8. Extractor y hooks de ofuscación

Extractor: "aceptar PC-rel intra-.text, rechazar todo lo demás". 11 hooks en todas las capas.

## 9–10. Extensión y no-objetivos

Costo: 1 fila TargetDesc + tabla syscall + case extractor + tests. No-objetivos: C++/ObjC, 32-bit, incrustación libc, direcciones absolutas.
