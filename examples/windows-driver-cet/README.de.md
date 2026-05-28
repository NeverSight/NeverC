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
