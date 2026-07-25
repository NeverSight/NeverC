**Lingue**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# API MIR dei plugin NeverC

La prima ABI pubblica dei plugin espone la Machine IR tramite `PluginMIR.h`.
L'API usa identificatori C stabili e handle opachi; i plugin non dipendono dai
layout delle classi LLVM, dai numeri di enumerazione o dall'ABI C++.

## Negoziazione

Interrogare `NEVERC_INTERFACE_MIR` per `NevercMIRAPI` e
`NEVERC_INTERFACE_MIR_PASS` per `NevercMIRPassAPI`. Verificare la dimensione della
tabella restituita prima di usare un puntatore a funzione e ignorare i campi
aggiunti da un host più recente.

Il digest dello schema identifica l'esatta corrispondenza tra i valori stabili e
l'host. `GetEntityInfo`, `GetOperandKindInfo`, `GetGenericOpcodeInfo` e
`GetMachinePropertyInfo` espongono i nomi canonici e indicano se un'operazione
richiede uno schema di destinazione.

## Modello stabile

Gli handle opachi rappresentano:

- funzioni macchina e blocchi base;
- istruzioni macchina e operandi;
- transazioni di mutazione;
- risultati di analisi;
- voci del pool di costanti, oggetti di frame, tabelle di salto, operandi di
  memoria e riferimenti di destinazione.

Un handle appartiene a un solo task di generazione del codice. Le entità
cancellate, quelle annullate e i risultati di analisi invalidati da una mutazione
diventano obsoleti.

Lo schema generico copre opcode indipendenti dalla destinazione, generi di
operandi, proprietà macchina, tipi di basso livello, flag di istruzione,
assegnazioni di registri, oggetti di frame, costanti, tabelle di salto, forme di
puntatore a memoria e ordinamenti atomici. Gli opcode specifici della destinazione
richiedono uno schema di destinazione negoziato esplicitamente.

## Leggere la MIR

`NevercMIRAPI` supporta:

- proprietà delle funzioni macchina e attraversamento dei blocchi;
- enumerazione di predecessori, successori, live-in, istruzioni e operandi;
- interrogazioni su opcode e flag delle istruzioni;
- tutte le forme pubbliche di operando macchina;
- informazioni sui registri virtuali e fisici;
- stato di frame, pool di costanti, tabelle di salto e operandi di memoria.

Usare coppie conteggio/interrogazione e buffer di output limitati. Salvo diversa
indicazione, le viste restituite sono prese in prestito per la callback corrente.

## Mutazione transazionale

Le modifiche alla MIR avvengono sotto un lease di mutazione:

1. `BeginMutation` per una funzione macchina.
2. Creare, spostare o cancellare blocchi e istruzioni.
3. Aggiungere o aggiornare operandi e archi del CFG.
4. Applicare le modifiche alle proprietà macchina con la prova richiesta.
5. `CommitMutation` oppure `AbortMutation`.

Il commit esegue un controllo strutturale preliminare e la verifica della Machine
IR. Operandi, CFG, uso di opcode generici o asserzioni di proprietà non validi
vengono annullati atomicamente. L'abort ripristina l'ordine dei blocchi, le
istruzioni, gli operandi, gli archi del CFG e le proprietà macchina.

Le modifiche alle proprietà usano `NevercMIRPropertyProof`. Una prova deve
invalidare una proprietà le cui assunzioni non valgono più, oppure richiedere un
controllo strutturale prima di stabilirla.

## Pass e fasi

`NevercMIRPassDescriptor.Level` supporta gli adattatori MachineModule,
MachineFunction e MachineBasicBlock. Gli hook stabili sono:

- dopo la selezione delle istruzioni;
- dopo la legalizzazione;
- prima e dopo lo scheduler;
- prima e dopo l'allocazione dei registri;
- dopo prologo/epilogo;
- pre-emit;
- lo slot finale riservato ai plugin.

I pass di funzione possono essere eseguiti in partizioni di generazione del codice
parallele. I pass a livello di modulo vengono eseguiti in barriere di pipeline
serializzate. Le dichiarazioni di concorrenza e rientranza del plugin restano
valide.

Ogni pipeline di generazione del codice termina con un `MachineVerifier` di
proprietà dell'host, dopo lo slot finale dei plugin. È un gate sigillato e nessun
plugin può disabilitarlo.

## Analisi

La tabella delle analisi espone variabili vive, intervalli di vita, indici di
slot, albero dei dominatori, informazioni sui cicli e pressione sui registri. La
disponibilità dipende dall'hook scelto, perché alcune analisi LLVM non esistono
prima o dopo la loro fase nativa nella pipeline.

Dichiarare nel descrittore del pass le analisi richieste e quelle preservate. Una
mutazione di cui è stato eseguito il commit invalida gli handle dei risultati
interessati. Dichiarare "preserva tutto" dopo una mutazione viene rifiutato.

## Esempio minimo

`pluginsdk/examples/MachinePass.c` registra un pass di funzione macchina in sola
lettura all'hook stabile pre-emit.

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Usare il suffisso di modulo che CMake produce per la piattaforma.

## Requisiti di sicurezza

- Non conservare handle di task, handle MIR o viste prese in prestito dopo una
  callback.
- Non fabbricare valori di handle né numeri di opcode LLVM.
- Non mutare al di fuori di un lease.
- Inizializzare le intestazioni delle tabelle e lo spazio riservato.
- Restituire stati attraverso il confine C; non lasciare mai che un'eccezione C++
  lo attraversi.

Per le dichiarazioni normative e le prove di copertura si vedano `PluginMIR.h`,
`MIRSchema.json`, `PluginPhaseSchema.h` e `coverage.json`.
