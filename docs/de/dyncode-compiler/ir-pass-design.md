**Sprachen**: [English](../../dyncode-compiler/ir-pass-design.md) | [简体中文](../../zh-CN/dyncode-compiler/ir-pass-design.md) | [繁體中文](../../zh-TW/dyncode-compiler/ir-pass-design.md) | [日本語](../../ja/dyncode-compiler/ir-pass-design.md) | [한국어](../../ko/dyncode-compiler/ir-pass-design.md) | [Français](../../fr/dyncode-compiler/ir-pass-design.md) | [Deutsch](ir-pass-design.md) | [Español](../../es/dyncode-compiler/ir-pass-design.md) | [Italiano](../../it/dyncode-compiler/ir-pass-design.md) | [Русский](../../ru/dyncode-compiler/ir-pass-design.md) | [العربية](../../ar/dyncode-compiler/ir-pass-design.md)

[← DynCode-Compiler](README.md)

# IR-Pass-Design — Prinzipien, Pipeline und Vorher/Nachher-Beispiele

> Dieses Dokument erklärt das **Warum** hinter jedem Pass in der DynCode-Kompilierungspipeline.

## 0. Kernidee

DynCode-Ziel in einem Satz: **Alles in der `.o` eliminieren, was zu einer Relocation würde, nur einen reinen Befehlsstrom übrig lassen, der direkt `mmap(RWX)` + `memcpy` + `blr` werden kann.**

## 1. ZeroRelocPass
- **Prep**: Linkage-Vereinheitlichung, `internal` + `alwaysinline`. Hardcoded Blocker ablehnen.
- **Stackify**: Mutable Globals → Entry `alloca`. Finale Validierung.
- **`placeEntryFirst`**: Entry an Offset 0 sicherstellen.

## 2. IndirectBrPass
GCC computed-goto → `switch` (null Relocations).

## 3. SyscallStubPass
libc extern → TargetDesc-gesteuerte Inline-Asm-Traps. POSIX-Compat + K&R-Autofix.

## 4. WinPEBImportPass
Win32 extern → PEB-Walk-Resolver (~210 APIs, 6 DLLs) + verschlüsselter Adress-Cache. Windows POSIX-Compat (13 Funktionsgruppen).

### 4.1 Adress-Cache-Verschlüsselung

Aufgelöste API-Adressen werden vor dem Speichern in Cache-Globalen verschlüsselt (Anti-Memory-Scan). Standard: XOR-freie arithmetische Zerlegung `(a + b) - 2*(a & b)` mit `volatile` Zwischenvariablen, um LLVMs Re-Optimierung zu `xor` zu verhindern. Infrastruktur (`PtrCacheHelpers.h`) wird von `WinPEBImportPass` und `KernelImportPass` geteilt.

**Drei steckbare Hilfsfunktionen** (`internal alwaysinline`):

| Funktion | Signatur | Zweck |
|----------|----------|-------|
| `__sc_derive_key` | `() → i64` | Laufzeit-Schlüsselableitung |
| `__sc_ptr_encrypt` | `(ptr) → i64` | Funktionszeiger für Cache verschlüsseln |
| `__sc_ptr_decrypt` | `(i64) → ptr` | Cache-Wert zurück zum Funktionszeiger entschlüsseln |

**Standard**: XOR-freie arithmetische Zerlegung. `key = (PEB + seed) - 2*(PEB & seed)` (Windows User-Mode) oder reiner Seed (Kernel). `encrypt/decrypt = (a + b) - (a & b) - (b & a)`, `volatile` Zwischenvariablen verhindern LLVMs Re-Optimierung zu `xor`.

**Cache-Slots**: `@__sc_cache_<dll>_<api>` (i64, Init 0, `.text`-Sektion, 8-Byte-Alignment). Fast/Slow-Pfad: `atomic_load → decrypt → indirekter Aufruf` (~10 Instruktionen) bzw. vollständiger PEB-Walk → `encrypt → cmpxchg weak` (lock-free, threadsicher).

**Überschreiben**: Gleichnamige Funktion im Quellcode definieren. `encrypt`/`decrypt` müssen mathematische Inverse sein, `always_inline`, keine externen Aufrufe.

## 5. MemIntrinPass
memcpy/memset/strlen/strcpy etc. → `internal alwaysinline` Byte-Loop-Helfer.

## 6. CompilerRtPass
`__int128` Division/Modulo → Inline Shift-Subtract Langdivision.

## 7. Data2TextPass
Phase 1: Konstant-GVs → Immediates/Stack-Stores. Phase 2: SROA-Residual-Split + Vektor-Inline.

## 8. AllBlrPass (optional)
Direkte Aufrufe → indirekte via volatile Slot.

## 9. Obfuskations-Interposes
11 Interpose-Punkte. Siehe [mir-pass-design.md §3](mir-pass-design.md#3-benutzer-obfuskations-interposes).

## 10. Zwei-Phasen-Design
Phase 1 reinigt originales IR; Phase 2 reinigt optimizer-eingeführte Konstrukte.

## 11. KernelImportPass (nur Ring-0)
Automatische Umschreibung extern → Resolver-gestützte indirekte Aufrufe. Siehe [kernel-mode-dyncode.md](kernel-mode-dyncode.md).

## 12. StringRuntimePass
Eingebaute `string`-Methoden → Stack-Arena-Varianten.

## 13. HeapArenaPass

Schreibt `malloc`/`free`/`calloc`/`realloc`-Aufrufe im DynCode in eine hybride Allokationsstrategie um (standardmäßig aktiviert, über `-fdyncode-heap-arena` / `-fno-dyncode-heap-arena` steuerbar):

- **Kleine Allokationen (≤ 64 KB)**: Aus der `StringRuntimePass`-Stack-Arena (Bump-Allocator + Free-List-Wiederverwendung).
- **Große Allokationen (> 64 KB) oder Arena-OOM**: Fallback zum OS-Allokator (Windows: msvcrt.dll, Linux/macOS: mmap-Syscall).

**Sicherheit**: `free(NULL)` ist No-Op, `calloc` prüft Überlauf via `llvm.umul.with.overflow`, `realloc` liest die alte Blockgröße je nach Pointer-Herkunft (Arena / Fallback).

---

## 14. Fehlerdiagnose-Philosophie
Jeder harte Fehler = genau **eine umsetzbare Diagnose**. Keine Kaskaden.
