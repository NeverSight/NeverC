# Driver kernel Windows con virgola mobile

Un driver kernel WDM costruito con NeverC che dimostra **l'uso sicuro
di operazioni in virgola mobile / SIMD in modalità kernel**. Compilazione
incrociata da macOS / Linux.

## Compilazione

```bash
cd examples/windows-driver-float
make
```

Da una versione standalone di NeverC:

```bash
make NEVERC=/path/to/neverc
```

L'output è `FloatDriver.sys` (ottimizzato auto-LTO).
La compilazione predefinita include `-g` per il debug; rimuovere `-g` per
le versioni di rilascio.

---

## Due problemi da gestire

La virgola mobile in modalità kernel ha due problemi distinti:

### Problema 1 — il marcatore ABI `_fltused` (tempo di compilazione/linking)

Il compilatore MSVC emette un riferimento non definito al simbolo `_fltused`
ogni volta che un'unità di traduzione esegue qualsiasi operazione in virgola
mobile. Nei programmi in modalità utente, `libcmt.lib` fornisce questo
simbolo soddisfacendo il linker e includendo alcune parti CRT specifiche per FP.

I driver kernel **non** sono collegati a `libcmt` (passiamo `-nostdlib` e
`-Xlinker --nodefaultlib`), quindi un `_fltused` non risolto causerebbe un errore di linking.

**Come NeverC lo risolve**: con `-fms-kernel`, il backend X86 di LLVM
definisce `_fltused` localmente come 0. Puoi vederlo nell'assembly generato:

```asm
# Target modalità utente:
    .globl  _fltused              # riferimento esterno -- richiede libcmt
```

```asm
# Target -fms-kernel:
    .globl  _fltused
    .set    _fltused, 0           # definizione locale! nessun simbolo esterno richiesto
```

Quindi **non devi mai scrivere manualmente `int _fltused = 0;`** nel tuo driver.

### Problema 2 — il kernel NON preserva i registri FP/SIMD (tempo di esecuzione)

Il kernel Windows non salva/ripristina i registri x87 / XMM / YMM / ZMM
durante i cambi di contesto per impostazione predefinita. Se un driver
tocca uno di questi da codice kernel arbitrario, corromperà silenziosamente
lo stato SIMD del thread in modalità utente che si trova sulla CPU.

**Soluzione**: racchiudi ogni regione virgola mobile / SIMD con
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
e `KeRestoreExtendedProcessorState`:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... il tuo codice FP / SIMD qui ...

KeRestoreExtendedProcessorState(&save);
```

### Maschere XSTATE

| Maschera | Copre |
|----------|-------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (bit 0) | stack x87 |
| `XSTATE_MASK_LEGACY_SSE` (bit 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | bit 0 \| bit 1 (copre la maggior parte del codice `double` / SSE semplice) |
| `XSTATE_MASK_GSSE` / AVX (bit 2) | metà superiori di YMM0–15 |
| `XSTATE_MASK_AVX512` | registri ZMM AVX-512 |

Passa la maschera combinata con OR corrispondente ai registri più ampi utilizzati dal tuo codice.

---

## Cosa fa questo driver

- Crea un oggetto dispositivo in `\Device\FloatDriver` e un collegamento simbolico in `\DosDevices\FloatDriver`
- In `DriverEntry`, chiama `ComputeAreaSafe()` (che avvolge `ComputeArea()`
  con salvataggio/ripristino dello stato FP) due volte con `radius=1.0` e `radius=5.0`
- Stampa i bit grezzi del double tramite `DbgPrint` (poiché `%f` non è
  supportato da `DbgPrint` — usiamo `RtlCopyMemory` per estrarre il pattern a 64 bit)
- Definisce implicitamente `_fltused` tramite `-fms-kernel`

## Verifica dell'emissione di `_fltused`

Confronta l'output del compilatore con e senza `-fms-kernel`:

```bash
# Modalità utente (richiederebbe libcmt):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# Kernel (definito localmente come 0):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## Caricamento (su una macchina di test Windows)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

Abilitare la firma di test o utilizzare un certificato di firma del codice per la produzione.

## Avvertenze

- **`%f` non funziona con `DbgPrint`** — la routine di stampa di debug del
  kernel non ha formattazione in virgola mobile. Converti il tuo double in
  intero a virgola fissa per la visualizzazione, o stampa i bit grezzi come
  fa questo esempio.
- **Non usare la virgola mobile a IRQL ≥ DISPATCH_LEVEL** a meno che non
  sia assolutamente necessario. `KeSaveExtendedProcessorState` documenta i
  vincoli IRQL.
- **Prestazioni**: il salvataggio/ripristino dello stato non è gratuito; per
  i percorsi critici considera di raggruppare il lavoro FP in una singola
  regione racchiusa.
