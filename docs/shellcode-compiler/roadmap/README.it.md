**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Roadmap

Questo documento traccia le funzionalità pianificate, in corso o differite per scelta di design.

## Stato attuale

La pipeline shellcode di NeverC copre:

- Pipeline LLVM IR completa con 11+ pass dedicati
- Estrattori COFF / ELF / Mach-O
- Risoluzione importazioni Win32 PEB-walk (hash ROR-13, 6 bucket DLL)
- Abbassamento diretto syscall (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Supporto modalità kernel (Windows, Linux)
- Audit dei byte proibiti con profili configurabili
- SDK plugin per riscrittori di byte proibiti e codificatori di set di caratteri
- Vincoli di dimensione / allineamento / padding (`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`)
- 11 hook di offuscamento sui livelli IR, MIR e flusso di byte

## Completato (2026-04)

1. **Vincoli di dimensione / allineamento / padding** — Integrato. `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` vengono eseguiti alla fine di `finalizeShellcodeBytes`. Il driver rifiuta configurazioni contraddittorie (es. byte di padding nel set di byte proibiti, o padding senza align/max-length).

2. **Interfaccia riscrittore di byte proibiti** — Scheletro integrato, nessuna strategia incorporata. `Plugin.h::registerBadByteRewriteStrategy` espone l'SDK. `-fshellcode-bad-byte-rewrite` / `-fno-...` controlla se la catena di finalizzazione invoca i riscrittori. La disattivazione torna alla modalità solo audit. Le librerie downstream registrano strategie basate su Capstone o personalizzate.

3. **Interfaccia codificatore di set di caratteri** — Scheletro integrato, nessun set incorporato. `Plugin.h::registerCharsetEncoder` espone una tupla `(name, Encode, Stub, IsCharsetMember)`. Con `-fshellcode-charset=<name>` impostato, la fase di finalizzazione sostituisce `.text` con `Stub(target) || Encode(text, target)` e valida tutti i byte di output contro il set di caratteri. I codificatori stampabili / alfanumerici / personalizzati sono registrati dalle librerie downstream.

## Pianificato — Livello plugin (tramite hook)

Queste capacità **non sono intenzionalmente integrate**. Appartengono al livello strategia/offuscamento e sono progettate per essere fornite da plugin di terze parti tramite le interfacce hook e plugin.

| Funzionalità | Punto hook | Note |
|-------------|-----------|------|
| Anti-disassemblaggio | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Interferenza prefisso istruzione, riordinamento salti, inserimento spazzatura |
| Polimorfismo | `RunAfterFinalMIR` / `RunPostExtract` | Variazione dell'output basata su seed per compilazione |
| Codificatore a stadi (XOR / RC4 / auto-decifrante) | `RunPostExtract` / `RunPostFinalize` | Generazione stub a compilazione + cifratura del payload |
| Syscall indiretti (Halos / Tartarus / Recycled Gate) | Plugin livello IR o `RunPostExtract` | Scansione gadget ntdll a runtime |
| Sleep mask / spoofing dello stack di chiamata | Plugin pass IR | Pattern Ekko / FOLIAGE / Cronos |
| Patching ETW / AMSI | Plugin pass IR | Sequenze di patch a runtime |
| Module stomping / unhooking | Plugin pass IR | Pattern di manipolazione della memoria |

## Riepilogo hook plugin

11 hook su tre livelli:

**Livello IR (6 hook, ricevono `ModulePassManager &`)**:
- `RunBeforePrep` — Prima di qualsiasi pass shellcode
- `RunAfterPrep` — Dopo l'unificazione del linkage
- `RunBeforeInlining` — Ultima opportunità prima di AlwaysInliner
- `RunAfterInlining` — IR completamente appiattito in una funzione
- `RunAfterStackify` — Forma IR finale prima del codegen
- `RunAfterFinalIR` — Dopo `AllBlrPass`, l'ultimo hook IR in assoluto

**Livello MIR (3 hook, ricevono `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Registri allocati, pseudo CFI/EH ancora presenti
- `RunAfterPreEmit` — Dopo la pulizia di `MIRPrepPass`, più vicino ai byte finali
- `RunAfterFinalMIR` — Dopo LLVM `addPreEmitPass2()`, appena prima di AsmPrinter

**Livello flusso di byte (2 hook, ricevono `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Pre-finalizzazione, ancora elaborato da riscrittore/codificatore/audit/dimensionamento
- `RunPostFinalize` — Post-finalizzazione, ultimo momento prima della scrittura su disco; NeverC non esegue ulteriori audit

## Pipeline di finalizzazione

Ogni estrattore chiama `finalizeShellcodeBytes` prima di scrivere il `.bin`:

```
applyPostExtractObfuscationHook       (ObfuscationHooks::RunPostExtract)
        |
runBadByteRewriters                   (Plugin.h::registerBadByteRewriteStrategy)
        |
runCharsetEncoder                     (Plugin.h::registerCharsetEncoder)
        |
auditFinalBadBytes                    (audit rigido integrato)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
```

Utilizzo ed esempi di codice nella [documentazione Plugin API](../../plugin-api/README.it.md).

## Non pianificato

- **Frontend multi-linguaggio** — NeverC accetta solo il proprio frontend C23. La pipeline IR è disaccoppiata dal frontend, ma l'accettazione di bitcode esterno (es. da `rustc` o `zig`) non è un obiettivo del progetto.
