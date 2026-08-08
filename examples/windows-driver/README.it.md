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
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
```

Il Makefile memorizza `ARCH`, `PROFILE` e `TESTSIGN`. Per il rilascio usare
`neverc make release` (`-O2 --strip`; restano import/export PE e metadati
del loader). Con firma di test: `neverc make release TESTSIGN=1`
(strip e poi firma nello stesso link).
Vedi [Build di rilascio](../../docs/release-builds/README.it.md).

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

## Compilazione manuale (senza Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --driver \
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
`--driver` contrassegna l'immagine come kernel-mode: codice e dati diventano non
paginabili, le tabelle di import si spostano nella sezione INIT scartabile e il
linker scrive il checksum PE che il caricatore del kernel verifica.

> `-g` incorpora le informazioni di debug DWARF nel PE; ispezionare con
> `llvm-dwarfdump`. Omettere questa opzione nelle versioni di rilascio per
> ridurre la dimensione del binario.

## Firma di test

Windows rifiuta di caricare un driver kernel non firmato. `-ftest-sign` allega
una firma Authenticode in modo che l'immagine superi quel controllo su una
macchina di test:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

oppure aggiungere `-ftest-sign` a un'invocazione manuale. L'opzione è accettata
solo insieme a `-fms-kernel`, poiché una firma di test non ha alcun significato
per un binario in modalità utente.

L'identità di firma è integrata nel compilatore: un certificato autofirmato la
cui chiave privata è pubblica per costruzione. Non fornisce alcuna autenticità;
soddisfa soltanto il controllo di integrità del codice su una macchina che avete
deliberatamente aperto. Configurate quella macchina una volta sola, come
amministratore:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

quindi riavviate. Esportate il certificato dal compilatore stesso, così
corrisponde sempre all'identità con cui firma:

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

(Una copia si trova anche in `utils/neverc-test-signing.cer` nell'albero dei
sorgenti, ma non fa parte di un pacchetto di release.)

Senza una macchina Windows, verificate la firma con `osslsigncode`. Notate che
`-CAfile` vuole PEM mentre il certificato è DER: convertitelo prima. Passare il
DER direttamente fallisce con un fuorviante «signature verification failed» la
cui causa reale è «no certificate found»:

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**Non usatelo mai per nulla che esca da una macchina di test.** In produzione,
firmate con un vero certificato di firma del codice (e, per Windows 10 1607 e
successivi, una firma di attestazione del Microsoft Hardware Dev Center).

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
