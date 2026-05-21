**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# NeverC Shellcode Cross-Platform-Architektur — Überblick

Dieses Dokument beschreibt die Designprinzipien hinter „ein Satz Passes für macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". Lesen Sie dies vor der Erweiterung auf eine neue Plattform.

Verwandte Dokumente:
- [README.md](../README.de.md) — Überblick, CLI-Optionen, Schnellstart
- [ir-pass-design.md](../ir-pass-design/README.de.md) — IR-Schicht Pass-Verantwortlichkeiten
- [mir-pass-design.md](../mir-pass-design/README.de.md) — MIR-Schicht
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.de.md) — Kernel-Kontext
- [platform-extension-guide.md](../platform-extension-guide/README.de.md) — Neue Plattformen hinzufügen

---

## 1. Dreidimensionale Matrix: OS × Arch × ExecutionLevel

Alle Plattformunterschiede konvergieren in einer **3D-Matrix**: 8 (OS, arch) × 2 ExecutionLevel = **16 Tabelleneinträge** von `describeTriple()`.

**Kernprinzip**: Passes lesen immer aus der Tabelle, nie `if (OS == Darwin)`. Neue Plattform = 1 Zeile + 1 Extraktor-Case.

## 2–3. Pipeline und PIC

Feste Reihenfolge mit 11 Obfuskations-Hooks. `isPICDefaultForced()` gibt überall **true** zurück.

## 4. User / Kernel orthogonal

- **User**: PEB-Walk / Syscall-Stub Pipeline.
- **Kernel**: SyscallStub/WinPEB kurzgeschlossen; KernelImportPass aktiviert.

## 5. User-Mode „normales C" Matrix

Große Arrays, FP-Konstanten, computed-goto, memcpy, `__int128`, Atomics, POSIX/Win32-Header — alles **direkt unterstützt** ohne Benutzereingriff.

## 6. MIR-Schicht: 3-Stufen-Pipeline (Reparatur / Fallback / Extraktion)

1. Plattformübergreifende Pseudo-Bereinigung
2. Tabellengesteuerte Befehlsumschreibung
3. Externe-Referenz / Konstantenpool-Audit

## 7–8. Extraktor und Obfuskations-Hooks

Extraktor: „intra-.text PC-rel akzeptieren, alles andere ablehnen". 11 Hooks über alle Schichten.

## 9–10. Erweiterung und Nicht-Ziele

Kosten: 1 TargetDesc-Zeile + Syscall-Tabelle + Extraktor-Case + Tests. Nicht-Ziele: C++/ObjC, 32-bit, libc-Einbettung (Heap-Allokation via `HeapArenaPass`), absolute Adressen.
