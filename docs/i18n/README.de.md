**Sprachen**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Sicherheitsforschungsorientierter C23-Compiler auf LLVM-Basis**

Integrierter Linker · Shellcode-Pipeline · Eingebauter `string`-Typ

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#funktionen)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-kompilierung-nach-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#funktionen)

[Dokumentationsindex](../README.de.md) · [Shellcode-Leitfaden](../shellcode-compiler/README.de.md) · [Eingebauter String](../builtins/string/README.de.md)

</div>

---

> **Hinweis:** GitHub zeigt auf der Repository-Startseite immer `README.md` (Englisch) – keine automatische Spracherkennung. Nutzen Sie die Sprachlinks oben; in der [Dokumentation](../README.de.md) und dem [Shellcode-Leitfaden](../shellcode-compiler/README.de.md) dieselbe Sprache über Sprachleiste und Breadcrumbs beibehalten.

## Überblick

NeverC kompiliert Standard-C in gehostete Binärdateien, Freestanding-Executables und positionsunabhängigen Shellcode — alles aus einer Toolchain. Zielarchitekturen: **x86_64** und **AArch64** (nur Little-Endian).

## Funktionen

- **[Shellcode-Compiler](../shellcode-compiler/README.de.md)** — mehrstufige IR/MIR-Pipeline, plattformübergreifende Extraktion, Import-/Syscall-Lowering, Kernelmodus, Bad-Byte-Audit, Plugin-Architektur
- **Integrierter Linker** — COFF, ELF und Mach-O in einem Binary; kein externes `ld` oder `link.exe`
- **Cross-Kompilierung** — Windows-PE von macOS/Linux mit gebündeltem MSVC-SDK
- **[Eingebauter `string`-Typ](../builtins/string/README.de.md)** — Werttyp-String mit Punkt-Methodensyntax, automatischer Speicherverwaltung und nativer UTF-8-Unterstützung
- **Schlanker LLVM-Build** — nur x86_64 / AArch64-Backends; C++/ObjC/OpenMP-Pfade entfernt

## Schnellbeispiel

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());
    return 0;
}
```

> **Hinweis:** Der eingebaute **`string`**-Typ erfordert **`-fbuiltin-string`** bei normalen gehosteten Binärdateien. **`-fshellcode`** aktiviert ihn automatisch.

```bash
# macOS arm64
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin

# Windows x86_64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
```

Ausführliche Designnotizen, Plattformmatrix, CLI-Referenz und Beispiele: **[Dokumentationsindex](../README.de.md)**.

## Bauen

### Voraussetzungen

- CMake 3.20+
- Ninja
- C++17-Host-Compiler (GCC, Clang oder MSVC)

### Konfigurieren

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### Bauen

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` wird automatisch erkannt und aktiviert, falls vorhanden.

### Testen

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### Verifizieren

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-Kompilierung nach Windows

[xwin](https://github.com/Jake-Shadle/xwin)-SDK-Splat nach `build-neverc/sdk/msvc/` legen.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows-Shellcode (`-fshellcode`, PEB-Importauflösung usw.): [Shellcode-Compiler-Dokumentation](../shellcode-compiler/README.de.md).

## Lizenz

[AGPL-3.0](../../LICENSE)

LLVM-Komponenten behalten die [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT)-Lizenz.
