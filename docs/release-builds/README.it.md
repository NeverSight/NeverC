**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md) · [← Progetto NeverC](../../README.md)

# Binari di rilascio e `--strip`

Usa `--strip` per produrre un eseguibile o una libreria condivisa da distribuire.
L'alias breve è `-s`; le due forme hanno lo stesso comportamento.

## Avvio rapido

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app
```

NeverC esegue lo stripping nel linker integrato. Non avvia un `llvm-strip`
esterno, quindi lo stesso comando funziona per output cross-target ELF, Mach-O
e PE/COFF.

Non confondere questa opzione CLI con l'opzione di pacchettizzazione CMake
`NEVERC_STRIP_BINARY`: quest'ultima elabora dopo la build soltanto l'eseguibile
del compilatore `neverc` e può invocare uno strumento strip esterno. Non influisce
sui programmi compilati da NeverC.

## Politica di debug e simboli

| Invocazione | Informazioni di debug a livello sorgente | Nomi di simboli statici ordinari | `.dSYM` Darwin |
|-------------|------------------------------------------|---------------------------------|----------------|
| Predefinita (senza `-g`) | Non generate | Possono restare; il valore esatto dipende dal formato | Non generato |
| `-g` | Generate | Restano | Generato da un normale link Darwin |
| `--strip` | Rimosse se presenti | Rimossi i nomi non necessari a runtime | Non generato |
| `-g --strip` | Prevale la policy di strip; assenti dall'immagine consegnata | Rimossi i nomi non necessari a runtime | Soppresso |

Senza `-g`, il frontend non genera debug a livello sorgente. Ciò **non** indica
che l'output sia completamente strippato: ELF e Mach-O possono ancora contenere
nomi ordinari; PE normalmente non ha una tabella simboli COFF statica se il
debug non la richiede. Auto-LTO può scartare nomi locali, ma non garantisce
strip-all.

`-g` passa da nessun debug sorgente alla sua generazione; non aggiunge “più”
informazioni sopra un debug predefinito. Metadati di unwinding come `.eh_frame`
in ELF/Mach-O o `.pdata`/`.xdata` in PE sono dati runtime, non DWARF sorgente,
e possono restare nell'immagine strippata.

## Implementazione e comportamento dei formati

Il driver converte `--strip` in un'unica policy di linker fortemente tipizzata e
la passa ai tre backend. Ciascuno la applica conoscendo il formato e conserva
nomi e record richiesti dal loader o dall'ABI dinamica.

| Formato | Rimosso | Conservato quando necessario |
|---------|---------|------------------------------|
| ELF | Dati `.debug*` e normali tabelle statiche di simboli/stringhe | Import/export dinamici, metadati di rilocazione e loader, informazioni di unwinding |
| Mach-O | Mappe debug/STABS, voci locali/globali non necessarie a runtime e generazione del `.dSYM` associato | Dati di binding/import, nomi ABI esportati, export trie, simboli referenziati a runtime |
| PE/COFF | Sezioni DWARF incorporate e tabella statica COFF di simboli/stringhe se presente | Import/export PE, tabelle di unwinding, configurazione di caricamento e altri metadati loader |

## Ambito e precedenza

- `--strip` supporta eseguibili e librerie condivise collegati finali.
- NeverC lo rifiuta con `-c`, `-r`, `--emit-static-lib` o `-fdyncode` invece di
  produrre silenziosamente un artefatto intermedio non strippato.
- La policy di strip prevale su `-g` e sugli switch di debug backend.
- Sono coperti sia Auto-LTO predefinito sia `-fno-lto`.
- I nomi di import/export necessari all'ABI dinamica rimangono.

## Confine di sicurezza

Lo stripping rimuove nomi e metadati preziosi e aumenta il costo dell'analisi,
ma **non è** offuscamento e non impedisce il reverse engineering del codice
macchina. Un binario correttamente strippato può contenere ancora:

- nomi dinamici di import/export richiesti dal loader;
- stringhe letterali, tabelle di reflection o metadati applicativi;
- record di unwinding, rilocazione, firma e configurazione di caricamento;
- codice macchina e flusso di controllo osservabile.

`--strip` controlla soltanto l'immagine finale. Non elimina artefatti richiesti
separatamente, come mappe di link, record di ottimizzazione o output di
`-save-temps`; verifica la directory di rilascio e non distribuire questi file
accessori.

Usa cifratura delle stringhe, offuscamento e anti-manomissione come livelli
separati quando opportuno e non incorporare segreti che devono restare riservati.

## Verifica di un artefatto

Controlla gli artefatti di rilascio in CI con gli strumenti oggetto LLVM.
Adatta i comandi al formato e consenti esplicitamente i nomi ABI necessari.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM
```

Un artefatto strippato non deve avere sezioni di debug sorgente o nomi di
simboli statici privati. Nomi dinamici e metadati runtime necessari sono
previsti e non rappresentano un errore.
