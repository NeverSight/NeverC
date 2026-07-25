**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# ABI de plugins de NeverC

La primera ABI pública de plugins de NeverC es una interfaz en C puro basada en
fases. Un plugin es un módulo compartido que exporta una sola función, negocia
tablas de capacidades versionadas y se ejecuta dentro de ámbitos explícitos de
Process, Session y Task. Nunca incluye una cabecera de LLVM, nunca enlaza el
compilador y nunca intercambia un tipo de C++ a través de la frontera.

La API prototipo no publicada y su punto de entrada `nevercGetPluginInfo` han
sido **eliminados**. Los binarios prototipo se rechazan con un diagnóstico de
migración; recompile sus fuentes contra las cabeceras públicas. Consulte
[Migración desde la API prototipo](migration-from-prototype.es.md) para el mapeo
completo de antiguo a nuevo.

## Empezar aquí

- [API de Source y E/S](source.es.md)
- [API del preprocesador](prep.es.md)
- [API de AST y semántica](ast-sema.es.md)
- [API de IR](ir.es.md)
- [API de MIR](mir.es.md)
- [APIs de Target, MC, ensamblador y objeto](target-mc-object.es.md)
- [API de DynCode](dyncode.es.md)
- [Convenciones de llamada personalizadas](custom-callconv/README.es.md)
- [Migración desde la API prototipo](migration-from-prototype.es.md)
- [Evidencia de cobertura de fases](coverage.json)

## Modelo de ejecución

El anfitrión conduce el plugin a través de tres ámbitos anidados. Cada ámbito
entrega al plugin un puntero de estado opaco que el propio plugin reserva y
posee, de modo que un plugin bien escrito no necesita ningún estado global
mutable.

| Ámbito | Retrollamadas | Significado |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Un proceso del compilador. Aquí se consultan interfaces y se registran capacidades. |
| Session | `SessionBegin`, `SessionEnd` | Una invocación del controlador. |
| Task | `TaskBegin`, `TaskEnd` | Una unidad de trabajo, identificada por `NevercTaskKind`. |

Los tipos de tarea son `INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`,
`CODEGEN`, `OBJECT` y `DYNCODE`.

El anfitrión llama primero a `ProcessBegin` y luego a `Register` exactamente una
vez. El registro es el único lugar donde pueden añadirse opciones,
observadores, interceptores y proveedores; después el grafo de fases queda
congelado.

## Fases

Una fase es una transición con nombre y versión, de un artefacto de entrada a
uno de salida. NeverC incluye **130 fases integradas** en los dominios de
controlador, source, preprocesador, sintaxis, semántica, IR, codegen, MIR, MC,
ensamblador, objeto, enlazado y dyncode, además de 8 familias de ID de extensión
reservadas para fases definidas por plugins.

Cada fase anuncia una política, y un plugin solo puede engancharse según lo que
esa política permita:

| Bandera de política | Lo que puede hacer un plugin |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | Registrar un observador para notificación de solo lectura. |
| `NEVERC_PHASE_INTERCEPTABLE` | Envolver la fase y decidir si llamar al resto de la cadena. |
| `NEVERC_PHASE_REPLACEABLE` | Registrar un proveedor que suministre la salida por sí mismo. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | Omitir la transición aportando un handle de prueba. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | Nada. Los verificadores y las confirmaciones pertenecen al anfitrión y no se pueden reemplazar, interceptar ni omitir. |

Los observadores se entregan en los puntos que declara la fase:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` y
`NEVERC_OBSERVER_AFTER_COMMIT`.

Un interceptor recibe una `NevercPhaseContinuation`. Debe llamar a `InvokeNext`
**como máximo una vez**, en el hilo de la retrollamada, y después informar
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` o `NEVERC_PHASE_SKIP` en
`NevercPhaseResult.Action`.

La fuente normativa de los ID de fase, políticas, niveles de estabilidad y
compuertas de verificación es
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`. El archivo generado
`PluginPhaseSchema.inc` los expone como constantes de compilación tales como
`NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`.

## Un plugin mínimo completo

Este es `pluginsdk/templates/minimal/Plugin.c`. Se carga, negocia la ABI, no
registra nada y se descarga limpiamente: copie el directorio y hágalo crecer
desde aquí.

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
  /* Registre aquí opciones, observadores, interceptores o proveedores. */
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

`OutPlugin` es un búfer propiedad del llamador. A la entrada, su
`Header.StructSize` es la capacidad escribible; el plugin escribe como mucho esa
cantidad de bytes e informa del tamaño que realmente produjo.

## Negociación de interfaces

Las tablas de capacidades se obtienen por un ID de interfaz de 128 bits, no por
símbolo. Solicite la versión mayor con la que compiló y la menor más baja con la
que puede funcionar:

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
llamar es la regla que hace extensible esta ABI: un anfitrión más nuevo añade
campos al final y un plugin más antiguo sigue funcionando porque nunca lee más
allá del prefijo que verificó. La macro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` aplica la misma prueba a una
estructura recibida.

Las interfaces públicas y sus cabeceras:

| Interfaz | Tabla | Cabecera |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`, `..._SOURCE_LOCATION` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`, `..._PARSER` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`, `..._BUILDER`, `..._ANALYSIS`, `..._PASS`, `..._GEN`, `..._OPTIMIZATION` | Tablas de IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`, `..._TARGET_ABI`, `..._CALLING_CONVENTION` | Tablas de Target | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`, `..._MIR_ANALYSIS`, `..._MIR_PASS`, `..._MIR_PROVIDER` | Tablas de MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`, `..._MC_EMISSION`, `..._MC_PROVIDER`, `..._ASSEMBLY_PROVIDER` | Tablas de MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`, `..._OBJECT_FORMAT`, `..._OBJECT_PHASE` | Tablas de Object | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`, `..._LINK_REGISTRAR`, `..._LINK_PHASE` | Tablas de Link | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`, `..._LTO_REGISTRAR` | Tablas de LTO | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`, `..._DYNCODE_REGISTRAR`, `..._DYNCODE_PHASE` | Tablas de DynCode | `PluginDynCode.h` |

Una interfaz es STABLE (un anfitrión más nuevo solo puede añadir) o LOCKSTEP
(esquemas específicos del destino que deben coincidir exactamente). Compare el
resumen del esquema antes de consumir valores LOCKSTEP.

## Compilación

Incluya la cabecera agregada o solo los dominios que use:

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

Construya un módulo compartido con el propio NeverC:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

O con CMake contra un SDK instalado:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

O con pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Use `.so`, `.dylib` o `.dll` según el anfitrión. El SDK no enlaza LLVM ni el
runtime de NeverC: `NevercPluginSDK::headers` es un objetivo de solo cabeceras.

## Carga y configuración

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Opción | Forma | Propósito |
|---|---|---|
| `-fplugin=<path>` | repetible | Cargar un módulo compartido de plugin. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | repetible | Pasar un valor con espacio de nombres a una opción de plugin registrada. |
| `-fplugin-provider=<phase>:<plugin-id>` | repetible | Elegir qué plugin provee una fase reemplazable. |

El calificador `<plugin-id>:` solo puede omitirse cuando hay exactamente un
plugin activo. Las opciones que un plugin registra con `RegisterOption` también
se aceptan directamente con la grafía declarada, en forma de bandera, unida,
separada o de múltiples argumentos. Dar argumentos de plugin o selecciones de
proveedor sin `-fplugin=` es un error duro, no una omisión silenciosa.

## Reglas de la ABI

- Consulte las tablas mediante `QueryInterface`; exija la misma versión mayor y
  compruebe `StructSize` antes de tocar un campo.
- Inicialice el `Header` y el almacenamiento reservado de cada estructura
  pública. Ponga la estructura a cero y luego fije `StructSize`, `Major`,
  `Minor` y `Flags`.
- Trate los handles y las vistas prestadas como valores opacos con ámbito. Nunca
  conserve un handle de ámbito de tarea más allá de su retrollamada, nunca lo
  use en otra sesión o tarea y nunca fabrique un valor de handle.
- Devuelva `NevercStatus` desde cada retrollamada. No deje que una excepción de
  C++ ni un puntero propiedad del anfitrión crucen la frontera de C.
- Declare el `NevercConcurrencyModel` (`SESSION_SERIAL`, `THREAD_SAFE`,
  `PROCESS_SERIAL`) y el `NevercReentrancyModel` (`NONE`, `ALLOWED`) más
  estrictos que sean **veraces**.
- Realice los cambios de IR, MIR, AST, grafos y artefactos mediante las API
  transaccionales del anfitrión: iniciar una mutación, preparar los cambios y
  después confirmar o abortar. La confirmación verifica y publica de forma
  atómica; una confirmación fallida deja intacto el estado anterior.
- Mantenga el estado mutable en los estados de process/session/task que
  proporciona el anfitrión. El estado global mutable lo comprueba
  `utils/plugin-api/check-global-state.py`.

Las funciones nuevas se añaden al final de tablas de capacidades versionadas de
forma independiente. El prefijo estable de una tabla no cambia dentro de la
primera versión mayor de la ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Estado y diagnósticos

`NevercStatus` lleva un `Code`, unos `Flags` y una palabra `Detail`. Códigos
habituales:

| Código | Significado |
|---|---|
| `NEVERC_STATUS_OK` | Éxito. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Faltaba un puntero o valor requerido, o estaba mal formado. |
| `NEVERC_STATUS_ABI_MISMATCH` | La tabla negociada es demasiado pequeña o la versión mayor difiere. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | El anfitrión no ofrece la capacidad solicitada. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | Un handle se usó fuera de su validez. |
| `NEVERC_STATUS_POLICY_VIOLATION` | La política de la fase no permite la operación. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Un verificador sellado del anfitrión rechazó el producto. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | Cancelación cooperativa o límites de recursos. |

Los bits de bandera (`RECOVERABLE`, `OUTPUT_ALREADY_COMMITTED`,
`OUTPUT_MAY_BE_PARTIAL`, `OUTPUT_RECOVERY_REQUIRED`, `DURABILITY_UNCONFIRMED`)
describen qué le ocurrió a la salida, que es justo lo que un sistema de
construcción necesita para decidir si reintentar es seguro.

Informe de problemas con `NevercCoreAPI.EmitDiagnostic` y un
`NevercDiagnosticDescriptor` que lleve gravedad, código, ID de plugin, ID de
fase, mensaje, notas, ubicación en el fuente, rangos y correcciones. Llame a
`CheckCancelled` antes de trabajo costoso.

## Ejemplos

Construirlos todos:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Cada ejemplo se compila dos veces —una con el compilador C anfitrión configurado
y otra con el NeverC recién construido—, de modo que la ABI queda demostrada por
ambos lados. Los módulos acaban en
`build-neverc/neverc/pluginsdk/examples/host/`.

| Ejemplo | Objetivo CMake | Muestra |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Registro de opciones, observación de fases, interceptación de trabajos |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Un proveedor VFS que sirve una cabecera en memoria |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Interceptación del analizador y mutación atómica del AST |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | Pase IR a nivel de módulo que recorre la lista de funciones con un cursor de valores |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Un pase de función de IR estable |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Un pase MIR estable en el gancho pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Eventos de emisión MC de solo lectura |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Reescritura transaccional del ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Convenciones de llamada dirigidas por datos |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Observación del pipeline de dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Interceptación de la codificación de juego de caracteres de dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Un plugin sin ninguna dependencia del CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Microbenchmark de rendimiento de llamadas de la ABI |

Cargar uno:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Fuentes normativas

| Archivo | Garantiza |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | ID de fases, políticas, estabilidad, compuertas de verificación |
| `pluginsdk/manifest/plugin.json` | Versión de ABI, ID/versiones/estabilidad de interfaces, resúmenes de esquemas, destinos admitidos |
| `pluginsdk/abi/plugin.json` | Tamaño, alineación y desplazamientos medidos de cada estructura pública, por clave de ABI del anfitrión |
| `docs/plugin-api/coverage.json` | Asocia cada fase estable con pruebas positivas, negativas, de reemplazo, de observador y de compuerta sellada |

Así, un SDK puede validarse mecánicamente contra un anfitrión, y la compilación
de un plugin puede afirmar la disposición de sus estructuras frente a la clave
de ABI en la que se cargará.
