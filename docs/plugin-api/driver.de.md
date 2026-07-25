**Sprachen**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC Plugin-Driver-API

Der Driver verwandelt eine Kommandozeile in eine Menge ausgeführter Jobs.
[`PluginDriver.h`] legt diese Pipeline als sechs Phasen und eine
Capability-Tabelle `NevercDriverAPI` offen, sodass ein Plugin Argumente
umschreiben, eine Toolchain wählen, den Aktionsgraphen umbauen, Jobs hinzufügen
oder ersetzen und einen Job sogar prozessintern ausführen kann, statt einen
Prozess zu starten.

## Schnittstelle

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` ist eine flache Tabelle mit 67 Funktions-Slots, gegliedert in
fünf Bereiche: rohe Argumente, geparste Optionen, Toolchain-Auswahl,
Aktionsgraph und Job-Graph. Prüfen Sie `TableSize` gegen den Offset des letzten
Slots, den Sie verwenden — das aktuelle Ende ist `GetJobResult`.

## Die sechs Driver-Phasen

| Phase | Policy | Eingabe → Ausgabe |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | geparste Optionsliste → geparste Optionsliste |
| `neverc.driver.select_toolchain` | zusätzlich REPLACEABLE | Toolchain-Anfrage → Toolchain-Auswahl |
| `neverc.driver.build_actions` | zusätzlich REPLACEABLE | Anfrage → Aktionsgraph |
| `neverc.driver.build_jobs` | zusätzlich REPLACEABLE | Aktionsgraph → Job-Graph |
| `neverc.driver.execute_job` | zusätzlich REPLACEABLE | Job-Ausführungsanfrage → Job-Ergebnis |

Die zugehörigen Makros folgen dem üblichen Muster:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## Eine Option registrieren

Optionen werden genau einmal während `Register` deklariert; danach akzeptiert
der Driver sie auf der Kommandozeile so, als wären sie eingebaut.

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

Aus [`pluginsdk/examples/DriverTracePlugin.c`]:

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

`Validator` wird pro Vorkommen mit einem `NevercOptionValidationContext`
aufgerufen, der Plugin-ID, Schreibweise, Ziel-Triple und Vorkommensindex trägt.
Ein Wert lässt sich damit mit einer echten Diagnose ablehnen, statt später zu
scheitern. `TargetPredicate` beschränkt eine Option auf passende Triples. Werte
liest man mit `NevercCoreAPI.GetPluginOptionValueCount` und
`GetPluginOptionValue` zurück.

## Rohe Argumente

Bei `neverc.driver.raw_arguments` ist das Artefakt der argv-Vektor. Gelesen wird
indexbasiert, und jeder Eintrag meldet seine Herkunft:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

Das Bearbeiten ist transaktional und nur aus einem Interceptor heraus zulässig,
weil die Mutation an die Continuation gebunden ist:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* oder Abort */
```

## Geparste Argumente

`neverc.driver.parsed_arguments` arbeitet auf Optionsvorkommen statt auf
Zeichenketten — genau das, was man braucht, wenn man ein Flag hinzufügt, das
nicht erneut lexikalisch zerlegt werden darf:

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

`GetOptionOccurrenceCount` und `GetOptionOccurrence` lesen; danach bearbeiten
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence` sowie
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation`.

## Toolchain-Auswahl

Die Anfrage beschreibt, was verlangt wurde und was der Driver berechnet hat:

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

Ein Interceptor kann die Anfrage mit `BeginToolChainMutation`,
`SetToolChainTriple`, `SetToolChainCPU`, `SetToolChainFeatures` und
`CommitToolChainMutation` anpassen. Ein Provider beantwortet die Phase
stattdessen direkt mit `CreateToolChainSelection` und benennt eine der
eingebauten Toolchain-IDs oder seine eigene:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` liest das Ergebnis zurück und meldet
`BuiltinProviderUsed`, sodass ein Observer erkennt, ob ein Plugin die Phase für
sich entschieden hat.

## Der Aktionsgraph

Ein Aktionsknoten ist ein typisierter Kompilierschritt. Knoten referenzieren
Driver-Eingaben und andere Knoten:

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

Gelesen wird mit `GetDriverInputCount` / `GetDriverInput`,
`GetActionNodeCount` / `GetActionNode` / `GetActionNodeInput` sowie
`GetActionRootCount` / `GetActionRoot`.

Ein Ersatzgraph entsteht über einen Builder und wird einmal veröffentlicht:

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

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType` und
`SetActionNodeBindArch` bearbeiten einen laufenden Builder. Um den bestehenden
Graphen des Hosts anzupassen, statt ihn neu zu bauen, verwenden Sie
`BeginActionGraphMutation` und `CommitActionGraphMutation`;
`AbortActionGraphEdit` verwirft beide Formen.

## Der Job-Graph

Ein Job ist ein auszuführendes Kommando. `NevercJobDescriptor` beschreibt eines:

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

Setzen Sie `Kind` auf `NEVERC_JOB_PLUGIN` und geben Sie einen `Callback` an —
dann führt der Driver Ihre Funktion dort aus, wo er sonst einen Prozess starten
würde:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs sind geliehen. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

Das Lesen des Graphen entspricht dem Aktionsgraphen: `GetJobCount` / `GetJob`,
`GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`, `GetJobInput` /
`GetJobOutput`. Beachten Sie, dass `NevercJob` nur Anzahlen meldet — holen Sie
jede Zeichenkette und jede Datei über den Index, statt ein eingebettetes Array
zu erwarten.

Bearbeitet wird über `CreateJobGraphBuilder` oder `BeginJobGraphMutation` und
dann `AddJob`, `RemoveJob`, `MoveJobBefore`, `ReplaceJob`, `SetJobArgument`,
`SetJobEnvironment`, `SetJobInput`, `SetJobOutput` und
`ReplaceJobDependencies`. Veröffentlichen mit `PublishJobGraph` oder
`CommitJobGraphMutation`; verwerfen mit `AbortJobGraphEdit`.

## Einen Job ausführen

Bei `neverc.driver.execute_job` ist das Eingabeartefakt ein
`NevercJobExecutionRequest` — der Job samt seiner vollständig materialisierten
Argument-, Umgebungs-, Eingabe-, Ausgabe- und Abhängigkeitslisten. Ein Provider
führt den Job aus und meldet das Ergebnis:

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

`OutputSeals` trägt die über die I/O-API erzeugten `NevercOutputSealHandle`s
(siehe [Source und I/O](source.de.md)). So bestätigt der Host, dass die Dateien,
die ein Job zu schreiben behauptete, tatsächlich mit den gemeldeten Digests
existieren. `GetJobResult` liest ein committetes Ergebnis und meldet wie die
Toolchain-Auswahl `BuiltinProviderUsed`.

## Durchgerechnetes Beispiel: Argumente beobachten, Job-Ausführung abfangen

Verdichtet aus [`pluginsdk/examples/DriverTracePlugin.c`]. Das Plugin hält keine
globalen Variablen: Der Prozesszustand hält die ausgehandelten Tabellen, und die
Zähler pro Session und pro Task werden in jedem Callback vom Host geholt.

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

Die Registrierung verdrahtet beide mit ihren Phasen:

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

Bauen und ausführen:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## Regeln

- Mutationen an Argumenten, geparsten Argumenten, Toolchain, Aktionsgraph und
  Job-Graph benötigen alle die `NevercPhaseContinuation` des Interceptors;
  außerhalb davon werden sie mit `NEVERC_STATUS_WRONG_SCOPE` abgewiesen.
- Rufen Sie `InvokeNext` höchstens einmal und nur auf dem Callback-Thread auf.
- Jedes Mutations-Handle muss genau ein `Commit*` oder `Abort*` erreichen.
- Von einem `Get*`-Aufruf zurückgegebene Sichten sind für die Dauer des
  Callbacks geliehen. Kopieren Sie, was Sie behalten wollen.
- Ein `NEVERC_JOB_PLUGIN`-Callback darf nicht den Prozess starten, den der Host
  gestartet hätte, und zusätzlich Erfolg für den eingebauten Pfad melden;
  deklarieren Sie `REPLACE` und verantworten Sie das Ergebnis selbst.
- Melden Sie einen fehlgeschlagenen Job über
  `NevercJobResultDescriptor.ExecutionFailed` und `ErrorMessage`, statt für
  einen Job, der lief und berechtigt fehlschlug, einen Nicht-OK-Status
  zurückzugeben.

Die normativen Deklarationen stehen in [`PluginDriver.h`], die Policies der
Driver-Phasen in [`PhaseSchema.json`] und die Testnachweise in [`coverage.json`].

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
