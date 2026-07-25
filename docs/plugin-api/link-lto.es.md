**Idiomas**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API de enlazado y LTO de plugins de NeverC

El enlazado se modela como una **máquina de estados sobre un único grafo**.
[`PluginLink.h`] expone ese grafo —entradas, secciones, átomos, símbolos, aristas,
COMDAT, importaciones, exportaciones, registros de desenrollado, sintéticos y
restricciones de disposición— junto con las veinte fases que lo llevan de una
lista de archivos a una imagen binaria confirmada. [`PluginLTO.h`] cubre las dos
fases intermedias donde el bitcode se convierte en objetos.

Un plugin puede observar cada paso, interceptar la mayoría, sustituir un único
paso, sustituir el enlazado entero o fusionar objetos. Nunca ve una estructura
de datos de lld: el grafo es una proyección normalizada sobre la que se
proyectan los backends de ELF, COFF y Mach-O.

## Interfaces

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* incluye PluginLink.h */
```

| Interfaz | Tabla | Propósito |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | Leer y modificar el grafo de enlazado (52 ranuras) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | Registrar proveedores de enlazador, fusión de objetos y verificación de imagen |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | Alcanzar el grafo o la imagen tras un `NevercArtifactHandle` |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | Leer la petición LTO, los módulos y las resoluciones de símbolos |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | Registrar un proveedor de generación de código LTO |

Las cinco son `NEVERC_INTERFACE_STABLE` en la mayor 1, así que un anfitrión más
nuevo solo puede añadir. Empareje cada una con su `NEVERC_LINK_API_MAJOR` /
`NEVERC_LTO_API_MAJOR` y compruebe `TableSize` frente a la última ranura que
invoque.

## La máquina de estados

`NevercLinkGraphInfo.State` es uno de catorce valores, y trece de las veinte
fases existen únicamente para hacerlo avanzar un paso:

| Fase | `NEVERC_LINK_STATE_…` resultante | Verificador del anfitrión |
|---|---|---|
| — | `INITIAL` | — |
| `neverc.link.input_probe` | `INPUT_PROBED` | `verify_input_probe` |
| `neverc.link.read_inputs` | `INPUTS_READ` | `verify_inputs` |
| `neverc.link.lto_resolve` | `LTO_RESOLUTION_READY` | |
| `neverc.link.lto_generate` | `LTO_GENERATED` | |
| `neverc.link.resolve_symbols` | `SYMBOLS_RESOLVED` | |
| `neverc.link.select_comdat` | `COMDAT_SELECTED` | |
| `neverc.link.gc` | `GC_COMPLETE` | `verify_liveness` |
| `neverc.link.icf` | `ICF_COMPLETE` | |
| `neverc.link.synthesize` | `SYNTHETICS_READY` | |
| `neverc.link.relax_thunks` | `THUNKS_RELAXED` | `verify_relaxation` |
| `neverc.link.layout` | `LAYOUT_COMPLETE` | `verify_layout` |
| `neverc.link.relocate` | `RELOCATIONS_APPLIED` | |
| `neverc.link.emit_image` | `IMAGE_EMITTED` | |

Cada una de esas trece es
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`, así que un
proveedor puede suministrar la transición él mismo, y un plugin que tenga un
`NevercLinkProofHandle` válido puede omitirla.

Las siete restantes son estructurales:

| Fase | Política | Papel |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Sustituir todo el enlazado, de `INITIAL` directo a una imagen binaria |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Fusión reubicable `-r` de ObjectGraphs |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | Última ocasión de tocar los bytes de la imagen |
| `neverc.link.image_verify` | OBSERVABLE, **SELLADA** | Verificador de imagen del anfitrión |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **SELLADA** | Archivos de mapa, dSYM, artefactos laterales |
| `neverc.link.commit` | OBSERVABLE, **SELLADA** | Publicación atómica del paquete de salida |
| `neverc.link.after_commit` | OBSERVABLE | Notificación posterior a la confirmación |

Las tres puertas selladas pueden observarse, pero nunca interceptarse,
sustituirse ni omitirse. `NEVERC_BUILTIN_LINK_PHASE_COUNT` vale 20.

## Alcanzar el grafo desde una fase

`NevercLinkPhaseAPI` convierte el artefacto del marco en un handle utilizable:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` es la `NevercLinkAPI` ligada a esta tarea, de modo que un
observador no necesita un `QueryInterface` aparte. Un proveedor publica su
resultado con `PublishGraph`, y `GetImage` hace lo propio con un artefacto de
imagen, devolviendo un `NevercLinkPhaseImageInfo` con la imagen, el paquete de
salida y un `NevercBinaryImageState` (`CANDIDATE`, `VERIFIED`, `COMMITTED`,
`ABORTED` o `FAILED_PARTIAL`).

## Leer el grafo

`NevercLinkGraphInfo` es el resumen: objetivo, formato, estado, generación,
diecisiete recuentos de entidades y un `SemanticDigest` de 32 bytes. Las
entidades en sí llegan mediante una llamada de paginación por especie, todas
compartiendo una página propiedad del llamante:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* arreglo que usted aporta y posee   */
  uint64_t ElementCapacity;  /* cuántas entradas caben             */
  uint64_t ElementStride;    /* sizeof de su elemento              */
  uint64_t OutCount;         /* cuántas escribió el anfitrión      */
  uint64_t NextCursor;       /* devuélvalo para continuar          */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

El anfitrión no escribe más de `ElementCapacity` entradas de `ElementStride`
bytes y nunca retiene `Data`, así que un arreglo en la pila sirve:

```c
NevercLinkSymbolInfo Symbols[64];
NevercLinkEntityPage Page = {0};
uint64_t Cursor = 0;

do {
  Page.Header = (NevercABITableHeader){sizeof(Page), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
  Page.Data            = Symbols;
  Page.ElementCapacity = 64;
  Page.ElementStride   = sizeof(Symbols[0]);
  Status = Link->GetSymbolPage(Link->Context, Task, Graph, Cursor, &Page);
  if (Status.Code != NEVERC_STATUS_OK)
    break;
  for (uint64_t I = 0; I != Page.OutCount; ++I) {
    /* Symbols[I].Name, .Binding, .Definition, .IsPrevailing, … */
  }
  Cursor = Page.NextCursor;
} while (Page.HasMore);
```

Quince paginadores de grafo siguen esa forma: `GetInputPage`, `GetArchivePage`,
`GetArchiveMemberPage`, `GetSharedLibraryPage`, `GetBitcodeModulePage`,
`GetSectionPage`, `GetAtomPage`, `GetSymbolPage`, `GetEdgePage`,
`GetComdatPage`, `GetImportPage`, `GetExportPage`, `GetUnwindPage`,
`GetSyntheticPage` y `GetConstraintPage`; y otros dos,
`GetBinarySegmentPage` y `GetBinarySectionPage`, paginan una imagen emitida.
Cada uno tiene su `Get…Info` correspondiente para un handle suelto.

Toda información de entidad lleva un `NevercLinkOrigin`:

```c
typedef struct NevercLinkOrigin {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkArchiveMemberHandle ArchiveMember;
  NevercObjectGraphHandle ObjectGraph;
  uint64_t ObjectEntityID;
  NevercInterfaceID CreatedByPhase;
  NevercStringView CreatedByProvider;
  NevercInterfaceID LastMutationPhase;
  NevercStringView LastMutationPlugin;
} NevercLinkOrigin;
```

Eso es lo que hace auditable un enlazado: para cualquier átomo de la salida
puede nombrar el archivo de entrada, el miembro de archivo del que se extrajo,
la fase que lo creó y el plugin que lo tocó por última vez.

### Las entidades

| Especie | Estructura Info | Campos destacados |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Archivo / miembro | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Biblioteca compartida | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Módulo bitcode | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Sección | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Átomo | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Símbolo | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Arista | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Import / export | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Desenrollado | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Sintético | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Restricción | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

Los indicadores de átomo son `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`,
`ADDRESS_SIGNIFICANT`, `TLS` y `UNWIND`. Los enlaces de símbolo son `LOCAL`,
`GLOBAL`, `WEAK` y `COMMON`; las definiciones, `UNDEFINED`, `DEFINED`,
`ABSOLUTE`, `COMMON` y `SHARED`. Las especies de arista son `RELOCATION`,
`ASSOCIATION`, `KEEP_ALIVE`, `UNWIND` y `FORMAT_EXTENSION`. La selección COMDAT
abarca `ANY`, `EXACT_MATCH`, `SAME_SIZE`, `LARGEST`, `NEWEST` y
`NO_DUPLICATES`.

## Modificar el grafo

La mutación es transaccional y siempre está acotada a un grafo:

```c
NevercLinkMutationHandle Mutation;
Link->BeginMutation(Link->Context, Task, Graph, &Mutation);

Link->SetSymbolRoot(Link->Context, Task, Mutation, Symbol, NEVERC_TRUE);
Link->ReplaceAtomContent(Link->Context, Task, Mutation, Atom,
                         (NevercByteView){Bytes, Length},
                         /*ZeroFillSize=*/0);

Status = Link->CommitMutation(Link->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Link->AbandonMutation(Link->Context, Task, Mutation);
```

La confirmación prepara una copia de trabajo, la verifica y solo entonces
publica e incrementa `Generation`. `AbandonMutation` lo descarta todo.
Confirmar mientras el grafo está en `GC_COMPLETE`, por ejemplo, vuelve a
ejecutar el verificador de vivacidad, de modo que una mutación que dejaría
huérfano un átomo vivo se rechaza en lugar de escribirse.

### Las mutaciones invalidan el estado aguas abajo

Esta es la parte que sorprende. Cada llamada de preparación se clasifica, y la
clasificación determina **el estado más temprano que queda inválido**; el
anfitrión debe reejecutar todas las fases a partir de ahí:

| Llamada | Estado invalidado más temprano |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

Una mutación que toque varias toma el mínimo. Revincular un símbolo tras la
disposición descarta, por tanto, los resultados de disposición, reubicación e
imagen: barato durante `gc`, caro durante `post_emit`. Mute tan pronto en la
máquina de estados como su cambio lo permita.

`SetSymbolResolution` toma un pequeño registro de actualización en vez de un
símbolo entero, lo que evita que un cambio de resolución reescriba por accidente
un nombre o un valor:

```c
NevercLinkSymbolResolutionUpdate Update = {0};
Update.Header = (NevercABITableHeader){sizeof(Update), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
Update.Binding      = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
Update.Visibility   = NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
Update.Definition   = NEVERC_LINK_SYMBOL_DEFINED;
Update.IsPrevailing = NEVERC_TRUE;
Update.IsExported   = NEVERC_FALSE;
Link->SetSymbolResolution(Link->Context, Task, Mutation, Symbol, &Update);
```

## Omitir una fase con una prueba

Una fase `SKIPPABLE_WITH_PROOF` acepta un `NevercLinkProofHandle` en lugar de
ejecutarse. La prueba fija todo aquello de lo que depende la omisión:

```c
typedef struct NevercLinkProofInfo {
  NevercABITableHeader Header;
  NevercLinkProofHandle Proof;
  NevercLinkGraphHandle Graph;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercInterfaceID OutputArtifact;
  uint8_t RouteDigest[32];
  uint8_t SemanticDigest[32];
  uint64_t ImageBase;
  uint64_t EntryAddress;
} NevercLinkProofInfo;
```

Como se registran tanto `GraphGeneration` como `SemanticDigest`, cualquier
mutación confirmada entre la emisión de la prueba y su uso la deja obsoleta, y
el anfitrión ejecuta la fase de verdad.

## La imagen binaria

Tras `emit_image` el producto es un `NevercBinaryImageHandle`:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                     */
```

Los tipos de salida son `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY` y
`BUNDLE`. Los indicadores de segmento son `READ`, `WRITE` y `EXECUTE`.

`Image.Binary` e `Image.Builder` son el escritor transaccional acotado de
[`PluginObject.h`]: `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`,
`Append`, `Resize`. Un interceptor de `post_emit` que parchee bytes debe pasar
por él; las escrituras más allá del límite reservado abortan la preparación en
lugar de agrandar el archivo.

## Proveedores

Registre durante `Register`, nunca después.

### Sustituir el enlazador

```c
NevercLinkerProviderDescriptor Provider = {0};
Provider.Header = (NevercABITableHeader){sizeof(Provider),
                                         NEVERC_LINK_REGISTRAR_API_MAJOR,
                                         NEVERC_LINK_REGISTRAR_API_MINOR, 0};
Provider.ProviderID   = SV("com.example.my-linker");
Provider.TargetID     = MyTargetID;
Provider.InputFormat  = ELFFormatID;
Provider.OutputFormat = ELFFormatID;
Provider.OutputKind   = NEVERC_LINK_OUTPUT_EXECUTABLE;
Provider.Flags        = NEVERC_LINK_PROVIDER_DETERMINISTIC |
                        NEVERC_LINK_PROVIDER_CACHEABLE;
Provider.Link         = my_link;
Provider.VerifyImage  = my_verify;      /* opcional */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

El callback recibe la petición y el conjunto de entradas en bruto, y rellena un
candidato:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs es un NevercRawLinkInput[], Inputs->OrderDigest fija el orden */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` lleva los indicadores sobre los que un enlazador realmente
bifurca —`PIE`, `STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`,
`ALLOW_UNDEFINED`, `WHOLE_ARCHIVE`, `DETERMINISTIC`— más `EntrySymbol`,
`InstallName`, `Soname`, `ImageBase`, `PageSize`, `ThreadBudget`, rutas de
búsqueda y bibliotecas. Los indicadores por entrada son `WHOLE_ARCHIVE`,
`AS_NEEDED`, `START_GROUP`, `END_GROUP` y `LAZY`.

Si tiene éxito, el anfitrión adopta el candidato. Si falla, el proveedor sigue
siendo dueño de lo que haya creado. Las puertas selladas de verificación y
confirmación se ejecutan en ambos casos.

### Fusionar objetos y verificar imágenes

`RegisterObjectMergeProvider` atiende `-r`: la petición lleva los
`NevercObjectMergeInput[]` de entrada y un grafo de salida y una mutación ya
abiertos, de modo que el proveedor escribe en una transacción propiedad del
anfitrión en lugar de construir un archivo.

`RegisterBinaryImageVerifier` añade una comprobación de solo lectura que se
ejecuta junto al verificador de imagen del propio anfitrión. No puede
sustituirlo.

## LTO

`lto_resolve` produce las resoluciones de símbolos; `lto_generate` convierte el
bitcode en objetos. `NevercLTOAPI` lee ambas cosas.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` y `GetResolutionPage` usan el mismo protocolo
`NevercLinkEntityPage`, rellenando `NevercLTOInputModuleInfo` y
`NevercLTOSymbolResolution`. Cada resolución nombra el módulo, el símbolo, el
`NevercLinkSymbolHandle` correspondiente y sus indicadores:

| Indicador | Significado |
|---|---|
| `PREVAILING` | Este módulo posee la definición. |
| `VISIBLE_TO_REGULAR_OBJECT` | Un objeto no bitcode puede verla. |
| `EXPORTED` | Presente en la tabla de símbolos dinámicos. |
| `FINAL_DEFINITION` | Ninguna definición posterior puede sustituirla. |
| `CAN_INLINE` | Se permite el inlining a través de la frontera. |
| `CAN_INTERNALIZE` | Se permite la internalización. |
| `LINKER_REDEFINED` | El enlazador la sobrescribió. |
| `REFERENCED_BY_REGULAR_OBJECT` | Un objeto normal la referencia. |

`NevercLTOOptions` selecciona `NEVERC_LTO_FULL` o `NEVERC_LTO_THIN`, los niveles
de optimización, `ThreadBudget`, `ThinBackendPartitions`, la CPU y las
características, y un ámbito de caché entre `DISABLED`, `TASK`, `LOCAL_SHARED` o
`REMOTE_SHARED`. Los indicadores de opciones son `EMIT_OPTIMIZED_BITCODE`,
`EMIT_INDEX`, `SAVE_TEMPS`, `WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO` y
`DETERMINISTIC`.

### Un proveedor de LTO

```c
NevercLTOProviderDescriptor Provider = {0};
Provider.Header = /* … */;
Provider.ProviderID    = SV("com.example.my-lto");
Provider.TargetID      = MyTargetID;
Provider.Flags         = NEVERC_LTO_PROVIDER_THIN |
                         NEVERC_LTO_PROVIDER_DETERMINISTIC |
                         NEVERC_LTO_PROVIDER_CACHEABLE;
Provider.BuildCacheKey = my_cache_key;
Provider.Codegen       = my_codegen;
LTORegistrar->RegisterProvider(LTORegistrar->Context, RegistrarContext,
                               &Provider);
```

`BuildCacheKey` escribe en un `NevercMutableByteView` suministrado por el
llamante e informa del tamaño que necesitaba, de modo que el anfitrión pueda
dimensionar el búfer y reintentar. Debe ser una función pura de la petición:
derivarla de `RequestDigest` y `ResolutionDigest` es la construcción segura.
Declarar `CACHEABLE` con una clave que ignore parte de la petición produce
objetos obsoletos que sobreviven a una reconstrucción limpia.

`Codegen` rellena un `NevercLTOProductCandidate`: un arreglo de
`NevercLTOObjectProduct` (cada uno nombrando su módulo de origen, su ObjectGraph
y su artefacto), opcionalmente `OptimizedBitcode` y `ThinIndex`, y la
`CacheKey` que realmente usó.

## Reglas

- Los handles están acotados a la tarea y pertenecen al anfitrión. No guarde
  ninguno más allá del callback, no lo use en otra tarea y nunca fabrique un
  valor.
- `NevercLinkEntityPage.Data` es suyo. El anfitrión escribe como mucho
  `ElementCapacity × ElementStride` bytes y no conserva referencia alguna.
- Todo `BeginMutation` alcanza exactamente un `CommitMutation` o
  `AbandonMutation`, también en la ruta de error.
- Mute tan pronto en la máquina de estados como el cambio lo permita; una
  mutación tardía invalida en silencio todas las fases aguas abajo.
- No mute desde un observador. Los observadores reciben un puente de solo
  lectura y el intento se rechaza con `NEVERC_STATUS_POLICY_VIOLATION`.
- Escriba los bytes de la imagen solo mediante `NevercBinaryImageInfo.Binary` y
  su constructor. Un desbordamiento aborta la preparación en lugar de agrandar
  la salida.
- Reclame `DETERMINISTIC` solo si el mismo resumen de petición produce siempre
  una salida idéntica byte a byte, y `CACHEABLE` solo si su clave de caché cubre
  toda entrada capaz de cambiar esa salida.
- `image_verify`, `side_outputs_verify` y `commit` están selladas. Obsérvelas;
  no intente interceptarlas ni omitirlas.

Consulte [`PluginLink.h`] y [`PluginLTO.h`] para las declaraciones normativas,
[`Schema/PhaseSchema.json`] para las políticas de las veinte fases y
[`coverage.json`] para las pruebas que fijan cada una.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
