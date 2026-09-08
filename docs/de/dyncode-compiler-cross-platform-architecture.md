**Sprachen**: [English](../dyncode-compiler-cross-platform-architecture.md) | [简体中文](../zh-CN/dyncode-compiler-cross-platform-architecture.md) | [繁體中文](../zh-TW/dyncode-compiler-cross-platform-architecture.md) | [日本語](../ja/dyncode-compiler-cross-platform-architecture.md) | [한국어](../ko/dyncode-compiler-cross-platform-architecture.md) | [Français](../fr/dyncode-compiler-cross-platform-architecture.md) | [Deutsch](dyncode-compiler-cross-platform-architecture.md) | [Español](../es/dyncode-compiler-cross-platform-architecture.md) | [Italiano](../it/dyncode-compiler-cross-platform-architecture.md) | [Русский](../ru/dyncode-compiler-cross-platform-architecture.md) | [العربية](../ar/dyncode-compiler-cross-platform-architecture.md)

[← DynCode-Compiler](dyncode-compiler.md)

# NeverC DynCode Cross-Platform-Architektur — Überblick

Dieses Dokument beschreibt die Designprinzipien hinter „ein Satz Passes für macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Lesen Sie dies vor der Erweiterung auf eine neue Plattform.

Verwandte Dokumente:
- [README.md](dyncode-compiler.md) — Überblick, CLI-Optionen, Schnellstart
- [ir-pass-design.md](dyncode-compiler-ir-pass-design.md) — IR-Schicht Pass-Verantwortlichkeiten
- [mir-pass-design.md](dyncode-compiler-mir-pass-design.md) — MIR-Schicht
- [kernel-mode-dyncode.md](dyncode-compiler-kernel-mode-dyncode.md) — Kernel-Kontext
- [platform-extension-guide.md](dyncode-compiler-platform-extension-guide.md) — Neue Plattformen hinzufügen

---

## 1. Dreidimensionale Matrix: OS × Arch × ExecutionLevel

Alle Plattformunterschiede konvergieren in einer **3D-Matrix**: 8 (OS, arch) × 2 ExecutionLevel = **16 Tabelleneinträge** von `describeTriple()`.

**Kernprinzip**: Passes lesen immer aus der Tabelle, nie `if (OS == Darwin)`. Neue Plattform = 1 Zeile + 1 Extraktor-Case.

## 2–3. Pipeline und PIC

Feste Reihenfolge mit 11 Obfuskations-Interposes. `isPICDefaultForced()` gibt überall **true** zurück.

## 4. User / Kernel orthogonal

- **User**: PEB-Walk / Syscall-Stub Pipeline.
- **Kernel**: SyscallStub/WinPEB kurzgeschlossen; KernelImportPass aktiviert.

## 5. User-Mode „normales C" Matrix

Große Arrays, FP-Konstanten, computed-goto, memcpy, `__int128`, Atomics, POSIX/Win32-Header — alles **direkt unterstützt** ohne Benutzereingriff.

## 6. MIR-Schicht: 3-Stufen-Pipeline (Reparatur / Fallback / Extraktion)

1. Plattformübergreifende Pseudo-Bereinigung
2. Tabellengesteuerte Befehlsumschreibung
3. Externe-Referenz / Konstantenpool-Audit

## 7–8. Extraktor und Obfuskations-Interposes

Extraktor: „intra-.text PC-rel akzeptieren, alles andere ablehnen". 11 Interposes über alle Schichten.

## 9–10. Erweiterung und Nicht-Ziele

Kosten: 1 TargetDesc-Zeile + Syscall-Tabelle + Extraktor-Case + Tests. Nicht-Ziele: C++/ObjC, 32-bit, libc-Einbettung (Heap-Allokation via `HeapArenaPass`), absolute Adressen.
