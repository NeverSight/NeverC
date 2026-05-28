# Driver kernel Windows con CET Shadow Stack

Un driver kernel WDM minimale costruito con NeverC, con Intel CET
(Control-flow Enforcement Technology) Kernel Shadow Stack abilitato.
Compilazione incrociata da macOS / Linux.

## Compilazione

```bash
cd examples/windows-driver-cet
make
```

Da una versione standalone di NeverC:

```bash
make NEVERC=/path/to/neverc
```

L'output è `CetDriver.sys` (ottimizzato auto-LTO).
La compilazione predefinita include `-g` per il debug; **le versioni di rilascio
dovrebbero rimuovere `-g`** per eliminare i simboli di debug e ridurre la
dimensione del binario.

## Flag specifici CET

| Flag | Livello | Scopo |
|------|---------|-------|
| `-fcf-protection=return` | Compilatore | Generare codice compatibile con Shadow Stack |
| `-Xlinker --cetcompat` | Linker | Impostare `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` nel PE |

## Compilazione manuale (senza Make)

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

## Funzionalità

- Crea un oggetto dispositivo in `\Device\CetDriver`
- Crea un collegamento simbolico in `\DosDevices\CetDriver`
- Esegue chiamate indirette (puntatore a funzione `ComputeFn`) per validare la compatibilità CET — Shadow Stack protegge gli indirizzi di ritorno di queste chiamate
- Stampa messaggi di caricamento/scaricamento tramite `DbgPrint`

---

## Dettagli tecnici CET

CET dispone di **due meccanismi di protezione indipendenti**:

### 1. Shadow Stack — protezione del bordo posteriore (RET)

L'hardware mantiene un secondo stack (shadow stack) che rispecchia le operazioni CALL/RET.
**Nessuna istruzione speciale necessaria all'ingresso della funzione** — completamente trasparente:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  Stack normale:  PUSH return_addr  (RSP)       │
│  Shadow stack:   PUSH return_addr  (SSP, HW)   │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  Stack normale:  POP return_addr_A  (RSP)      │
│  Shadow stack:   POP return_addr_B  (SSP, HW)  │
│                                                │
│  Confronto: return_addr_A == return_addr_B ?    │
│    ✓ corrispondenza → ritorno normale           │
│    ✗ non corrispondenza → eccezione #CP         │
│                                                │
└────────────────────────────────────────────────┘
```

### 2. Indirect Branch Tracking (IBT) — protezione del bordo anteriore (CALL/JMP indiretto)

Richiede un'istruzione `ENDBR64` (`F3 0F 1E FA`, 4 byte) in ogni target valido di chiamata/salto indiretto. Su CPU senza CET, `ENDBR64` è un NOP.

### Scelta del kernel Windows

| Protezione | Meccanismo | Usato dal kernel Windows? |
|------------|-----------|---------------------------|
| Bordo posteriore (RET) | CET Shadow Stack | **Sì** (KCET) |
| Bordo anteriore (CALL/JMP indiretto) | CET IBT (ENDBR) | **No** — CFG usato al suo posto |

### Confronto assembly: modalità `-fcf-protection`

Codice sorgente:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (nessun CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (solo Shadow Stack — questo esempio usa questa modalità)

```asm
rotate13:
    mov  eax, ecx      ; identico a "none"!
    rol  eax, 13        ; Shadow Stack è completamente trasparente
    ret
```

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← marcatore IBT (F3 0F 1E FA)
    mov  eax, ecx
    rol  eax, 13
    ret
```

---

## Attivazione KCET sulla macchina target

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Riavvio necessario. Verificare con `msinfo32.exe`.

**Requisiti:** HVCI abilitato, Windows build 21389+, CPU con supporto CET (Intel Tiger Lake+ / AMD Zen 3+).

## Caricamento

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Abilitare la firma di test o utilizzare un certificato di firma del codice per la produzione.
