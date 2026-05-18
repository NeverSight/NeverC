**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# Plattform-Erweiterungsanleitung

Dieses Dokument erklärt, wie der Shellcode-Compiler auf neue Zielplattformen erweitert wird. Aktuell unterstützt: **arm64 / x86_64 auf macOS / Linux / Android / Windows** (8 Triples), jeweils mit unabhängigen **User** / **Kernel** Kontexten (16 Varianten insgesamt). Das Hinzufügen einer neuen Plattform erfordert typischerweise einige hundert Codezeilen.

## Designphilosophie: Tabellengesteuert, nicht verzweigungsgesteuert

Alle Passes sind zielunabhängig. Plattformunterschiede konzentrieren sich auf **zwei Stellen**:

1. `TargetDesc.cpp`s `describeTriple()` Tabelleneinträge
2. Drei Extraktoren (Mach-O / ELF / COFF) Architektur-Switches

Neue Plattform hinzufügen = eine Zeile in (1) + ein Case in (2).

## Schritte

### 1. Zeile in `TargetDesc` hinzufügen

Den entsprechenden OS-Zweig in `describeTriple()` hinzufügen:

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

**Pflichtfelder** (alle in `TargetDesc.h` definiert):

| Feld | Zweck | Bei Fehlen |
|------|-------|------------|
| `OS` / `Arch` / `Format` | Dispatch-Schlüssel | `describeTriple` gibt Unknown zurück → Treiber lehnt früh ab |
| `TextSectionName` | Extraktor findet Eingangssektion | Extraktor findet `.text` nicht → Ablehnung |
| `Syscall` | SyscallStubPass Ersetzungsentscheidung | `None` → SyscallStubPass No-Op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | SyscallStubPass Inline-Asm-Erzeugung | Eines leer → SyscallStubPass No-Op |
| `TCBReadAsm` / `TCBReadConstraint` | WinPEBImportPass TEB-Lese-Inline-Asm | Leer → PEB-Walk erzeugt leeres InlineAsm (Windows: erforderlich) |
| `DriverInjectFlags` | Plattformspezifische Flags im Shellcode-Modus | null → keine Injektion |

### 2. `SyscallStub` / `SyscallTables` erweitern (wenn OS Kernel-Traps hat)

- Enum-Wert zu `SyscallABI` in `TargetDesc.h` hinzufügen
- `kXxxTable` in `SyscallTables.cpp` hinzufügen
- Case in `lookupSyscall`s Switch hinzufügen
- `SyscallStubPass` bleibt unverändert — InlineAsm-Vorlagen/Einschränkungen kommen von `TargetDesc`

### 2.5 Windows Win32 API Whitelist erweitern

Windows hat kein stabiles Syscall-ABI; Benutzeraufrufe an `WriteFile` / `CreateThread` / `VirtualAlloc` gehen durch `WinPEBImportPass`. Die Whitelist ist eine Multi-DLL-Tabelle:

- Definiert in `Tables/Win32Apis.def`
- Jede Zeile: `NEVERC_WIN32_API(ApiName, "dll.dll")`
- Der Resolver unterstützt bereits beliebige DLLs über `__neverc_win_resolve(dll_hash, api_hash)`

**Neue API hinzufügen** (z.B. `DeviceIoControl`):
1. Eine Zeile zu `Win32Apis.def` hinzufügen
2. Deklaration im Shellcode-Zweig von `lib/Headers/windows.h` spiegeln
3. Keine Pass-Änderungen nötig

**Neuen DLL-Bucket hinzufügen** (z.B. `winhttp.dll`):
- Einfach Zeilen mit dem neuen DLL-Namen in `Win32Apis.def` hinzufügen

### 3. Entsprechenden Extraktor erweitern

Drei Dinge zu behandeln:
1. Reloc-Typen identifizieren → Bytes patchen oder ablehnen
2. Verbotene Datensektionsnamen-Liste aktualisieren (neues OS hat möglicherweise eigene Sektionen)
3. Eingang-bei-Offset-0 Reloc-Zielbereichsvalidierung aktualisieren

Für ein völlig neues Objektformat (z.B. WASM-Module):
1. `ObjectFormat` Enum-Wert hinzufügen
2. Case in `ShellcodeExtractor.cpp`s Dispatch-Switch hinzufügen
3. `<Format>Extractor.cpp` schreiben (Struktur von `ELFExtractor.cpp` folgen)

### 4. Loader hinzufügen (nur Testwerkzeug)

- Referenz `tests/neverc/shellcode/loader_linux.c` und `loader_windows.c`
- Typischerweise: `mmap(RWX) → memcpy → icache flush → call`

### 5. Tests aktualisieren

- `cross_compile_check` Zeile in `run_cross_target_tests.sh` hinzufügen
- Wenn CI auf der Plattform ausführen kann, Loader-Roundtrip-Test hinzufügen

---

## Bekannte plattformübergreifende Fallstricke

- **Endianness**: NeverC unterstützt nur Little-Endian (LE), deckt alle Mainstream-Ziele ab.
- **ABI-Unterschiede**: Win64 (rcx/rdx/r8/r9) vs System V AMD64 (rdi/rsi/rdx/rcx/r8/r9) haben komplett verschiedene Argumentregister. Dies wird in der Clang-Frontend-Schicht behandelt; die Shellcode-Pipeline muss sich nicht darum kümmern.
- **Syscall-Nummern**: Auf Linux pro Architektur unterschiedlich, Android gleich Linux, Darwin hat eigene BSD-Nummern, Windows hat keine stabilen Nummern (daher PEB-Walk). Im Tabelle per (OS, arch) indiziert.
- **Cache-Kohärenz**: ARM braucht expliziten i-Cache-Flush; x86 nicht. macOS arm64 JIT braucht auch `pthread_jit_write_protect_np`; Linux arm64 nutzt `__builtin___clear_cache`; Windows nutzt `FlushInstructionCache` (auf x86 No-Op).
- **SELinux / W^X**: Android wird durch SELinux `execmem` eingeschränkt; nicht-gejailbreaktes iOS lehnt `mmap(RWX)` komplett ab, `MAP_JIT` + Code-Signierung erforderlich.

## Zukünftige Erweiterungs-Roadmap

| Ziel | Geschätzter Aufwand | Abhängigkeiten |
|------|-------------------|----------------|
| **iOS arm64** (Jailbreak / `MAP_JIT`) | 1 Tag | Mach-O Extraktor wiederverwenden, Loader modifizieren |
| **FreeBSD / OpenBSD x86_64** | Halber Tag | ELF Extraktor wiederverwenden + neue Syscall-Tabelle |
| **RISC-V64 Linux** | 2 Tage | RISC-V TargetDesc + neue AllBlr-Variante + RISC-V Reloc-Patching nötig |

## Obfuskations-Pass Erweiterungsschnittstelle

Die Shellcode-Pipeline exponiert 11 Hooks über `Pipeline.h::ObfuscationHooks` für Drittanbieter-Obfuskationsbibliotheken:

```
PipelineStartEP:
  RunBeforePrep → [ZeroReloc Prep] → RunAfterPrep →
  [IndirectBr → MemIntrin → CompilerRt → SyscallStub →
   WinPEBImport → KernelImport → Data2Text phase 1] →
  RunBeforeInlining

OptimizerLastEP:
  RunAfterInlining → [Data2Text phase 2 → ZeroReloc Stackify] →
  RunAfterStackify → [AllBlrPass] → RunAfterFinalIR

MIR: RunBeforePreEmit → [MIRPrepPass] → RunAfterPreEmit →
     [LLVM addPreEmitPass/addPreEmitPass2] → RunAfterFinalMIR

Byte-Stream: RunPostExtract → [finalize Kette] → RunPostFinalize
```

Eingebautes MIR-Patching ist ebenfalls tabellengesteuert: `Tables/MIRRewritePatterns.def` und `Tables/MIRRewriteOpcodes.def`. Beim Hinzufügen neuer shellcode-freundlicher Backend-Formen Tabelleneinträge und schmale Helfer bevorzugen statt zielspezifische Verzweigungen im Pass-Body zu verstreuen.
