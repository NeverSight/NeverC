**Idiomas**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API Driver de plugins de NeverC

El driver convierte una línea de órdenes en un conjunto de trabajos ejecutados.
[`PluginDriver.h`] expone esa canalización como seis fases y una tabla de
capacidades, `NevercDriverAPI`, de modo que un plugin puede reescribir
argumentos, elegir una cadena de herramientas, reestructurar el grafo de
acciones, añadir o sustituir trabajos e incluso ejecutar un trabajo en el propio
proceso en lugar de lanzar uno nuevo.

## Interfaz

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` es una única tabla plana de 67 ranuras de función agrupadas en
cinco áreas: argumentos en bruto, opciones analizadas, selección de cadena de
herramientas, grafo de acciones y grafo de trabajos. Valide `TableSize` frente
al desplazamiento de la última ranura que use: la cola actual es
`GetJobResult`.

## Las seis fases del driver

| Fase | Política | Entrada → salida |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | lista de opciones analizadas → lista de opciones analizadas |
| `neverc.driver.select_toolchain` | además REPLACEABLE | petición de cadena → selección de cadena |
| `neverc.driver.build_actions` | además REPLACEABLE | petición → grafo de acciones |
| `neverc.driver.build_jobs` | además REPLACEABLE | grafo de acciones → grafo de trabajos |
| `neverc.driver.execute_job` | además REPLACEABLE | petición de ejecución → resultado del trabajo |

Sus macros siguen el patrón habitual:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## Registrar una opción

Las opciones se declaran una sola vez, durante `Register`, y a partir de ahí el
driver las acepta en la línea de órdenes exactamente como si fueran
incorporadas.

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

De [`pluginsdk/examples/DriverTracePlugin.c`]:

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

`Validator` se invoca en cada aparición con un
`NevercOptionValidationContext` que lleva el identificador del plugin, la
grafía, el triple objetivo y el índice de aparición, de forma que un valor se
puede rechazar con un diagnóstico real en vez de fallar más tarde.
`TargetPredicate` restringe una opción a los triples coincidentes. Los valores
se releen con `NevercCoreAPI.GetPluginOptionValueCount` y
`GetPluginOptionValue`.

## Argumentos en bruto

En `neverc.driver.raw_arguments` el artefacto es el vector argv. La lectura se
hace por índice y cada entrada informa de su procedencia:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

La edición es transaccional y solo es legal desde un interceptor, porque la
mutación queda ligada a la continuación:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* o Abort */
```

## Argumentos analizados

`neverc.driver.parsed_arguments` trabaja sobre apariciones de opciones en vez de
cadenas, que es justo lo que se necesita al añadir un indicador que no debe
volver a analizarse léxicamente:

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

`GetOptionOccurrenceCount` y `GetOptionOccurrence` leen; después
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence` y
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` editan.

## Selección de la cadena de herramientas

La petición describe qué se pidió y qué calculó el driver:

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

Un interceptor puede ajustar la petición con `BeginToolChainMutation`,
`SetToolChainTriple`, `SetToolChainCPU`, `SetToolChainFeatures` y
`CommitToolChainMutation`. Un proveedor, en cambio, responde a la fase por
completo con `CreateToolChainSelection`, nombrando uno de los identificadores de
cadena incorporados o el suyo propio:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` relee el resultado e informa de `BuiltinProviderUsed`,
así que un observador puede saber si un plugin ganó la fase.

## El grafo de acciones

Un nodo de acción es un paso de compilación tipado. Los nodos referencian
entradas del driver y otros nodos:

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

Lectura con `GetDriverInputCount` / `GetDriverInput`, `GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`, y `GetActionRootCount` /
`GetActionRoot`.

Construir un grafo de reemplazo pasa por un constructor y una única publicación:

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

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType` y
`SetActionNodeBindArch` editan un constructor en curso. Para ajustar el grafo
existente del anfitrión en lugar de reconstruirlo, use
`BeginActionGraphMutation` y `CommitActionGraphMutation`;
`AbortActionGraphEdit` descarta cualquiera de las dos formas.

## El grafo de trabajos

Un trabajo es una orden que ejecutar. `NevercJobDescriptor` describe uno:

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

Ponga `Kind` en `NEVERC_JOB_PLUGIN` con un `Callback` y el driver ejecutará su
función donde de otro modo lanzaría un proceso:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs son prestados. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

La lectura del grafo refleja la del grafo de acciones: `GetJobCount` / `GetJob`,
`GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`, `GetJobInput` /
`GetJobOutput`. Tenga en cuenta que `NevercJob` solo informa de recuentos:
obtenga cada cadena o archivo por índice en lugar de esperar un arreglo en
línea.

La edición usa `CreateJobGraphBuilder` o `BeginJobGraphMutation` y después
`AddJob`, `RemoveJob`, `MoveJobBefore`, `ReplaceJob`, `SetJobArgument`,
`SetJobEnvironment`, `SetJobInput`, `SetJobOutput` y
`ReplaceJobDependencies`. Publique con `PublishJobGraph` o
`CommitJobGraphMutation`; descarte con `AbortJobGraphEdit`.

## Ejecutar un trabajo

En `neverc.driver.execute_job` el artefacto de entrada es un
`NevercJobExecutionRequest`: el trabajo más sus listas de argumentos, entorno,
entradas, salidas y dependencias totalmente materializadas. Un proveedor ejecuta
el trabajo e informa del resultado:

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

`OutputSeals` transporta los `NevercOutputSealHandle` producidos a través de la
API de E/S (véase [Source y E/S](source.es.md)), que es como el anfitrión
confirma que los archivos que un trabajo afirmó escribir existen realmente con
los resúmenes informados. `GetJobResult` lee un resultado confirmado y, igual
que la selección de cadena, informa de `BuiltinProviderUsed`.

## Ejemplo completo: observar argumentos, interceptar la ejecución

Condensado de [`pluginsdk/examples/DriverTracePlugin.c`]. El plugin no guarda
variables globales: el estado de proceso conserva las tablas negociadas y los
contadores por sesión y por tarea se piden al anfitrión dentro de cada callback.

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

El registro conecta ambos con sus fases:

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

Compílelo y ejecútelo:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## Reglas

- Las mutaciones de argumentos, argumentos analizados, cadena de herramientas,
  grafo de acciones y grafo de trabajos requieren todas la
  `NevercPhaseContinuation` del interceptor; fuera de él se rechazan con
  `NEVERC_STATUS_WRONG_SCOPE`.
- Llame a `InvokeNext` como máximo una vez y solo en el hilo del callback.
- Todo handle de mutación debe alcanzar exactamente un `Commit*` o un `Abort*`.
- Las vistas devueltas por una llamada `Get*` están prestadas mientras dure el
  callback. Copie lo que necesite conservar.
- Un callback `NEVERC_JOB_PLUGIN` no debe lanzar el proceso que el anfitrión
  habría lanzado y además informar del éxito de la ruta incorporada; declare
  `REPLACE` y asuma el resultado.
- Informe de un trabajo fallido mediante
  `NevercJobResultDescriptor.ExecutionFailed` y `ErrorMessage` en lugar de
  devolver un estado distinto de OK para un trabajo que se ejecutó y falló
  legítimamente.

Consulte [`PluginDriver.h`] para las declaraciones normativas,
[`PhaseSchema.json`] para las políticas de las fases del driver y
[`coverage.json`] para las pruebas de cobertura.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
