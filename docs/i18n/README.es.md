**Idiomas**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**El compilador C23 compatible con IA para investigación en seguridad, construido sobre LLVM**

Enlazador integrado · Pipeline shellcode · Runtimes integrados (`string` · `mimalloc` · `xorstr`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#características)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#compilación-cruzada-a-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#características)

[Documentación](../README.es.md) · [Guía shellcode](../shellcode-compiler/README.es.md) · [Runtimes integrados](../builtins/README.es.md) · [API de Plugins](../plugin-api/README.es.md)

</div>

---

> **Nota:** GitHub siempre muestra `README.md` (inglés) como página principal del repositorio (sin detección automática de idioma). Use los enlaces de idioma arriba; en la [documentación](../README.es.md) y la [guía shellcode](../shellcode-compiler/README.es.md), mantenga el mismo idioma con la barra de idioma y las migas de pan.

## Descripción general

NeverC compila C estándar en binarios alojados, ejecutables freestanding y shellcode independiente de la posición, todo desde una única cadena de herramientas. Apunta a **x86_64** y **AArch64** (solo little-endian).

## ¿Por qué NeverC?

C ya es el lenguaje de sistemas más simple. NeverC lo hace aún más simple:

- **C23 puro, nada más** — Sin templates, sin RAII, sin sobrecarga de operadores, sin flujo de control oculto. Lo que lees es lo que se ejecuta.
- **`string` integrado** — Tipo string con semántica de valor, `+`, `==`, `.starts_with()` y liberación automática — sin C++.
- **Sin excepciones** — El manejo de errores es siempre explícito. Sin desenrollado de pila, sin sorpresas de rendimiento.
- **Binario único** — Compilador + enlazador + runtimes en un solo ejecutable. Cero dependencias externas.
- **Compatible con LLM** — Gramática mínima y semántica determinista hacen que el código NeverC generado por IA compile correctamente con más frecuencia que las alternativas en C++.
- **Verdadera compilación cruzada** — Compile ejecutables Windows y shellcode desde macOS o Linux — sin VM, sin arranque dual, sin buscar SDKs. El Windows SDK viene integrado en el compilador.
- **Extensible sin fricción** — Un encabezado C, 20+ puntos de enganche, y tienes un [plugin de compilador](../plugin-api/README.es.md) capaz de intervenir en cualquier etapa — desde la optimización IR hasta la salida binaria final — sin conocer LLVM.
- **Investigación en seguridad integrada** — Compilación de shellcode, cifrado de cadenas en tiempo de compilación y generación PE multiplataforma están integrados nativamente en el compilador — no parches añadidos con scripts externos.

## Características

- **[Compilador de shellcode](../shellcode-compiler/README.es.md)** — pipeline IR/MIR multietapa, extracción multiplataforma, resolución de importaciones/syscalls, modo kernel, auditoría de bytes prohibidos, arquitectura de plugins
- **Enlazador integrado** — COFF, ELF y Mach-O en un solo binario; sin `ld` o `link.exe` externos
- **Compilación cruzada** — PE de Windows desde macOS/Linux con SDK MSVC incluido
- **[Runtimes integrados](../builtins/README.es.md)** — runtimes LLVM bitcode integrados en el compilador: [`string`](../builtins/string/README.es.md) (string con semántica de valor, gestión automática de memoria), [`mimalloc`](../builtins/mimalloc/README.es.md) (reemplazo transparente de asignador de alto rendimiento) y [`xorstr`](../builtins/xorstr/README.es.md) (cifrado de cadenas en tiempo de compilación con descifrado anti-firma)
- **[API de Plugins](../plugin-api/README.es.md)** — ABI C pura para plugins de pases fuera del árbol; SDK de un solo encabezado, cero dependencias LLVM/CRT, puntos de enganche IR, MIR, Binary y Linker
- **[Extensión `.nc`](../nc-extension/README.es.md)** — usa `.nc` para habilitar automáticamente todas las funcionalidades NeverC (`string`, tipos enteros estilo Rust) sin flags adicionales
- **Build LLVM ligero** — solo backends x86_64 / AArch64; rutas C++/ObjC/OpenMP eliminadas

## Ejemplo rápido

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
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

Consulte el **[índice de documentación](../README.es.md)** para diseño detallado, matriz de plataformas, referencia CLI y ejemplos. Ejemplos compilables completos: **[examples/](../../examples/)**.

## Binarios macOS precompilados

La release está firmada en ad-hoc (sin Apple Developer ID, sin notarización). Si la descargó vía navegador, elimine una sola vez el atributo quarantine tras extraer:

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

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
cmake --build build-neverc --target check-neverc
```

### Verificar

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Compilación cruzada a Windows

NeverC incluye un Windows SDK y WDK en `runtime/`; no se necesita configuración externa.

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
