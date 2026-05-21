**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# IR-Pass-Design — Prinzipien, Pipeline und Vorher/Nachher-Beispiele

> Dieses Dokument erklärt das **Warum** hinter jedem Pass in der Shellcode-Kompilierungspipeline.

## 0. Kernidee

Shellcode-Ziel in einem Satz: **Alles in der `.o` eliminieren, was zu einer Relocation würde, nur einen reinen Befehlsstrom übrig lassen, der direkt `mmap(RWX)` + `memcpy` + `blr` werden kann.**

## 1. ZeroRelocPass
- **Prep**: Linkage-Vereinheitlichung, `internal` + `alwaysinline`. Hardcoded Blocker ablehnen.
- **Stackify**: Mutable Globals → Entry `alloca`. Finale Validierung.
- **`placeEntryFirst`**: Entry an Offset 0 sicherstellen.

## 2. IndirectBrPass
GCC computed-goto → `switch` (null Relocations).

## 3. SyscallStubPass
libc extern → TargetDesc-gesteuerte Inline-Asm-Traps. POSIX-Compat + K&R-Autofix.

## 4. WinPEBImportPass
Win32 extern → PEB-Walk-Resolver (~190 APIs, 6 DLLs) + verschlüsselter Adress-Cache. Windows POSIX-Compat (13 Funktionsgruppen). Drei steckbare Funktionen (`__sc_derive_key`, `__sc_ptr_encrypt`, `__sc_ptr_decrypt`) für benutzerdefinierte Verschlüsselung.

## 5. MemIntrinPass
memcpy/memset/strlen/strcpy etc. → `internal alwaysinline` Byte-Loop-Helfer.

## 6. CompilerRtPass
`__int128` Division/Modulo → Inline Shift-Subtract Langdivision.

## 7. Data2TextPass
Phase 1: Konstant-GVs → Immediates/Stack-Stores. Phase 2: SROA-Residual-Split + Vektor-Inline.

## 8. AllBlrPass (optional)
Direkte Aufrufe → indirekte via volatile Slot.

## 9. Obfuskations-Hooks
11 Hook-Punkte. Siehe [plugin-interface.md §6](../plugin-interface/README.de.md#6-registration-position-selection--pic-coverage-matrix).

## 10. Zwei-Phasen-Design
Phase 1 reinigt originales IR; Phase 2 reinigt optimizer-eingeführte Konstrukte.

## 11. KernelImportPass (nur Ring-0)
Automatische Umschreibung extern → Resolver-gestützte indirekte Aufrufe. Siehe [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.de.md).

## 12. StringRuntimePass
Eingebaute `string`-Methoden → Stack-Arena-Varianten.

## 13. HeapArenaPass

Schreibt `malloc`/`free`/`calloc`/`realloc`-Aufrufe im Shellcode in eine hybride Allokationsstrategie um (standardmäßig aktiviert, über `-fshellcode-heap-arena` / `-fno-shellcode-heap-arena` steuerbar):

- **Kleine Allokationen (≤ 64 KB)**: Aus der `StringRuntimePass`-Stack-Arena (Bump-Allocator + Free-List-Wiederverwendung).
- **Große Allokationen (> 64 KB) oder Arena-OOM**: Fallback zum OS-Allokator (Windows: msvcrt.dll, Linux/macOS: mmap-Syscall).

**Sicherheit**: `free(NULL)` ist No-Op, `calloc` prüft Überlauf via `llvm.umul.with.overflow`, `realloc` liest die alte Blockgröße je nach Pointer-Herkunft (Arena / Fallback).

---

## 14. Fehlerdiagnose-Philosophie
Jeder harte Fehler = genau **eine umsetzbare Diagnose**. Keine Kaskaden.
