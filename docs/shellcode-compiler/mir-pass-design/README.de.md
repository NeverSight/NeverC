**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# MIR-Pass-Design — Prinzipien und Hook-Punkte

> Begleitdokument zu [ir-pass-design.md](../ir-pass-design/README.de.md). Die IR-Schicht eliminiert Konstrukte, die auf IR-Ebene sichtbar Relocations erzeugen. Die MIR-Schicht dient als **Auffangnetz** nach Befehlsauswahl und Registerallokation: sie entfernt codegen-eingeführte Pseudo-/Metadaten-Befehle und bietet Hook-Punkte für Drittanbieter-Obfuskations-Passes.
>
> Implementierung: `neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> Hook-Interface: `neverc/include/neverc/Shellcode/Pipeline/Pipeline.h`.

---

## 0. Warum eine MIR-Schicht benötigt wird

Die IR-Schicht hat bereits eliminiert: Konstant-GVs (Data2Text), mem*/str* (MemIntrin), __int128 (CompilerRt), extern libc (SyscallStub/WinPEB), mutable Globals (ZeroReloc), computed-goto (IndirectBr).

Aber das LLVM-Backend führt während **IR → MIR Lowering** zusätzliche Konstrukte ein:

1. **CFI / EH_LABEL Pseudo-Befehle** → `__compact_unwind` / `.eh_frame` / `.pdata`.
2. **XRay / patchable Stubs** → `.xray_instr_map`.
3. **Sanitizer-Metadaten**: StackMap / PatchPoint / PseudoProbe.
4. **Backend MC-Level Fixups**.

MIR-Hooks ermöglichen außerdem **Drittanbieter-Befehlsebene-Obfuskation** (Befehlssubstitution, Registerumbenennung).

---

## 1. Integration mit LLVM

`Pipeline.cpp` registriert in `TargetPassConfig`s globalem Callback:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
      const ObfuscationHooks &H = getShellcodeObfuscationHooks();
      runMIRHook(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
      runMIRHook(H.RunAfterPreEmit, TPC, Opts);
    });
```

---

## 2. Eingebauter MIRPrepPass

Plattformübergreifend, einzelne Verantwortung: Scannt jeden `MachineBasicBlock` und löscht drei Kategorien von Pseudo-Befehlen. Echte Maschinenbefehle werden **nie berührt**.

### 2.1 Seitenabschnitt-Metadaten

| Opcode | Quelle | Auswirkung bei Nicht-Entfernung |
|--------|--------|-------------------------------|
| `CFI_INSTRUCTION` | Frame-Lowering / `-g` | `.eh_frame` / `.pdata` nicht leer |
| `EH_LABEL` | EH / try-catch | LSDA nicht leer |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / Sandbox | `.llvm_stackmaps` |
| `PSEUDO_PROBE` | Profiling | `.pseudo_probe` |
| `PATCHABLE_*` | XRay / Kcov | `.xray_instr_map` |
| `FENTRY_CALL` | `-mfentry` | extern `__fentry__` |
| `LOCAL_ESCAPE` | Microsoft SEH | SEH-Handler-Referenzen |

### 2.2 Windows SEH (Prefix-Match)

```cpp
if (Name.starts_with("SEH_")) eraseFromParent();
```

### 2.3 Befehlsrewrite-Tabelle (`MIRRewritePatterns.def`)

Zwei registrierte Muster:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

---

## 3. Benutzer-Obfuskations-Hooks

11 Hook-Punkte: 6 IR + 3 MIR + 2 Byte-Level.

- `RunBeforePreEmit`: MIR mit CFI/EH-Pseudos.
- `RunAfterPreEmit`: Bereinigte MIR — nächster Zustand zu AsmPrinter.
- `RunPostExtract`: Reiner Byte-Stream.

---

## 4. Vollständige Ausführungsreihenfolge

```
[IR PassBuilder]
  ├─ RunBeforePrep → ZeroRelocPass → RunAfterPrep
  ├─ IndirectBr / MemIntrin / CompilerRt / SyscallStub / WinPEB / KernelImport
  ├─ Data2Text #1 → RunBeforeInlining
  ├─ (LLVM Optimierungen)
  ├─ RunAfterInlining → Data2Text #2 → ZeroReloc(Stackify) → RunAfterStackify → AllBlr
[Codegen]
  ├─ RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Extractor → RunPostExtract → .bin]
```

## 5. Design-Begründung

| Problem | IR? | MIR? |
|---------|-----|------|
| Konstant-GV-Eliminierung | Ja | Nicht nötig |
| CFI-Pseudo-Befehle | Nein (Backend) | Ja |
| Befehlsebene-Obfuskation | Nein | Ja |
| Registerumbenennung | Nein | Ja |

## 6. Erweiterungsanleitung

- **Pseudo-Entfernung**: Ein Case in `isShellcodeStripPseudo`.
- **MIR-Rewrite**: `tryRewriteXxx` schreiben + `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def`.
- **Drittanbieter**: `setShellcodeObfuscationHooks()`.

## 7. Beziehung zu ShellcodeExtractor

| Schicht | Zeitpunkt | Fähigkeit |
|---------|-----------|-----------|
| MIR | Vor AsmPrinter | MachineInstr einfügen/löschen |
| Extraktor | Nach AsmPrinter | Nur Bytes ändern oder ablehnen |

## 8. Aktive Korrektur vs Diagnose-Durchleitung

1. **Aktiv**: MachineInstr direkt ändern. Kostengünstig, zielunabhängig.
2. **Durchleitung**: Erkennen → MIR-Fehler → Extraktor lehnt auf Byte-Ebene ab.
3. **Fallback**: Harter Fehler bei verbleibenden externen Relocs.
