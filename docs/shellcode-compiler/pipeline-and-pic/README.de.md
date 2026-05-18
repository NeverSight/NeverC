**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# Shellcode-Pipeline, MIR & PIC-Strategie (Design-Notizen)

Dieses Dokument beschreibt die Design-Kompromisse in NeverCs Shellcode-Modus über die **IR → LLVM-Optimierung → Backend MIR → Objektdatei → Extraktion/Patching** Kette und die Beziehung zur **compilerweiten Standard-PIC**-Richtlinie. Implementierungsdetails sind im Quellcode und den englischen Kommentaren maßgeblich.

## 1. Warum standardmäßig PIC erzwingen (auch bei Nicht-Shellcode)

Der Shellcode-Extraktor nimmt an, dass Referenzen auf externe Symbole im ausführbaren Fragment auf **PC-relative** oder intra-`.text` auflösbaren Relocations landen, nicht auf hartcodierten absoluten Adressen oder Konstantenpools, die einen Loader zum Füllen von `.data` benötigen.

NeverC gibt **true** in `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()` und `MSVCToolChain::isPICDefaultForced()` zurück und unterscheidet sich damit vom Upstream-Clang-Verhalten „optionales Standard-PIC": **Plattformübergreifend wird immer nur PIC als Modell verwendet**. Das bedeutet:

- Normale C-Kompilierung und `-fshellcode`-Kompilierung teilen dieselben Relocation-Gewohnheiten, was die kognitive Last „funktioniert normal, bricht unter Shellcode" reduziert.
- Linux / Android / macOS / Windows Backends teilen unter tabellengesteuerten Deskriptoren (`TargetDesc` + `Options.td.h`) dieselben Annahmen und vermeiden `if (linux)`-Hardcoding im Treiber.

Diese Richtlinie unterscheidet nicht, ob `-fshellcode` aktiviert ist oder ob der Kontext user/kernel ist. Selbst wenn der Benutzer `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic` übergibt, behält `ParsePICArgs()` `Reloc::PIC_` bei und verwendet die gleichen PC-relativen Annahmen für normale Kompilierung, Benutzermodul-Shellcode und Kernelmodul-Shellcode.

## 2. Zwei-Phasen IR- und MIR-Arbeitsteilung

### 2.1 IR-Schicht (`registerShellcodePasses`)

Verantwortlich für die Komprimierung der „normalen C"-Semantik in eine **Einzeleingabe-, keine unabhängigen Datensektionen-, keine problematischen Globals**-Form: `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass` (nur Kernel), `Data2TextPass` etc.

**Prinzip**: Probleme, die in IR mit strukturierten Ansätzen lösbar sind, werden zuerst in IR behoben (Konstantenpools, BlockAddress, `memcpy`-Durchfall in libc, `__int128 /`-Durchfall in `__udivti3` etc.), wodurch der vom Backend und Extraktor gesehene Bytestream einfacher wird. Für Szenarien mit hoher Benutzerkognitionslast, die sicher internalisiert werden können, injiziert der Treiber proaktiv Regeln (z.B. AArch64 Linux / Android / Windows `long double` wird im Shellcode-Modus auf binary64 herabgestuft). Nur Konstrukte, die ohne Runtime nicht unterstützt werden können, lösen MIR-/Extraktor-Diagnosen aus.

### 2.2 MIR-Schicht (`registerShellcodeMachinePasses`)

Registriert Callbacks in LLVMs Legacy `TargetPassConfig` **nach Registerallokation, vor `addPreEmitPass`**, in dieser Reihenfolge:

1. Benutzer/Obfuskationsbibliothek: `RunBeforePreEmit` (CFI / EH-Pseudos noch vorhanden; nützlich für metadatenabhängige Transformationen).
2. **`ShellcodeMIRPrepPass`**: Entfernt Pseudos, die `.eh_frame` / `.pdata` / `.xray_*` Nebensektionen erzeugen würden, sodass der Befehlsstrom vor AsmPrinter möglichst nahe an „purem Code" ist.
3. Benutzer/Obfuskationsbibliothek: `RunAfterPreEmit` (geeignet für Befehlssubstitution, Registerumbenennung und ähnliche „finale Maschinencode-Form"-Obfuskation).

**Prinzip**: Wenn native Befehlssequenzen noch Probleme haben, in MIR beheben (besonders um `ShellcodeMIRPrepPass`); **Extraktion und Patching sind das letzte Sicherheitsnetz**, um Logikduplikation über COFF/ELF/Mach-O-Schichten zu vermeiden.

MIR-Opcode-Namen werden nicht in der Pass-Kontrollfluss verstreut; `ShellcodeMIRPrepPass` verwendet die `Tables/MIRRewriteOpcodes.def` `(pattern, role, opcode)`-Tabelle über `TargetInstrInfo::getName()`. Beim Hinzufügen shellcode-freundlicher Befehlssubstitutionen werden Tabelleneinträge und schmale MIR-Rewrites bevorzugt; nur bei Bedarf auf Backend-`.td`-Befehlsauswahländerungen zurückfallen, Extraktor-Level-Objektformat-Fallback als letztes Mittel.

> Hinweis: `ShellcodeMIRPrepPass` wird nur bei aktiviertem `-fshellcode` registriert. Normale Programme dürfen CFI/EH nicht global entfernen, da dies die reguläre Ausnahmebehandlung und Debug-Informationen zerstören würde.

Sowohl IR- als auch MIR-globale Callbacks verwenden ein **einmal registrieren, zur Laufzeit den aktuellen `ShellcodeOptions`-Snapshot lesen**-Muster. Dies unterstützt langlebige eingebettete Compiler-Prozesse: Wenn derselbe Prozess zuerst Shellcode und dann normales C kompiliert, erbt die normale C-Kompilierung nicht die vorherigen IR/MIR-Passes; bei aufeinanderfolgender Kompilierung mehrerer Shellcode-TUs stapeln doppelte globale Callback-Registrierungen nicht den gleichen Pass-Satz mehrfach.

## 3. Tabellengesteuerte Plattformunterschiede

- **Triple → Verhalten**: Zentralisiert in `TargetDesc.cpp`s `describeTriple()` und `TargetDesc`-Feldern (Sektionsname, Syscall-ABI, Inline-Assemblierung-Template, Treiber-Injektionsflags etc.). Beim Hinzufügen neuer OS/Arch wird das **Hinzufügen von Tabelleneinträgen** gegenüber dem Schreiben langer Verzweigungen in Extraktoren oder Passes bevorzugt.
- **CLI-Optionen**: Definiert in `neverc/include/neverc/Invoke/Options.td.h`; konsumiert von `DriverIntegration.cpp` über `OPT_*`-Enums, String-Magie vermeidend.

## 4. Windows MSVC-Toolchain und SDK-Layout

Beim Cross-Kompilieren für Windows-Ziele unterstützt NeverC zwei SDK-Quellen **ohne hartcodierte absolute Pfade**:

1. **Mit dem Build-Baum gebündeltes SDK** (empfohlen): Benutzer und Testskripte behandeln `build-neverc/sdk` als SDK-Root. NeverC erkennt automatisch `sdk/msvc/` im Installationsverzeichnis und injiziert include/lib-Pfade in `MSVCToolChain::AddClangSystemIncludeArgs` / `Linker::ConstructJob`. Typisches Layout:

   ```
   build-neverc/bin/neverc
   build-neverc/sdk/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **Echtes VS-Stil-Sysroot** (optional): Bei vorhandenem `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...`-Verzeichnisbaum über `-winsysroot=<path>` oder die `NEVERC_WIN_SYSROOT`-Umgebungsvariable verweisen.

Beide Quellen funktionieren ohne Registry oder OS-bereitgestellte VS-Umgebungsvariablen, was Windows-Shellcode-Cross-Kompilierung von macOS / Linux ermöglicht.

## 5. Obfuskation und Erweiterungspunkte

- **IR-Obfuskation**: Über `setShellcodeObfuscationHooks` mit mehreren IR-Stufen-Hooks; `-fshellcode-obfuscate=` übergibt den Spec-String an die externe Bibliothek. Jede Schicht bietet **pre** (vor Optimierung) und **post** (nach Optimierung) Hooks. `RunAfterFinalIR` ist der echte letzte IR-Injektionspunkt — nach hier registrierten Obfuskations-Passes gibt es keine nachfolgenden Passes. 11 Hook-Punkte insgesamt (6 IR + 3 MIR + 2 Byte-Stream).
- **MIR-Obfuskation**: `RunBeforePreEmit` / `RunAfterPreEmit` sind MIR-Hooks mittlerer Granularität; `RunAfterFinalMIR` ist der **echte letzte** MIR-Hook (Fork-Erweiterung fügt `RegisterTargetPassConfigPostPreEmitCallbackFn` hinzu, aufgerufen nach `addPreEmitPass2()`). `-fshellcode-mir-obfuscate=` spezifiziert MIR-Spec separat; Standard ist IR-Spec wenn nicht gesetzt.
- **Byte-Stream-Hooks**: `RunPostExtract` ist der Finalize-**Pre**-Hook; `RunPostFinalize` ist der Finalize-**Post**-Hook (letzter Moment vor Festplattenschreiben, NeverC prüft danach nicht mehr).
- **Finalize Plugin SDK**: `Plugin.h` exponiert `registerBadByteRewriteStrategy` (verkettete Befehlsebene-Bad-Byte-Rewrite-Strategien) und `registerCharsetEncoder` (benannte Zeichensatzregistrierung). Siehe [plugin-interface.md §2–§3](../plugin-interface/README.de.md#2-bad-byte-rewriter-badbyterewritestrategy).
- **Größe / Ausrichtung / Padding**: `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` werden am Ende von Finalize ausgeführt; der Treiber lehnt widersprüchliche Konfigurationen ab.
- **Design-Entscheidung**: Obfuskation, Polymorphismus, Stufen-Encoder, indirekte Syscalls und ähnliche Strategie-Schicht-Features sind **bewusst nicht eingebaut**, sondern nur als optionale Plugins verfügbar.

## 6. Kernel-Modus (Ring-0) Dimension

Der Shellcode-Modus führt `-mshellcode-context=user|kernel` als zweite Pipeline-Dimension ein, über dem Triple geschichtet:

- **Benutzermodus**: PEB-Walk / Syscall-Stub-Pipeline.
- **Kernelmodus**:
  - `SyscallStubPass` / `WinPEBImportPass` kehren auf Pass-Ebene früh zurück.
  - `TargetDesc::KernelInjectFlags` fügt OS/arch-gerechte Backend-Flags hinzu (Unix x86_64: `-mno-red-zone -mcmodel=kernel`, Windows: `/kernel`, AArch64: `-mgeneral-regs-only`).
  - `KernelImportPass` schreibt ungelöste extern-Direktaufrufe in resolver-gestützte indirekte Aufrufe um, injiziert bei Bedarf `(resolver, cookie)` implizite Präfixparameter.
  - `<neverc/kernel.h>` exponiert `neverc_kern_resolve_t`, `neverc_kern_hash()` und verwandte Kernel-Signaturen; Benutzermodus-Shims (`<windows.h>`, `<unistd.h>` etc.) lehnen im Kernelmodus via `#error` ab.

Siehe [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.de.md) für Details.

## 7. Windows POSIX-Kompatibilitätsschicht

### 7.1 Problem

Plattformübergreifender C-Code verwendet häufig `write(fd, buf, n)`, `read(fd, buf, n)`, `exit(code)` etc. Auf Unix ersetzt `SyscallStubPass` diese durch Inline-Syscalls. Auf Windows haben diese POSIX-Namen keine entsprechende Win32-API, was zu „unauflösbarer Relocation"-Fehlern führt.

### 7.2 Design-Ziel

**Null Benutzer-Bewusstsein**: Derselbe C-Quellcode kompiliert über alle 8 Ziel-Triples ohne `#ifdef _WIN32` oder manuelle Win32-API-Aufrufe.

### 7.3 Implementierung

`WinPEBImportPass` implementiert dreiphasige Verarbeitung:

1. **Phase 1 — POSIX-Scan**: Scannt nicht-gematchte extern-Deklarationen gegen eine POSIX-Kompatibilitätstabelle.
2. **Phase 2 — Bridge-Wrapper-Generierung**: `Win32PosixCompat.def` dispatcht POSIX-Namen an Wrapper-Builder, die `always_inline`-Wrapper generieren (z.B. `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc` mit Prot-Mapping, `exit` → `ExitProcess` etc.). 13 POSIX-Funktionsgruppen abgedeckt.
3. **Phase 3 — PEB-Auflösung**: Von Wrappern referenzierte Win32-APIs werden über den normalen PEB-Walk-Resolver aufgelöst.

### 7.4 Erweiterbarkeit

Neue POSIX-Kompatibilitätsfunktionen hinzufügen: Nur-Alias-Ergänzungen ändern nur `Win32PosixCompat.def`; neue Semantik erfordert kleinen IR-Builder + einen Tabelleneintrag. Zustandsbehaftete Operationen wie `open→CreateFileA` (benötigen fd/handle-Lebensdauertabellen) werden absichtlich nicht emuliert.

## 8. K&R Implizite-Deklaration-Autofix

Wenn Benutzer POSIX-Funktionen ohne `#include` aufrufen, generiert C89 K&R-implizite Deklarationen mit 0 formalen Parametern. `SyscallStubPass` pflegt eine `getCanonicalSyscallType()`-Tabelle mit kanonischen LLVM-IR-Funktionstypen für 50+ gängige POSIX-Funktionen. Bei Erkennung einer 0-Parameter-K&R-Deklaration wird die kanonische Signatur automatisch substituiert.

## 9. Zusammenfassung

| Thema | Ansatz |
|-------|--------|
| Standard-PIC | Alle Toolchains `isPICDefaultForced()==true`, an Shellcode-Annahmen ausgerichtet |
| Zuerst in IR beheben | Konstanten, indirekte Sprünge, Speicher-Intrinsics möglichst in IR eliminieren |
| MIR-Sicherheitsnetz | `ShellcodeMIRPrepPass` + Pre/Post-Hooks, dann Objekt-Extraktion/Patching als letztes Mittel |
| Hardcoding minimieren | `TargetDesc` + `Options.td.h` tabellengesteuert |
| User/Kernel zwei Dimensionen | `-fshellcode` × `-mshellcode-context={user,kernel}`; jedes (OS, arch, level) ist eine Zeile in `describeTriple()` |
| Windows POSIX-Kompatibilität | `WinPEBImportPass` überbrückt 13 POSIX-Funktionsgruppen (write→WriteFile, mmap→VirtualAlloc etc.) |
| K&R Autofix | `SyscallStubPass` fällt bei 0-Parameter-Deklarationen auf kanonische POSIX-Signaturen zurück |

## 10. Shim-Header Plattformübergreifende Konstanten

Shim-Header (`sys/mman.h`, `fcntl.h` etc.) exponieren Konstanten, die zum Ziel-Kernel-ABI passen müssen, da Shellcode-Syscall-Stubs diese Werte direkt ohne libc-Übersetzung an den Kernel übergeben.

Wichtige Unterschiede:

| Konstante | Darwin | Linux/Android |
|-----------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

Implementierung: `#if defined(__APPLE__)`-Guards in Shim-Headern. `SyscallTables.cpp` POSIX-Kompatibilitätstabelle verwendet Linux-Werte (`AT_FDCWD = -100`), nur auf `SyscallABI::LinuxSvc0` / `LinuxSyscall`-Pfaden aktiv. Windows-Ziele verwenden diese POSIX-Header nicht; POSIX→Win32-Bridging wird von `WinPEBImportPass`-Kompatibilitätswrappern behandelt.
