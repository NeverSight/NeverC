**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Guida all'estensione della piattaforma

Questo documento spiega come estendere il compilatore shellcode a nuove piattaforme target. Attualmente supportato: **arm64 / x86_64 su macOS / Linux / Android / Windows** (8 triple), ciascuno con contesti indipendenti **User** / **Kernel** (16 varianti totali). L'aggiunta di una nuova piattaforma richiede tipicamente poche centinaia di righe di codice.

## Filosofia di design: Guidato da tabelle, non da rami

Tutti i pass sono indipendenti dal target. Le differenze di piattaforma sono concentrate in **due punti**:

1. Voci di tabella `describeTriple()` in `TargetDesc.cpp`
2. Switch architettura dei tre estrattori (Mach-O / ELF / COFF)

Aggiunta nuova piattaforma = una riga in (1) + un case in (2).

## Passaggi

### 1. Aggiungere riga in `TargetDesc`

Aggiungere il ramo OS corrispondente in `describeTriple()`:

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**Campi obbligatori** (tutti in `TargetDesc.h`):

| Campo | Scopo | Se mancante |
|-------|-------|-------------|
| `OS` / `Arch` / `Format` | Chiave di dispatch | `describeTriple` ritorna Unknown → driver rifiuta anticipatamente |
| `TextSectionName` | Estrattore cerca sezione di ingresso | `.text` non trovato → rifiuto |
| `Syscall` | Decisione di sostituzione SyscallStubPass | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | Generazione InlineAsm SyscallStubPass | Qualcuno vuoto → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | InlineAsm lettura TEB WinPEBImportPass | Vuoto → PEB walk genera InlineAsm vuoto (Windows: richiesto) |
| `DriverInjectFlags` | Flag specifici della piattaforma in modalità shellcode | null → nessuna iniezione |

### 2. Estendere `SyscallStub` / `SyscallTables` (se l'OS ha trap kernel)

- Aggiungere valore enum a `SyscallABI` in `TargetDesc.h`
- Aggiungere `kXxxTable` in `SyscallTables.cpp`
- Aggiungere case nello switch di `lookupSyscall`
- `SyscallStubPass` invariato — template/vincoli InlineAsm vengono da `TargetDesc`

### 2.5 Estendere whitelist Win32 API Windows

Windows non ha ABI syscall stabile. La whitelist è una tabella multi-DLL in `Tables/Win32Apis.def`.

**Aggiungere nuova API**: 1 riga in `Win32Apis.def` + 1 dichiarazione in `lib/Headers/windows.h`.

### 3. Estendere l'estrattore corrispondente

1. Identificare tipi di rilocazione → patchare byte o rifiutare
2. Aggiornare lista nomi sezione dati proibiti
3. Aggiornare validazione range target rilocazione ingresso-a-offset-0

### 4. Aggiungere Loader (solo strumento di test)

Riferimento `loader_linux.c` e `loader_windows.c`. Tipicamente: `mmap(RWX) → memcpy → icache flush → call`.

### 5. Aggiornare test

Aggiungere un controllo di cross-compilazione in `tests/neverc/ShellcodeCrossTargetTests.cpp`.

---

## Problemi noti multipiattaforma

- **Endianness**: NeverC supporta solo little-endian (LE).
- **Differenze ABI**: Win64 vs System V AMD64 hanno registri argomento completamente diversi. Gestito a livello frontend NeverC.
- **Numeri syscall**: Diversi per architettura su Linux, Android uguale a Linux, Darwin ha propri numeri BSD, Windows senza numeri stabili (PEB walk).
- **Coerenza cache**: ARM richiede flush esplicito i-cache; x86 no.
- **SELinux / W^X**: Android vincolato da SELinux `execmem`; iOS non-jailbroken rifiuta completamente `mmap(RWX)`.

## Roadmap estensioni future

| Target | Sforzo stimato | Dipendenze |
|--------|---------------|------------|
| **iOS arm64** (jailbreak / `MAP_JIT`) | 1 giorno | Riutilizzare estrattore Mach-O |
| **FreeBSD / OpenBSD x86_64** | Mezza giornata | Riutilizzare estrattore ELF + nuova tabella syscall |
| **RISC-V64 Linux** | 2 giorni | Serve RISC-V TargetDesc + nuova variante AllBlr + patching rilocazione RISC-V |

## Interfaccia estensione pass di offuscamento

La pipeline shellcode espone 11 hook via `Pipeline.h::ObfuscationHooks` per librerie di offuscamento terze parti. Il patching MIR integrato è anch'esso guidato da tabelle: `Tables/MIRRewritePatterns.def` e `Tables/MIRRewriteOpcodes.def`. Preferire voci di tabella e helper ristretti rispetto a rami specifici del target dispersi nel corpo del pass.
