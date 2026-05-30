**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows-Kerneltreiber mit CET Shadow Stack

Ein minimaler WDM-Kerneltreiber, erstellt mit NeverC, mit aktiviertem Intel CET
(Control-flow Enforcement Technology) Kernel Shadow Stack. Cross-Kompilierung
von macOS / Linux.

## Bauen

```bash
cd examples/windows-driver-cet
make
```

Mit einer eigenständigen NeverC-Version:

```bash
make NEVERC=/path/to/neverc
```

Die Ausgabe ist `CetDriver.sys` (auto-LTO-optimiert).
Der Standard-Build enthält `-g` zum Debuggen; **Release-Builds sollten `-g`
entfernen**, um Debug-Symbole zu entfernen und die Binärgröße zu reduzieren.

## CET-spezifische Flags

| Flag | Ebene | Zweck |
|------|-------|-------|
| `-fcf-protection=return` | Compiler | Shadow-Stack-kompatiblen Code generieren |
| `-Xlinker --cetcompat` | Linker | `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` in PE setzen |

## Manuelles Bauen (ohne Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## Funktionen

- Erstellt ein Geräteobjekt unter `\Device\CetDriver`
- Erstellt einen symbolischen Link unter `\DosDevices\CetDriver`
- Nutzt indirekte Aufrufe (`ComputeFn`-Funktionszeiger) zur Validierung der CET-Kompatibilität — Shadow Stack schützt die Rücksprungadressen dieser Aufrufe
- Gibt Lade-/Entlade-Nachrichten über `DbgPrint` aus

---

## Technische Details zum CET

CET verfügt über **zwei unabhängige Schutzmechanismen**:

### 1. Shadow Stack — Rückwärtskantenschutz (RET)

Die Hardware unterhält einen zweiten Stack (Shadow Stack), der CALL/RET spiegelt.
**Keine speziellen Instruktionen am Funktionseintritt erforderlich** — vollständig transparent:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  Normaler Stack:  PUSH return_addr  (RSP)      │
│  Shadow Stack:    PUSH return_addr  (SSP, HW)  │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  Normaler Stack:  POP return_addr_A  (RSP)     │
│  Shadow Stack:    POP return_addr_B  (SSP, HW) │
│                                                │
│  Vergleich: return_addr_A == return_addr_B ?    │
│    ✓ Übereinstimmung → normaler Rücksprung      │
│    ✗ Keine Übereinstimmung → #CP-Ausnahme       │
│                                                │
└────────────────────────────────────────────────┘
```

### 2. Indirect Branch Tracking (IBT) — Vorwärtskantenschutz (indirektes CALL/JMP)

Erfordert eine `ENDBR64`-Instruktion (`F3 0F 1E FA`, 4 Bytes) an jedem gültigen indirekten Aufruf-/Sprungziel. Auf CPUs ohne CET ist `ENDBR64` ein NOP.

### Wahl des Windows-Kernels

| Schutz | Mechanismus | Vom Windows-Kernel verwendet? |
|--------|-------------|-------------------------------|
| Rückwärtskante (RET) | CET Shadow Stack | **Ja** (KCET) |
| Vorwärtskante (indirektes CALL/JMP) | CET IBT (ENDBR) | **Nein** — CFG wird stattdessen verwendet |

### Assembler-Vergleich: `-fcf-protection`-Modi

Quellcode:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (kein CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (nur Shadow Stack — dieses Beispiel verwendet diesen Modus)

```asm
rotate13:
    mov  eax, ecx      ; identisch mit "none"!
    rol  eax, 13        ; Shadow Stack ist vollständig transparent
    ret
```

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← IBT-Markierung (F3 0F 1E FA)
    mov  eax, ecx
    rol  eax, 13
    ret
```

---

## Compiler vs bin2bin: Wer ist CET-freundlich?

CET zieht eine klare Linie zwischen **Compilern auf Quellcode-Ebene** und
**bin2bin-Tools** (Packer, Obfuskatoren, Hooker, dump+rebuild). Der
Hardware-Shadow-Stack erzwingt drei Regeln, die die gesamte Schutz- /
Obfuskationsindustrie umgestalten:

> 1. **Rücksprungadressen nicht modifizieren.**
> 2. **Code nicht selbst-patchen** (HVCI erzwingt W^X auf Codeseiten).
> 3. **Starke Obfuskations-Transformationen suchen**, die 1 & 2 respektieren.

### Kann ein Compiler „Rücksprungadressen verschlüsseln"?

**Nein.** Das ist ein häufiges Missverständnis. Shadow Stack wird von der CPU
erzwungen, nicht vom OS, und ist für User-Mode-Code unsichtbar. Wenn Sie die
Rücksprungadresse auf dem regulären Stack im Funktionsepilog XOR-verschlüsseln:

```c
void my_func() {
    // ... Funktionskörper ...
    // Epilog versucht, Rücksprungadresse zu verschlüsseln:
    // XOR [rsp], 0xDEADBEEF
    // RET           <- Hardware vergleicht regulären Stack vs Shadow Stack
                     //   sie stimmen nicht mehr überein -> #CP-Ausnahme -> BUGCHECK
}
```

Der Shadow Stack hält weiterhin die ursprüngliche Rücksprungadresse. RET
löst einen Hardware-Vergleich aus; bei Nichtübereinstimmung wird `#CP` ausgelöst
und der Kernel bugcheckt. Der Compiler **kann den Shadow Stack nicht erreichen**:

- User-Mode: Keine Instruktion kann auf den Shadow Stack schreiben
- Kernel-Mode: `WRSSQ` ist privilegiert, nur `ntoskrnl` verwendet es

### CET-freundliche Obfuskationen, die der Compiler durchführen KANN

| Transformation | Warum CET-sicher |
|----------------|------------------|
| **Control-Flow Flattening** | Switch-Dispatcher verwendet direkte CALL/JMP; Cases erhalten ENDBR64 bei Bedarf |
| **VM-basierte Virtualisierung** | Handler über indirekten JMP (mit ENDBR64) verbunden, nicht push+ret |
| **String- / Konstantenverschlüsselung** | Reine Datentransformation, keine Auswirkung auf Kontrollfluss |
| **MBA-Ausdrücke** | `x + y → (x ^ y) + 2*(x & y)` — nur Daten |
| **Opake Prädikate** | Bedingte Verzweigungen über direkte Sprünge |
| **Funktionsklonen / Inlining** | Keine Änderung der Call-Stack-Semantik |
| **Instruktionssubstitution** | `MOV → XOR + ADD` — keine Stack-Effekte |

### CET-feindliche Muster (sterben unter KCET)

| Muster | Warum es bricht |
|--------|----------------|
| **Rücksprungadressenverschlüsselung** | Shadow-Stack-Diskrepanz → `#CP` |
| **PUSH addr; RET Dispatcher** (klassischer VMProtect / Themida-Stil) | Dasselbe — Shadow Stack hat keinen Eintrag für `addr` |
| **Stack-Pivoting** (ROP-Gadget-Ketten) | Shadow Stack kann dem Pivot nicht folgen |
| **Selbstmodifizierender Code** | HVCI blockiert Schreibvorgänge auf ausführbare Seiten |
| **Laufzeit-Codegenerierung** | Dasselbe — HVCI W^X-Verletzung |
| **Trampolin-basierte Inline-Hooks** | Funktionsprolog-Modifikation löst HVCI aus; selbst bei HVCI-Umgehung bricht der Shadow Stack beim Trampolin-RET |

### Warum bin2bin-Tools einen strukturellen Nachteil haben

Ein Compiler emittiert CET-korrekten Code aus semantischem IR. Ein
bin2bin-Tool muss die Semantik aus kompilierten Bytes **wiederentdecken**:

1. **Mehrdeutigkeit der Instruktionsgrenzen** — x86 ist variabel lang. Das Hinzufügen von ENDBR64 (4 Bytes) am falschen Offset bricht alle RIP-relativen Adressierungen und Relokationen.
2. **Identifizierung indirekter Ziele** — bin2bin kann nicht immer sagen, welche Adressen in `.data` Jumptable-Einträge vs rohe Daten sind. Entweder Übermarkierung (Codeaufblähung, neue ROP-Gadget-Keime) oder Untermarkierung (Laufzeit-`#CP`).
3. **Selbstattestations-Gefahr** — Das Setzen von `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` ist ein Versprechen. Enthält die bin2bin-Ausgabe ein CET-feindliches Muster, lädt der Treiber auf Nicht-CET-Maschinen problemlos, aber BSODt sofort auf KCET-Hosts.
4. **CFG-Vollständigkeit** — Compiler sehen den gesamten Aufrufgraphen; bin2bin muss ihn ableiten, und indirekte Aufrufe ohne präzise Ziele erzwingen konservative ENDBR-Platzierung.

### Branchenstatus

| Tool / Klasse | CET-Status |
|---------------|-----------|
| **NeverC / Clang / MSVC (Compiler)** | Nativ CET-freundlich über `-fcf-protection` + Linker-Flag |
| **OLLVM / Tigress / NeverC-Passes** | IR-Level-Transformationen → natürlich CET-sicher |
| **Microsoft Detours (4.0+)** | Auf CET-Kompatibilität aktualisiert |
| **VMProtect / Themida (ältere Versionen)** | Push+RET-Dispatcher tötet den Treiber auf KCET-Hosts |
| **VMProtect / Themida (neuere Versionen)** | ENDBR-bewusste Dispatcher werden hinzugefügt, gemischte Unterstützung |
| **Manual Map / dump+rebuild-Loader** | Müssen alle ENDBR-Marker rekonstruieren — fehleranfällig |

### Game-Security-Perspektive

Anti-Cheat-Treiber (EAC, BattlEye, FACEIT AC, Vanguard) werden mit gesetztem
`--cetcompat` ausgeliefert, daher laufen sie sauber auf KCET-aktivierten
Maschinen. Cheat-Treiber — typischerweise gepackt, gehookt oder via
Trampolin-Injection durch bin2bin-Tooling — kämpfen damit, CET-konform zu
bleiben. KCET + HVCI bilden eine **„compiler-freundliche, bin2bin-feindliche"
Hardware-Mauer**, die asymmetrisch gut konstruierte Sicherheitssoftware
gegenüber Malware-Stil-Code begünstigt.

Dies ist der tiefere Grund, warum Microsoft KCET für Kernel-Software so stark
vorantreibt: Es macht legitimen Kernel-Code einfacher zu härten, während es
das Handwerk des Angreifers fortschreitend schwieriger macht.

---

## KCET auf dem Zielrechner aktivieren

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Neustart erforderlich. Überprüfen Sie mit `msinfo32.exe`.

**Voraussetzungen:** HVCI aktiviert, Windows Build 21389+, CPU mit CET-Unterstützung (Intel Tiger Lake+ / AMD Zen 3+).

## Laden

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Aktivieren Sie die Testsignierung oder verwenden Sie ein Codesignaturzertifikat für die Produktion.
