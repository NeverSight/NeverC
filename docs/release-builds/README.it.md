**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md) · [← Progetto NeverC](../../README.md)

# Binari di rilascio e `--strip`

Usa `--strip` per produrre un eseguibile, una libreria condivisa o un modulo
kernel Android finale da distribuire. La forma breve è `-s`; le due forme hanno
lo stesso comportamento.

## Avvio rapido

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
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
| Kernel Android `.ko` (ELF ET_REL) | `.debug*`, `.comment`, voci locali/non definite inutili alle rilocazioni e nomi leggibili delle normali definizioni conservate | Un `.symtab` collegato a `.strtab`, tutte le rilocazioni e i target, nomi esatti di loader/CFI, import esatti, nomi nelle sezioni protette e metadati ABI del modulo |
| Mach-O | Mappe debug/STABS, voci locali/globali non necessarie a runtime e generazione del `.dSYM` associato | Dati di binding/import, nomi ABI esportati, export trie, simboli referenziati a runtime |
| PE/COFF | Sezioni DWARF incorporate e tabella statica COFF di simboli/stringhe se presente | Import/export PE, tabelle di unwinding, configurazione di caricamento e altri metadati loader |

## Ambito e precedenza

- `--strip` supporta eseguibili, librerie condivise e la stretta eccezione del
  `.ko` Android finale descritta sotto.
- NeverC lo rifiuta con `-c`, un normale `-r`, un `.o` Android intermedio,
  `--emit-static-lib` o `-fdyncode`.
- La policy di strip prevale su `-g` e sugli switch di debug backend.
- Sono coperti sia Auto-LTO predefinito sia `-fno-lto`.
- I nomi di import/export necessari all'ABI dinamica rimangono.

## Moduli kernel Android

Un `.ko` finale resta ELF `ET_REL`. Il loader dei moduli Linux richiede tabella
simboli, relativa tabella stringhe, import non definiti e rilocazioni, quindi
rifiuta strip-all. NeverC accetta `-r --strip` solo per un target Android con
`-fandroid-kernel-driver-mode`, `-r` e un nome di output che termina in `.ko`.
Il normale `-r` e gli `.o` intermedi restano rifiutati.

`neverc make release` resta il comando consigliato e si espande in
`-O2 --strip`. Senza `.nvk-build-flags`, `make` usa debug per impostazione
predefinita e non sceglie release da solo. I Makefile di esempio salvano un
profilo scelto esplicitamente affinché i successivi `make push`, `make run` e
`make` senza target usino lo stesso artefatto. `make debug` o un
`PROFILE=...` esplicito sostituisce la scelta; `make clean` cancella lo stato e
riporta la build successiva a debug. In questo percorso finale NeverC rimuove
sezioni di debug, `.comment` e voci locali/non definite inutili alle
rilocazioni, quindi ricostruisce `.strtab`.

Dopo una release riuscita, NeverC crea atomicamente
`<module>.ko.symbols.json` accanto al modulo. Il file registra i nomi
`original` e `release` per ogni simbolo conservato il cui nome è cambiato e
lega la mappa ai byte del modulo finale tramite `image_sha256`:

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 1,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

Le voci sono ordinate per `release`. I simboli rimossi e i nomi esatti di
loader, import o CFI sono omessi perché non richiedono traduzione. Se una build
debug o un'altra build senza strip sovrascrive lo stesso percorso di output,
NeverC elimina la mappa obsoleta. La mappa contiene nomi originali leggibili:
archiviala come artefatto di debug privato; non distribuirla con il `.ko` e non
inviarla al dispositivo. Prima di tradurre un nome release da un rapporto di
crash, verifica che la mappa appartenga al `.ko` corrente:

```bash
test "$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" = "$(jq -r '.image_sha256' nvk_hello.ko.symbols.json)"

jq -r '.symbols[] | select(.release == "fn_C000") | .original' \
  nvk_hello.ko.symbols.json
```

Le definizioni conservate idonee ricevono nomi strutturali deterministici
ispirati a IDA, senza usare i suoi prefissi riservati:

- `STT_FUNC` diventa `fn_HEX`;
- `STT_OBJECT` diventa `obj_HEX`;
- `STT_NOTYPE` eseguibile diventa `code_HEX`;
- altro `STT_NOTYPE` allocato diventa `sym_HEX`;
- `SHN_ABS` diventa `abs_HEX`;
- una definizione fuori da `SHF_ALLOC` diventa
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`.

Ogni campo `HEX`, compresi entrambi i campi della forma non allocata, usa cifre
esadecimali maiuscole senza zeri iniziali superflui. Se più simboli richiedono
la stessa grafia, vengono aggiunte varianti decimali deterministiche `_1`,
`_2` e così via.

Queste grafie si ispirano a IDA senza occupare il suo spazio dei nomi fittizi.
In un nuovo database IDA 9.4, i simboli utente ELF `sub_0`, `sub_4` e `loc_8`
appaiono come `_sub_0`, `_sub_4` e `_loc_8`, mentre `fn_0`, `code_8` e `obj_10`
restano invariati. La documentazione Hex-Rays di
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) conferma
che un nome utente che inizia con un prefisso fittizio come `sub_` riceve un
trattino basso iniziale. NeverC non svuota intenzionalmente lo `st_name` di una
definizione ordinaria per far sintetizzare `sub_` a IDA: kallsyms dei moduli
Android/Linux ha storicamente ignorato le voci senza nome, e un nome vuoto
eliminerebbe il contratto serializzato verificabile. Le voci che devono già
essere vuote e i simboli di sezione restano esatti.

ELF consente a più simboli di condividere la stessa canonical analysis EA.
NeverC conserva o genera in `.symtab` l'insieme completo degli alias; tuttavia,
il modello di nomi per indirizzo di IDA 9.4 può materializzare un solo nome
principale tra i simboli allo stesso indirizzo. Un alias non mostrato da IDA
non è quindi necessariamente scomparso dall'ELF; l'insieme completo va
verificato con `llvm-readelf` o `llvm-nm`.

Per un simbolo allocato, `HEX` è la canonical analysis EA di NeverC, cioè
l'indirizzo effettivo canonico usato solo per l'analisi statica. Partendo da un
cursore zero, NeverC visita le sezioni `SHF_ALLOC` finali conservate nell'ordine
finale della tabella sezioni, allinea il cursore a `max(sh_addralign, 1)`,
registra la base e avanza di `max(sh_size, 1)`; la EA è tale base più lo
`st_value` finale. `abs_HEX` usa lo `st_value` assoluto finale. Nella forma non
allocata, `FINAL_SECTION_ORDINAL_HEX` è l'ordinale finale di sezione e
`OFFSET_HEX` è lo `st_value` finale al suo interno. Queste coordinate non sono
un'impronta crittografica, cifratura, offset di file, indirizzo virtuale ELF o
indirizzo runtime del kernel. Loader e KASLR possono collocare il modulo altrove
durante l'esecuzione.

Restano esatti:

- ogni import `SHN_UNDEF`, perché il loader lo risolve per nome;
- i simboli definiti in `.modinfo`, `.text.ftrace_trampoline`,
  `.gnu.linkonce.this_module`, `__versions` o `.codetag.alloc_tags`;
- `init_module`, `cleanup_module`, `__cfi_check`, `__cfi_check_fail`,
  `__cfi_jt_init_module` e `__cfi_jt_cleanup_module`;
- i nomi che iniziano con `__typeid__` o `__kcfi_typeid_`.

L'area `extern` mostrata da IDA è una vista di analisi sintetica, non una sezione
ELF reale. In un `.ko` `ET_REL` finale, i target di rilocazione esterni sono voci
`SHN_UNDEF` di `.symtab`, i cui nomi esatti servono al loader. La policy segue
quindi la classe ELF effettiva del simbolo e la sua sezione di definizione: gli
import non definiti restano esatti, mentre le definizioni idonee vengono
rinominate indipendentemente da come lo strumento di analisi le raggruppa.

Tutti i nomi vengono pianificati globalmente prima della modifica. Le definizioni
con lo stesso candidato base ricevono, in ordine deterministico, il nome senza
suffisso, poi `_1`, `_2` e così via; questo caso normale non è un
errore. La finalizzazione si interrompe se un nome generato collide con lo
spazio riservato ai nomi da conservare invariati oppure se il calcolo delle
coordinate o della numerazione supera l'intervallo numerico. Inoltre rifiuta il
risultato per sicurezza, senza tentare di indovinare, davanti a
`SHN_COMMON`, `SHN_LIVEPATCH` o un indice di sezione ELF riservato sconosciuto.
`SHN_COMMON` non è valido in un modulo finale caricabile: compila con
`-fno-common`. I moduli livepatch richiedono ordine e indici originali della
tabella simboli e metadati di rilocazione aggiuntivi, che questa policy non
pretende di conservare.

Il rilevamento usa segnali ridondanti: qualunque simbolo `SHN_LIVEPATCH`, sezione
`.klp.*`, flag `SHF_RELA_LIVEPATCH` o campo `.modinfo` separato da NUL e
iniziante con `livepatch=` identifica un modulo livepatch e ne provoca il rifiuto
per sicurezza. Il solo marcatore `.modinfo` è sufficiente anche senza sezioni
`.klp.*` o flag di rilocazione livepatch.

Vengono sostituiti solo i nomi idonei in `.symtab`. Un `.ko` caricabile
richiede ancora `.symtab`, la `.strtab` collegata e le rilocazioni, quindi gli
strumenti generici possono legittimamente indicarlo come `not stripped`.
Archivi e interfacce indipendenti come BTF, export del modulo, `.modinfo`,
`__versions`, metadati di tracing, `__ksymtab_strings`, `.rodata` e stringhe
letterali possono ancora rivelare nomi originali o testo identificativo. I
normali nomi kernel cambiano anche in kallsyms e nella diagnostica, riducendo
l'utilità di ftrace per simbolo, attach kprobe/BPF e rapporti di crash. Per
diagnosticare usa una build debug non strippata e non dipendere dal nome
originale di un simbolo privato nel modulo release.

### Confine plugin di una release Android finalizzata

La finalizzazione stabilisce due confini d'identità indipendenti e fail-closed
attorno alle fasi di output dei plugin:

- Prima di qualsiasi fase `ObjectGraph` sostituibile, il sigillo del grafo lega
  `section ID`, `final ordinal` e nome esatto di ogni sezione logica conservata.
  Lega inoltre il `symbol ID` di ogni simbolo a nome esatto al suo nome, classe,
  sezione, valore, dimensione, binding, tipo e `st_other` completo. Il verifier
  release ricalcola separatamente i normali nomi strutturali.
- Dopo che l'host ha stabilito una baseline di scrittura attendibile e prima di
  `neverc.object.post_write`, il sigillo dell'immagine lega ordinal e nome di
  ogni sezione logica conservata, il numero totale di entry `.symtab`, e nome e
  attributi di ogni simbolo a nome esatto al suo `slot` `.symtab` grezzo.

La matrice delle capacità è quindi volutamente ristretta:

| Binding di fase | Comportamento della release Android finalizzata |
|-----------------|------------------------------------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED` prima che possa sostituire la baseline di scrittura attendibile stabilita dall'host |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`; una release Android finalizzata richiede il graph writer dell'host che stabilisce la baseline attendibile |
| `observer` | `READ_ONLY`; l'osservazione resta consentita, la mutazione dell'artefatto no |
| `neverc.object.post_write` `interceptor` | `VALIDATED`; può modificare solo byte di payload fuori dalla superficie d'identità e il risultato deve continuare a superare verifier release, contratto ABI d'ingresso ed entrambi i sigilli d'identità |

Anche la proprietà del merge finalizzato è sigillata dall'host. Qualsiasi
`MergedImage` o byte indipendente di un `third-party ObjectMergeProvider` viene
scartato; il `host-owned graph writer` serializza il grafo verificato e
finalizzato di quel provider. Viceversa, `built-in finalized input serialization`
aggira le `external object phases` e passa al merger dell'host gli esatti
`audited native bytes`; questo passo di input interno non aggira il confine di
output precedente.

La finalizzazione è accettata solo con `Android module merge semantics`; richiede
anche sia una `relocatable output request` sia una
`relocatable driver configuration`, altrimenti fallisce `before routing`. Per
una release Android relocatable finalizzata, `frozen input format`,
`TargetKey.ObjectFormatID` e `frozen output format` devono condividere
`one format identity`. Una discrepanza viene rifiutata
`before provider dispatch`, quindi anche prima del route planning o della
creazione del sink; capability preflight e graph-writer dispatch effettivo non
possono così osservare formati diversi.

Per un input ordinario rappresentabile dal grafo, i precedenti graph interceptor
possono operare solo preservando il sigillo e tutta la semantica release. Se
l'input richiede native-image passthrough per fatti non rappresentabili
dall'`ObjectGraph`, ogni `route-matching provider` sostituibile e ogni
interceptor vengono rifiutati. Un provider la cui route
target/CPU/features/object-format/execution-level non corrisponde non viene
eseguito e non blocca la release; sono ammessi solo observer in sola lettura.
Solo un rifiuto o una validazione
fallita `before sealed commit` interrompe lo staging senza pubblicare file. Un
errore di observer `AFTER_COMMIT` viene segnalato dopo la pubblicazione e non può
ripristinare il file già pubblicato.

Non post-processare un `.ko` con `llvm-strip --strip-all` o `objcopy` e non
rimuovere alla cieca sezioni codetag/BTF/ABI. Se il modulo va firmato, esegui
prima lo strip e firma i byte finali: ogni modifica successiva invalida la firma.
`clean` deve solo cancellare file, mai strippare o firmare un modulo esistente.

## Confine di sicurezza

Lo stripping rimuove nomi e metadati preziosi e aumenta il costo dell'analisi,
ma non impedisce il reverse engineering del codice macchina. Un binario
correttamente strippato può contenere ancora:

- nomi dinamici di import/export richiesti dal loader;
- nomi richiesti dal loader e nomi memorizzati fuori da `.symtab` in un `.ko`;
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
Il controllo `strings` negato qui sotto non deve trovare corrispondenze e ha
successo solo in quel caso.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

Per un `.ko` ELF `ET_REL` caricabile, l'utility generica `file` può ancora
mostrare `not stripped` perché `.symtab` viene conservata intenzionalmente. Non
usare tale etichetta per decidere l'esito della release. Verifica invece che
DWARF e `.comment` siano assenti, che le definizioni idonee usino le forme
esadecimali maiuscole canoniche `fn_`/`obj_`/`code_`/`sym_`/`abs_`, che
gli import `SHN_UNDEF` e i nomi necessari a loader/CFI restino esatti e che le
rilocazioni siano valide. Controlla separatamente BTF, export, modinfo, versions,
metadati di tracing e stringhe se conta la divulgazione dei nomi.

Un artefatto strippato non deve avere sezioni di debug sorgente o nomi di
simboli statici privati. Nomi dinamici e metadati runtime necessari sono
previsti e non rappresentano un errore.
