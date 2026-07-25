**Lingue**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API Driver dei plugin NeverC

Il driver trasforma una riga di comando in un insieme di job eseguiti.
`PluginDriver.h` espone quella pipeline come sei fasi e una tabella di
capacità, `NevercDriverAPI`, così che un plugin possa riscrivere gli argomenti,
scegliere una toolchain, ristrutturare il grafo delle azioni, aggiungere o
sostituire job e persino eseguire un job nel processo corrente invece di
avviarne uno nuovo.

## Interfaccia

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` è un'unica tabella piatta di 67 slot di funzione raggruppati
in cinque aree: argomenti grezzi, opzioni analizzate, selezione della toolchain,
grafo delle azioni e grafo dei job. Validate `TableSize` rispetto all'offset
dell'ultimo slot che usate: la coda attuale è `GetJobResult`.

## Le sei fasi del driver

| Fase | Policy | Ingresso → uscita |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | elenco di opzioni analizzate → elenco di opzioni analizzate |
| `neverc.driver.select_toolchain` | più REPLACEABLE | richiesta di toolchain → selezione |
| `neverc.driver.build_actions` | più REPLACEABLE | richiesta → grafo delle azioni |
| `neverc.driver.build_jobs` | più REPLACEABLE | grafo delle azioni → grafo dei job |
| `neverc.driver.execute_job` | più REPLACEABLE | richiesta di esecuzione → risultato del job |

Le relative macro seguono lo schema consueto:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## Registrare un'opzione

Le opzioni si dichiarano una sola volta, durante `Register`; da quel momento il
driver le accetta sulla riga di comando esattamente come se fossero integrate.

```c
typedef struct NevercOptionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  NevercStringList Aliases;
  NevercOptionForm Form;                  /* FLAG, JOINED, SEPARATE, MULTI_ARG */
  NevercOptionValueType ValueType;        /* BOOL, INT, UINT, STRING, ENUM, PATH */
  NevercOptionMultiplicity Multiplicity;  /* SINGLE, LAST_WINS, APPEND */
  uint32_t ArgumentCount;
  NevercBool Required;
  NevercBool Hidden;
  NevercStringView Help;
  NevercStringView Metavar;
  NevercStructArrayView EnumValues;       /* NevercOptionEnumValue[] */
  NevercStringList Conflicts;
  NevercStringList Requires;
  NevercStringView TargetPredicate;
  NevercOptionValidatorFn Validator;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercOptionDescriptor;
```

Da `pluginsdk/examples/DriverTracePlugin.c`:

```c
NevercOptionDescriptor Option = {0};
Option.Header = (NevercABITableHeader){sizeof(Option), NEVERC_DRIVER_API_MAJOR,
                                       NEVERC_DRIVER_API_MINOR, 0};
Option.Spelling     = SV("--driver-trace");
Option.Form         = NEVERC_OPTION_FLAG;
Option.ValueType    = NEVERC_OPTION_BOOL;
Option.Multiplicity = NEVERC_OPTION_SINGLE;
Option.Help         = SV("enable the driver trace example plugin");
Status = Registrar->RegisterOption(RegistrarContext, &Option);
```

`Validator` viene chiamato a ogni occorrenza con un
`NevercOptionValidationContext` che porta l'ID del plugin, la grafia, il triple
di destinazione e l'indice di occorrenza: un valore può quindi essere rifiutato
con una diagnostica vera anziché fallire più avanti. `TargetPredicate` limita
un'opzione ai triple corrispondenti. I valori si rileggono con
`NevercCoreAPI.GetPluginOptionValueCount` e `GetPluginOptionValue`.

## Argomenti grezzi

In `neverc.driver.raw_arguments` l'artefatto è il vettore argv. La lettura è
basata su indice e ogni voce segnala da dove proviene:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

La modifica è transazionale ed è lecita solo da un interceptor, perché la
mutazione è legata alla continuation:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* oppure Abort */
```

## Argomenti analizzati

`neverc.driver.parsed_arguments` lavora sulle occorrenze delle opzioni invece
che sulle stringhe: è esattamente ciò che serve quando si aggiunge un flag che
non deve essere rianalizzato lessicalmente:

```c
typedef struct NevercOptionOccurrence {
  NevercABITableHeader Header;
  uint64_t Occurrence;
  NevercStringView Spelling;
  NevercStringList Values;
  NevercArgumentOrigin Origin;
  uint32_t Reserved;
} NevercOptionOccurrence;
```

`GetOptionOccurrenceCount` e `GetOptionOccurrence` leggono; poi
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence` e
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` modificano.

## Selezione della toolchain

La richiesta descrive cosa è stato chiesto e cosa ha calcolato il driver:

```c
typedef struct NevercToolChainRequest {
  NevercABITableHeader Header;
  NevercStringView RequestedTriple;
  NevercStringView ComputedTriple;
  NevercStringView SysRoot;
  NevercStringView ResourceDir;
  NevercStringView CPU;
  NevercStringList Features;
  NevercExecutionLevel ExecutionLevel;  /* UNSPECIFIED, USER, KERNEL */
  NevercBool DynamicCodeProfile;
  uint32_t Reserved;
} NevercToolChainRequest;
```

Un interceptor può ritoccare la richiesta con `BeginToolChainMutation`,
`SetToolChainTriple`, `SetToolChainCPU`, `SetToolChainFeatures` e
`CommitToolChainMutation`. Un provider invece risponde direttamente alla fase
con `CreateToolChainSelection`, indicando uno degli ID di toolchain integrati o
il proprio:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` rilegge il risultato e riporta `BuiltinProviderUsed`,
così un observer può capire se un plugin si è aggiudicato la fase.

## Il grafo delle azioni

Un nodo azione è un passo di compilazione tipizzato. I nodi fanno riferimento
agli input del driver e ad altri nodi:

```c
typedef struct NevercActionNode {
  NevercABITableHeader Header;
  NevercActionNodeID Node;
  NevercActionKind Kind;
  NevercDriverType OutputType;
  uint64_t InputCount;
  NevercDriverInputID DriverInput;
  NevercStringView BindArch;
  uint64_t Reserved;
} NevercActionNode;
```

| `NevercActionKind` | | `NevercDriverType` | |
|---|---|---|---|
| `INPUT` | `BIND_ARCH` | `PP_C`, `C`, `C_HEADER` | `PP_ASM`, `ASM` |
| `PREPROCESS` | `COMPILE` | `LLVM_IR`, `LLVM_BC` | `LTO_IR`, `LTO_BC` |
| `BACKEND` | `ASSEMBLE` | `OBJECT`, `IMAGE` | `DSYM` |
| `LINK`, `LIPO` | `DSYMUTIL` | `DEPENDENCIES` | `NOTHING` |
| `STATIC_LIB` | `DYNCODE` | | |

Lettura con `GetDriverInputCount` / `GetDriverInput`, `GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput` e `GetActionRootCount` /
`GetActionRoot`.

Costruire un grafo sostitutivo passa da un builder e da una sola pubblicazione:

```c
NevercActionGraphBuilderHandle Builder;
Driver->CreateActionGraphBuilder(Driver->Context, Frame, Request, &Builder);

NevercActionNodeDescriptor Node = {0};
Node.Header     = (NevercABITableHeader){sizeof(Node), NEVERC_DRIVER_API_MAJOR,
                                         NEVERC_DRIVER_API_MINOR, 0};
Node.Kind       = NEVERC_ACTION_COMPILE;
Node.OutputType = NEVERC_DRIVER_TYPE_OBJECT;
Node.Inputs     = /* NevercActionNodeIDList */;
NevercActionNodeID Created;
Driver->AddActionNode(Driver->Context, Builder, &Node, &Created);

Driver->SetActionRoots(Driver->Context, Builder, Roots);
Driver->PublishActionGraph(Driver->Context, Frame, Builder, &OutGraph);
```

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType` e
`SetActionNodeBindArch` modificano un builder in corso. Per aggiustare il grafo
esistente dell'host invece di ricostruirlo, usate `BeginActionGraphMutation` e
`CommitActionGraphMutation`; `AbortActionGraphEdit` scarta entrambe le forme.

## Il grafo dei job

Un job è un comando da eseguire. `NevercJobDescriptor` ne descrive uno:

```c
typedef struct NevercJobDescriptor {
  NevercABITableHeader Header;
  NevercJobKind Kind;                             /* COMMAND, FRONTEND, LINKER,
                                                     ARCHIVE, PLUGIN, DYNCODE  */
  NevercResponseFileKind ResponseFileKind;        /* NONE, FULL, LIST          */
  NevercResponseFileEncoding ResponseFileEncoding;/* UTF8, CURRENT_CODE_PAGE,
                                                     UTF16                     */
  NevercBool InProcess;
  NevercActionNodeID SourceAction;
  NevercLinkerFlavor LinkerFlavor;                /* NONE, GNU, WIN_LINK, DARWIN */
  uint32_t Reserved;
  NevercStringView Executable;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
  NevercJobIDList Dependencies;
  NevercStringView CallbackID;
  NevercPluginJobCallbackFn Callback;
  void *UserData;
} NevercJobDescriptor;
```

Impostate `Kind` a `NEVERC_JOB_PLUGIN` con un `Callback` e il driver eseguirà la
vostra funzione dove altrimenti avvierebbe un processo:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs sono in prestito. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

La lettura del grafo rispecchia quella del grafo delle azioni: `GetJobCount` /
`GetJob`, `GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`,
`GetJobInput` / `GetJobOutput`. Notate che `NevercJob` riporta solo i conteggi:
recuperate ogni stringa o file per indice, senza aspettarvi un array inline.

La modifica usa `CreateJobGraphBuilder` o `BeginJobGraphMutation`, poi `AddJob`,
`RemoveJob`, `MoveJobBefore`, `ReplaceJob`, `SetJobArgument`,
`SetJobEnvironment`, `SetJobInput`, `SetJobOutput` e
`ReplaceJobDependencies`. Pubblicate con `PublishJobGraph` o
`CommitJobGraphMutation`; scartate con `AbortJobGraphEdit`.

## Eseguire un job

In `neverc.driver.execute_job` l'artefatto di ingresso è un
`NevercJobExecutionRequest`: il job più i suoi elenchi di argomenti, ambiente,
input, output e dipendenze completamente materializzati. Un provider esegue il
job e riporta il risultato:

```c
typedef struct NevercJobResultDescriptor {
  NevercABITableHeader Header;
  int32_t ExitCode;
  NevercBool ExecutionFailed;
  NevercBool HasProcessStatistics;
  uint32_t Reserved;
  NevercStringView ErrorMessage;
  NevercOutputSealList OutputSeals;
  uint64_t TotalTimeMicroseconds;
  uint64_t UserTimeMicroseconds;
  uint64_t PeakMemoryKiB;
} NevercJobResultDescriptor;
```

`OutputSeals` porta gli `NevercOutputSealHandle` prodotti tramite l'API di I/O
(vedi [Source e I/O](source.it.md)): è così che l'host conferma che i file che
un job ha dichiarato di scrivere esistono davvero con i digest riportati.
`GetJobResult` legge un risultato committato e, come la selezione della
toolchain, riporta `BuiltinProviderUsed`.

## Esempio completo: osservare gli argomenti, intercettare l'esecuzione

Condensato da `pluginsdk/examples/DriverTracePlugin.c`. Il plugin non tiene
variabili globali: lo stato di processo conserva le tabelle negoziate e i
contatori per sessione e per task vengono richiesti all'host dentro ogni
callback.

```c
static NevercStatus NEVERC_CALL
observe_arguments(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                  void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  DriverTraceSessionState *Session = NULL;
  uint64_t ArgumentCount = 0;
  NevercStatus Status;
  if (Frame == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(Process->Core->Context,
                                          Frame->Session, plugin_id(),
                                          (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Driver->GetArgumentCount(Process->Driver->Context, Frame,
                                             Frame->Input, &ArgumentCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->ArgumentCallbacks;
  if (Point == NEVERC_OBSERVER_BEFORE && !Session->Announced) {
    Session->Announced = NEVERC_TRUE;
    return emit_trace_remark(Process, Frame, "driver argument phase observed",
                             30, 1001);
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
intercept_job(const NevercPhaseFrame *Frame,
              NevercPhaseContinuation *Continuation,
              NevercPhaseResult *OutResult, void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  NevercJobExecutionRequest Request = {0};
  NevercPhaseResult Downstream = {0};
  NevercStatus Status;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL || !Process)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Request.Header = (NevercABITableHeader){
      sizeof(Request), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  Status = Process->Driver->GetJobExecutionRequest(
      Process->Driver->Context, Frame, Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  *OutResult = (NevercPhaseResult){0};
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
```

La registrazione collega entrambi alle rispettive fasi:

```c
Observer.Phase = phase_id(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW);
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Registrar->RegisterObserver(RegistrarContext, &Observer);

Interceptor.Phase = phase_id(NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                             NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW);
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

Compilatelo ed eseguitelo:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## Regole

- Le mutazioni di argomenti, argomenti analizzati, toolchain, grafo delle azioni
  e grafo dei job richiedono tutte la `NevercPhaseContinuation`
  dell'interceptor; al di fuori vengono rifiutate con
  `NEVERC_STATUS_WRONG_SCOPE`.
- Chiamate `InvokeNext` al massimo una volta e solo sul thread della callback.
- Ogni handle di mutazione deve raggiungere esattamente un `Commit*` o un
  `Abort*`.
- Le viste restituite da una chiamata `Get*` sono in prestito per la durata
  della callback. Copiate ciò che volete conservare.
- Una callback `NEVERC_JOB_PLUGIN` non deve avviare il processo che l'host
  avrebbe avviato e insieme riportare il successo del percorso integrato:
  dichiarate `REPLACE` e assumetevi l'esito.
- Segnalate un job fallito tramite
  `NevercJobResultDescriptor.ExecutionFailed` e `ErrorMessage` invece di
  restituire uno stato diverso da OK per un job che è stato eseguito ed è
  legittimamente fallito.

Vedete `PluginDriver.h` per le dichiarazioni normative, `PhaseSchema.json` per
le policy delle fasi del driver e `coverage.json` per le prove dei test.
