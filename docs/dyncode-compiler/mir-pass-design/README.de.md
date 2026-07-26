**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← DynCode-Compiler](../README.de.md)

# MIR-Pass-Design — Prinzipien und Interpose-Punkte

> Begleitdokument zu [ir-pass-design.md](../ir-pass-design/README.de.md). Die IR-Schicht eliminiert Konstrukte, die auf IR-Ebene sichtbar Relocations erzeugen. Die MIR-Schicht dient als **Auffangnetz** nach Befehlsauswahl und Registerallokation: sie entfernt codegen-eingeführte Pseudo-/Metadaten-Befehle und bietet Interpose-Punkte für Drittanbieter-Obfuskations-Passes.
>
> Implementierung: `neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> Interpose-Interface: [`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`].

---

## 0. Warum eine MIR-Schicht benötigt wird

Die IR-Schicht hat bereits eliminiert: Konstant-GVs (Data2Text), mem*/str* (MemIntrin), __int128 (CompilerRt), extern libc (SyscallStub/WinPEB), mutable Globals (ZeroReloc), computed-goto (IndirectBr).

Aber das LLVM-Backend führt während **IR → MIR Lowering** zusätzliche Konstrukte ein:

1. **CFI / EH_LABEL Pseudo-Befehle** → `__compact_unwind` / `.eh_frame` / `.pdata`.
2. **XRay / patchable Stubs** → `.xray_instr_map`.
3. **Sanitizer-Metadaten**: StackMap / PatchPoint / PseudoProbe.
4. **Backend MC-Level Fixups**.

MIR-Interposes ermöglichen außerdem **Drittanbieter-Befehlsebene-Obfuskation** (Befehlssubstitution, Registerumbenennung).

---

## 1. Integration mit LLVM

`Pipeline.cpp` registriert in `TargetPassConfig`s globalem Callback:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
      const ObfuscationInterposes &H = getDynCodeObfuscationInterposes();
      runMIRInterpose(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createDynCodeMIRPrepPass(Opts));
      runMIRInterpose(H.RunAfterPreEmit, TPC, Opts);
    });
```

---

## 2. Eingebauter MIRPrepPass

Plattformübergreifend, einzelne Verantwortung: Scannt jeden `MachineBasicBlock` und löscht drei Kategorien von Pseudo-Befehlen. Echte Maschinenbefehle werden **nie berührt**.

### 2.1 Seitenabschnitt-Metadaten

| Opcode | Quelle | Wenn nicht entfernt |
|--------|--------|--------------------|
| `CFI_INSTRUCTION` | Frame-Lowering aller Plattformen / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` nicht leer |
| `EH_LABEL` | EH / try-catch setjmp-Punkte | LSDA-Seitensektion nicht leer |
| `GC_LABEL` / `ANNOTATION_LABEL` | GC / Annotation-Marker | MCSymbol mit sektionsrelativen Metadaten |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / Sandbox-Stackmap | `.llvm_stackmaps`-Seitensektion |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe`-Seitensektion |
| `PATCHABLE_*`-Familie | XRay / Kcov-Stubs | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | `-mfentry` Entry-Probe | extern `__fentry__`-Aufruf |
| `LOCAL_ESCAPE` | Microsoft SEH Frame-Escape | zieht `_local_unwind2` / `__except_handler3` herein |
| `JUMP_TABLE_DEBUG_INFO` | Jump-Table-Debug-Info | `.debug_rnglists`-Eintrag |

### 2.2 Windows SEH (Prefix-Match)

```cpp
if (Name.starts_with("SEH_")) eraseFromParent();
```

### 2.3 Befehlsrewrite-Tabelle (`MIRRewritePatterns.def`)

Zwei registrierte Muster:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

---

## 3. Benutzer-Obfuskations-Interposes

11 Interpose-Punkte: 6 IR + 3 MIR + 2 Byte-Level.

Drei Signaturtypen:

```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

- `RunBeforePreEmit`: MIR mit CFI/EH-Pseudos.
- `RunAfterPreEmit`: Bereinigte MIR — nächster Zustand zu AsmPrinter.
- `RunPostExtract`: Reiner Byte-Stream.

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::dyncode::getDynCodeObfuscationInterposes();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::dyncode::DynCodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  // Register via Plugin API: NEVERC_INTERPOSE_SC_BEFORE_PREEMIT / AFTER_PREEMIT / AFTER_FINAL_MIR
}
```

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

| Problem | IR-Ebene? | MIR-Ebene? |
|---------|-----------|-----------|
| Konstante GV-Eliminierung | Ja (Data2Text) | Nicht nötig |
| extern libc-Eliminierung | Ja (SyscallStub / WinPEB) | Nicht nötig |
| Stack-ifizierung veränderlicher Globals | Ja (ZeroReloc) | Nicht nötig |
| Computed goto | Ja (IndirectBr) | Nicht nötig |
| CFI-Pseudo-Instruktionen | Nein (backend-generiert) | Ja (scannen und löschen) |
| XRay-Stubs | Nein (backend-generiert) | Ja (scannen und löschen) |
| Instruktionsebenen-Obfuskation | Nein (IR fehlen physische Register) | Ja (echte Register/MI) |
| Registerumbenennung | Nein | Ja |
| Peephole-Konstantenexpansion | Teilweise | Ja (sauberer) |

## 6. Erweiterungsanleitung

- **Pseudo-Entfernung**: Ein Case in `isDynCodeStripPseudo`.
- **MIR-Rewrite**: `tryRewriteXxx` schreiben + `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def`.
- **Drittanbieter**: [Plugin-API](../../plugin-api/README.de.md) (`NEVERC_INTERPOSE_SC_*` Interposes).

## 7. Beziehung zu DynCodeExtractor

| Schicht | Zeitpunkt | Fähigkeit |
|---------|-----------|-----------|
| MIR | Vor AsmPrinter | MachineInstr einfügen/löschen |
| Extraktor | Nach AsmPrinter | Nur Bytes ändern oder ablehnen |

## 8. Aktive Korrektur vs Diagnose-Durchleitung

1. **Aktiv**: MachineInstr direkt ändern. Kostengünstig, zielunabhängig.
2. **Durchleitung**: Erkennen → MIR-Fehler → Extraktor lehnt auf Byte-Ebene ab.
3. **Fallback**: Harter Fehler bei verbleibenden externen Relocs.

[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]: ../../../neverc/include/neverc/DynCode/Pipeline/Pipeline.h
