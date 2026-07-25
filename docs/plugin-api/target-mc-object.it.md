**Lingue**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# Plugin per target, MC, assembly e oggetti

L'ABI dei plugin della prima release di NeverC consente a un plugin in C di
descrivere un target, sostituire percorsi di generazione del codice, osservare
l'emissione del codice macchina, analizzare o stampare assembly e leggere o
scrivere file oggetto. Il confine pubblico è un'ABI C pura: i plugin non devono
scambiare oggetti C++ di LLVM, tipi STL, eccezioni o puntatori di proprietà
dell'host la cui durata non sia dichiarata da una tabella API.

## Livelli di compatibilità

Descrittori indipendenti dal target, ID di fase, ID di artefatto, contenitori MC,
contenitori ObjectGraph, transazioni di output e contratti di callback sono ABI
STABLE della prima release. Gli schemi specifici del target — opcode, registri,
operandi, fixup, rilocazioni e convenzioni di chiamata — sono LOCKSTEP. Un plugin
deve confrontare ID e digest dello schema del target prima di consumare valori
LOCKSTEP. NeverC rifiuta gli schemi discordanti prima di invocare il provider.

## Registrare un target e un percorso di generazione del codice

Interrogare `NevercTargetAPI` durante la registrazione, registrare uno o più
record `NevercTargetDescriptor` e collegare descrittori di target machine e archi
di generazione del codice. Un percorso è selezionato dalla chiave canonica del
target: ID del target, triple, CPU, funzionalità, ABI, modello di rilocazione,
modello di codice, formato oggetto e digest dello schema.

I percorsi a grana fine usano `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`. Un
arco a grana grossa può sostituire l'intero percorso `IR -> ObjectImage`. Anche
l'output a grana grossa attraversa il verificatore di prodotto obbligatorio
dell'host e il commit transazionale dell'output; un provider non può aggirare
nessuno dei due gate.

## Costruire e osservare l'MC

`NevercMCAPI` possiede le mutazioni di `MCUnit` locali al task. Avviare una
mutazione, creare sezioni, frammenti, simboli, espressioni, istruzioni e operandi,
quindi eseguirne il commit o abbandonarla. Gli handle sono limitati al task e
controllati per generazione.

Il flusso di emissione indipendente dal target espone eventi ordinati per cambi di
sezione, etichette, istruzioni, allineamento, attributi dei simboli, CFI,
posizioni di debug e dati. `neverc.mc.emission.pre_instruction` è sostituibile; le
restanti fasi di evento sono punti di osservazione in sola lettura. Si veda
`pluginsdk/examples/MCObserverPlugin.c`.

I provider di codifica, decodifica e layout operano sulla stessa chiave di target
e sullo stesso digest di schema. Il layout gestisce la relaxation ed emette un
digest di prova. Qualsiasi mutazione successiva al layout invalida quella prova e
impone un nuovo layout prima della scrittura dell'oggetto.

## Sostituire la sintassi assembly

Un provider di parser assembly consuma byte sorgente e pubblica un `MCUnit`. Un
printer assembly consuma un `MCUnit` e scrive esclusivamente attraverso la
transazione di output fornita. L'assembly preprocessato (`.S`) attraversa il
normale preprocessore del frontend prima del provider di parsing; l'assembly puro
(`.s`) entra direttamente nel parser.

I provider mettono prima l'output in stage. La verifica di parsing/stampa e il
gate di commit dell'host vengono eseguiti prima che i byte diventino visibili,
così un fallimento non lascia output parziale.

## Leggere, riscrivere e scrivere oggetti

`NevercObjectAPI` rappresenta un file rilocabile come ObjectGraph normalizzato:
sezioni, simboli, rilocazioni, gruppi/COMDAT, import/export, metadati TLS, record
di unwind e record di debug. Gli adattatori integrati coprono ELF, COFF e Mach-O,
e i plugin possono registrare formati aggiuntivi.

La pipeline degli oggetti è:

1. sondare e leggere i byte in un ObjectGraph;
2. eseguire gli intercettori di grafo `object.pre_write`;
3. eseguire il layout e `object.post_layout` (rifare il layout dopo una
   mutazione);
4. scrivere un'immagine candidata limitata;
5. eseguire gli intercettori binari `object.post_write`;
6. eseguire il verificatore finale sigillato e il commit atomico dell'host.

Gli osservatori ricevono ponti in sola lettura. Le mutazioni tentate da un
osservatore vengono respinte con `NEVERC_STATUS_POLICY_VIOLATION`. Gli scrittori e
gli intercettori post-scrittura possono accedere solo al costruttore transazionale
limitato; overflow, fallimento della callback o fallimento della verifica
interrompono lo staging. Si veda
`pluginsdk/examples/ObjectRewritePlugin.c`.

## Regole di concorrenza e di errore

- Mantenere lo stato mutabile nello stato process/session/task fornito dall'host.
- Non memorizzare in cache handle di task o viste prese in prestito dopo il
  ritorno della callback.
- Invocare la continuazione di un intercettore al massimo una volta e sul thread
  della callback.
- Restituire il `NevercStatus` originale; non pubblicare prodotti parziali.
- Dichiarare le modalità di concorrenza e rientranza più restrittive che siano
  veritiere.

Il contratto di copertura eseguibile è `docs/plugin-api/coverage.json`. Mappa ogni
fase stabile su test positivi, negativi, di sostituzione, di osservatore in sola
lettura e di gate sigillato.
