**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← DynCode-Compiler](../README.de.md)

# Roadmap

Dieses Dokument verfolgt geplante, laufende oder absichtlich zurückgestellte Funktionen.

## Aktueller Stand

Die DynCode-Pipeline von NeverC umfasst:

- Vollständige LLVM-IR-Pipeline mit 11+ dedizierten Passes
- COFF- / ELF- / Mach-O-Extraktoren
- Win32-PEB-Walk-Import-Auflösung (ROR-13-Hash, 6 DLL-Buckets)
- Direkte Syscall-Absenkung (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Kernel-Modus-Unterstützung (Windows, Linux)
- Bad-Byte-Audit mit konfigurierbaren Profilen
- Plugin-SDK für Bad-Byte-Rewriter und Zeichensatz-Encoder
- Größen- / Ausrichtungs- / Padding-Beschränkungen (`-fdyncode-max-length=`, `-fdyncode-align=`, `-fdyncode-pad=`)
- 11 Obfuskations-Interposes über IR-, MIR- und Byte-Stream-Schichten

## Abgeschlossen (2026-04)

1. **Größen- / Ausrichtungs- / Padding-Beschränkungen** — Eingebaut. `-fdyncode-max-length=`, `-fdyncode-align=`, `-fdyncode-pad=` werden am Ende von `finalizeDynCodeBytes` ausgeführt. Der Treiber lehnt widersprüchliche Konfigurationen ab (z.B. Padding-Byte im Bad-Byte-Set oder Padding ohne align/max-length).

2. **Out-of-Tree C Plugin-API** — Reine C-ABI-Plugin-Schnittstelle ([`NevercPluginAPI.h`]) für benutzerdefinierte IR-, MIR-, Binary- und Linker-Passes. Plugins registrieren sich an 11 DynCode-Interpose-Punkten (`NEVERC_INTERPOSE_SC_*`). Single-Header-SDK, null LLVM/CRT-Abhängigkeiten. Siehe [Plugin-API-Dokumentation](../../plugin-api/README.de.md).

## Geplant — Plugin-Schicht (über Interposes)

Diese Fähigkeiten sind **absichtlich nicht eingebaut**. Sie gehören zur Strategie-/Obfuskationsschicht und sind so konzipiert, dass sie von Drittanbieter-Plugins über Interpose- und Plugin-Schnittstellen bereitgestellt werden.

| Funktion | Interpose-Punkt | Hinweise |
|----------|-----------|----------|
| Anti-Disassembly | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Instruktionspräfix-Störung, Sprung-Umordnung, Junk-Einfügung |
| Polymorphismus | `RunAfterFinalMIR` / `RunPostExtract` | Seed-basierte Ausgabevariation pro Kompilierung |
| Stufen-Encoder (XOR / RC4 / selbstentschlüsselnd) | `RunPostExtract` / `RunPostFinalize` | Kompilierzeit-Stub-Erzeugung + Payload-Verschlüsselung |
| Indirekte Syscalls (Halos / Tartarus / Recycled Gate) | IR-Level-Plugin oder `RunPostExtract` | Runtime-ntdll-Gadget-Scanning |
| Sleep-Maske / Callstack-Spoofing | IR-Pass-Plugin | Ekko- / FOLIAGE- / Cronos-Muster |
| ETW- / AMSI-Patching | IR-Pass-Plugin | Runtime-Patch-Sequenzen |
| Modul-Stomping / Uninterposing | IR-Pass-Plugin | Speichermanipulationsmuster |

## Plugin-Interpose-Übersicht

11 Interposes in drei Schichten:

**IR-Schicht (6 Interposes, empfangen `ModulePassManager &`)**:
- `RunBeforePrep` — Vor jedem DynCode-Pass
- `RunAfterPrep` — Nach Linkage-Vereinheitlichung
- `RunBeforeInlining` — Letzte Chance vor AlwaysInliner
- `RunAfterInlining` — IR vollständig in eine Funktion geflacht
- `RunAfterStackify` — Endgültige IR-Form vor Codegen
- `RunAfterFinalIR` — Nach `AllBlrPass`, der absolut letzte IR-Interpose

**MIR-Schicht (3 Interposes, empfangen `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Register zugewiesen, CFI/EH-Pseudos noch vorhanden
- `RunAfterPreEmit` — Nach `MIRPrepPass`-Bereinigung, nächster Zustand zu finalen Bytes
- `RunAfterFinalMIR` — Nach LLVM `addPreEmitPass2()`, direkt vor AsmPrinter

**Byte-Stream-Schicht (2 Interposes, empfangen `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Vor Finalize, wird noch von Rewriter/Encoder/Audit/Sizing verarbeitet
- `RunPostFinalize` — Nach Finalize, letzter Moment vor dem Schreiben auf die Festplatte; NeverC führt keine weitere Prüfung durch

## Finalize-Pipeline

Jeder Extraktor ruft `finalizeDynCodeBytes` auf, bevor die `.bin` geschrieben wird:

```
applyPostExtractObfuscationInterpose       (C Plugin API: NEVERC_INTERPOSE_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (eingebautes hartes Audit)
        |
applyDynCodeSizing                  (-fdyncode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationInterpose      (C Plugin API: NEVERC_INTERPOSE_SC_POST_FINALIZE)
```

Verwendung und Codebeispiele siehe [Plugin API Dokumentation](../../plugin-api/README.de.md).

## Nicht geplant

- **Cross-Language-Frontend** — NeverC akzeptiert nur sein eigenes C23-Frontend. Die IR-Pipeline ist vom Frontend entkoppelt, aber die Annahme von externem Bitcode (z.B. von `rustc` oder `zig`) ist kein Projektziel.

[`NevercPluginAPI.h`]: ../../../neverc/include/neverc/Plugin/NevercPluginAPI.h
