**Idiomas**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API de fuentes y E/S de los plugins de NeverC

`PluginSource.h` publica dos tablas. `NevercIOAPI` es el sistema de archivos:
proveedores de archivos virtuales, lecturas, recorridos de directorios,
sumideros de salida y registro de dependencias. `NevercSourceLocationAPI`
devuelve las posiciones internas del compilador a archivos, líneas y al texto
tal como está escrito. Entre las dos, un plugin puede servir una cabecera que
solo existe en memoria, resolver una expansión de macro hasta su lugar de
escritura, o escribir una salida lateral que participa en la contabilidad de
durabilidad de la compilación.

## Interfaces

```c
#include "neverc/Plugin/PluginSource.h"
```

| Interfaz | Tabla | Macros de versión |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` y `_MINOR` son alias del par source-location.

## Las tres fases de fuente

| Fase | Política | Significado |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | Convertir una entrada del controlador en una entrada de fuente |
| `neverc.source.open` | además REPLACEABLE | Producir la unidad de fuente de una entrada |
| `neverc.source.after_open` | OBSERVABLE | Aviso de que una unidad ya está disponible |

Como `neverc.source.open` es reemplazable, un proveedor puede devolver una
unidad cuyos bytes ha sintetizado él mismo: esa es la manera admitida de
inyectar código generado sin tocar el disco.

## Proveedores de sistema de archivos virtual

Un proveedor VFS reclama un prefijo de ruta y responde a las cuatro preguntas
que el compilador hace sobre un archivo.

```c
typedef struct NevercVFSProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercStringView RoutePrefix;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint64_t Reserved;
  NevercVFSPathPredicateFn MatchesPath;
  NevercVFSProviderStatusFn Status;
  NevercVFSProviderOpenReadFn OpenRead;
  NevercVFSProviderReadDirectoryFn ReadDirectory;
  NevercVFSProviderCanonicalizeFn Canonicalize;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercVFSProviderDescriptor;
```

Cada devolución de llamada rellena un resultado cuyo `Disposition` indica si el
proveedor atendió la petición:

```c
static NevercStatus NEVERC_CALL
open_read(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  static const char Header[] = "#define GENERATED 1\n";
  if (!path_matches(Path)) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition   = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Status.Type   = NEVERC_VFS_FILE_REGULAR;
  OutResult->Status.Size   = sizeof(Header) - 1;
  OutResult->Content.Data  = (const uint8_t *)Header;
  OutResult->Content.Length = sizeof(Header) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}
```

Devolver `NEVERC_VFS_RESULT_NOT_HANDLED` pasa al siguiente proveedor y, al
final, al sistema de archivos real. Los tipos de archivo son
`NEVERC_VFS_FILE_UNKNOWN`, `REGULAR`, `DIRECTORY`, `SYMLINK` y `OTHER`.

El registro se hace durante `Register`:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

Para un único archivo en memoria que solo debe existir durante una sesión, el
proveedor sobra:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
es un proveedor completo y funcional.

## Leer archivos

```c
NevercVFSStatus Status;
IO->Stat(IO->Context, Task, Path, &Status);

NevercFileHandle File;
IO->OpenFileForRead(IO->Context, Task, Path, &File);

NevercBufferHandle Buffer;
IO->ReadFile(IO->Context, Task, File, /*Offset=*/0, /*Length=*/Status.Size,
             &Buffer);

NevercBufferView View;
IO->GetBufferView(IO->Context, Task, Buffer, &View);
/* View.Data / View.Length / View.NullTerminated */

IO->ReleaseBuffer(IO->Context, Task, Buffer);
IO->CloseFile(IO->Context, Task, File);
```

`CopyBuffer` convierte bytes de su propiedad en un búfer del anfitrión,
`Canonicalize` resuelve una ruta, y `GetWorkingDirectory` /
`SetWorkingDirectory` manejan el directorio actual de la tarea. Los directorios
se recorren con `OpenDirectory`, `ReadDirectory` (que al terminar pone
`OutHasEntry` a `NEVERC_FALSE`) y `CloseDirectory`.

Los códigos de error de E/S se informan en `NevercStatus.Detail`:
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH` e `IO`.

## Escribir salidas

Las salidas son transaccionales. Se abre un sumidero, se escribe y luego se
cierra para obtener un sello: un tamaño y un resumen de 32 bytes que el sistema
de compilación puede verificar.

```c
NevercOutputSinkHandle Sink;
IO->BeginFileOutput(IO->Context, Task, SV("out.json"), /*SizeBudget=*/0, &Sink);
IO->OutputWrite(IO->Context, Task, Sink, Bytes);
IO->OutputMetadataSet(IO->Context, Task, Sink, SV("content-type"),
                      SV("application/json"));

NevercOutputSeal Seal = {0};
Seal.Header = (NevercABITableHeader){sizeof(Seal), NEVERC_IO_API_MAJOR,
                                     NEVERC_IO_API_MINOR, 0};
IO->OutputFinish(IO->Context, Task, Sink, &Seal);
```

| Función | Propósito |
|---|---|
| `BeginMemoryOutput` | Sumidero respaldado por memoria, con nombre lógico |
| `BeginFileOutput` | Sumidero que aterriza atómicamente en una ruta final |
| `BeginStreamOutput` | Sumidero sobre `NEVERC_OUTPUT_STREAM_STDOUT` o `_STDERR` |
| `OutputWrite`, `OutputWriteAt` | Añadir, o escribir en un desplazamiento |
| `OutputTell`, `OutputTruncate` | Control de posición y tamaño |
| `OutputMetadataSet` | Adjuntar un par clave/valor a la salida |
| `OutputFinish` | Sellar la salida y producir un `NevercOutputSeal` |
| `OutputAbort` | Descartar todo lo escrito |
| `OutputGetSummary` | Inspeccionar estado, banderas, tamaño y resumen en cualquier momento |

`NevercOutputSummary.State` recorre `NEVERC_OUTPUT_OPEN`, `FINISHED`,
`COMMITTED`, `ABORTED` o `FAILED_PARTIAL`, y `Flags` registra `PUBLISHED`,
`DURABLE`, `MAY_BE_PARTIAL`, `RECOVERY_REQUIRED` y `DURABILITY_UNCONFIRMED`.
Esas banderas son la misma información que el controlador expone en
`NevercStatus.Flags`, de modo que un fallo a mitad de escritura se distingue de
un error limpio.

Un `SizeBudget` de cero significa sin límite; un presupuesto distinto de cero
hace que un exceso falle con `NEVERC_STATUS_RESOURCE_EXHAUSTED` en lugar de
llenar un disco.

## Registrar dependencias

Si un plugin lee algo que el sistema de compilación debería seguir, dígalo. De
lo contrario, una compilación incremental no reconstruirá cuando esa entrada
cambie.

```c
NevercDependencyDescriptor Dependency = {0};
Dependency.Header = (NevercABITableHeader){sizeof(Dependency),
                                           NEVERC_IO_API_MAJOR,
                                           NEVERC_IO_API_MINOR, 0};
Dependency.CanonicalPath = SV("/etc/mytool/rules.txt");
Dependency.ContentDigest = Digest;
Dependency.Kind          = NEVERC_INPUT_DEPENDENCY_RESOURCE;
Dependency.System        = NEVERC_FALSE;
Dependency.ProviderID    = SV("com.example.myplugin");

NevercDependencyHandle Handle;
IO->RecordDependency(IO->Context, Task, &Dependency, &Handle);
```

Los tipos son `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`,
`RESOURCE`, `TOOL` y `PLUGIN`.

## Ubicaciones de fuente

Una `NevercSourceLocation` es opaca. La tabla de ubicaciones la convierte en
algo que se puede imprimir o comparar.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind es NEVERC_SOURCE_LOCATION_FILE o _MACRO;
   a continuación van Info.FileOffset, Info.Line, Info.Column. */
```

Cuatro transformaciones se mueven entre las vistas de una ubicación, y todas
comparten la firma `NevercTransformSourceLocationFn`:

| Función | Devuelve |
|---|---|
| `GetSpellingLocation` | Dónde están escritos realmente los caracteres del token |
| `GetExpansionLocation` | Dónde aparece la expansión de macro en la fuente |
| `GetFileLocation` | La ubicación de archivo más cercana |
| `GetIncludeLocation` | El `#include` que trajo el archivo |
| `GetTokenEnd` | Justo después del último carácter del token |

`GetPresumedLocation` aplica las directivas `#line` y da un nombre de archivo,
línea, columna y ubicación de inclusión. `GetLocationFile` junto con
`GetFileInfo` proporciona la ruta canónica, el tamaño, la fecha de
modificación, el identificador único y si el archivo es de usuario, de sistema
o de sistema extern-C:

```c
typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;      /* {Device, File} */
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;
```

Los rangos se leen con `GetRangeInfo` (que informa de `Begin`, `End` y de si el
rango es `NEVERC_SOURCE_RANGE_CHARACTER` o `_TOKEN`), y los bytes en sí con
`GetSourceText` o `GetCharacterData`.

Cuando se necesitan muchas ubicaciones a la vez —una pasada de diagnóstico sobre
una función entera, por ejemplo— use la forma por lotes en lugar de una llamada
por ubicación:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## Unidades de fuente

La vista de una entrada y sus bytes a nivel de fase:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind (FILE o BUFFER), .Language, .System, .Preprocessed */
```

Un proveedor para `neverc.source.open` responde con una unidad respaldada por
memoria:

```c
NevercMemorySourceUnitDescriptor Unit = {0};
Unit.Header = (NevercABITableHeader){sizeof(Unit),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Unit.LogicalPath      = SV("/virtual/generated.c");
Unit.CanonicalIdentity = SV("com.example:generated:v1");
Unit.Content          = Bytes;
Unit.ProviderID       = SV("com.example.myplugin");
Unit.Deterministic    = NEVERC_TRUE;
Unit.Cacheable        = NEVERC_TRUE;

NevercArtifactHandle Output;
Source->CreateMemorySourceUnit(Source->Context, Frame, Frame->Input, &Unit,
                               &Output);
```

La caché se indexa por `CanonicalIdentity`, así que debe cambiar siempre que
cambie el contenido. `GetSourceUnit` vuelve a leer una unidad e informa además
de `MemoryBacked`.

## Reglas

- Los búferes de `ReadFile`, `CopyBuffer` y `PathToBuffer` pertenecen al
  anfitrión; libere cada uno con `ReleaseBuffer`.
- Cada `OpenFileForRead` necesita un `CloseFile`; cada `OpenDirectory`, un
  `CloseDirectory`; cada sumidero de salida, un `OutputFinish` o un
  `OutputAbort`.
- Las vistas dentro de `NevercFileInfo`, `NevercVFSStatus` y los resultados de
  ubicación están prestadas solo durante la devolución de llamada.
- La devolución de llamada de un proveedor VFS se ejecuta en el hilo de la tarea
  y no debe volver a llamar al compilador; responda con los datos que ya tiene.
- Declare `Deterministic` y `Cacheable` con honestidad. Un proveedor que lee el
  reloj o el entorno y afirma ser determinista producirá una caché de
  compilación envenenada.
- `AddMemoryFile` tiene alcance de sesión; cuando el contenido depende de la
  tarea, el proveedor es la herramienta correcta.

Consulte `PluginSource.h` para las declaraciones normativas y
`pluginsdk/examples/VirtualHeaderPlugin.c` para un proveedor completo.
