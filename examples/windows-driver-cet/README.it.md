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

## Compilatore vs bin2bin: chi è compatibile con CET?

CET traccia una linea netta tra i **compilatori a livello sorgente** e gli
**strumenti bin2bin** (packer, offuscatori, hooker, dump+rebuild). Lo
Shadow Stack hardware impone tre regole che rimodellano l'intera industria
della protezione / offuscamento:

> 1. **Non modificare gli indirizzi di ritorno.**
> 2. **Non auto-modificare il codice** (HVCI impone W^X sulle pagine di codice).
> 3. **Cercare trasformazioni di offuscamento forti** che rispettino 1 e 2.

### Un compilatore può "cifrare gli indirizzi di ritorno"?

**No.** Questo è un fraintendimento comune. Shadow Stack è imposto dalla CPU,
non dal SO, ed è invisibile al codice in modalità utente. Se XOR-cifri
l'indirizzo di ritorno sullo stack regolare nell'epilogo della tua funzione:

```c
void my_func() {
    // ... corpo della funzione ...
    // l'epilogo tenta di cifrare l'indirizzo di ritorno:
    // XOR [rsp], 0xDEADBEEF
    // RET           <- l'hardware confronta stack regolare vs shadow stack
                     //   non corrispondono più -> eccezione #CP -> BUGCHECK
}
```

Lo shadow stack mantiene comunque l'indirizzo di ritorno originale. RET attiva
un confronto hardware; la mancata corrispondenza scatena `#CP` e fa bugcheck
del kernel. Il compilatore **non può** raggiungere lo shadow stack:

- Modalità utente: nessuna istruzione può scrivere lo shadow stack
- Modalità kernel: `WRSSQ` è privilegiato, solo `ntoskrnl` lo usa

### Offuscamenti compatibili con CET che il compilatore PUÒ fare

| Trasformazione | Perché è sicuro per CET |
|----------------|-------------------------|
| **Appiattimento del flusso di controllo** | Il dispatcher switch usa CALL/JMP diretto; i case ricevono ENDBR64 se necessario |
| **Virtualizzazione basata su VM** | Handler connessi tramite JMP indiretto (con ENDBR64), non push+ret |
| **Cifratura stringhe / costanti** | Pura trasformazione dati, nessun impatto sul flusso di controllo |
| **Espressioni MBA** | `x + y → (x ^ y) + 2*(x & y)` — solo dati |
| **Predicati opachi** | Rami condizionali via salti diretti |
| **Clonazione / inlining di funzioni** | Nessun cambio nella semantica dello stack di chiamate |
| **Sostituzione di istruzioni** | `MOV → XOR + ADD` — nessun effetto sullo stack |

### Pattern ostili a CET (muoiono sotto KCET)

| Pattern | Perché si rompe |
|---------|----------------|
| **Cifratura indirizzo di ritorno** | Mancata corrispondenza shadow stack → `#CP` |
| **PUSH addr; RET dispatcher** (stile VMProtect / Themida classico) | Stesso — lo shadow stack non ha voce per `addr` |
| **Stack pivoting** (catene di gadget ROP) | Lo shadow stack non può seguire il pivot |
| **Codice auto-modificante** | HVCI blocca le scritture sulle pagine eseguibili |
| **Generazione di codice a runtime** | Stesso — violazione HVCI W^X |
| **Hook inline basati su trampolino** | Modificare il prologo della funzione attiva HVCI; anche bypassando HVCI, lo shadow stack si rompe sul RET del trampolino |

### Perché gli strumenti bin2bin hanno uno svantaggio strutturale

Un compilatore emette codice CET-corretto da IR semantico. Uno strumento
bin2bin deve **riscoprire** la semantica dai byte compilati:

1. **Ambiguità dei confini delle istruzioni** — x86 ha lunghezza variabile. Aggiungere ENDBR64 (4 byte) all'offset sbagliato rompe tutto l'indirizzamento RIP-relativo e le rilocazioni.
2. **Identificazione dei target indiretti** — bin2bin non può sempre dire quali indirizzi in `.data` sono voci di tabella di salto vs dati grezzi. O sovra-marca (inflazione del codice, nuovi semi di gadget ROP) o sotto-marca (`#CP` a runtime).
3. **Pericolo di auto-attestazione** — Impostare `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` è una promessa. Se l'output bin2bin contiene qualsiasi pattern ostile a CET, il driver si caricherà bene su macchine non-CET ma BSOD istantaneamente su host KCET.
4. **Completezza del CFG** — I compilatori vedono l'intero grafo delle chiamate; bin2bin deve inferirlo, e le chiamate indirette senza target precisi forzano un posizionamento conservativo di ENDBR.

### Stato dell'industria

| Strumento / classe | Stato CET |
|-------------------|-----------|
| **NeverC / Clang / MSVC (compilatori)** | Nativamente compatibile con CET tramite `-fcf-protection` + flag del linker |
| **OLLVM / Tigress / pass di NeverC** | Trasformazioni a livello IR → naturalmente sicure per CET |
| **Microsoft Detours (4.0+)** | Aggiornato per essere compatibile con CET |
| **VMProtect / Themida (vecchio)** | Il dispatcher Push+RET uccide il driver su host KCET |
| **VMProtect / Themida (nuovo)** | Aggiungendo dispatcher consapevoli di ENDBR, supporto misto |
| **Loader manual map / dump+rebuild** | Devono ricostruire tutti i marcatori ENDBR — soggetto a errori |

### Prospettiva di sicurezza dei giochi

I driver anti-cheat (EAC, BattlEye, FACEIT AC, Vanguard) vengono spediti con
`--cetcompat` impostato, quindi funzionano puliti su macchine con KCET
abilitato. I driver di cheat — tipicamente impacchettati, hookati o iniettati
con trampolino tramite tooling bin2bin — faticano a rimanere conformi a CET.
KCET + HVCI formano un **muro hardware "amico del compilatore, ostile al
bin2bin"** che favorisce asimmetricamente il software di sicurezza ben
ingegnerizzato rispetto al codice in stile malware.

Questa è la ragione più profonda per cui Microsoft spinge così tanto KCET
per il software kernel: rende il codice kernel legittimo più facile da
indurire, rendendo allo stesso tempo il mestiere dell'attaccante
progressivamente più difficile.

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
