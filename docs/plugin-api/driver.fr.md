**Langues**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← ABI de plugin NeverC](README.fr.md)

# API Driver des plugins NeverC

Le driver transforme une ligne de commande en un ensemble de tâches exécutées.
[`PluginDriver.h`] expose ce pipeline sous la forme de six phases et d'une table
de capacités, `NevercDriverAPI`, ce qui permet à un plugin de réécrire les
arguments, de choisir une chaîne d'outils, de restructurer le graphe d'actions,
d'ajouter ou de remplacer des tâches, et même d'exécuter une tâche dans le
processus courant au lieu d'en lancer un nouveau.

## Interface

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` est une table plate de 67 emplacements de fonctions répartis
en cinq domaines : arguments bruts, options analysées, sélection de la chaîne
d'outils, graphe d'actions et graphe de tâches. Validez `TableSize` par rapport
au décalage du dernier emplacement que vous utilisez — la queue actuelle est
`GetJobResult`.

## Les six phases du driver

| Phase | Politique | Entrée → sortie |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | liste d'options analysées → liste d'options analysées |
| `neverc.driver.select_toolchain` | plus REPLACEABLE | requête de chaîne d'outils → sélection |
| `neverc.driver.build_actions` | plus REPLACEABLE | requête → graphe d'actions |
| `neverc.driver.build_jobs` | plus REPLACEABLE | graphe d'actions → graphe de tâches |
| `neverc.driver.execute_job` | plus REPLACEABLE | requête d'exécution → résultat de tâche |

Leurs macros suivent le schéma habituel :
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## Déclarer une option

Les options sont déclarées une seule fois, pendant `Register` ; le driver les
accepte ensuite sur la ligne de commande exactement comme si elles étaient
intégrées.

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

Extrait de [`pluginsdk/examples/DriverTracePlugin.c`] :

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

`Validator` est appelé à chaque occurrence avec un
`NevercOptionValidationContext` portant l'identifiant du plugin, l'orthographe,
le triplet cible et l'indice d'occurrence : une valeur peut donc être rejetée
par un vrai diagnostic plutôt que d'échouer plus tard. `TargetPredicate`
restreint une option aux triplets correspondants. Relisez les valeurs avec
`NevercCoreAPI.GetPluginOptionValueCount` et `GetPluginOptionValue`.

## Arguments bruts

À `neverc.driver.raw_arguments`, l'artefact est le vecteur argv. La lecture se
fait par indice, et chaque entrée indique d'où elle vient :

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

L'édition est transactionnelle et n'est légale que depuis un intercepteur, car
la mutation est liée à la continuation :

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* ou Abort */
```

## Arguments analysés

`neverc.driver.parsed_arguments` travaille sur des occurrences d'options plutôt
que sur des chaînes, ce qui est exactement ce qu'il faut pour ajouter un drapeau
qui ne doit pas être ré-analysé lexicalement :

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

`GetOptionOccurrenceCount` et `GetOptionOccurrence` lisent ; puis
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence` et
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` modifient.

## Sélection de la chaîne d'outils

La requête décrit ce qui a été demandé et ce que le driver a calculé :

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

Un intercepteur peut ajuster la requête avec `BeginToolChainMutation`,
`SetToolChainTriple`, `SetToolChainCPU`, `SetToolChainFeatures` et
`CommitToolChainMutation`. Un fournisseur, lui, répond directement à la phase
avec `CreateToolChainSelection`, en nommant l'un des identifiants de chaîne
d'outils intégrés ou le sien :

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` relit le résultat et signale `BuiltinProviderUsed` : un
observateur peut ainsi savoir si un plugin a remporté la phase.

## Le graphe d'actions

Un nœud d'action est une étape de compilation typée. Les nœuds référencent les
entrées du driver et d'autres nœuds :

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

Lecture avec `GetDriverInputCount` / `GetDriverInput`, `GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`, et `GetActionRootCount` /
`GetActionRoot`.

Construire un graphe de remplacement passe par un constructeur, puis une seule
publication :

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

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType` et
`SetActionNodeBindArch` modifient un constructeur en cours. Pour ajuster le
graphe existant de l'hôte au lieu de le reconstruire, utilisez
`BeginActionGraphMutation` et `CommitActionGraphMutation` ;
`AbortActionGraphEdit` abandonne l'une ou l'autre forme.

## Le graphe de tâches

Une tâche est une commande à exécuter. `NevercJobDescriptor` en décrit une :

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

Mettez `Kind` à `NEVERC_JOB_PLUGIN` avec un `Callback` et le driver exécute
votre fonction là où il aurait autrement lancé un processus :

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs sont empruntés. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

La lecture du graphe reflète celle du graphe d'actions : `GetJobCount` /
`GetJob`, `GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`,
`GetJobInput` / `GetJobOutput`. Notez que `NevercJob` ne rapporte que des
compteurs — récupérez chaque chaîne ou fichier par indice au lieu d'attendre un
tableau en ligne.

L'édition utilise `CreateJobGraphBuilder` ou `BeginJobGraphMutation`, puis
`AddJob`, `RemoveJob`, `MoveJobBefore`, `ReplaceJob`, `SetJobArgument`,
`SetJobEnvironment`, `SetJobInput`, `SetJobOutput` et
`ReplaceJobDependencies`. Publiez avec `PublishJobGraph` ou
`CommitJobGraphMutation` ; abandonnez avec `AbortJobGraphEdit`.

## Exécuter une tâche

À `neverc.driver.execute_job`, l'artefact d'entrée est un
`NevercJobExecutionRequest` — la tâche accompagnée de ses listes d'arguments,
d'environnement, d'entrées, de sorties et de dépendances entièrement
matérialisées. Un fournisseur exécute la tâche et rapporte le résultat :

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

`OutputSeals` porte les `NevercOutputSealHandle` produits via l'API d'E/S (voir
[Source et E/S](source.fr.md#écrire-des-sorties)) : c'est ainsi que l'hôte confirme que les
fichiers qu'une tâche a prétendu écrire existent réellement avec les empreintes
annoncées. `GetJobResult` lit un résultat validé et, comme la sélection de
chaîne d'outils, rapporte `BuiltinProviderUsed`.

## Exemple complet : observer les arguments, intercepter l'exécution

Condensé depuis [`pluginsdk/examples/DriverTracePlugin.c`]. Le plugin ne garde
aucune variable globale : l'état de processus détient les tables négociées, et
les compteurs par session et par tâche sont récupérés auprès de l'hôte dans
chaque callback.

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

L'enregistrement relie les deux à leurs phases :

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

Compilez et lancez :

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## Règles

- Les mutations d'arguments, d'arguments analysés, de chaîne d'outils, de graphe
  d'actions et de graphe de tâches exigent toutes la
  `NevercPhaseContinuation` de l'intercepteur ; en dehors, elles sont rejetées
  avec `NEVERC_STATUS_WRONG_SCOPE`.
- Appelez `InvokeNext` au plus une fois, et uniquement sur le thread du
  callback.
- Chaque handle de mutation doit atteindre exactement un `Commit*` ou un
  `Abort*`.
- Les vues renvoyées par un appel `Get*` sont empruntées pour la durée du
  callback. Copiez ce que vous devez conserver.
- Un callback `NEVERC_JOB_PLUGIN` ne doit pas lancer le processus que l'hôte
  aurait lancé tout en rapportant aussi le succès du chemin intégré ; déclarez
  `REPLACE` et assumez le résultat.
- Signalez une tâche en échec via
  `NevercJobResultDescriptor.ExecutionFailed` et `ErrorMessage` plutôt que de
  renvoyer un statut non OK pour une tâche qui s'est exécutée et a légitimement
  échoué.

Voir [`PluginDriver.h`] pour les déclarations normatives, [`PhaseSchema.json`] pour
les politiques des phases du driver, et [`coverage.json`] pour les preuves de
test.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
