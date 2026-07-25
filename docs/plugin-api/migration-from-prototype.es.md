**Idiomas**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# Migración desde la API de plugins prototipo

La API de plugins prototipo nunca publicada — su punto de entrada
`nevercGetPluginInfo`, la única vtable `NevercHostAPI`, las llamadas
`Register*Pass`, los hooks `NEVERC_INTERPOSE_*` y el cargador
`-fplugin-pass=` — se eliminó antes de la primera versión pública. La primera
ABI pública es la ABI de descriptores basada en fases documentada en
[README.md](README.md): los plugins exportan `neverc_plugin_entry` y negocian
tablas de capacidades versionadas de forma independiente.

No hay capa de compatibilidad ni división `v1`/`v2`. Recompile el *código
fuente* del plugin contra las cabeceras públicas; esta página asigna cada
construcción del prototipo a su sustituto de la primera versión, a un cambio
semántico o a una no continuidad explícita.

## Los binarios prototipo se rechazan

Cargar un objeto compartido prototipo falla con un diagnóstico estable:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

Una biblioteca que no exporte ninguno de los dos puntos de entrada falla con
`plugin has no 'neverc_plugin_entry' entry`. No se carga nada hasta que el
código fuente esté portado.

## Punto de entrada

| Prototipo | Primera ABI pública |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

El punto de entrada ya no *devuelve* una estructura por valor. Rellena un
`NevercPluginDescriptor` proporcionado por el llamador, respetando
`OutPlugin->Header.StructSize`, y devuelve un `NevercStatus`. Consulte a
`Bootstrap` las tablas de capacidades que necesite antes de anunciar su
compatibilidad.

## Campos de `NevercPluginInfo`

| Campo del prototipo | Correspondencia en la primera versión |
|---|---|
| `APIVersion` | `Descriptor.Header` (`NevercABITableHeader` con `StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`), más un `Descriptor.PluginID` estable en DNS inverso usado como clave del estado de cada ámbito |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`, más las retrollamadas de ciclo de vida `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(sin equivalente en el prototipo)* | `Descriptor.Concurrency` y `Descriptor.Reentrancy` deben declararse con veracidad (por ejemplo `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## Acceso al anfitrión: una vtable → tablas de capacidades

El prototipo pasaba a cada retrollamada una única vtable `NevercHostAPI` de más
de 200 entradas y protegía los campos nuevos con `NEVERC_API_FN`. La primera
versión la sustituye por tablas de capacidades versionadas de forma
independiente y consultadas bajo demanda:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

Exija la versión mayor correspondiente y compruebe `TableSize` con `offsetof`
antes de leer un campo. Las interfaces se delimitan por dominio: Core, Driver,
Source, Prep, AST, Sema, IR, MIR, Target, MC, Object, Link, LTO y DynCode.

## Registro: `Register*Pass` + hooks → observadores/interceptores/proveedores

El registro del prototipo asociaba una retrollamada a un hook:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

La primera versión registra, dentro de `Register`, un manejador tipado sobre
una fase identificada por un `NevercInterfaceID` de 128 bits:

| Llamada del prototipo | Llamada al registrar en la primera versión |
|---|---|
| pase de solo lectura | `Registrar->RegisterObserver(NevercObserverDescriptor)` con los puntos `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` |
| pase que envuelve o cortocircuita una fase | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`; llame a `Continuation->InvokeNext` como mucho una vez y fije `OutResult->Action` |
| pase que sustituye una transformación integrada | `Registrar->RegisterProvider(...)` sobre una fase `REPLACEABLE` |
| lectura de `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)` para declarar una opción real del controlador |

Un «pase de módulo en `PRE_OPT`» del prototipo pasa a ser un observador,
interceptor o proveedor en la fase de IR `neverc.ir.pass.pre_opt`.

## Correspondencia hook → fase

| Hook del prototipo | Fase de la primera versión (nombre) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | fases LTO `neverc.link.lto_resolve` / `neverc.link.lto_generate` (véase [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `neverc.link.layout` observada en `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | las fases dyncode tipadas de [dyncode.md](dyncode.md) |

La lista normativa de identificadores de fase, políticas, niveles de
estabilidad y puertas de verificación es
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`; el contrato de
cobertura ejecutable es [coverage.json](coverage.json). Un hook que antes era
un único punto puede corresponder a más de un identificador de fase, cada uno
con su propia política y prueba.

## Retrollamadas de pase, handles y ediciones de bytes

| Prototipo | Primera versión |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` y similares | las retrollamadas reciben un `NevercPhaseFrame`; los objetos de IR/MIR/AST/grafo son handles tipados, delimitados y opacos obtenidos de la tabla de capacidades correspondiente (véanse [ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md)) |
| `NevercValueRef` genérico | eliminado en favor de handles de IR tipados |
| mutación in situ de un `Ref` vivo | todos los cambios pasan por las API transaccionales del anfitrión |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | eliminado; las ediciones de bytes de dyncode usan el constructor de imagen verificado (read/write/insert/append/resize), véase [dyncode.md](dyncode.md) |

Los handles y las vistas prestadas solo son válidos en el ámbito de la
retrollamada, exactamente igual que antes; no los guarde en caché después de
que la retrollamada retorne.

## Capas de conveniencia eliminadas

El prototipo incluía utilidades de propósito general en la vtable. **No**
forman parte de la primera ABI pública:

| Prototipo | Primera versión |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | no se mantienen; use `Core->Allocate`/`Core->Deallocate` con sus propios contenedores, o las API de dominio tipadas |
| macros `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` | sustituidas por la iteración tipada de la tabla de capacidades de cada dominio |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | declare opciones con `RegisterOption` y léalas mediante la API Driver |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## Carga y línea de órdenes

| Prototipo | Primera versión |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | la grafía de opción que declare en `RegisterOption` (por ejemplo `--driver-trace` o `--my-opt=value`) |
| dos cargadores (`-fplugin` frente a `-fplugin-pass`) | un solo cargador; un módulo se entrega a un único cargador |

## Versionado

El prototipo dependía de una única vtable en crecimiento monótono más las
guardas `NEVERC_API_FN`. En la primera versión cada tabla de capacidades se
versiona por separado: exija la mayor correspondiente y compruebe
`StructSize`/`TableSize` antes de leer un campo añadido. Las funciones nuevas
se añaden tras el prefijo estable de una tabla dentro de la primera mayor de
ABI, de modo que un plugin construido contra una menor anterior sigue
funcionando con un anfitrión más reciente.

## Ejemplo trabajado

`pluginsdk/examples/DriverTracePlugin.c` muestra la forma completa de la
primera versión: el descriptor `neverc_plugin_entry`, el ciclo de vida
`ProcessBegin`/`Session`/`Task`, un `RegisterOption` para un indicador real de
línea de órdenes, un `RegisterObserver` sobre `neverc.driver.raw_arguments` y
un `RegisterInterceptor` sobre `neverc.driver.execute_job` que llama a
`InvokeNext` exactamente una vez. `pluginsdk/examples/ExamplePlugin.c` cubre
las fases de IR, MIR, object y link.
