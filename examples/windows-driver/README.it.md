**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio di driver kernel Windows

Un driver kernel WDM minimale costruito con NeverC. Punta a **x64** per
impostazione predefinita e può essere compilato anche per ARM64. Compilazione
incrociata da macOS / Linux.

NeverC è un compilatore all-in-one — una singola invocazione gestisce preprocessing,
compilazione, ottimizzazione (auto-LTO) e linking tramite il linker integrato.

## Compilazione

Dal repository:

```bash
cd examples/windows-driver
neverc make
```

Questo produce `ExampleDriver-x64.sys`. Per compilare per ARM64, o per entrambe:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Da una versione standalone di NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

L'output è `ExampleDriver-<arch>.sys` (ottimizzato auto-LTO).
La compilazione predefinita include `-g` per il debug; **le versioni di
rilascio dovrebbero rimuovere `-g`** per eliminare i simboli di debug e ridurre
la dimensione del binario (~38 KB → ~3 KB).

## Compilazione manuale (senza Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Per ARM64 basta sostituire il target con `aarch64-pc-windows-msvc`; il resto
resta invariato. `-fms-kernel` seleziona gli header e le librerie di importazione
del WDK corrispondenti al target e definisce le macro di architettura che il WDK
si aspetta, quindi non vanno mai passate a mano.

> `-g` incorpora le informazioni di debug DWARF nel PE; ispezionare con
> `llvm-dwarfdump`. Omettere questa opzione nelle versioni di rilascio per
> ridurre la dimensione del binario.

## Funzionalità

- Crea un oggetto dispositivo in `\Device\ExampleDriver`
- Crea un collegamento simbolico in `\DosDevices\ExampleDriver`
- Gestisce `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Stampa messaggi di caricamento/scaricamento tramite `DbgPrint`

## Caricamento (su una macchina di test Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Abilitare la firma di test o utilizzare un certificato di firma del codice per la produzione.
