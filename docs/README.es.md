**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Proyecto NeverC](../README.es.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# Documentación de NeverC

Notas de diseño, referencia API y guías para cada subsistema de NeverC.

---

## Compilador de shellcode

El pipeline de compilación de shellcode es el foco principal de investigación de NeverC. Arquitectura, opciones CLI, matriz de plataformas y ejemplos:

**[Compilador de shellcode →](shellcode-compiler/README.es.md)**

| Documento | Descripción |
|-----------|-------------|
| [README](shellcode-compiler/README.es.md) | Resumen, inicio rápido, objetivos soportados |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.es.md) | Diseño IR → objeto → extracción |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.es.md) | Razón de cada pasada IR |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.es.md) | Pasadas MIR del backend |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.es.md) | Compilación Ring-0 |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.es.md) | Plugins de ofuscación y codificación |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.es.md) | `TargetDesc` y extractores |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.es.md) | Añadir plataforma |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.es.md) | Instrucciones ARM64 desde la perspectiva de shellcode |
| [Roadmap](shellcode-compiler/roadmap/README.es.md) | Trabajo planificado |
| [Progress](shellcode-compiler/progress/README.es.md) | Estado de implementación |

---

## Runtimes Integrados

NeverC extiende el C estándar con runtimes integrados como bitcode LLVM. Cada uno se controla con un flag `-fbuiltin-<name>`.

**[Sistema de Runtime Integrado →](builtins/README.es.md)**

| Integrado | Flag | Descripción |
|-----------|------|-------------|
| [String integrado](builtins/string/README.es.md) | `-fbuiltin-string` | Tipo `string` con semántica de valor, métodos con punto, gestión automática de memoria, UTF-8 nativo |
| [mimalloc integrado](builtins/mimalloc/README.es.md) | `-fbuiltin-mimalloc` | Reemplazo transparente de asignador `mimalloc` de alto rendimiento `malloc`/`free`/`calloc`/`realloc` |
