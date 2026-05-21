**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Pipeline Shellcode, MIR e Strategia PIC (Note di progettazione)

Questo documento descrive i compromessi di progettazione nella modalità shellcode di NeverC attraverso la catena **IR → ottimizzazione LLVM → backend MIR → file oggetto → estrazione/patching**, e la sua relazione con la politica di **PIC predefinito a livello compilatore**. I dettagli implementativi fanno fede nel codice sorgente e nei commenti in inglese.

## 1. Perché forzare PIC per default (inclusa la compilazione non-shellcode)

L'estrattore shellcode presuppone che i riferimenti a simboli esterni nel frammento eseguibile cadano su rilocazioni **relative al PC** o risolvibili intra-`.text`, non su indirizzi assoluti codificati o pool di costanti che necessitano un loader per riempire `.data`.

NeverC ritorna **true** da `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()` e `MSVCToolChain::isPICDefaultForced()`, distinguendosi dal comportamento di Clang upstream "PIC predefinito opzionale": **tutte le compilazioni multipiattaforma usano sempre e solo PIC come modello**. Questo significa:

- La compilazione C normale e la compilazione `-fshellcode` condividono le stesse abitudini di rilocazione, riducendo il carico cognitivo "funziona normalmente, si rompe sotto shellcode".
- I backend Linux / Android / macOS / Windows condividono le stesse assunzioni sotto i descrittori guidati da tabella (`TargetDesc` + `Options.td.h`), evitando codifica rigida `if (linux)` nel driver.

Questa politica non distingue se `-fshellcode` è abilitato o se il contesto è user/kernel. Anche se l'utente passa `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`, `ParsePICArgs()` mantiene `Reloc::PIC_`, usando le stesse assunzioni relative al PC per compilazione normale, shellcode modalità utente e shellcode modalità kernel.

## 2. Divisione del lavoro IR e MIR in due fasi

### 2.1 Livello IR (`registerShellcodePasses`)

Responsabile della compressione della semantica "C normale" in una forma **ingresso singolo, nessuna sezione dati indipendente, nessun globale problematico**: `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `HeapArenaPass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass` (solo kernel), `Data2TextPass`, ecc.

**Principio**: I problemi risolvibili in IR con approcci strutturati vengono corretti prima in IR (pool di costanti, BlockAddress, fallthrough di `memcpy` a libc, fallthrough di `__int128 /` a `__udivti3`, ecc.), semplificando il flusso di byte visto dal backend e dall'estrattore. Per scenari con alto carico cognitivo utente ma internalizzabili in sicurezza, il driver inietta proattivamente regole (es. `long double` di AArch64 Linux / Android / Windows degradato a binary64 in modalità shellcode). Solo le costruzioni non supportabili senza runtime attivano diagnostiche MIR/estrattore.

### 2.2 Livello MIR (`registerShellcodeMachinePasses`)

Registra callback nel `TargetPassConfig` legacy di LLVM **dopo l'allocazione dei registri, prima di `addPreEmitPass`**, in quest'ordine:

1. Utente/libreria di offuscamento: `RunBeforePreEmit` (pseudo CFI / EH ancora presenti; utile per trasformazioni dipendenti dai metadati).
2. **`ShellcodeMIRPrepPass`**: Rimuove pseudo che genererebbero sezioni laterali `.eh_frame` / `.pdata` / `.xray_*`, rendendo il flusso di istruzioni il più vicino possibile a "codice puro" prima di AsmPrinter.
3. Utente/libreria di offuscamento: `RunAfterPreEmit` (adatto a sostituzione istruzioni, rinomina registri e simile offuscamento della "forma finale del codice macchina").

**Principio**: Se le sequenze di istruzioni native hanno ancora problemi, correggere in MIR (specialmente intorno a `ShellcodeMIRPrepPass`); **estrazione e patching sono l'ultima rete di sicurezza**, evitando di duplicare la stessa logica nei livelli COFF/ELF/Mach-O.

I nomi di opcode MIR non sono dispersi nel flusso di controllo del pass; `ShellcodeMIRPrepPass` usa la tabella `(pattern, role, opcode)` di `Tables/MIRRewriteOpcodes.def` tramite `TargetInstrInfo::getName()`. Per aggiunte di sostituzioni istruzioni shellcode-friendly, preferire l'aggiunta di voci di tabella e piccole riscritture MIR; ricorrere a modifiche di selezione istruzioni backend `.td` solo se necessario, con il fallback a livello estrattore come ultima risorsa.

> Nota: `ShellcodeMIRPrepPass` è registrato solo quando `-fshellcode` è abilitato. I programmi normali non devono rimuovere globalmente CFI/EH, poiché ciò romperebbe la gestione normale delle eccezioni e le informazioni di debug.

Sia i callback globali IR che MIR usano un pattern **registra una volta, leggi lo snapshot corrente di `ShellcodeOptions` a runtime**. Questo supporta processi compilatore embedded di lunga durata.

## 3. Differenze di piattaforma guidate da tabella

- **Triple → comportamento**: Centralizzato in `describeTriple()` di `TargetDesc.cpp` e campi `TargetDesc`. Per nuovi OS/Arch, preferire **aggiunta di voci di tabella**.
- **Opzioni CLI**: Definite in `neverc/include/neverc/Invoke/Options.td.h`; consumate tramite enum `OPT_*`.

## 4. Toolchain Windows MSVC e layout SDK

Per la compilazione incrociata verso target Windows, NeverC supporta due fonti SDK **senza percorsi assoluti codificati**:

1. **SDK incluso nell'albero di build** (consigliato): `build-neverc/sdk` come root SDK, con auto-rilevamento di `sdk/msvc/`.

   ```
   build-neverc/bin/neverc
   build-neverc/sdk/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **Sysroot reale stile VS** (opzionale): Tramite `-winsysroot=<path>` o `NEVERC_WIN_SYSROOT`.

Entrambe funzionano senza registro o variabili d'ambiente VS dell'OS.

## 5. Punti di offuscamento ed estensione

- **Offuscamento IR**: Via `setShellcodeObfuscationHooks`; `-fshellcode-obfuscate=` passa la stringa spec. 11 hook totali (6 IR + 3 MIR + 2 flusso byte).
- **Offuscamento MIR**: `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR`. `-fshellcode-mir-obfuscate=` per spec MIR separato.
- **Hook flusso byte**: `RunPostExtract` (pre-finalize) e `RunPostFinalize` (post-finalize).
- **SDK plugin Finalize**: `Plugin.h` espone `registerBadByteRewriteStrategy` e `registerCharsetEncoder`. Vedere [plugin-interface.md §2–§3](../plugin-interface/README.it.md#2-bad-byte-rewriter-badbyterewritestrategy).
- **Dimensione / allineamento / padding**: `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.
- **Scelta di design**: Offuscamento, polimorfismo, encoder a stadi, syscall indiretti **intenzionalmente non integrati**, solo come plugin opzionali.

## 6. Dimensione modalità kernel (Ring-0)

`-mshellcode-context=user|kernel` come seconda dimensione:

- **Modalità utente**: Pipeline PEB walk / syscall stub.
- **Modalità kernel**: `SyscallStubPass` / `WinPEBImportPass` ritornano anticipatamente; `KernelImportPass` riscrive chiamate extern non risolte; `<neverc/kernel.h>` espone i tipi kernel.

Vedere [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.it.md).

## 7. Livello compatibilità Windows POSIX

**Zero consapevolezza utente**: Stesso sorgente C compila su tutti gli 8 triple senza `#ifdef _WIN32`. `WinPEBImportPass` implementa 3 fasi: scansione POSIX, generazione wrapper ponte (13 gruppi funzioni), risoluzione PEB. Dettagli: `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc`, `exit` → `ExitProcess`, ecc.

## 8. Auto-correzione dichiarazione implicita K&R

`SyscallStubPass` mantiene una tabella `getCanonicalSyscallType()` con 50+ firme POSIX canoniche. Le dichiarazioni K&R a 0 parametri vengono automaticamente sostituite.

## 9. Riepilogo

| Argomento | Approccio |
|-----------|-----------|
| PIC predefinito | Tutte le toolchain `isPICDefaultForced()==true` |
| Correggere prima in IR | Costanti, salti indiretti, intrinseci memoria eliminati in IR |
| Rete di sicurezza MIR | `ShellcodeMIRPrepPass` + hook pre/post |
| Minimizzare codifica rigida | `TargetDesc` + `Options.td.h` guidato da tabella |
| Due dimensioni user/kernel | `-fshellcode` × `-mshellcode-context={user,kernel}` |
| Compatibilità Windows POSIX | `WinPEBImportPass` ponte 13 gruppi POSIX |
| Auto-correzione K&R | `SyscallStubPass` ricade su firme POSIX canoniche |

## 10. Costanti multipiattaforma header shim

Gli header shim espongono costanti che devono corrispondere all'ABI del kernel target. Differenze chiave:

| Costante | Darwin | Linux/Android |
|----------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

Implementazione: guard `#if defined(__APPLE__)` negli header shim. La tabella POSIX di `SyscallTables.cpp` usa valori Linux, attiva solo su percorsi `SyscallABI::LinuxSvc0` / `LinuxSyscall`. I target Windows non usano questi header POSIX; il ponte POSIX→Win32 è gestito dai wrapper di compatibilità di `WinPEBImportPass`.
