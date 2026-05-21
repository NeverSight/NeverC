**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema de Runtime Integrado de NeverC](../README.es.md)

# Asignador `mimalloc` Integrado

## Descripción

NeverC puede integrar [mimalloc](https://github.com/microsoft/mimalloc) — el asignador de memoria de alto rendimiento de Microsoft — directamente en los binarios compilados mediante fusión de bitcode LLVM. Al activarlo, `malloc`, `free`, `calloc` y `realloc` se reemplazan transparentemente por las implementaciones de mimalloc en tiempo de compilación.

**Activación:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## Uso

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # básico
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # combinado con `string`
neverc -fno-builtin-mimalloc main.c -o main                    # desactivar
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("Usando asignador mimalloc\n");
#endif
```

---

## Soporte de Plataformas

| Plataforma | Triple | Estado |
|-----------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Soportado |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Soportado |
| Android | `aarch64-linux-android` | Soportado |
| macOS x86_64 | `x86_64-apple-macosx` | Soportado |
| macOS AArch64 | `arm64-apple-macosx` | Soportado |
| iOS | `arm64-apple-ios` | Soportado |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Soportado |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Soportado |

---

## Supresión Automática

| Flag / Modo | Razón |
|-------------|-------|
| `-fno-builtin` | Sin escenario de override de CRT |
| `-mkernel` | Sin heap de espacio de usuario en el kernel |
| `-fshellcode-mode` | Sin heap en shellcode |
| `-ffreestanding` | Sin libc para reemplazar |

---

## Proceso de Bootstrap

```bash
ninja neverc                         # Etapa 1: Placeholders de bitcode vacíos
ninja neverc-bootstrap-mimalloc-bc   # Etapa 2: Compilar bitcode por SO
ninja neverc                         # Etapa 3: Integrar bitcode real
```

---

## Arquitectura

mimalloc se integra como bitcode LLVM en el binario del compilador. Durante la compilación del usuario, un Module Pass fusiona el bitcode en el IR antes del pipeline de optimización. Compilado por separado por SO (Linux `mmap`, macOS `vm_allocate`, Windows `VirtualAlloc`), seleccionado vía target triple. Semántica de **archivo completo** — todas las funciones se enlazan.

---

## Estructura de Archivos

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## Referencia de Flags del Compilador

| Flag | Descripción |
|------|-------------|
| `-fbuiltin-mimalloc` | Activar inyección de override `mimalloc` (activado por defecto para builds alojados) |
| `-fno-builtin-mimalloc` | Desactivar explícitamente la inyección `mimalloc` |

| Macro | Valor | Cuándo se define |
|-------|-------|-----------------|
| `__NEVERC_MIMALLOC__` | `1` | Cuando `-fbuiltin-mimalloc` está activo |
