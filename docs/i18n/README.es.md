**Idiomas**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Compilador C23 orientado a la investigación en seguridad, construido sobre LLVM**

Enlazador integrado · Pipeline shellcode · Runtimes integrados (`string` · `mimalloc`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#características)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#compilación-cruzada-a-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#características)

[Documentación](../README.es.md) · [Guía shellcode](../shellcode-compiler/README.es.md) · [Runtimes integrados](../builtins/README.es.md)

</div>

---

> **Nota:** GitHub siempre muestra `README.md` (inglés) como página principal del repositorio (sin detección automática de idioma). Use los enlaces de idioma arriba; en la [documentación](../README.es.md) y la [guía shellcode](../shellcode-compiler/README.es.md), mantenga el mismo idioma con la barra de idioma y las migas de pan.

## Descripción general

NeverC compila C estándar en binarios alojados, ejecutables freestanding y shellcode independiente de la posición, todo desde una única cadena de herramientas. Apunta a **x86_64** y **AArch64** (solo little-endian).

## Características

- **[Compilador de shellcode](../shellcode-compiler/README.es.md)** — pipeline IR/MIR multietapa, extracción multiplataforma, resolución de importaciones/syscalls, modo kernel, auditoría de bytes prohibidos, arquitectura de plugins
- **Enlazador integrado** — COFF, ELF y Mach-O en un solo binario; sin `ld` o `link.exe` externos
- **Compilación cruzada** — PE de Windows desde macOS/Linux con SDK MSVC incluido
- **[Runtimes integrados](../builtins/README.es.md)** — runtimes LLVM bitcode integrados en el compilador: [`string`](../builtins/string/README.es.md) (string con semántica de valor, gestión automática de memoria) y [`mimalloc`](../builtins/mimalloc/README.es.md) (reemplazo transparente de asignador de alto rendimiento)
- **[Extensión `.nc`](../nc-extension/README.es.md)** — usa `.nc` para habilitar automáticamente todas las funcionalidades NeverC (`string`, tipos enteros estilo Rust) sin flags adicionales
- **Build LLVM ligero** — solo backends x86_64 / AArch64; rutas C++/ObjC/OpenMP eliminadas

## Ejemplo rápido

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());
    return 0;
}
```

> **Nota:** El tipo **`string`** integrado requiere **`-fbuiltin-string`** para archivos `.c`. Se habilita automáticamente para [**archivos `.nc`**](../nc-extension/README.es.md) y en modo **`-fshellcode`**.

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

Consulte el **[índice de documentación](../README.es.md)** para diseño detallado, matriz de plataformas, referencia CLI y ejemplos.

## Compilación

### Requisitos

- CMake 3.20+
- Ninja
- Compilador host C++17 (GCC, Clang o MSVC)

### Configurar

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### Compilar

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` se detecta y habilita automáticamente si está presente.

### Probar

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### Verificar

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Compilación cruzada a Windows

Colocar un splat de SDK [xwin](https://github.com/Jake-Shadle/xwin) en `build-neverc/sdk/msvc/`.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Para shellcode de Windows (`-fshellcode`, resolución PEB, etc.), véase la [documentación del compilador de shellcode](../shellcode-compiler/README.es.md).

## Contribuir

La rama de desarrollo predeterminada es **`dev`**. Clone el repositorio, cambie a `dev` antes de trabajar y abra pull requests contra `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Licencia

[AGPL-3.0](../../LICENSE)

Los componentes LLVM conservan la licencia [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).
