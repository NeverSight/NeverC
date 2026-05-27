**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentación NeverC](../README.es.md)

# Sistema de Runtime Integrado de NeverC

NeverC extiende el C estándar con runtimes integrados opcionales, incrustados directamente en el binario del compilador como bitcode LLVM. Al activarlos mediante flags del compilador, el runtime correspondiente se fusiona en el IR del usuario en tiempo de compilación — sin necesidad de cabeceras externas, bibliotecas o dependencias de enlace.

## Funcionalidades Integradas Disponibles

| Integrado | Flag | Predeterminado | Descripción |
|-----------|------|---------------|-------------|
| [**`string`**](string/README.es.md) | `-fbuiltin-string` | Desactivado | Tipo string con semántica de valor, métodos con sintaxis de punto, gestión automática de memoria y UTF-8 nativo |
| [**`mimalloc`**](mimalloc/README.es.md) | `-fbuiltin-mimalloc` | **Activado** | Asignador de memoria de alto rendimiento que reemplaza transparentemente `malloc`/`free`/`calloc`/`realloc` |
| [**`xorstr`**](xorstr/README.es.md) | `-fencrypt-call-strings` | Desactivado | Cifrado de cadenas en tiempo de compilación, descifrado XOR en pila, algoritmo anti-firma |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Visión General de la Arquitectura

Todas las funcionalidades integradas comparten la misma arquitectura de cuatro capas:

1. **Opciones de lenguaje y flags del controlador** — `LangOption` definido en `LangOptions.def`
2. **API Foundation** — proporciona `getEmbeddedBitcode()` e `isSupported()`
3. **Infraestructura CMake Bootstrap** — generación de bitcode en dos etapas
4. **Pase de fusión IR** — fusión de bitcode en el módulo del usuario en `PipelineStartEP`

---

## Diferencias de Diseño entre Integrados

| Aspecto | `string` | `mimalloc` |
|---------|----------|------------|
| **Estrategia de fusión** | Bajo demanda (BFS grafo de llamadas) | Archivo completo (todos los símbolos) |
| **Bitcode por plataforma** | Único (independiente de arquitectura) | Por SO (Linux / Darwin / Windows) |
| **Manejo de símbolos** | Todos internalizados | Puntos de entrada de override mantienen enlace externo |
| **Macro de preprocesador** | *(ninguna)* | `__NEVERC_MIMALLOC__` |
| **Modo shellcode** | Auto-activado, reescritura de arena | Suprimido (HeapArenaPass gestiona heap) |
| **Nivel de optimización** | `-O0` (compilación bitcode) | `-O2` (asignador crítico en rendimiento) |
| **DCE** | Poda pre-fusión + mark-and-sweep post-fusión | Sin DCE (semántica de archivo completo) |

---

## Bloqueos de Seguridad

| Condición | Efecto | Razón |
|-----------|--------|-------|
| `-fno-builtin` | Suprime mimalloc | Sin escenario de override de CRT |
| `-mkernel` | Suprime mimalloc | Sin heap de espacio de usuario en el kernel |
| `-fshellcode-mode` | Suprime mimalloc | Reemplazado por HeapArenaPass (basado en arena) |
| `-ffreestanding` | Suprime mimalloc | Sin libc para reemplazar |

El built-in `string` tiene su propia lógica de supresión (la reescritura de arena en el pipeline de shellcode reemplaza la asignación de heap).

### HeapArenaPass (Asignación de Heap en Shellcode)

Cuando `-fshellcode-mode` está activo, `mimalloc` se suprime pero las llamadas `malloc`/`free`/`calloc`/`realloc` se reescriben automáticamente por `HeapArenaPass` (habilitado por defecto). El pass usa una estrategia híbrida:

- **Asignaciones pequeñas (≤ 64 KB)**: Servidas desde una arena residente en la pila compartida con el runtime built-in `string` (asignador bump + reutilización de lista libre).
- **Asignaciones grandes (> 64 KB) o arena OOM**: Fallback al asignador del SO:
  - **Windows**: `malloc`/`free` resueltos desde `msvcrt.dll` vía PEB walk (`-mshellcode-win-peb-import`).
  - **Linux / macOS / Android**: `mmap`/`munmap` inlineados como llamadas al sistema nativas (`-mshellcode-syscall`).
  - **Sin pass de importación habilitado**: Solo arena; OOM devuelve `NULL`.

Control mediante flags del driver:

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass activado (por defecto)
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass desactivado (comportamiento original)
```

---

## Macros de Preprocesador

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc está activo — malloc/free se reemplazan transparentemente
#endif
```

---

## Estructura de Archivos

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h / BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   ├── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## Añadir una Nueva Funcionalidad Integrada

1. Añadir `LANGOPT` en `LangOptions.def`
2. Añadir flags del controlador en `Options.td.h`
3. Crear API Foundation (`BuiltinFoo.h` + `.cpp`)
4. Crear generador de código fuente
5. Añadir objetivos CMake bootstrap
6. Crear pase IR y registrar en `PipelineStartEP`
7. Definir macro de preprocesador
8. Añadir verificaciones de seguridad
9. Añadir pruebas
10. Añadir documentación y traducciones i18n
