**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore dyncode](../README.it.md)

# Roadmap

Questo documento traccia le funzionalità pianificate, in corso o differite per scelta di design.

## Stato attuale

La pipeline dyncode di NeverC copre:

- Pipeline LLVM IR completa con 11+ pass dedicati
- Estrattori COFF / ELF / Mach-O
- Risoluzione importazioni Win32 PEB-walk (hash ROR-13, 6 bucket DLL)
- Abbassamento diretto syscall (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Supporto modalità kernel (Windows, Linux)
- Audit dei byte proibiti con profili configurabili
- SDK plugin per riscrittori di byte proibiti e codificatori di set di caratteri
- Vincoli di dimensione / allineamento / padding (`-fdyncode-max-length=`, `-fdyncode-align=`, `-fdyncode-pad=`)
- 11 interpose di offuscamento sui livelli IR, MIR e flusso di byte

## Completato (2026-04)

1. **Vincoli di dimensione / allineamento / padding** — Integrato. `-fdyncode-max-length=`, `-fdyncode-align=`, `-fdyncode-pad=` vengono eseguiti alla fine di `finalizeDynCodeBytes`. Il driver rifiuta configurazioni contraddittorie (es. byte di padding nel set di byte proibiti, o padding senza align/max-length).

2. **API Plugin C fuori dall'albero** — Interfaccia plugin C ABI pura ([`NevercPluginAPI.h`]) per pass IR, MIR, Binary e Linker personalizzati. I plugin si registrano a 11 interpose point dyncode (`NEVERC_INTERPOSE_SC_*`). SDK a singolo header, zero dipendenze LLVM/CRT. Vedere [documentazione API Plugin](../../plugin-api/README.it.md).

## Pianificato — Livello plugin (tramite interpose)

Queste capacità **non sono intenzionalmente integrate**. Appartengono al livello strategia/offuscamento e sono progettate per essere fornite da plugin di terze parti tramite le interfacce interpose e plugin.

| Funzionalità | Punto interpose | Note |
|-------------|-----------|------|
| Anti-disassemblaggio | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Interferenza prefisso istruzione, riordinamento salti, inserimento spazzatura |
| Polimorfismo | `RunAfterFinalMIR` / `RunPostExtract` | Variazione dell'output basata su seed per compilazione |
| Codificatore a stadi (XOR / RC4 / auto-decifrante) | `RunPostExtract` / `RunPostFinalize` | Generazione stub a compilazione + cifratura del payload |
| Syscall indiretti (Halos / Tartarus / Recycled Gate) | Plugin livello IR o `RunPostExtract` | Scansione gadget ntdll a runtime |
| Sleep mask / spoofing dello stack di chiamata | Plugin pass IR | Pattern Ekko / FOLIAGE / Cronos |
| Patching ETW / AMSI | Plugin pass IR | Sequenze di patch a runtime |
| Module stomping / uninterposing | Plugin pass IR | Pattern di manipolazione della memoria |

## Riepilogo interpose plugin

11 interpose su tre livelli:

**Livello IR (6 interpose, ricevono `ModulePassManager &`)**:
- `RunBeforePrep` — Prima di qualsiasi pass dyncode
- `RunAfterPrep` — Dopo l'unificazione del linkage
- `RunBeforeInlining` — Ultima opportunità prima di AlwaysInliner
- `RunAfterInlining` — IR completamente appiattito in una funzione
- `RunAfterStackify` — Forma IR finale prima del codegen
- `RunAfterFinalIR` — Dopo `AllBlrPass`, l'ultimo interpose IR in assoluto

**Livello MIR (3 interpose, ricevono `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Registri allocati, pseudo CFI/EH ancora presenti
- `RunAfterPreEmit` — Dopo la pulizia di `MIRPrepPass`, più vicino ai byte finali
- `RunAfterFinalMIR` — Dopo LLVM `addPreEmitPass2()`, appena prima di AsmPrinter

**Livello flusso di byte (2 interpose, ricevono `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Pre-finalizzazione, ancora elaborato da riscrittore/codificatore/audit/dimensionamento
- `RunPostFinalize` — Post-finalizzazione, ultimo momento prima della scrittura su disco; NeverC non esegue ulteriori audit

## Pipeline di finalizzazione

Ogni estrattore chiama `finalizeDynCodeBytes` prima di scrivere il `.bin`:

```
applyPostExtractObfuscationInterpose       (C Plugin API: NEVERC_INTERPOSE_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (audit rigido integrato)
        |
applyDynCodeSizing                  (-fdyncode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationInterpose      (C Plugin API: NEVERC_INTERPOSE_SC_POST_FINALIZE)
```

Utilizzo ed esempi di codice nella [documentazione Plugin API](../../plugin-api/README.it.md).

## Non pianificato

- **Frontend multi-linguaggio** — NeverC accetta solo il proprio frontend C23. La pipeline IR è disaccoppiata dal frontend, ma l'accettazione di bitcode esterno (es. da `rustc` o `zig`) non è un obiettivo del progetto.

[`NevercPluginAPI.h`]: ../../../neverc/include/neverc/Plugin/NevercPluginAPI.h
