**Lingue**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# API IR dei plugin NeverC

La prima ABI pubblica dei plugin espone l'IR di LLVM attraverso tabelle C
stabili. I plugin non includono header LLVM e non devono convertire handle di
NeverC in oggetti LLVM.

## Interfacce

Interrogare le interfacce da `neverc_plugin_entry` con
`NevercBootstrapAPI.QueryInterface`:

- `NEVERC_INTERFACE_IR_CORE` — interrogazioni su moduli, tipi, valori, CFG,
  metadati, attributi, costanti e serializzazione.
- `NEVERC_INTERFACE_IR_BUILDER` — costruzione e mutazione transazionali dell'IR.
- `NEVERC_INTERFACE_IR_ANALYSIS` — analisi integrate e definite dai plugin.
- `NEVERC_INTERFACE_IR_PASS` — pass Module, CGSCC, Function e Loop.
- `NEVERC_INTERFACE_IR_GEN` — sostituzione dell'abbassamento da SemanticUnit a
  IR.
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — sostituzione completa della pipeline di
  ottimizzazione.

Richiedere sempre la coppia major/minor dell'header e verificare che lo
`StructSize` restituito arrivi fino all'ultimo puntatore a funzione usato dal
plugin. Un host più recente può aggiungere campi; il plugin deve ignorare le code
sconosciute.

## Handle e proprietà

Gli handle IR sono coppie opache `{Owner, Value}` limitate a un task. Tutti gli
oggetti a cui fanno riferimento appartengono all'host.

- Non conservare mai un handle di ambito task oltre la fine della sua callback o
  del suo task.
- Non usare mai un handle in un'altra sessione o in un altro task.
- Una sostituzione di cui è stato eseguito il commit invalida gli handle degli
  oggetti sostituiti.
- Una mutazione abortita rende obsoleti gli handle che ha creato.
- Le API segnalano `NEVERC_STATUS_STALE_HANDLE`, `WRONG_OWNER` o `WRONG_TYPE`
  invece di esporre un puntatore LLVM.

Le stringhe e le viste di byte restituite dalle interrogazioni sono prese in
prestito, a meno che un'API non restituisca esplicitamente un buffer
rilasciabile.

## Leggere l'IR

`NevercIRCoreAPI` fornisce:

- identificatore del modulo, triple, data layout e assembly inline;
- cursori di valori stabili per funzioni, globali, blocchi, istruzioni, use e
  operandi;
- ID stabili di tipi e opcode;
- proprietà di funzioni, globali, istruzioni, metadati e attributi;
- costanti intere, in virgola mobile, aggregate, null, poison e undef;
- esportazione/importazione di bitcode e artefatti di modulo verificati.

I cursori di collezione sono limitati: passare una capacità di output e ripetere
la raccolta finché il numero restituito non è zero.

## Mutazione transazionale

Ogni mutazione strutturale usa `NevercIRBuilderAPI`:

1. Avviare una mutazione di modulo o di funzione.
2. Creare un costruttore legato a quella mutazione.
3. Impostare il punto di inserimento e costruire istruzioni, funzioni o blocchi.
4. Eseguire il commit della mutazione.
5. Distruggere i costruttori e l'handle di mutazione.

Il commit verifica l'IR candidata e la pubblica atomicamente. Se il verificatore
fallisce, l'host annulla la mutazione e mantiene il modulo precedente.
`AbortMutation` annulla sempre le modifiche in stage.

Non dichiarare `NEVERC_IR_PRESERVE_ALL` dopo aver modificato l'IR. L'adattatore
dei pass controlla la generazione del modulo e rifiuta una dichiarazione di
conservazione incoerente.

## Livelli di pass e fasi

`NevercIRPassDescriptor.Level` supporta:

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

Le fasi di inserimento stabili sono `PRE_OPT`, `PIPELINE_START`,
`OPTIMIZER_LAST`, `POST_OPT` e `PRE_CODEGEN`. L'invocazione contiene solo gli
handle validi per il proprio livello. I pass di funzione e di ciclo possono essere
eseguiti in concorrenza, quindi lo stato mutabile del plugin deve rispettare il
contratto di concorrenza dichiarato.

L'host esegue sempre il verificatore IR sigillato finale. Un plugin non può
sostituirlo, intercettarlo o saltarlo.

## Analisi

Gli ID delle analisi integrate coprono il grafo delle chiamate, l'albero dei
dominatori, l'albero dei post-dominatori, le informazioni sui cicli, la scalar
evolution, MemorySSA e l'analisi degli alias.

Le analisi dei plugin dichiarano dipendenze e callback di ciclo di vita. I
risultati sono memorizzati in cache per invocazione e invalidati secondo il
risultato di conservazione del pass. I cicli ricorsivi di dipendenze e le
mutazioni da una callback di analisi vengono rifiutati.

## Provider completi

Un provider di generazione dell'IR può sostituire l'abbassamento integrato e
pubblicare un artefatto di modulo verificato. Un provider di ottimizzazione può
sostituire l'intera pipeline di ottimizzazione integrata. Entrambe le strade:

- consumano un input di fase esplicito;
- pubblicano tramite un'API dell'host anziché restituire un puntatore LLVM;
- verificano la compatibilità del target e la validità del modulo;
- mantengono atomicamente il modulo precedente se la pubblicazione fallisce.

Il verificatore finale resta obbligatorio anche dopo un provider di
ottimizzazione.

## Esempio minimo

`pluginsdk/examples/FunctionPass.c` è un pass di funzione in sola lettura.
`pluginsdk/examples/ExamplePlugin.c` mostra l'enumerazione di un modulo e
`pluginsdk/examples/CustomCallConvPlugin.c` illustra attributi e proprietà dei
siti di chiamata.

Compilare e caricare un esempio:

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Usare il suffisso di modulo che CMake produce per la piattaforma.

## Regole di errore

Restituire un `NevercStatus` da ogni callback. I fallimenti del plugin diventano
diagnostiche strutturate; non lanciare eccezioni attraverso il confine C.
Inizializzare ogni intestazione di tabella di output e ogni campo riservato, e
restituire `INVALID_ARGUMENT` in caso di puntatore obbligatorio mancante.

Si vedano `PluginIR.h`, `PluginPhaseSchema.h` e `coverage.json` per le
dichiarazioni normative dell'ABI, le politiche di fase e le prove dei test.
