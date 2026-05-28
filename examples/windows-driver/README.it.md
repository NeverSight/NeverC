# Esempio di driver kernel Windows

Un driver kernel WDM minimale costruito con NeverC. Compilazione incrociata da macOS / Linux.

NeverC è un compilatore all-in-one — una singola invocazione gestisce preprocessing,
compilazione, ottimizzazione (auto-LTO) e linking tramite il linker integrato.

## Compilazione

Dal repository:

```bash
cd examples/windows-driver
make
```

Da una versione standalone di NeverC:

```bash
make NEVERC=/path/to/neverc
```

L'output è `ExampleDriver.sys` (ottimizzato auto-LTO).
La compilazione predefinita include `-g` per il debug; **le versioni di
rilascio dovrebbero rimuovere `-g`** per eliminare i simboli di debug e ridurre
la dimensione del binario (~38 KB → ~3 KB).

## Compilazione manuale (senza Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g` incorpora le informazioni di debug DWARF nel PE; ispezionare con
> `llvm-dwarfdump` o caricare i simboli in WinDbg. Omettere questa opzione
> nelle versioni di rilascio per ridurre la dimensione del binario.

## Funzionalità

- Crea un oggetto dispositivo in `\Device\ExampleDriver`
- Crea un collegamento simbolico in `\DosDevices\ExampleDriver`
- Gestisce `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Stampa messaggi di caricamento/scaricamento tramite `DbgPrint`

## Caricamento (su una macchina di test Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Abilitare la firma di test o utilizzare un certificato di firma del codice per la produzione.
