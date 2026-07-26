**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice documentazione](../README.it.md) · [← Progetto NeverC](../i18n/README.it.md)

# ABI dei plugin NeverC

Un plugin NeverC è un modulo condiviso che esporta esattamente una funzione,
negozia tabelle di capacità versionate tramite un identificatore di interfaccia
a 128 bit e si aggancia a un grafo congelato di fasi del compilatore dotate di
nome. L'intera interfaccia è C11 puro. Un plugin non include mai un header
LLVM, non collega mai il compilatore e non fa mai attraversare il confine a un
tipo C++.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

Questa firma, dichiarata in [`PluginCore.h`], è l'intero contratto di
collegamento. Tutto il resto — leggere l'IR, riscrivere un grafo oggetto,
sostituire la pipeline di ottimizzazione — si raggiunge tramite tabelle che si
chiedono all'host per identificatore.

## Guide

| Guida | Contenuto |
|---|---|
| [API Driver](driver.it.md) | [Riga di comando](driver.it.md#argomenti-grezzi), [scelta della toolchain](driver.it.md#selezione-della-toolchain), [grafo delle azioni](driver.it.md#il-grafo-delle-azioni), [grafo dei job](driver.it.md#il-grafo-dei-job) |
| [API Source e I/O](source.it.md) | [Provider VFS](source.it.md#provider-di-file-system-virtuale), [posizioni sorgente](source.it.md#posizioni-sorgente), [buffer](source.it.md#leggere-file), [sink di output](source.it.md#scrivere-output), [dipendenze](source.it.md#registrare-le-dipendenze) |
| [API Preprocessore](prep.it.md) | [Token](prep.it.md#leggere-i-token), [macro](prep.it.md#identificatori-e-macro), [pragma](prep.it.md#pragma-e-interrogazioni-di-funzionalità), [inclusioni](prep.it.md#reindirizzare-uninclusione), [interrogazioni sulle funzionalità](prep.it.md#pragma-e-interrogazioni-di-funzionalità), [39 tipi di evento](prep.it.md#sottoscrizione-agli-eventi) |
| [API AST e semantica](ast-sema.it.md) | [Estensione del parser](ast-sema.it.md#estensione-del-parser), [mutazione dell'AST](ast-sema.it.md#costruire-e-modificare), [ricerca dei nomi](ast-sema.it.md#interrogazioni-semantiche), [tipi](ast-sema.it.md#accessori-tipizzati), [costanti](ast-sema.it.md#interrogazioni-semantiche) |
| [API IR](ir.it.md) | [Lettura dell'IR LLVM](ir.it.md#percorrere-un-modulo), [costruzione transazionale](ir.it.md#modifica-transazionale), [analisi](ir.it.md#analisi), [pass](ir.it.md#passi), [provider](ir.it.md#sostituire-generazione-e-ottimizzazione) |
| [API MIR](mir.it.md) | [Funzioni macchina](mir.it.md#leggere-la-mir), [registri](mir.it.md#registri), [stack frame](mir.it.md#lo-stack-frame), [pass e analisi MIR](mir.it.md#pass) |
| [Target, MC, assembly, oggetto](target-mc-object.it.md) | [Registrazione di target](target-mc-object.it.md#registrare-un-target), [convenzioni di chiamata](target-mc-object.it.md#abi-e-convenzioni-di-chiamata), [codifica MC](target-mc-object.it.md#encoder-decoder-e-layout), [grafi oggetto](target-mc-object.it.md#grafi-oggetto) |
| [API Link e LTO](link-lto.it.md) | [Grafo di collegamento](link-lto.it.md#leggere-il-grafo), [risoluzione dei simboli](link-lto.it.md#modificare-il-grafo), [GC/ICF](link-lto.it.md#la-macchina-a-stati), [provider di linker e LTO](link-lto.it.md#provider) |
| [API DynCode](dyncode.it.md) | [Immagini piatte indipendenti dalla posizione](dyncode.it.md#immagine-report-e-modifiche-di-byte-limitate), [abbassamento degli import](dyncode.it.md#riferimenti-esterni-e-abbassamento-degli-import), [codifica del set di caratteri](dyncode.it.md#immagine-report-e-modifiche-di-byte-limitate) |
| [Convenzioni di chiamata personalizzate](custom-callconv/README.it.md) | [Plugin di convenzione di chiamata guidati dai dati](custom-callconv/README.it.md#formato-della-spec) |
| [Prove di copertura delle fasi](coverage.json) | Corrispondenza dei test per ogni fase stabile |

## Modello di esecuzione

L'host guida un plugin attraverso tre ambiti annidati. Ogni ambito consegna al
plugin un puntatore di stato opaco che il plugin stesso alloca e possiede, così
un plugin scritto correttamente non ha bisogno di alcuno stato globale
mutabile.

| Ambito | Callback | Significato |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Un processo del compilatore. Qui si interrogano le interfacce e si registrano le capacità. |
| Session | `SessionBegin`, `SessionEnd` | Una invocazione del driver. |
| Task | `TaskBegin`, `TaskEnd` | Una unità di lavoro, identificata da `NevercTaskKind`. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

In pratica solo `PluginID` e `Register` sono obbligatori; qualsiasi slot di
callback può restare `NULL`. I tipi di task sono `NEVERC_TASK_INVOCATION`,
`TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`, `OBJECT` e `DYNCODE`.

L'host chiama prima `ProcessBegin`, poi `Register` esattamente una volta. La
registrazione è l'unico punto in cui si possono aggiungere opzioni,
osservatori, intercettori e provider; dopodiché il grafo delle fasi è
congelato.

Lo stato si recupera dentro una callback, non lo si cattura in anticipo:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## Fasi

Una fase è una transizione con nome e versione da un artefatto di ingresso a
un artefatto di uscita. NeverC include **130 fasi predefinite**, più 8 famiglie
di identificatori di estensione riservate alle fasi definite dai plugin:

| Dominio | Fasi | Dominio | Fasi |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

Tutte e 130 hanno livello di stabilità `stable` nella major 1 dell'ABI. Ogni
fase dichiara una policy, e un plugin può agganciarsi solo nei modi che quella
policy consente:

| Flag di policy | Fasi | Cosa può fare un plugin |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | Registrare un osservatore per una notifica in sola lettura. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | Avvolgere la fase e decidere se chiamare il resto della catena. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | Registrare un provider che fornisce l'output da sé. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | Saltare la transizione fornendo un handle di prova. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | Nulla. Verificatori e commit appartengono all'host. |

I 14 gate sigillati sono `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify` e `dyncode.commit`. Si possono
osservare, ma mai intercettare, sostituire o saltare.

Gli osservatori vengono notificati nei punti che la fase dichiara:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` e
`NEVERC_OBSERVER_AFTER_COMMIT`. Un intercettore riceve una
`NevercPhaseContinuation` e deve chiamare `InvokeNext` **al massimo una
volta**, sul thread della callback, quindi riportare
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` o `NEVERC_PHASE_SKIP` in
`NevercPhaseResult.Action`.

Ogni callback di fase riceve lo stesso frame:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

[`Schema/PhaseSchema.json`] è la fonte normativa per identificatori di fase,
policy, livelli di stabilità e gate di verifica. [`PluginPhaseSchema.h`] e il
file generato [`Schema/PluginPhaseSchema.inc`] che include espongono ciascuno
di essi come costante di compilazione — per la fase
`neverc.ir.pass.pipeline_start`:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT` e le costanti per dominio
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` permettono a un plugin di asserire il
grafo contro cui è stato compilato.

## Un plugin minimo completo

Questo è [`pluginsdk/templates/minimal/Plugin.c`] alla lettera. Si carica,
negozia l'ABI, non registra nulla e si scarica in modo pulito: copiate la
directory e fatela crescere da qui.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Register options, observers, interceptors, or providers here. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` è un buffer di proprietà del chiamante. All'ingresso il suo
`Header.StructSize` indica la capacità scrivibile; il plugin scrive al massimo
quel numero di byte e riporta la dimensione che ha effettivamente prodotto.
Scrivere per primo l'`Header` del descrittore stesso e poi troncare la copia
soddisfa entrambe le metà di quella regola.

## Negoziazione delle interfacce

Le tabelle di capacità si ottengono tramite identificatore di interfaccia a 128
bit, non tramite simbolo. Chiedete la versione major contro cui avete compilato
e la minor più bassa con cui riuscite a lavorare:

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

Confrontare `TableSize` con l'offset dell'ultima funzione che chiamate è la
regola che rende estensibile questo ABI: un host più recente aggiunge campi in
coda e un plugin più vecchio continua a funzionare perché non legge mai oltre
il prefisso che ha verificato. La macro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` applica lo stesso controllo a
una struttura che avete ricevuto. La stessa firma di `QueryInterface` è
presente anche su `NevercCoreAPI`, così potete negoziare in un secondo momento
invece che all'ingresso.

Le interfacce pubbliche, le loro tabelle e le loro macro identificative:

| Coppia di macro di interfaccia | Tabella | Header |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | [`PluginCore.h`] |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | [`PluginDriver.h`] |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | [`PluginSource.h`] |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | [`PluginPrep.h`] |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | [`PluginAST.h`] |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | [`PluginSema.h`] |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | sei tabelle IR | [`PluginIR.h`] |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | [`PluginTarget.h`] |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | quattro tabelle MIR | [`PluginMIR.h`] |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | quattro tabelle MC | [`PluginMC.h`] |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | tre tabelle oggetto | [`PluginObject.h`] |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | tre tabelle di collegamento | [`PluginLink.h`] |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | [`PluginLTO.h`] |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | tre tabelle dyncode | [`PluginDynCode.h`] |

Ogni header definisce anche i corrispondenti `NEVERC_<DOMAIN>_API_MAJOR` e
`_MINOR` da passare a `QueryInterface`.

Un'interfaccia è o `NEVERC_INTERFACE_STABLE` (un host più recente può solo
aggiungere) o `NEVERC_INTERFACE_LOCKSTEP` (schemi specifici del target che
devono corrispondere esattamente). Confrontate il digest dello schema prima di
consumare valori LOCKSTEP.

## Registrazione

`Register` riceve una `NevercRegistrarAPI` e un `RegistrarContext` opaco:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

Ognuna di queste chiamate prende `RegistrarContext` come primo argomento e un
descrittore azzerato come secondo. Quale chiamata scegli è ciò che decide come
l'host ti tratta alla fase:

| Chiamata | Descrittore | Callback | La fase deve dichiarare |
|---|---|---|---|
| `RegisterObserver` | `NevercObserverDescriptor` | `NevercPhaseObserverFn` | `OBSERVABLE` |
| `RegisterInterceptor` | `NevercInterceptorDescriptor` | `NevercPhaseInterceptorFn` | `INTERCEPTABLE` |
| `RegisterProvider` | `NevercProviderDescriptor` | `NevercPhaseProviderFn` | `REPLACEABLE` |
| `RegisterPhase` | `NevercPhaseDescriptor` | — | un ID definito dal plugin |
| `RegisterOption` | `NevercOptionDescriptor` | `Validator` facoltativo | — |
| `RegisterInterface` | argomenti semplici | — | — |

Un descrittore che non supera la validazione strutturale viene respinto sul posto con
`NEVERC_STATUS_INVALID_DESCRIPTOR`. Il controllo della policy avviene quando l'host
applica la registrazione: una fase sconosciuta, o una che non dichiara la policy
richiesta dalla tua chiamata, viene rifiutata lì. Un gate sigillato accetta soltanto
osservatori.

I registrar di dominio — `NevercIRPassAPI.RegisterPass`,
`NevercTargetAPI.RegisterTarget`, `NevercObjectFormatAPI.RegisterFormat` e gli
altri — prendono quello stesso `RegistrarContext` come secondo argomento: è
così che l'host attribuisce una registrazione al vostro plugin.

### Osservatori

```c
typedef struct NevercObserverDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercObserverPoint Points;
  uint32_t Reserved;
  NevercPhaseObserverFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObserverDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseObserverFn)(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData);
```

`Points` è una maschera di bit fra `NEVERC_OBSERVER_BEFORE` (1),
`NEVERC_OBSERVER_AFTER` (2) e `NEVERC_OBSERVER_AFTER_COMMIT` (4); deve essere diversa
da zero, e l'argomento `Point` dice al callback quale si è attivato. Da
[`pluginsdk/examples/DriverTracePlugin.c`]:

```c
NevercObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){
    sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Observer.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                                     NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
```

`UserData` ti viene restituito intatto. Impostare `DestroyUserData` — presente su
ogni descrittore di questa sezione — fa sì che l'host liberi quella memoria quando la
registrazione scompare, così un'allocazione per registrazione non deve essere seguita
in `Destroy`.

### Intercettori

```c
typedef struct NevercInterceptorDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercPhaseInterceptorFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercInterceptorDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseInterceptorFn)(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);
```

La continuazione è tutto il resto della catena, e il risultato è il modo in cui
riferisci che cosa ne hai fatto:

```c
typedef struct NevercPhaseContinuation {
  NevercABITableHeader Header;
  NevercInvokeNextFn InvokeNext;
  void *Context;
  uint64_t Generation;
} NevercPhaseContinuation;

typedef struct NevercPhaseResult {
  NevercABITableHeader Header;
  NevercPhaseAction Action;
  uint32_t Reserved;
  NevercArtifactHandle Output;
  NevercProofHandle Proof;
} NevercPhaseResult;
```

Le tre azioni non sono intercambiabili. L'host confronta il risultato con quello che
hai fatto davvero e fa fallire la catena con `NEVERC_STATUS_POLICY_VIOLATION` a ogni
discordanza:

| `Action` | `InvokeNext` | `Output` | `Proof` | Richiede inoltre |
|---|---|---|---|---|
| `NEVERC_PHASE_CONTINUE` | chiamato una volta | vuoto | vuoto | — |
| `NEVERC_PHASE_REPLACE` | non chiamato | impostato | vuoto | `REPLACEABLE` |
| `NEVERC_PHASE_SKIP` | non chiamato | impostato | impostato | `SKIPPABLE_WITH_PROOF` |

`InvokeNext` può essere chiamato al più una volta e solo sul thread del callback: una
seconda chiamata è una violazione di policy, e una chiamata da un altro thread
segnala `NEVERC_STATUS_WRONG_SCOPE`. Anche un intercettore che restituisce `CONTINUE`
senza averlo chiamato viola la policy, perché la fase non produrrebbe nulla in
silenzio.

```c
NevercInterceptorDescriptor Interceptor = {0};
Interceptor.Header = (NevercABITableHeader){
    sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Interceptor.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                                        NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW};
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

### Provider

Un provider sostituisce la fase per intero, perciò dichiara anche il contratto di
determinismo su cui fa affidamento la cache di build:

```c
typedef struct NevercProviderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView ProviderID;
  NevercPhaseRoute Route;
  NevercBool Deterministic;
  NevercBool Cacheable;
  NevercBool FallbackSafe;
  uint32_t Reserved;
  NevercPhaseProviderFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercProviderDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseProviderFn)(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData);
```

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

`ProviderID` deve essere un nome canonico: al massimo 255 byte fra lettere minuscole,
cifre, `.`, `_` e `-`, che non inizi né finisca con un punto e non contenga mai `..`.
Basta una maiuscola perché la registrazione venga rifiutata. `Route.Header` va
inizializzato come qualunque altra intestazione di tabella.

Qui non c'è continuazione: il callback *è* la fase. Deve riportare
`NEVERC_PHASE_REPLACE` con un `Output` e un `Proof` vuoto — qualsiasi altra cosa è una
violazione di policy.

`FallbackSafe` è l'unico di questi flag con un effetto a runtime che va oltre la
contabilità. Quando vale `NEVERC_TRUE` e il provider fallisce con uno stato
contrassegnato `NEVERC_STATUS_FLAG_RECOVERABLE`, l'host può scartare gli effetti
parziali ed eseguire invece l'implementazione integrata. Lascialo a `NEVERC_FALSE`
quando un tentativo lasciato a metà non può essere annullato.

### Fasi definite dal plugin

`RegisterPhase` aggiunge una transizione che l'host non conosce, ed è esattamente
quello per cui sono riservate le 8 famiglie di ID di estensione:

```c
typedef struct NevercPhaseDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView CanonicalName;
  NevercInterfaceID InputArtifact;
  NevercInterfaceID OutputArtifact;
  NevercPhasePolicy Policy;
  NevercObserverPoint ObserverPoints;
  uint32_t Reserved;
} NevercPhaseDescriptor;
```

`Phase`, `InputArtifact` e `OutputArtifact` devono essere tutti diversi da zero, e
`Policy` deve essere diversa da zero e contenere solo flag noti. Dichiarare
`ObserverPoints` senza `NEVERC_PHASE_OBSERVABLE` viene rifiutato, così come combinare
`NEVERC_PHASE_SEALED_HOST_GATE` con `INTERCEPTABLE`, `REPLACEABLE` o
`SKIPPABLE_WITH_PROOF`: sono gli stessi invarianti rispetto ai quali viene verificato
il grafo integrato. Prendi l'ID dalla famiglia del tuo dominio, così non potrà
collidere con una futura fase integrata:

```c
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

/* NEVERC_EXTENSION_FAMILY_COUNT is 8; family 1 is "neverc.ir.extension". */
NevercInterfaceID MyPhase = {NEVERC_EXTENSION_FAMILY_1_ID_HIGH,
                             NEVERC_EXTENSION_FAMILY_1_ID_LOW_MIN};
```

Ogni famiglia pubblica `_NAMESPACE`, `_ID_HIGH`, `_ID_LOW_MIN` e `_ID_LOW_MAX`, e la
metà bassa è tua da assegnare all'interno di quell'intervallo.

### Pubblicare un'interfaccia per altri plugin

`RegisterInterface` è l'unica chiamata che non prende un descrittore. Consegna
all'host una tabella tua, così che un altro plugin possa raggiungerla attraverso lo
stesso `QueryInterface` usato per le interfacce integrate:

```c
Registrar->RegisterInterface(RegistrarContext, MyInterfaceID,
                             NEVERC_INTERFACE_STABLE, &MyTable,
                             /* Compatibility = */ NULL);
```

Passa invece `NEVERC_INTERFACE_LOCKSTEP` quando la tabella trasporta valori di schema
specifici del target che non sopravviverebbero a uno sfasamento di versione.
Un'interfaccia lockstep deve fornire una `NevercCompatibilityKey`, che vincola il
consumatore a una singola build del produttore:

```c
typedef struct NevercCompatibilityKey {
  NevercABITableHeader Header;
  NevercStringView ProducerBuildID;   /* compare against Bootstrap->HostBuildID */
  NevercStringView TargetABIKey;
  uint32_t LLVMMajor;                 /* compare against Bootstrap->LLVMMajor   */
  uint32_t Reserved;
} NevercCompatibilityKey;
```

Per una registrazione lockstep vanno riempiti tutti e tre i campi; un ID di build
vuoto, una chiave ABI vuota o un major LLVM pari a zero vengono rifiutati come
descrittore non valido.

## Compilazione

Includete l'header aggregato [`NevercPluginAPI.h`] oppure solo i domini che
usate:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

Costruire un modulo condiviso con NeverC stesso:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Oppure contro un SDK installato con CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Oppure con pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Usate `.so`, `.dylib` o `.dll` a seconda dell'host. L'SDK non collega alcun
LLVM né alcun runtime NeverC: `NevercPluginSDK::headers` è di soli header.

## Caricamento e configurazione

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Opzione | Forma | Scopo |
|---|---|---|
| `-fplugin=<path>` | ripetibile | Caricare un modulo condiviso di plugin per l'intera toolchain. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | ripetibile | Passare un valore con spazio dei nomi a un'opzione di plugin registrata. |
| `-fplugin-provider=<phase>:<plugin-id>` | ripetibile | Scegliere quale plugin fornisce una fase sostituibile. |
| `-fplugin-pass=<dsopath>` | ripetibile | Caricare un plugin di pass out-of-tree con ABI C. |
| `-fplugin-pass-arg=<key>=<value>` | ripetibile | Passare un argomento ai plugin di pass con ABI C. |

Il qualificatore `<plugin-id>:` può essere omesso solo quando è attivo
esattamente un plugin. Le opzioni che un plugin registra con `RegisterOption`
sono accettate anche direttamente con la grafia dichiarata, in forma di flag,
unita, separata o a più argomenti. Argomenti di plugin e selezioni di provider
privi di un `-fplugin=` corrispondente sono un errore netto, non un'operazione
ignorata in silenzio.

Un'opzione registrata si può rileggere in qualsiasi momento tramite la tabella
core:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## Regole dell'ABI

- Interrogate le tabelle di capacità tramite `QueryInterface`; pretendete la
  major corrispondente e controllate `StructSize` prima di toccare un campo.
- Inizializzate l'`Header` e lo spazio riservato di ogni struttura pubblica.
  Azzerate la struttura, poi impostate `StructSize`, `Major`, `Minor` e
  `Flags`.
- Trattate handle e viste prese in prestito come valori opachi con ambito. Non
  conservate mai un handle di ambito task oltre la sua callback, non usatelo
  mai in un'altra sessione o task e non fabbricate mai un valore di handle.
- Restituite `NevercStatus` da ogni callback. Non lasciate che un'eccezione C++
  o un puntatore di proprietà dell'host attraversino il confine C.
- Dichiarate il `NevercConcurrencyModel` più stretto che sia veritiero
  (`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`) e il
  `NevercReentrancyModel` (`NONE`, `ALLOWED`).
- Eseguite le modifiche a IR, MIR, AST, grafi e artefatti tramite le API
  transazionali dell'host: aprite una mutazione, preparate le modifiche, poi
  fate commit o abort. Il commit verifica e pubblica in modo atomico; un commit
  fallito lascia intatto lo stato precedente.
- Allocate tramite `NevercCoreAPI.Allocate` / `Reallocate` / `Deallocate`
  quando la memoria deve essere contabilizzata dall'host.
- Tenete lo stato mutabile nello stato process/session/task fornito dall'host.
  Lo stato globale mutabile è controllato da
  [`utils/plugin-api/check-global-state.py`].

Tutte le strutture pubbliche sono disposte sotto `NEVERC_ABI_PACK_BEGIN`
(packing a 8 byte) e usano solo tipi a larghezza fissa. Le nuove funzioni
vengono aggiunte in coda a tabelle di capacità versionate in modo
indipendente; il prefisso stabile di una tabella non cambia entro la prima
major dell'ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Stati e diagnostica

`NevercStatus` porta un `Code`, dei `Flags` e una parola `Detail`. L'insieme
completo dei codici:

| Codice | Significato |
|---|---|
| `NEVERC_STATUS_OK` | Successo. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Un puntatore o valore richiesto mancava o era malformato. |
| `NEVERC_STATUS_ABI_MISMATCH` | La tabella negoziata è troppo piccola o la major è diversa. |
| `NEVERC_STATUS_MISSING_INTERFACE` | L'host non pubblica l'interfaccia richiesta. |
| `NEVERC_STATUS_VERSION_MISMATCH` | La major/minor richiesta non può essere soddisfatta. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | Un descrittore non ha superato la validazione strutturale. |
| `NEVERC_STATUS_DUPLICATE_ID` | Un identificatore era già registrato. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | Una dipendenza dichiarata è assente. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | L'ordine di registrazione non può essere soddisfatto. |
| `NEVERC_STATUS_BUSY` | Una risorsa è trattenuta altrove. |
| `NEVERC_STATUS_CANCELLED` | È stata richiesta una cancellazione cooperativa. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | È stato raggiunto un budget o un limite. |
| `NEVERC_STATUS_STALE_HANDLE` | Un handle è sopravvissuto all'oggetto che nominava. |
| `NEVERC_STATUS_WRONG_SESSION` | Un handle è stato usato in un'altra sessione. |
| `NEVERC_STATUS_WRONG_SCOPE` | Un handle è stato usato fuori dal suo ambito. |
| `NEVERC_STATUS_WRONG_TYPE` | Un handle nominava un'entità di tipo diverso. |
| `NEVERC_STATUS_INVALID_STATE` | L'operazione non è lecita nello stato attuale. |
| `NEVERC_STATUS_POLICY_VIOLATION` | La policy della fase vieta l'operazione. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Un verificatore sigillato dell'host ha respinto il prodotto. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | L'host non può offrire qui quella capacità. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | Il plugin ha segnalato un fallimento generico. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | Un'eccezione è sfuggita da una callback del plugin. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | L'output è stato scritto solo in parte. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | Una chiamata rientrante è stata rifiutata. |
| `NEVERC_STATUS_NOT_FOUND` | L'entità indicata non esiste. |

I bit di flag descrivono che cosa è successo all'output, ed è ciò di cui un
sistema di build ha bisogno per decidere se un nuovo tentativo è sicuro:
`NEVERC_STATUS_FLAG_RECOVERABLE`, `_OUTPUT_ALREADY_COMMITTED`,
`_OUTPUT_MAY_BE_PARTIAL`, `_OUTPUT_RECOVERY_REQUIRED` e
`_DURABILITY_UNCONFIRMED`.

Segnalate i problemi con `NevercCoreAPI.EmitDiagnostic` e un
`NevercDiagnosticDescriptor` che porta gravità (`NOTE`, `REMARK`, `WARNING`,
`ERROR`, `FATAL`), codice, identificatore del plugin, identificatore della
fase, messaggio, note, posizione sorgente, intervalli e fix-it. Chiamate
`CheckCancelled` prima di lavori costosi.

## Esempi

Compilarli tutti:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Ogni esempio viene compilato due volte — una con il compilatore C host
configurato e una con il NeverC appena costruito — così l'ABI è dimostrato da
entrambi i lati. I moduli finiscono in
`build-neverc/neverc/pluginsdk/examples/host/`.

| Esempio | Target CMake | Mostra |
|---|---|---|
| [`DriverTracePlugin.c`] | `neverc-plugin-example-driver-trace` | Registrazione di opzioni, osservazione di fasi, intercettazione di job |
| [`VirtualHeaderPlugin.c`] | `neverc-plugin-example-virtual-header` | Un provider VFS che serve un header in memoria |
| [`ASTRewritePlugin.c`] | `neverc-plugin-example-ast-rewrite` | Intercettazione del parser e mutazione atomica dell'AST |
| [`ExamplePlugin.c`] | `neverc-plugin-example-ir-overview` | Un pass IR a livello di modulo che percorre l'elenco delle funzioni con un cursore di valori |
| [`FunctionPass.c`] | `neverc-plugin-example-function-pass` | Un pass IR di funzione stabile |
| [`MachinePass.c`] | `neverc-plugin-example-machine-pass` | Un pass MIR stabile all'hook pre-emit |
| [`MCObserverPlugin.c`] | `neverc-plugin-example-mc-observer` | Eventi di emissione MC in sola lettura |
| [`ObjectRewritePlugin.c`] | `neverc-plugin-example-object-rewrite` | Riscrittura transazionale di ObjectGraph |
| [`CustomCallConvPlugin.c`] | `neverc-plugin-example-custom-callconv` | Convenzioni di chiamata guidate dai dati |
| [`DynCodeTracePlugin.c`] | `neverc-plugin-example-dyncode-trace` | Osservazione della pipeline dyncode |
| [`DynCodeEncoderPlugin.c`] | `neverc-plugin-example-dyncode-encoder` | Intercettazione della codifica del set di caratteri dyncode |
| [`CrtShimPlugin.c`] | `neverc-plugin-example-crt-shim` | Un plugin con zero dipendenze dalla CRT |
| [`BenchPlugin.c`] | `neverc-plugin-example-abi-bench` | Microbenchmark del throughput di chiamata dell'ABI |

Caricarne uno:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Fonti normative

| File | Garanzie |
|---|---|
| [`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`] | Identificatori di fase, policy, stabilità, gate di verifica |
| [`pluginsdk/manifest/plugin.json`] | Versione dell'ABI, identificatori/versioni/stabilità delle interfacce, digest degli schemi, target supportati |
| [`pluginsdk/abi/plugin.json`] | Dimensione, allineamento e offset dei campi misurati di ogni struttura pubblica, per chiave di ABI dell'host |
| [`docs/plugin-api/coverage.json`] | Associa ogni fase stabile a test positivi, negativi, di sostituzione, di osservatore e di gate sigillato |

Un SDK può quindi essere validato meccanicamente contro un host, e una build di
plugin può asserire il layout delle proprie strutture contro la chiave di ABI
in cui verrà caricata.

<!-- reference links -->
[`ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`BenchPlugin.c`]: ../../pluginsdk/examples/BenchPlugin.c
[`CrtShimPlugin.c`]: ../../pluginsdk/examples/CrtShimPlugin.c
[`CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`docs/plugin-api/coverage.json`]: coverage.json
[`DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
[`DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
[`ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`NevercPluginAPI.h`]: ../../neverc/include/neverc/Plugin/NevercPluginAPI.h
[`ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginCore.h`]: ../../neverc/include/neverc/Plugin/PluginCore.h
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`PluginDynCode.h`]: ../../neverc/include/neverc/Plugin/PluginDynCode.h
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`PluginPhaseSchema.h`]: ../../neverc/include/neverc/Plugin/PluginPhaseSchema.h
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`pluginsdk/abi/plugin.json`]: ../../pluginsdk/abi/plugin.json
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
[`pluginsdk/manifest/plugin.json`]: ../../pluginsdk/manifest/plugin.json
[`pluginsdk/templates/minimal/Plugin.c`]: ../../pluginsdk/templates/minimal/Plugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPhaseSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPhaseSchema.inc
[`utils/plugin-api/check-global-state.py`]: ../../utils/plugin-api/check-global-state.py
[`VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
