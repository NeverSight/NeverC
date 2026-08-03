**Sprachen**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**Der KI-freundliche C23-Compiler für Sicherheitsforschung — auf LLVM gebaut**

Integrierter Linker · DynCode-Pipeline · Integrierte Laufzeiten (`string` · `mimalloc` · `xorstr` · `strhash`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#funktionen)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#funktionen)

[Dokumentationsindex](../README.de.md) · [DynCode-Leitfaden](../dyncode-compiler/README.de.md) · [Integrierte Laufzeiten](../builtins/README.de.md) · [Plugin-API](../plugin-api/README.de.md) · [Roadmap](../roadmap/README.de.md)

</div>

---

> **Hinweis:** GitHub zeigt auf der Repository-Startseite immer `README.md` (Englisch) – keine automatische Spracherkennung. Nutzen Sie die Sprachlinks oben; in der [Dokumentation](../README.de.md) und dem [DynCode-Leitfaden](../dyncode-compiler/README.de.md) dieselbe Sprache über Sprachleiste und Breadcrumbs beibehalten.

## Überblick

NeverC kompiliert Standard-C in gehostete Binärdateien, Freestanding-Executables und positionsunabhängigen DynCode — alles aus einer Toolchain. Zielarchitekturen: **x86_64** und **AArch64** (nur Little-Endian). Zukünftige Versionen werden **EVM** (Ethereum Smart Contracts) und **Solana eBPF** (On-Chain-Programme) als Kompilierungsziele hinzufügen.

## Warum NeverC?

C ist bereits die einfachste Systemsprache. NeverC macht sie noch einfacher:

- **Reines C23, mehr nicht** — Keine Templates, kein RAII, kein Operator-Overloading, kein versteckter Kontrollfluss. Was Sie lesen, wird ausgeführt.
- **Eingebauter `string`** — Werttyp-String mit `+`, `==`, `.starts_with()` und automatischer Freigabe — ohne C++.
- **Keine Exceptions** — Fehlerbehandlung bleibt explizit. Kein Stack-Unwinding, keine Performance-Überraschungen.
- **Einzelne Binärdatei** — Compiler + Linker + Laufzeiten in einer einzigen ausführbaren Datei. Null externe Abhängigkeiten.
- **LLM-freundlich** — Minimale Grammatik und deterministische Semantik sorgen dafür, dass KI-generierter NeverC-Code häufiger korrekt kompiliert als C++-Alternativen.
- **Echte Cross-Kompilierung** — Windows PE, Linux ELF, macOS Mach-O, Android ELF und DynCode von macOS oder Linux bauen — keine VM, kein Dual-Boot, keine SDK-Suche. Plattform-SDKs sind im Compiler integriert.
- **Erweiterbar ohne Reibung** — Ein einziger C-Header, 130 benannte Compilerphasen, und Sie haben ein [Compiler-Plugin](../plugin-api/README.de.md), das in jede Phase eingreifen kann — von der IR-Optimierung bis zur finalen Binärausgabe — ohne LLVM-Kenntnisse.
- **Sicherheitsforschung eingebaut** — DynCode-Kompilierung, Kompilierzeit-Stringverschlüsselung und plattformübergreifende PE-Generierung sind nativ in den Compiler integriert — keine nachträglich angehängten externen Skripte.

## Funktionen

- **[DynCode-Compiler](../dyncode-compiler/README.de.md)** — mehrstufige IR/MIR-Pipeline, plattformübergreifende Extraktion, Import-/Syscall-Lowering, Kernelmodus, Bad-Byte-Audit, Plugin-Architektur
- **Integrierter Linker** — COFF, ELF und Mach-O in einem Binary; kein externes `ld` oder `link.exe`
- **Cross-Kompilierung** — Windows PE, Linux ELF, macOS Mach-O und Android ELF von jedem Host mit integrierten Plattform-SDKs
- **[Integrierte Laufzeiten](../builtins/README.de.md)** — in den Compiler eingebettete LLVM-Bitcode-Laufzeiten: [`string`](../builtins/string/README.de.md) (Werttyp-String, automatische Speicherverwaltung), [`mimalloc`](../builtins/mimalloc/README.de.md) (transparenter Hochleistungs-Allokator-Override, außerhalb von Kernel- und Freestanding-Zielen standardmäßig aktiv), [`xorstr`](../builtins/xorstr/README.de.md) (Kompilierzeit-Stringverschlüsselung mit Anti-Signatur-Entschlüsselung) und [`strhash`](../builtins/strhash/README.de.md) (Kompilierzeit-Zeichenketten-Hashing mit übereinstimmender Laufzeit)
- **[Plugin-API](../plugin-api/README.de.md)** — Reine C-ABI für Out-of-Tree-Plugins; Single-Header-SDK, null LLVM/CRT-Abhängigkeiten, über Treiber-, Präprozessor-, AST-, IR-, MIR-, MC-, Objekt-, Link-, LTO- und dyncode-Phasen hinweg
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

> **Hinweis:** Der eingebaute **`string`**-Typ erfordert **`-fbuiltin-string`** für `.c`-Dateien. Er wird automatisch für [**`.nc`-Dateien**](../nc-extension/README.de.md) und im **`-fdyncode`**-Modus aktiviert.

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

Ausführliche Designnotizen, Plattformmatrix, CLI-Referenz und Beispiele: **[Dokumentationsindex](../README.de.md)**. Vollständig kompilierbare Beispiele: **[examples](../examples/README.de.md)**.

## Bauen

Voraussetzungen, Build-Befehle, vorgefertigte macOS-Binärdateien, Cross-Kompilierung nach Windows, PATH-Einrichtung und Umgebungskonfiguration — siehe **[Lokale Entwicklung](../local-dev/README.de.md)**.

## Mitwirken

NeverC ist **bewusst nur C** (C23). C++, Objective-C, CUDA und ähnliche Sprach-Frontends
liegen außerhalb des Projektumfangs; entsprechende Pull Requests werden geschlossen.
Wer eine C++-orientierte LLVM-Toolchain braucht, sollte
[llvm-msvc](https://github.com/backengineering/llvm-msvc) in Betracht ziehen.

Große Änderungen an Sprache, ABI oder Runtime bitte zuerst als Issue diskutieren, bevor
ein Pull Request kommt.

Der Standard-Entwicklungsbranch ist **`dev`**. Vor dem Start klonen und `dev` auschecken; Pull Requests bitte gegen `dev` öffnen.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Lizenz

[AGPL-3.0](../../LICENSE)

LLVM-Komponenten behalten die [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT)-Lizenz.
