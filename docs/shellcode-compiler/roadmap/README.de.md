**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# Roadmap

Dieses Dokument verfolgt geplante, laufende oder absichtlich zurückgestellte Funktionen.

## Aktueller Stand

Die Shellcode-Pipeline von NeverC umfasst:

- Vollständige LLVM-IR-Pipeline mit 11+ dedizierten Passes
- COFF- / ELF- / Mach-O-Extraktoren
- Win32-PEB-Walk-Import-Auflösung (ROR-13-Hash, 6 DLL-Buckets)
- Direkte Syscall-Absenkung (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Kernel-Modus-Unterstützung (Windows, Linux)
- Bad-Byte-Audit mit konfigurierbaren Profilen
- Plugin-SDK für Bad-Byte-Rewriter und Zeichensatz-Encoder
- Größen- / Ausrichtungs- / Padding-Beschränkungen (`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`)
- 11 Obfuskations-Hooks über IR-, MIR- und Byte-Stream-Schichten

## Abgeschlossen (2026-04)

1. **Größen- / Ausrichtungs- / Padding-Beschränkungen** — Eingebaut. `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` werden am Ende von `finalizeShellcodeBytes` ausgeführt. Der Treiber lehnt widersprüchliche Konfigurationen ab (z.B. Padding-Byte im Bad-Byte-Set oder Padding ohne align/max-length).

2. **Bad-Byte-Rewriter-Schnittstelle** — Grundgerüst eingebaut, keine eingebauten Strategien. `Plugin.h::registerBadByteRewriteStrategy` stellt das SDK bereit. `-fshellcode-bad-byte-rewrite` / `-fno-...` steuert, ob die Finalize-Kette Rewriter aufruft. Deaktivierung fällt auf Nur-Audit-Modus zurück. Downstream-Bibliotheken registrieren Capstone-basierte oder benutzerdefinierte Rewrite-Strategien.

3. **Zeichensatz-Encoder-Schnittstelle** — Grundgerüst eingebaut, keine eingebauten Zeichensätze. `Plugin.h::registerCharsetEncoder` stellt ein `(name, Encode, Stub, IsCharsetMember)`-Tupel bereit. Bei gesetztem `-fshellcode-charset=<name>` ersetzt die Finalize-Phase `.text` durch `Stub(target) || Encode(text, target)` und validiert alle Ausgabebytes gegen den Zeichensatz. Druckbare / alphanumerische / benutzerdefinierte Encoder werden von Downstream-Bibliotheken registriert.

## Geplant — Plugin-Schicht (über Hooks)

Diese Fähigkeiten sind **absichtlich nicht eingebaut**. Sie gehören zur Strategie-/Obfuskationsschicht und sind so konzipiert, dass sie von Drittanbieter-Plugins über Hook- und Plugin-Schnittstellen bereitgestellt werden.

| Funktion | Hook-Punkt | Hinweise |
|----------|-----------|----------|
| Anti-Disassembly | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Instruktionspräfix-Störung, Sprung-Umordnung, Junk-Einfügung |
| Polymorphismus | `RunAfterFinalMIR` / `RunPostExtract` | Seed-basierte Ausgabevariation pro Kompilierung |
| Stufen-Encoder (XOR / RC4 / selbstentschlüsselnd) | `RunPostExtract` / `RunPostFinalize` | Kompilierzeit-Stub-Erzeugung + Payload-Verschlüsselung |
| Indirekte Syscalls (Halos / Tartarus / Recycled Gate) | IR-Level-Plugin oder `RunPostExtract` | Runtime-ntdll-Gadget-Scanning |
| Sleep-Maske / Callstack-Spoofing | IR-Pass-Plugin | Ekko- / FOLIAGE- / Cronos-Muster |
| ETW- / AMSI-Patching | IR-Pass-Plugin | Runtime-Patch-Sequenzen |
| Modul-Stomping / Unhooking | IR-Pass-Plugin | Speichermanipulationsmuster |

## Plugin-Hook-Übersicht

11 Hooks in drei Schichten:

**IR-Schicht (6 Hooks, empfangen `ModulePassManager &`)**:
- `RunBeforePrep` — Vor jedem Shellcode-Pass
- `RunAfterPrep` — Nach Linkage-Vereinheitlichung
- `RunBeforeInlining` — Letzte Chance vor AlwaysInliner
- `RunAfterInlining` — IR vollständig in eine Funktion geflacht
- `RunAfterStackify` — Endgültige IR-Form vor Codegen
- `RunAfterFinalIR` — Nach `AllBlrPass`, der absolut letzte IR-Hook

**MIR-Schicht (3 Hooks, empfangen `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Register zugewiesen, CFI/EH-Pseudos noch vorhanden
- `RunAfterPreEmit` — Nach `MIRPrepPass`-Bereinigung, nächster Zustand zu finalen Bytes
- `RunAfterFinalMIR` — Nach LLVM `addPreEmitPass2()`, direkt vor AsmPrinter

**Byte-Stream-Schicht (2 Hooks, empfangen `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Vor Finalize, wird noch von Rewriter/Encoder/Audit/Sizing verarbeitet
- `RunPostFinalize` — Nach Finalize, letzter Moment vor dem Schreiben auf die Festplatte; NeverC führt keine weitere Prüfung durch

## Finalize-Pipeline

Jeder Extraktor ruft `finalizeShellcodeBytes` auf, bevor die `.bin` geschrieben wird:

```
applyPostExtractObfuscationHook       (C Plugin API: NEVERC_HOOK_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (eingebautes hartes Audit)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationHook      (C Plugin API: NEVERC_HOOK_SC_POST_FINALIZE)
```

Verwendung und Codebeispiele siehe [Plugin API Dokumentation](../../plugin-api/README.de.md).

## Nicht geplant

- **Cross-Language-Frontend** — NeverC akzeptiert nur sein eigenes C23-Frontend. Die IR-Pipeline ist vom Frontend entkoppelt, aber die Annahme von externem Bitcode (z.B. von `rustc` oder `zig`) ist kein Projektziel.
