**Lingue**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

# Plugin DynCode

`-fdyncode` compila un'unità di traduzione in un'immagine piatta e indipendente
dalla posizione (`.bin`), il cui codice non ha rilocazioni né sezione dati.
Prende di mira arm64/x86_64 su macOS, Linux, Android e Windows, a livello di
esecuzione utente o kernel. I plugin osservano, intercettano o sostituiscono le
fasi tipizzate che trasformano il C in quell'immagine attraverso la stessa ABI C
pura usata dagli altri domini: niente oggetti C++ di LLVM, niente tipi STL,
niente eccezioni e nessun puntatore dell'host la cui durata non sia dichiarata da
una tabella dell'API.

## Interfacce

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| Interfaccia | Tabella | Slot | Scopo |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | Leggere richiesta, immagine, report e le mappe di sezioni/simboli/rilocazioni/esterni |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`, `RegisterImportProvider`, `RegisterExtractor`, `RegisterCharsetEncoder`, `RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`, `GetRequest`, `GetImage`, `GetReport` |

Tutte e tre sono `NEVERC_INTERFACE_STABLE` alla major 1. Dall'interno di una
callback di fase, `NevercDynCodePhaseAPI` è il punto d'ingresso: trasforma il
frame negli handle che l'altra tabella consuma:

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

Le quattro famiglie di mappe — mappe di sezione, mappe di simboli, rilocazioni e
riferimenti esterni — si percorrono tutte con la stessa terna first/next/info,
per esempio `GetFirstRelocation`, `GetNextRelocation`, `GetRelocationInfo`. È
così che un plugin legge le decisioni dell'estrazione senza analizzare il JSON
del report.

## DynCode è un prodotto di compilazione, non un post-passo di `main()`

`-fdyncode` è una normale Action/Job nel DAG del driver. Il job di compilazione
pubblica un `ObjectGraph` verificato in memoria; un job `-dyncode-extract`
consuma quel grafo e scrive l'immagine `-o` dell'utente. `-###`, la stampa delle
fasi e il grafo dei job mostrano tutti il job di estrazione, così un plugin non
deve mai ricostruire un argv riscritto per scoprire la modalità. La richiesta
congelata è condivisa in modo locale al task con la generazione di codice
in-process; non esiste `getCurrentDynCodeOptions()`, né un flag di modalità
globale al processo, né un giro attraverso un oggetto temporaneo.

Esattamente un'unità di traduzione viene abbassata a un'immagine. Ingressi
multipli, `-c/-S/-E` e triple non supportate vengono rifiutati subito con
diagnostiche stabili.

## Livelli di compatibilità

Gli ID di fase, gli ID di artefatto, i contenitori di richiesta/report/immagine e
i contratti delle callback sono ABI STABLE della prima release. I tipi di
rilocazione specifici del target e gli schemi di sezioni/simboli dei formati
oggetto sono LOCKSTEP: confrontate l'ID di schema del target e il digest prima di
consumarli. NeverC rifiuta uno schema non corrispondente prima di invocare un
provider.

## La richiesta congelata

All'inizio del job il driver normalizza la riga di comando in un
`DynCodeRequest` immutabile e lo congela. I task figli prendono in prestito lo
snapshot; non lo mutano mai. La richiesta porta la chiave di target e il formato
oggetto, il livello di esecuzione (user/kernel), la policy di entry (simbolo
esplicito, elenco di candidati predefinito, requisito entry-a-zero), la policy
PIC/sezioni, la policy dei riferimenti esterni, l'insieme o profilo di byte
vietati e il flag di riscrittura, l'ID del provider di charset, e la lunghezza
massima, l'allineamento e il byte di riempimento.

## Il grafo tipizzato delle fasi

DynCode è un grafo fisso di 34 fasi. Trenta transizioni ordinarie sono
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`; quattro sono
`OBSERVABLE | SEALED_HOST_GATE`. I cancelli sigillati sono la verifica finale
dell'IR, la verifica finale del MIR, la verifica dell'immagine e il commit. Un
plugin può osservare qualsiasi fase, avvolgere una transizione sostituibile con
un interceptor o sostituirne del tutto il provider; non può mai sostituire,
saltare o aggirare un cancello sigillato, e non può esprimere una trasformazione
disabilitata come una callback saltata: una trasformazione disabilitata esegue un
provider no-op esplicito, il cui output equivalente il verificatore dell'host
dimostra comunque.

Le fasi, in ordine, sono:

1. congelamento della richiesta;
2. le trasformazioni IR — prepare, abbassamento dei salti indiretti,
   abbassamento degli intrinseci di memoria (pre e post-heap), abbassamento del
   runtime delle stringhe, arena di heap, tre posizioni `compiler_rt`
   (pre/post/final), abbassamento degli import di syscall/PEB/kernel, due
   posizioni `data_to_text` (pre/post), ottimizzazione di inlining, finalize
   delle stringhe, stackify, all-`blr`, e la verifica finale sigillata dell'IR;
3. la trasformazione di prepare del MIR e la verifica finale sigillata del MIR;
4. import dell'oggetto — legare l'`ObjectGraph` verificato al task;
5. estrazione — piano, layout, rilocazione e costruzione dell'immagine
   candidata;
6. le fasi binarie limitate — post-extract, riscrittura dei byte vietati,
   codifica del charset, dimensione/allineamento/riempimento e pre-verify;
7. la verifica sigillata dell'immagine;
8. il commit sigillato.

La fonte normativa di ID, policy, livelli di stabilità e cancelli è
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`; il contratto di copertura
eseguibile è `docs/plugin-api/coverage.json`.

## Anche le trasformazioni integrate sono provider

Ogni pass IR/MIR integrato è avvolto come provider tipizzato; l'oggetto pass di
LLVM non viene mai esposto attraverso l'ABI C. Sostituire una fase significa che
il provider integrato non gira: il test che passa dimostra il comportamento o la
traccia, non solo che una registrazione è riuscita. Le fasi `mem_intrin`,
`compiler_rt` e `data_to_text` compaiono in più di una posizione; ogni posizione
è un ID di fase distinto con la propria dimostrazione, così una riesecuzione è
idempotente e non si appoggia mai a stato nascosto del pass.

## ObjectGraph è l'unico input oggetto ordinario

L'estrazione consuma esattamente un `ObjectGraph` verificato prodotto dalla rotta
di generazione codice del target. `dyncode.object.import` lega quel grafo e
controlla chiave di target e provenienza; non rilegge mai byte dal disco né
esegue un secondo parsing dell'oggetto. Un formato oggetto personalizzato entra
in DynCode non appena può essere letto come `ObjectGraph` e ha provider di
rilocazione e di target corrispondenti. Oggetti multipli e insiemi di grafi LTO
vengono rifiutati al congelamento con un `CAPABILITY_UNAVAILABLE` stabile.

## Riferimenti esterni e abbassamento degli import

L'insieme degli esterni consentiti nella richiesta significa soltanto «un
provider può gestire questo»; non permette mai a una rilocazione irrisolta di
sopravvivere nell'immagine piatta. Ogni riferimento esterno deve finire come uno
di questi: eliminato in IR/MIR, risolto a un simbolo interno all'immagine,
convertito in un contratto di resolver a runtime dichiarato e accettato dal
verificatore, oppure errore netto. Stub di syscall, import di PEB e import di
kernel sono i tre `ImportProvider` integrati; ciascuno dichiara il proprio
matcher di target/livello/simbolo e il contratto ABI che produce. Un plugin può
aggiungere un `ImportProvider`, ma deve restituire la provenienza sostitutiva, il
cambiamento dell'ABI di entry, i parametri del resolver e i riferimenti residui.

## Immagine, report e modifiche di byte limitate

L'estrazione produce un `DynCodeImage` e un `DynCodeReport`. L'immagine è un
builder di byte limitato più l'offset/simbolo di entry, le mappe di output delle
sezioni e dei simboli sorgente, le disposizioni delle rilocazioni e i record dei
contratti esterni/runtime. Ogni modifica di byte passa per l'API controllata
read/write/insert/append/resize del builder; non esiste alcun `uint8_t **`. Una
modifica aggiorna la generazione dell'immagine e invalida ogni dimostrazione di
rilocazione/PIC/entry che si sovrappone all'intervallo cambiato.

Il report è un prodotto di audit immutabile e deterministico: digest di
richiesta/rotta/input/output, il giornale dei provider fase per fase, le sezioni
selezionate e rifiutate con il motivo, la scelta dell'entry, le rilocazioni
patchate/rifiutate/con contratto runtime, gli esterni rimanenti,
dimensione/allineamento/riempimento, la scansione dei byte vietati e la checklist
del verificatore. `-fdyncode-report=<path>` scrive il suo JSON canonico; le
diagnostiche verbose vengono rese dallo stesso report anziché da un secondo
insieme di conteggi.

La catena di riscrittura dei byte vietati gira in un ordine topologico congelato e
ogni passo restituisce un record di modifica. Il codificatore di charset viene
scelto per ID stabile esatto e restituisce uno stub decodificatore, il payload
codificato, un aggiornamento dell'entry e una dimostrazione di target; un ID
sconosciuto o ambiguo è un errore netto. Disabilitare la riscrittura seleziona un
passo no-op esplicito: l'audit finale gira comunque.

## Verificatore finale e tempistica post-finalizzazione

Tutte le fasi scrivibili terminano prima del verificatore finale sigillato. Il
verificatore controlla che non resti alcuna rilocazione o riferimento esterno non
gestito, che non sia presente alcuna sezione vietata di dati/TLS/unwind/debug/
metadati, che l'entry esista, sia allineato correttamente e (quando richiesto)
stia all'offset zero, che ogni sito di rilocazione ricada nell'intervallo con una
dimostrazione PIC coerente con i byte attuali dell'immagine, che le mappe di
sezioni e simboli non si sovrappongano, che valgano le regole di
lunghezza/allineamento/riempimento, e che i byte finali — decodificatore,
intestazione e riempimento inclusi — non contengano alcun byte vietato. Qualsiasi
fallimento restituisce una diagnostica strutturata e scarta l'intero bundle di
output.

Dopo l'audit non c'è alcun hook scrivibile. Se una trasformazione di byte tocca
un intervallo eseguibile, la rotta congelata deve fornire una capacità di
verifica binaria corrispondente, che l'host invoca per riemettere la
dimostrazione PIC sull'immagine finale e immutabile.

## Opzioni del driver

`-fdyncode` abilita la modalità. `-fdyncode-entry=` sceglie il simbolo di entry.
`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` impostano i byte vietati,
`-fdyncode-bad-byte-rewrite` (attivo per impostazione predefinita) seleziona la
catena di riscrittura, e `-fdyncode-charset=` seleziona un codificatore
registrato. `-fdyncode-max-length=`, `-fdyncode-align=` e `-fdyncode-pad=`
limitano la dimensione finale. `-fdyncode-keep-obj=` deriva l'oggetto rilocabile
intermedio e `-fdyncode-report=` scrive il report di audit.
`-mdyncode-context=user|kernel` seleziona il livello di esecuzione.

## Regole di concorrenza e di fallimento

- Tenete lo stato mutabile negli scope di processo/sessione/task forniti
  dall'host; non usate mai un singleton di plugin corrente o di opzioni correnti.
- Non mettete in cache handle di task o viste in prestito dopo il ritorno di una
  callback.
- Invocate la continuation di un interceptor al massimo una volta, sul thread
  della callback.
- Restituite il `NevercStatus` originale; un `REPLACE` dichiarato che fallisce
  non ripiega in silenzio sul provider integrato.
- Dichiarate i modelli di concorrenza e rientranza più stretti che siano veri.

Vedete `pluginsdk/examples/DynCodeTracePlugin.c` per un tracciatore di fasi in
sola lettura e `pluginsdk/examples/DynCodeEncoderPlugin.c` per un codificatore di
charset.
