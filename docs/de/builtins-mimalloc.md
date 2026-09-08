**Languages**: [English](../builtins-mimalloc.md) | [简体中文](../zh-CN/builtins-mimalloc.md) | [繁體中文](../zh-TW/builtins-mimalloc.md) | [日本語](../ja/builtins-mimalloc.md) | [한국어](../ko/builtins-mimalloc.md) | [Français](../fr/builtins-mimalloc.md) | [Deutsch](builtins-mimalloc.md) | [Español](../es/builtins-mimalloc.md) | [Italiano](../it/builtins-mimalloc.md) | [Русский](../ru/builtins-mimalloc.md) | [العربية](../ar/builtins-mimalloc.md)

[← NeverC Integriertes Laufzeitsystem](builtins.md)

# Integrierter `mimalloc`-Allokator

## Übersicht

NeverC bettet [mimalloc](https://github.com/microsoft/mimalloc) — Microsofts Hochleistungs-Speicherallokator — über LLVM-Bitcode-Zusammenführung direkt in kompilierte Binärdateien ein. `malloc`, `free`, `calloc` und `realloc` werden zur Kompilierzeit transparent durch mimallocs Implementierungen ersetzt.

**Standardmäßig aktiv**, überall dort, wo es einen libc-Heap zu ersetzen gibt: Ein gewöhnlicher Build alloziert bereits über mimalloc. Kernel- und Freestanding-Ziele werden automatisch ausgenommen; auf einem Host-Ziel schaltet `-fno-builtin-mimalloc` es ab.

```bash
neverc main.c -o main
```

---

## Verwendung

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # einfach
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # mit `string` kombiniert
neverc -fno-builtin-mimalloc main.c -o main                    # deaktivieren
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("Verwendung des mimalloc-Allokators\n");
#endif
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
| `-fms-kernel` | Windows-Kerneltreiber; ebenso, impliziert kein `-fno-builtin` |
| `-fandroid-kernel-driver-mode` | Android-Kernelmodul; ebenso |
| `-shared` / `-dynamiclib` | Das Ersetzen von `malloc` ist Sache des Programms, nicht einer Bibliothek; der Thread-lokale Heap braucht zudem initial-exec-TLS, das ein Shared Object nicht verwenden kann |
| `-fdyncode-mode` | Ersetzt durch HeapArenaPass (Arena + OS-Fallback) |
| `-ffreestanding` | Keine libc zum Überschreiben |

---

## Bootstrap-Prozess

```bash
ninja neverc                         # Phase 1: Leere Bitcode-Platzhalter
ninja neverc-bootstrap-mimalloc-bc   # Phase 2: Bitcode pro OS kompilieren
ninja neverc                         # Phase 3: Echtes Bitcode einbetten
```

---

## Architektur

mimalloc wird als LLVM-Bitcode in die Compiler-Binärdatei eingebettet. Bei der Benutzerkompilierung führt ein Module Pass den Bitcode vor der Optimierungspipeline in das Benutzer-IR zusammen. Pro OS separat kompiliert (Linux `mmap`, macOS `vm_allocate`, Windows `VirtualAlloc`), Auswahl über Target Triple. **Gesamtarchiv**-Semantik — alle Funktionen werden gelinkt.

---

## Dateistruktur

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPredefinedMacros.cpp
```

---

## Compiler-Flag-Referenz

| Flag | Beschreibung |
|------|-------------|
| `-fbuiltin-mimalloc` | `mimalloc`-Override-Injektion aktivieren (standardmäßig an für Hosted-Builds) |
| `-fno-builtin-mimalloc` | `mimalloc`-Injektion explizit deaktivieren |

| Makro | Wert | Wann definiert |
|-------|------|---------------|
| `__NEVERC_MIMALLOC__` | `1` | Wenn `-fbuiltin-mimalloc` aktiv ist |
