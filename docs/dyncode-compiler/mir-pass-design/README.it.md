**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore dyncode](../README.it.md)

# Progettazione dei pass MIR — Principi e punti interpose

> Documento compagno di [ir-pass-design.md](../ir-pass-design/README.it.md). Il livello IR elimina i costrutti che a livello IR producono visibilmente rilocazioni. Il livello MIR funge da **rete di sicurezza** dopo la selezione delle istruzioni e l'allocazione dei registri: rimuove le pseudo-istruzioni/metadati introdotti dal codegen ed espone punti interpose affinché pass di offuscamento terzi eseguano le trasformazioni finali a livello di istruzione.
>
> Implementazione: `neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> Interfaccia interpose: [`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`].

---

## 0. Perché serve un livello MIR

Il livello IR ha già eliminato:
- GV costanti → stack-ificati / immediati (Data2TextPass)
- `memcpy` / `memset` / `str*` / `abs*` → loop di byte inline (MemIntrinPass)
- helper compiler-rt `__int128` → always_inline inline (CompilerRtPass)
- syscall libc extern → svc / syscall inline (SyscallStubPass)
- extern Win32 → PEB walk + hash export (WinPEBImportPass)
- global mutabili → frame di stack d'ingresso (ZeroRelocPass)
- computed goto → switch (IndirectBrPass)
- Opzionale: call diretta → call indiretta (AllBlrPass)

Ma il backend LLVM introduce costrutti aggiuntivi durante il **lowering IR → MIR** che dyncode non può accettare:

1. **Pseudo-istruzioni CFI / EH_LABEL**: generate quando `-g` o le info di unwind di default sono abilitate, producendo `__compact_unwind` (Mach-O) / `.eh_frame` (ELF) / `.pdata + .xdata` (COFF).
2. **Stub XRay / patchable function**: `-fxray-instrument` o `-fpatchable-function-entry` inseriscono `PATCHABLE_FUNCTION_ENTER` e simili.
3. **Metadati sanitizer**: StackMap / PatchPoint / StateMap / PseudoProbe.
4. **Fixup MC-level del backend**: es. riferimenti GOT arm64 Windows, invisibili a livello IR.

Inoltre, gli interpose MIR hanno uno scopo critico: **abilitare l'offuscamento a livello di istruzione da terze parti** (sostituzione di istruzioni, rinomina dei registri) che l'IR non può esprimere (l'IR ha solo registri virtuali e istruzioni astratte).

---

## 1. Integrazione con LLVM (interpose nativi)

Il `TargetPassConfig` di LLVM ha una lista globale di callback. `addMachinePasses()` invoca ogni callback dopo `addPass(&PatchableFunctionID)` e prima di `addPreEmitPass()`. Abbiamo aggiunto un wrapper pubblico `addExternalPass(Pass *P)` per risolvere i problemi di controllo di accesso con il `addPass()` protected.

Registrazione in `Pipeline.cpp`:

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

Il callback non cattura `Opts`. Legge lo snapshot corrente di `DynCodeOptions` a runtime, evitando configurazioni obsolete quando lo stesso processo compila sia dyncode sia C normale.

---

## 2. MIRPrepPass integrato

Multipiattaforma, responsabilità singola: scansiona ogni `MachineBasicBlock` ed elimina tre categorie di pseudo-istruzioni. Le vere istruzioni macchina (`MOV` / `BL` / `ADRP` / `SYSCALL` / ...) **non vengono mai toccate**.

### 2.1 Metadati di sezione laterale (via `TargetOpcode::*`, multipiattaforma)

| Opcode | Origine | Se non rimosso |
|--------|---------|----------------|
| `CFI_INSTRUCTION` | frame-lowering di tutte le piattaforme / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` non vuoto |
| `EH_LABEL` | Punti EH / try-catch setjmp | Sezione laterale LSDA non vuota |
| `GC_LABEL` / `ANNOTATION_LABEL` | Marcatori GC / annotation | MCSymbol con metadati relativi alla sezione |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | Stackmap GC / sandbox | Sezione laterale `.llvm_stackmaps` |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | Sezione laterale `.pseudo_probe` |
| Famiglia `PATCHABLE_*` | Stub XRay / Kcov | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | Sonda d'ingresso `-mfentry` | Chiamata extern `__fentry__` |
| `LOCAL_ESCAPE` | Frame-escape SEH Microsoft | Trascina `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | Info di debug della jump table | Voce `.debug_rnglists` |

### 2.2 Windows SEH (matchato dal prefisso di `TargetInstrInfo::getName()`)

Le pseudo SEH di Windows sono opcode target-specifici definiti nei TD del backend AArch64/X86 (~20 istruzioni come `SEH_StackAlloc`, `SEH_PushReg`, ecc.). Per mantenere il pass MIR **multipiattaforma senza includere header del backend**, usiamo il match per prefisso di stringa:

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 Tabella di riscrittura istruzioni (`MIRRewritePatterns.def`)

Dopo la rimozione delle pseudo, `MIRPrepPass` esegue un pass di riscrittura per sostituire i pattern di istruzione selezionati dal codegen ma non dyncode-friendly con forme equivalenti dyncode-friendly, senza modificare i file `.td` di LLVM.

Due pattern registrati:

1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8` quando il pattern IEEE rientra nei 256 valori codificabili di FMOV.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd xmm, [rip+CPI]` che carica `+0.0` → `FsFLD0SS/FsFLD0SD` (`xorps xmm, xmm` da 3 byte).

I nomi degli opcode sono centralizzati in `Tables/MIRRewriteOpcodes.def`. Aggiungere un nuovo pattern di riscrittura richiede tre passaggi:
1. Scrivere `tryRewriteXxx(MachineFunction &)` usando `lookupMIRRewriteOpcode()` + `BuildMI(TII->get(...))`
2. Aggiungere i ruoli degli opcode a `MIRRewriteOpcodes.def`
3. Aggiungere la voce del pattern a `MIRRewritePatterns.def`

---

## 3. Interpose di offuscamento utente

`ObfuscationInterposes` espone **11 punti interpose**: 6 a livello IR, 3 a livello MIR, 2 a livello byte:

Tre tipi di firma:
```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

Differenze chiave:
- `RunBeforePreEmit`: la MIR **ha ancora le pseudo CFI/EH/XRay** — per la manipolazione dei metadati di prologo/epilogo.
- `RunAfterPreEmit`: **MIR ripulita** — la più vicina alla forma di AsmPrinter, ideale per sostituzione di istruzioni / rinomina dei registri.
- `RunPostExtract`: **flusso di byte puro** dopo che l'extractor ha applicato le patch alle reloc intra-text — per XOR/RC4 sull'intero payload, byte spazzatura, header personalizzati.

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

## 4. Ordine di esecuzione completo

```
[IR PassBuilder]
  ├─ RunBeforePrep       (interpose utente)
  ├─ ZeroRelocPass(Prep)
  ├─ RunAfterPrep        (interpose utente)
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1
  ├─ RunBeforeInlining   (interpose utente)
  │  (livello O LLVM: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining    (interpose utente)
  ├─ Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify    (interpose utente)
  └─ AllBlrPass          (opt)
        │
[Codegen (IR → MIR)]
  ├─ RunBeforePreEmit    (interpose utente, CFI presente)
  ├─ DynCodeMIRPrepPass  ← focus di questo documento
  └─ RunAfterPreEmit     (interpose utente, CFI rimosso)
        │
[AsmPrinter → file oggetto]
        │
[DynCodeExtractor]  ← audit di fallback a livello byte
  ├─ RunPostExtract   (interpose utente, byte puri)
  └─ .bin piatto
```

Il livello MIR gestisce **pulizia rete-di-sicurezza + punti interpose di offuscamento**, non la logica di business. La promessa "scrivi C normale, nessun trucco dyncode necessario" è mantenuta dai 5+ pass IR.

---

## 5. Fondamento di progettazione

| Problema | Livello IR? | Livello MIR? |
|----------|-------------|--------------|
| Eliminazione di GV costante | Sì (Data2Text) | Non necessario |
| Eliminazione di libc extern | Sì (SyscallStub / WinPEB) | Non necessario |
| Stack-ificazione di global mutabili | Sì (ZeroReloc) | Non necessario |
| Computed goto | Sì (IndirectBr) | Non necessario |
| Pseudo-istruzioni CFI | No (generate dal backend) | Sì (scansiona ed elimina) |
| Stub XRay | No (generati dal backend) | Sì (scansiona ed elimina) |
| Offuscamento a livello di istruzione | No (l'IR manca di registri fisici) | Sì (ha registri reali/MI) |
| Rinomina dei registri | No | Sì |
| Espansione peephole di costanti | Parziale | Sì (più pulita) |

## 6. Guida all'estensione

**Aggiungere una rimozione di pseudo integrata**: aggiungi un case allo switch `isDynCodeStripPseudo`.

**Aggiungere una riscrittura MIR integrata**: scrivi `tryRewriteXxx(MachineFunction &)` usando `TII->getName()` / `BuildMI(TII->get(...))`. Aggiungi il pattern a `MIRRewritePatterns.def`, gli opcode a `MIRRewriteOpcodes.def`.

**Libreria di offuscamento di terze parti**: registra via la [API Plugin](../../plugin-api/README.it.md) (interpose `NEVERC_INTERPOSE_SC_*`).

## 7. Relazione con DynCodeExtractor

| Livello | Timing | Capacità |
|---------|--------|----------|
| MIR | **Prima** di AsmPrinter | Può inserire/eliminare MachineInstr |
| Extractor | **Dopo** AsmPrinter | Può solo modificare byte o rifiutare |

**Principio**: correggi prima nella MIR (puoi ancora manipolare le istruzioni); ricorri all'extractor solo per patch a livello byte (es. reloc imm26 intra-sezione). Questa stratificazione garantisce che l'utente non ottenga mai un `.bin` "mezzo rotto": o funziona o c'è un errore chiaro e azionabile in fase di compilazione.

## 8. Correzione attiva vs passthrough di diagnostica

1. **Correzione attiva**: modifica direttamente i MachineInstr (rimuove pseudo, riscrive CPI→FMOV). Basso costo, indipendente dal target.
2. **Passthrough di diagnostica**: rileva i problemi, riporta errori a livello MIR, lascia che l'extractor rifiuti a livello byte. Usato per costrutti dove la riscrittura MIR richiederebbe codice target-specifico esteso (es. sostituire `adrp+ldr CPI` con sequenze `mov/movk`).
3. **Fallback dell'extractor**: hard-fail su qualsiasi reloc esterna rimanente o sezione dati non vuota.

Questo principio mantiene il livello MIR quasi immune agli aggiornamenti dell'ISA del backend. L'unica manutenzione è: "c'è una nuova pseudo in TargetOpcode? Se dyncode non ne ha bisogno, aggiungi un case."

[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]: ../../../neverc/include/neverc/DynCode/Pipeline/Pipeline.h
