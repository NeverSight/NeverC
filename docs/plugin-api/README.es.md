**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# ABI de complementos de NeverC

Un complemento de NeverC es un módulo compartido que exporta exactamente una
función, negocia tablas de capacidades versionadas mediante un identificador
de interfaz de 128 bits y se engancha a un grafo congelado de fases con nombre
del compilador. Toda la interfaz es C11 puro. Un complemento nunca incluye una
cabecera de LLVM, nunca enlaza el compilador y nunca hace cruzar un tipo de
C++ por la frontera.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

Esa firma, declarada en `PluginCore.h`, es todo el contrato de enlazado. Todo
lo demás —leer IR, reescribir un grafo de objetos, sustituir la tubería de
optimización— se alcanza a través de tablas que se le piden al anfitrión por
identificador.

## Guías

| Guía | Cubre |
|---|---|
| [API del driver](driver.es.md) | Línea de órdenes, selección de cadena de herramientas, grafo de acciones, grafo de trabajos |
| [API de fuentes y E/S](source.es.md) | Proveedores VFS, ubicaciones de origen, búferes, sumideros de salida, dependencias |
| [API del preprocesador](prep.es.md) | Tokens, macros, pragmas, inclusiones, consultas de características, 39 tipos de eventos |
| [API de AST y semántica](ast-sema.es.md) | Extensión del analizador, mutación del AST, búsqueda de nombres, tipos, constantes |
| [API de IR](ir.es.md) | Lectura de IR de LLVM, construcción transaccional, análisis, pases, proveedores |
| [API de MIR](mir.es.md) | Funciones máquina, registros, marcos de pila, pases y análisis de MIR |
| [Destino, MC, ensamblador, objeto](target-mc-object.es.md) | Registro de destinos, convenciones de llamada, codificación MC, grafos de objetos |
| [API de enlazado y LTO](link-lto.es.md) | Grafo de enlazado, resolución de símbolos, GC/ICF, proveedores de enlazador y LTO |
| [API de DynCode](dyncode.es.md) | Imágenes planas independientes de la posición, rebajado de importaciones, codificación de juegos de caracteres |
| [Convenciones de llamada personalizadas](custom-callconv/README.es.md) | Complementos de convención de llamada dirigidos por datos |
| [Evidencia de cobertura de fases](coverage.json) | Correspondencia de pruebas para cada fase estable |

## Modelo de ejecución

El anfitrión gobierna un complemento a través de tres ámbitos anidados. Cada
ámbito entrega al complemento un puntero de estado opaco que el propio
complemento reserva y posee, de modo que un complemento bien escrito no
necesita ningún estado global mutable.

| Ámbito | Retrollamadas | Significado |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Un proceso del compilador. Aquí se consultan interfaces y se registran capacidades. |
| Session | `SessionBegin`, `SessionEnd` | Una invocación del driver. |
| Task | `TaskBegin`, `TaskEnd` | Una unidad de trabajo, identificada por `NevercTaskKind`. |

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

En la práctica solo `PluginID` y `Register` son obligatorios; cualquier ranura
de retrollamada puede quedarse en `NULL`. Los tipos de tarea son
`NEVERC_TASK_INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`,
`OBJECT` y `DYNCODE`.

El anfitrión llama primero a `ProcessBegin` y después a `Register` exactamente
una vez. El registro es el único lugar donde pueden añadirse opciones,
observadores, interceptores y proveedores; después el grafo de fases queda
congelado.

El estado se recupera dentro de una retrollamada, no se captura de antemano:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## Fases

Una fase es una transición con nombre y versión desde un artefacto de entrada
hasta un artefacto de salida. NeverC incorpora **130 fases integradas**, más 8
familias de identificadores de extensión reservadas para fases definidas por
complementos:

| Dominio | Fases | Dominio | Fases |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

Las 130 tienen nivel de estabilidad `stable` en la versión mayor 1 del ABI.
Cada fase anuncia una política, y un complemento solo puede engancharse de las
formas que esa política permite:

| Bandera de política | Fases | Qué puede hacer un complemento |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | Registrar un observador para recibir notificaciones de solo lectura. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | Envolver la fase y decidir si se llama al resto de la cadena. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | Registrar un proveedor que produzca él mismo la salida. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | Omitir la transición aportando un manejador de prueba. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | Nada. Los verificadores y las confirmaciones son del anfitrión. |

Las 14 puertas selladas son `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify` y `dyncode.commit`. Se pueden
observar, pero jamás interceptar, sustituir ni omitir.

Los observadores se notifican en los puntos que declara la fase:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` y
`NEVERC_OBSERVER_AFTER_COMMIT`. Un interceptor recibe una
`NevercPhaseContinuation` y debe llamar a `InvokeNext` **como mucho una vez**,
en el hilo de la retrollamada, y luego informar de
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` o `NEVERC_PHASE_SKIP` en
`NevercPhaseResult.Action`.

Toda retrollamada de fase recibe el mismo marco:

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

`Schema/PhaseSchema.json` es la fuente normativa de identificadores de fase,
políticas, niveles de estabilidad y puertas de verificación. El archivo
generado `Schema/PluginPhaseSchema.inc` expone cada uno de ellos como
constante de compilación; para la fase `neverc.ir.pass.pipeline_start`:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT` y las constantes por dominio
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` permiten a un complemento aseverar el
grafo contra el que se compiló.

## Un complemento mínimo completo

Esto es `pluginsdk/templates/minimal/Plugin.c` literalmente. Se carga, negocia
el ABI, no registra nada y se descarga limpiamente: copie el directorio y
haga crecer el complemento a partir de aquí.

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

`OutPlugin` es un búfer que pertenece a quien llama. Al entrar, su
`Header.StructSize` indica la capacidad escribible; el complemento escribe
como mucho esa cantidad de bytes e informa del tamaño que realmente produjo.
Escribir primero el propio `Header` del descriptor y luego truncar la copia
satisface ambas mitades de esa regla.

## Negociación de interfaces

Las tablas de capacidades se obtienen por identificador de interfaz de 128
bits, no por símbolo. Pida la versión mayor contra la que compiló y la versión
menor más baja con la que puede funcionar:

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

Comprobar `TableSize` contra el desplazamiento de la última función que va a
llamar es la regla que hace extensible este ABI: un anfitrión más nuevo añade
campos al final y un complemento más antiguo sigue funcionando porque nunca
lee más allá del prefijo que verificó. La macro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` aplica la misma prueba a una
estructura que usted recibió. La misma firma de `QueryInterface` está también
en `NevercCoreAPI`, así que puede negociar tarde en vez de en la entrada.

Las interfaces públicas, sus tablas y sus macros de identificador:

| Par de macros de interfaz | Tabla | Cabecera |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | seis tablas de IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | cuatro tablas de MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | cuatro tablas de MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | tres tablas de objetos | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | tres tablas de enlazado | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | tres tablas de dyncode | `PluginDynCode.h` |

Cada cabecera define además los `NEVERC_<DOMAIN>_API_MAJOR` y `_MINOR`
correspondientes que debe pasar a `QueryInterface`.

Una interfaz es o bien `NEVERC_INTERFACE_STABLE` (un anfitrión más nuevo solo
puede añadir) o bien `NEVERC_INTERFACE_LOCKSTEP` (esquemas específicos del
destino que deben coincidir exactamente). Compare el resumen del esquema antes
de consumir valores LOCKSTEP.

## Registro

`Register` recibe una `NevercRegistrarAPI` y un `RegistrarContext` opaco:

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

Cada una de estas llamadas toma `RegistrarContext` como primer argumento y un
descriptor puesto a cero como segundo. Cuál de ellas invoques es lo que decide cómo
te trata el anfitrión en la fase:

| Llamada | Descriptor | Callback | La fase debe declarar |
|---|---|---|---|
| `RegisterObserver` | `NevercObserverDescriptor` | `NevercPhaseObserverFn` | `OBSERVABLE` |
| `RegisterInterceptor` | `NevercInterceptorDescriptor` | `NevercPhaseInterceptorFn` | `INTERCEPTABLE` |
| `RegisterProvider` | `NevercProviderDescriptor` | `NevercPhaseProviderFn` | `REPLACEABLE` |
| `RegisterPhase` | `NevercPhaseDescriptor` | — | un ID definido por el complemento |
| `RegisterOption` | `NevercOptionDescriptor` | `Validator` opcional | — |
| `RegisterInterface` | argumentos sueltos | — | — |

Un descriptor que no supera la validación estructural se rechaza en el acto con
`NEVERC_STATUS_INVALID_DESCRIPTOR`. La comprobación de política ocurre cuando el
anfitrión aplica el registro: una fase desconocida, o una que no declara la política
que tu llamada exige, se rechaza ahí. Una compuerta sellada solo admite
observadores.

Los registradores de cada dominio —`NevercIRPassAPI.RegisterPass`,
`NevercTargetAPI.RegisterTarget`, `NevercObjectFormatAPI.RegisterFormat` y los
demás— toman ese mismo `RegistrarContext` como segundo argumento, que es como
el anfitrión atribuye un registro a su complemento.

### Observadores

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

`Points` es una máscara de bits de `NEVERC_OBSERVER_BEFORE` (1),
`NEVERC_OBSERVER_AFTER` (2) y `NEVERC_OBSERVER_AFTER_COMMIT` (4); debe ser distinta
de cero, y el argumento `Point` le indica al callback cuál se disparó. De
`pluginsdk/examples/DriverTracePlugin.c`:

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

`UserData` se devuelve intacto. Establecer `DestroyUserData` —presente en todos los
descriptores de esta sección— hace que el anfitrión libere esa memoria cuando el
registro desaparece, de modo que una reserva por registro no tenga que seguirse en
`Destroy`.

### Interceptores

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

La continuación es todo el resto de la cadena, y el resultado es cómo informas de lo
que has hecho con ella:

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

Las tres acciones no son intercambiables. El anfitrión contrasta el resultado con lo
que realmente hiciste y hace fracasar la cadena con
`NEVERC_STATUS_POLICY_VIOLATION` ante cualquier discrepancia:

| `Action` | `InvokeNext` | `Output` | `Proof` | Exige además |
|---|---|---|---|---|
| `NEVERC_PHASE_CONTINUE` | llamado una vez | vacío | vacío | — |
| `NEVERC_PHASE_REPLACE` | no llamado | establecido | vacío | `REPLACEABLE` |
| `NEVERC_PHASE_SKIP` | no llamado | establecido | establecido | `SKIPPABLE_WITH_PROOF` |

`InvokeNext` puede llamarse como mucho una vez y solo en el hilo del callback: una
segunda llamada es una violación de política, y una llamada desde otro hilo informa
`NEVERC_STATUS_WRONG_SCOPE`. Un interceptor que devuelve `CONTINUE` sin haberlo
llamado también viola la política, porque entonces la fase no produciría nada en
silencio.

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

### Proveedores

Un proveedor sustituye la fase por completo, así que declara además el contrato de
determinismo del que depende la caché de compilación:

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

`ProviderID` debe ser un nombre canónico: como mucho 255 bytes de minúsculas,
dígitos, `.`, `_` y `-`, sin empezar ni terminar en punto y sin contener nunca `..`.
Basta una mayúscula para que el registro se rechace. `Route.Header` debe
inicializarse igual que cualquier otra cabecera de tabla.

Aquí no hay continuación: el callback *es* la fase. Debe informar
`NEVERC_PHASE_REPLACE` con un `Output` y un `Proof` vacío; cualquier otra cosa es una
violación de política.

`FallbackSafe` es la única de estas banderas con un efecto en tiempo de ejecución más
allá del registro contable. Cuando vale `NEVERC_TRUE` y el proveedor falla con un
estado marcado como `NEVERC_STATUS_FLAG_RECOVERABLE`, el anfitrión puede descartar
los efectos parciales y ejecutar en su lugar la implementación integrada. Déjala en
`NEVERC_FALSE` cuando un intento a medias no se pueda deshacer.

### Fases definidas por el complemento

`RegisterPhase` añade una transición que el anfitrión desconoce, que es justamente
para lo que están reservadas las 8 familias de ID de extensión:

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

`Phase`, `InputArtifact` y `OutputArtifact` deben ser todos distintos de cero, y
`Policy` debe ser distinta de cero y contener solo banderas conocidas. Declarar
`ObserverPoints` sin `NEVERC_PHASE_OBSERVABLE` se rechaza, igual que combinar
`NEVERC_PHASE_SEALED_HOST_GATE` con `INTERCEPTABLE`, `REPLACEABLE` o
`SKIPPABLE_WITH_PROOF`: son los mismos invariantes con los que se contrasta el grafo
integrado. Toma el ID de la familia de tu dominio para que no pueda chocar con una
futura fase integrada:

```c
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

/* NEVERC_EXTENSION_FAMILY_COUNT is 8; family 1 is "neverc.ir.extension". */
NevercInterfaceID MyPhase = {NEVERC_EXTENSION_FAMILY_1_ID_HIGH,
                             NEVERC_EXTENSION_FAMILY_1_ID_LOW_MIN};
```

Cada familia publica `_NAMESPACE`, `_ID_HIGH`, `_ID_LOW_MIN` y `_ID_LOW_MAX`, y la
mitad baja es tuya para repartirla dentro de ese rango.

### Publicar una interfaz para otros complementos

`RegisterInterface` es la única llamada que no toma descriptor. Entrega al anfitrión
una tabla propia para que otro complemento pueda alcanzarla mediante el mismo
`QueryInterface` que se usa con las interfaces integradas:

```c
Registrar->RegisterInterface(RegistrarContext, MyInterfaceID,
                             NEVERC_INTERFACE_STABLE, &MyTable,
                             /* Compatibility = */ NULL);
```

Pasa `NEVERC_INTERFACE_LOCKSTEP` cuando la tabla lleve valores de esquema
específicos del destino que no sobrevivirían a un desfase de versiones. Una interfaz
lockstep debe suministrar una `NevercCompatibilityKey`, que ata al consumidor a una
única compilación del productor:

```c
typedef struct NevercCompatibilityKey {
  NevercABITableHeader Header;
  NevercStringView ProducerBuildID;   /* compare against Bootstrap->HostBuildID */
  NevercStringView TargetABIKey;
  uint32_t LLVMMajor;                 /* compare against Bootstrap->LLVMMajor   */
  uint32_t Reserved;
} NevercCompatibilityKey;
```

En un registro lockstep hay que rellenar los tres campos; un ID de compilación vacío,
una clave ABI vacía o un mayor de LLVM igual a cero se rechazan como descriptor
inválido.

## Compilación

Incluya la cabecera agregada o solo los dominios que utilice:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

Construir un módulo compartido con el propio NeverC:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

O contra un SDK instalado con CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

O con pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Use `.so`, `.dylib` o `.dll` según corresponda al anfitrión. El SDK no enlaza
ningún LLVM ni ningún runtime de NeverC: `NevercPluginSDK::headers` es solo de
cabeceras.

## Carga y configuración

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Opción | Forma | Propósito |
|---|---|---|
| `-fplugin=<path>` | repetible | Cargar un módulo compartido de complemento para toda la cadena de herramientas. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | repetible | Pasar un valor con espacio de nombres a una opción de complemento registrada. |
| `-fplugin-provider=<phase>:<plugin-id>` | repetible | Elegir qué complemento provee una fase sustituible. |
| `-fplugin-pass=<dsopath>` | repetible | Cargar un complemento de pase fuera del árbol con ABI de C. |
| `-fplugin-pass-arg=<key>=<value>` | repetible | Pasar un argumento a los complementos de pase con ABI de C. |

El calificador `<plugin-id>:` solo puede omitirse cuando hay exactamente un
complemento activo. Las opciones que un complemento registra con
`RegisterOption` también se aceptan directamente con la grafía declarada, en
forma de bandera, unida, separada o de varios argumentos. Los argumentos de
complemento y las selecciones de proveedor sin un `-fplugin=` correspondiente
son un error grave, no una operación ignorada en silencio.

Una opción registrada puede releerse en cualquier momento mediante la tabla
core:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## Reglas del ABI

- Consulte las tablas de capacidades con `QueryInterface`; exija la versión
  mayor correspondiente y compruebe `StructSize` antes de tocar un campo.
- Inicialice el `Header` y el almacenamiento reservado de cada estructura
  pública. Ponga la estructura a cero y luego fije `StructSize`, `Major`,
  `Minor` y `Flags`.
- Trate los manejadores y las vistas prestadas como valores opacos con ámbito.
  Nunca conserve un manejador de ámbito de tarea más allá de su retrollamada,
  nunca lo use en otra sesión o tarea y nunca fabrique un valor de manejador.
- Devuelva `NevercStatus` desde cada retrollamada. No deje que una excepción
  de C++ ni un puntero propiedad del anfitrión crucen la frontera de C.
- Declare el `NevercConcurrencyModel` más estrecho que sea cierto
  (`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`) y el
  `NevercReentrancyModel` (`NONE`, `ALLOWED`).
- Realice los cambios de IR, MIR, AST, grafos y artefactos mediante las API
  transaccionales del anfitrión: inicie una mutación, prepare los cambios y
  después confirme o aborte. La confirmación verifica y publica de forma
  atómica; una confirmación fallida deja intacto el estado anterior.
- Reserve memoria con `NevercCoreAPI.Allocate` / `Reallocate` / `Deallocate`
  cuando el anfitrión deba contabilizarla.
- Mantenga el estado mutable en el estado de process/session/task que
  proporciona el anfitrión. El estado global mutable lo comprueba
  `utils/plugin-api/check-global-state.py`.

Todas las estructuras públicas se disponen bajo `NEVERC_ABI_PACK_BEGIN`
(empaquetado de 8 bytes) y usan solo tipos de anchura fija. Las funciones
nuevas se añaden al final de tablas de capacidades versionadas de forma
independiente; el prefijo estable de una tabla no cambia dentro de la primera
versión mayor del ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Estados y diagnósticos

`NevercStatus` lleva un `Code`, unos `Flags` y una palabra `Detail`. El
conjunto completo de códigos:

| Código | Significado |
|---|---|
| `NEVERC_STATUS_OK` | Éxito. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Faltaba un puntero o valor obligatorio, o estaba mal formado. |
| `NEVERC_STATUS_ABI_MISMATCH` | La tabla negociada es demasiado pequeña o la versión mayor difiere. |
| `NEVERC_STATUS_MISSING_INTERFACE` | El anfitrión no publica la interfaz solicitada. |
| `NEVERC_STATUS_VERSION_MISMATCH` | No puede satisfacerse la mayor/menor solicitada. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | Un descriptor no superó la validación estructural. |
| `NEVERC_STATUS_DUPLICATE_ID` | Ese identificador ya estaba registrado. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | Falta una dependencia declarada. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | No puede satisfacerse el orden de registro. |
| `NEVERC_STATUS_BUSY` | Un recurso está retenido en otro sitio. |
| `NEVERC_STATUS_CANCELLED` | Se solicitó una cancelación cooperativa. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | Se alcanzó un presupuesto o límite. |
| `NEVERC_STATUS_STALE_HANDLE` | Un manejador sobrevivió al objeto que nombraba. |
| `NEVERC_STATUS_WRONG_SESSION` | Un manejador se usó en otra sesión. |
| `NEVERC_STATUS_WRONG_SCOPE` | Un manejador se usó fuera de su ámbito. |
| `NEVERC_STATUS_WRONG_TYPE` | Un manejador nombraba otro tipo de entidad. |
| `NEVERC_STATUS_INVALID_STATE` | La operación no es legal en el estado actual. |
| `NEVERC_STATUS_POLICY_VIOLATION` | La política de la fase prohíbe la operación. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Un verificador sellado del anfitrión rechazó el producto. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | El anfitrión no puede ofrecer aquí esa capacidad. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | El complemento informó de un fallo genérico. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | Una excepción escapó de una retrollamada del complemento. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | La salida se escribió solo en parte. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | Se rechazó una llamada reentrante. |
| `NEVERC_STATUS_NOT_FOUND` | La entidad nombrada no existe. |

Los bits de bandera describen qué le ocurrió a la salida, que es lo que un
sistema de compilación necesita para decidir si es seguro reintentar:
`NEVERC_STATUS_FLAG_RECOVERABLE`, `_OUTPUT_ALREADY_COMMITTED`,
`_OUTPUT_MAY_BE_PARTIAL`, `_OUTPUT_RECOVERY_REQUIRED` y
`_DURABILITY_UNCONFIRMED`.

Informe de los problemas con `NevercCoreAPI.EmitDiagnostic` y un
`NevercDiagnosticDescriptor` que lleve la gravedad (`NOTE`, `REMARK`,
`WARNING`, `ERROR`, `FATAL`), el código, el identificador del complemento, el
de la fase, el mensaje, las notas, la ubicación de origen, los rangos y las
correcciones. Llame a `CheckCancelled` antes de un trabajo costoso.

## Ejemplos

Construirlos todos:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Cada ejemplo se compila dos veces —una con el compilador de C del anfitrión
configurado y otra con el NeverC recién construido—, de modo que el ABI queda
demostrado desde ambos lados. Los módulos acaban en
`build-neverc/neverc/pluginsdk/examples/host/`.

| Ejemplo | Objetivo de CMake | Muestra |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Registro de opciones, observación de fases, intercepción de trabajos |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Un proveedor VFS que sirve una cabecera en memoria |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Intercepción del analizador y mutación atómica del AST |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | Un pase de IR a nivel de módulo que recorre la lista de funciones con un cursor de valores |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Un pase de IR de función estable |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Un pase de MIR estable en el punto de enganche pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Eventos de emisión de MC de solo lectura |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Reescritura transaccional de ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Convenciones de llamada dirigidas por datos |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Observación de la tubería dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Intercepción de la codificación de juego de caracteres de dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Un complemento con cero dependencias de la CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Microbanco de pruebas del rendimiento de llamadas del ABI |

Cargar uno:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Fuentes normativas

| Archivo | Garantías |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | Identificadores de fase, políticas, estabilidad, puertas de verificación |
| `pluginsdk/manifest/plugin.json` | Versión del ABI, identificadores/versiones/estabilidad de las interfaces, resúmenes de esquema, destinos admitidos |
| `pluginsdk/abi/plugin.json` | Tamaño, alineación y desplazamientos de campo medidos de cada estructura pública, por clave de ABI del anfitrión |
| `docs/plugin-api/coverage.json` | Asocia cada fase estable con pruebas positivas, negativas, de sustitución, de observador y de puerta sellada |

Así, un SDK puede validarse mecánicamente contra un anfitrión, y una
compilación de complemento puede aseverar la disposición de sus estructuras
contra la clave de ABI en la que se cargará.
