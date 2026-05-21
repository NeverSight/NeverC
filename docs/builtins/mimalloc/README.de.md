**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Integriertes Laufzeitsystem](../README.de.md)

# Integrierter mimalloc-Allokator

## Übersicht

NeverC kann [mimalloc](https://github.com/microsoft/mimalloc) — Microsofts Hochleistungs-Speicherallokator — über LLVM-Bitcode-Zusammenführung direkt in kompilierte Binärdateien einbetten. Bei Aktivierung werden `malloc`, `free`, `calloc` und `realloc` zur Kompilierzeit transparent durch mimallocs Implementierungen ersetzt.

**Aktivierung:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## Plattformunterstützung

| Plattform | Triple | Status |
|-----------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Unterstützt |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Unterstützt |
| Android | `aarch64-linux-android` | Unterstützt |
| macOS x86_64 | `x86_64-apple-macosx` | Unterstützt |
| macOS AArch64 | `arm64-apple-macosx` | Unterstützt |
| iOS | `arm64-apple-ios` | Unterstützt |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Unterstützt |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Unterstützt |

---

## Automatische Unterdrückung

| Flag / Modus | Grund |
|-------------|-------|
| `-fno-builtin` | Kein CRT-Override-Szenario |
| `-mkernel` | Kein Userspace-Heap im Kernel |
| `-fshellcode-mode` | Kein Heap in Shellcode |
| `-ffreestanding` | Keine libc zum Überschreiben |

---

## Bootstrap-Prozess

```bash
ninja neverc                         # Phase 1: Leere Bitcode-Platzhalter
ninja neverc-bootstrap-mimalloc-bc   # Phase 2: Bitcode pro OS kompilieren
ninja neverc                         # Phase 3: Echtes Bitcode einbetten
```

---

## Compiler-Flag-Referenz

| Flag | Beschreibung |
|------|-------------|
| `-fbuiltin-mimalloc` | mimalloc-Override-Injektion aktivieren (standardmäßig aus) |
| `-fno-builtin-mimalloc` | mimalloc-Injektion explizit deaktivieren |

| Makro | Wert | Wann definiert |
|-------|------|---------------|
| `__NEVERC_MIMALLOC__` | `1` | Wenn `-fbuiltin-mimalloc` aktiv ist |
