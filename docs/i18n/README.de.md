**Sprachen**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Der KI-freundliche C23-Compiler für Sicherheitsforschung — auf LLVM gebaut**

Integrierter Linker · Shellcode-Pipeline · Integrierte Laufzeiten (`string` · `mimalloc` · `xorstr`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#funktionen)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-kompilierung-nach-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#funktionen)

[Dokumentationsindex](../README.de.md) · [Shellcode-Leitfaden](../shellcode-compiler/README.de.md) · [Integrierte Laufzeiten](../builtins/README.de.md) · [Plugin-API](../plugin-api/README.de.md)

</div>

---

> **Hinweis:** GitHub zeigt auf der Repository-Startseite immer `README.md` (Englisch) – keine automatische Spracherkennung. Nutzen Sie die Sprachlinks oben; in der [Dokumentation](../README.de.md) und dem [Shellcode-Leitfaden](../shellcode-compiler/README.de.md) dieselbe Sprache über Sprachleiste und Breadcrumbs beibehalten.

## Überblick

NeverC kompiliert Standard-C in gehostete Binärdateien, Freestanding-Executables und positionsunabhängigen Shellcode — alles aus einer Toolchain. Zielarchitekturen: **x86_64** und **AArch64** (nur Little-Endian).

## Warum NeverC?

C ist bereits die einfachste Systemsprache. NeverC macht sie noch einfacher:

- **Reines C23, mehr nicht** — Keine Templates, kein RAII, kein Operator-Overloading, kein versteckter Kontrollfluss. Was Sie lesen, wird ausgeführt.
- **Eingebauter `string`** — Werttyp-String mit `+`, `==`, `.starts_with()` und automatischer Freigabe — ohne C++.
- **Keine Exceptions** — Fehlerbehandlung bleibt explizit. Kein Stack-Unwinding, keine Performance-Überraschungen.
- **Einzelne Binärdatei** — Compiler + Linker + Laufzeiten in einer einzigen ausführbaren Datei. Null externe Abhängigkeiten.
- **LLM-freundlich** — Minimale Grammatik und deterministische Semantik sorgen dafür, dass KI-generierter NeverC-Code häufiger korrekt kompiliert als C++-Alternativen.
- **Echte Cross-Kompilierung** — Windows-Executables und Shellcode von macOS oder Linux bauen — keine VM, kein Dual-Boot, keine SDK-Suche. Das Windows SDK ist im Compiler integriert.
- **[Plugin-System](../plugin-api/README.de.md), ein Header genügt** — Reine C-API mit 20+ Hook-Points über IR, MIR, Binary und Linker. Schreiben Sie ein Plugin in jeder Sprache und greifen Sie an nahezu jedem Punkt der Kompilierungs-Pipeline ein — ohne LLVM-Header.
- **Sicherheitsforschung eingebaut** — Shellcode-Kompilierung, Kompilierzeit-Stringverschlüsselung und plattformübergreifende PE-Generierung sind nativ in den Compiler integriert — keine nachträglich angehängten externen Skripte.

## Funktionen

- **[Shellcode-Compiler](../shellcode-compiler/README.de.md)** — mehrstufige IR/MIR-Pipeline, plattformübergreifende Extraktion, Import-/Syscall-Lowering, Kernelmodus, Bad-Byte-Audit, Plugin-Architektur
- **Integrierter Linker** — COFF, ELF und Mach-O in einem Binary; kein externes `ld` oder `link.exe`
- **Cross-Kompilierung** — Windows-PE von macOS/Linux mit gebündeltem MSVC-SDK
- **[Integrierte Laufzeiten](../builtins/README.de.md)** — in den Compiler eingebettete LLVM-Bitcode-Laufzeiten: [`string`](../builtins/string/README.de.md) (Werttyp-String, automatische Speicherverwaltung), [`mimalloc`](../builtins/mimalloc/README.de.md) (transparenter Hochleistungs-Allokator-Override) und [`xorstr`](../builtins/xorstr/README.de.md) (Kompilierzeit-Stringverschlüsselung mit Anti-Signatur-Entschlüsselung)
- **[Plugin-API](../plugin-api/README.de.md)** — Reine C-ABI für Out-of-Tree-Pass-Plugins; Single-Header-SDK, null LLVM/CRT-Abhängigkeiten, IR-, MIR-, Binary- und Linker-Hook-Points
- **[`.nc`-Erweiterung](../nc-extension/README.de.md)** — `.nc`-Dateierweiterung aktiviert automatisch alle NeverC-Funktionen (`string`, Integer-Typen im Rust-Stil) ohne zusätzliche Flags
- **Schlanker LLVM-Build** — nur x86_64 / AArch64-Backends; C++/ObjC/OpenMP-Pfade entfernt

## Schnellbeispiel

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

> **Hinweis:** Der eingebaute **`string`**-Typ erfordert **`-fbuiltin-string`** für `.c`-Dateien. Er wird automatisch für [**`.nc`-Dateien**](../nc-extension/README.de.md) und im **`-fshellcode`**-Modus aktiviert.

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

Ausführliche Designnotizen, Plattformmatrix, CLI-Referenz und Beispiele: **[Dokumentationsindex](../README.de.md)**. Vollständig kompilierbare Beispiele: **[examples/](../../examples/)**.

## Vorgefertigte macOS-Binärdateien

Das Release ist ad-hoc signiert (keine Apple Developer ID, nicht notarisiert). Wenn Sie es über einen Browser heruntergeladen haben, entfernen Sie nach dem Entpacken einmalig das Quarantine-Attribut:

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

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
cmake --build build-neverc --target check-neverc
```

### Verifizieren

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-Kompilierung nach Windows

NeverC bündelt ein Windows SDK und WDK in `runtime/`; keine externe Einrichtung erforderlich.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows-Shellcode (`-fshellcode`, PEB-Importauflösung usw.): [Shellcode-Compiler-Dokumentation](../shellcode-compiler/README.de.md).

## Mitwirken

Der Standard-Entwicklungsbranch ist **`dev`**. Vor dem Start klonen und `dev` auschecken; Pull Requests bitte gegen `dev` öffnen.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Lizenz

[AGPL-3.0](../../LICENSE)

LLVM-Komponenten behalten die [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT)-Lizenz.
