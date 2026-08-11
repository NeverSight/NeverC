**Idiomas**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API de destino, MC, ensamblador y objetos de los complementos NeverC

El back-end son cuatro cabeceras y veintinueve fases. [`PluginTarget.h`]
describe un destino y las rutas que atraviesan la generación de código.
[`PluginMC.h`] construye y observa el código máquina. El análisis y la impresión
de ensamblador viven en la misma cabecera. [`PluginObject.h`] convierte un
archivo reubicable en un grafo normalizado, y a la inversa.

Juntas permiten que un complemento añada un destino, sustituya un paso de
rebajado o todos, vigile cada instrucción según se emite, defina un dialecto
de ensamblador o reescriba un archivo objeto, y todo ello a través de un ABI
de C puro que nunca expone un `MCInst`, un `MCSection` ni un
`object::ObjectFile` de LLVM.

## Interfaces

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| Interfaz | Tabla | Ranuras | Propósito |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | Leer y modificar un `MCUnit`; registrar codificadores, decodificadores, back-ends |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | Eventos de emisión e instantáneas de disposición |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | Sustituir MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | Sustituir el analizador o el impresor de ensamblador |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | Leer y modificar un ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## Dos niveles de compatibilidad

Esta es la regla que gobierna todo lo demás aquí.

**STABLE**, y seguro de fijar en el código: los descriptores independientes del
destino, los identificadores de fase, los identificadores de artefacto, los
contenedores MC y ObjectGraph, las transacciones de salida y todos los
contratos de retrollamada.

**LOCKSTEP**, e inseguro sin comprobación: los esquemas de opcode, registro,
operando, fixup, reubicación y convención de llamada específicos del destino.
Sus valores numéricos solo significan algo frente a una revisión de esquema
exacta.

En todos los sitios donde aparece un valor LOCKSTEP aparece a su lado un
resumen de esquema. Compárelo antes de leer el valor:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC también rechaza un esquema discordante antes de invocar a un proveedor,
así que la comprobación es doble protección; pero un complemento que se la
salte y lea de todos modos un opcode crudo interpretará mal las instrucciones
en silencio.

## Las fases

Veintinueve, en cuatro dominios.

### `codegen` — enrutado (4)

| Fase | Política |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — código máquina (13)

`neverc.mc.encode`, `neverc.mc.decode` y `neverc.mc.layout` son OBSERVABLE,
INTERCEPTABLE, REPLACEABLE.

`neverc.mc.emission.pre_instruction` es el único evento de emisión que además
es REPLACEABLE: ahí es donde se sustituye una instrucción. Los otros nueve
(`unit_begin`, `unit_end`, `section_change`, `post_instruction`,
`post_encode`, `fixup`, `relaxation_round`, `pre_layout`, `post_layout`) son
solo de observación.

### `assembly` (4)

`neverc.assembly.parse` y `neverc.assembly.print` son REPLACEABLE.
`neverc.assembly.final_verify` y `neverc.assembly.commit` son SEALED.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write` y `post_layout` son
REPLACEABLE; `neverc.object.post_write` es solo INTERCEPTABLE;
`neverc.object.final_verify` y `neverc.object.commit` son SEALED.

## Registrar un destino

`NevercTargetDescriptor` es el descriptor más grande del ABI porque lleva todo
lo que el front-end y el back-end necesitan saber:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` decide cuándo se selecciona el destino: cada emparejador
nombra una arquitectura, un fabricante, un sistema operativo y un entorno, más
una `Priority` que desempata frente a los destinos integrados.

`Machine` es un `NevercTargetMachineDescriptor`: disposición de datos, CPU
predeterminada y de ajuste, la tabla de características, los ABI, convenciones
de llamada y formatos objeto admitidos, espacios de direcciones, modelos de
reubicación y de código (tanto el predeterminado como la máscara de
compatibilidad), modelo de excepciones (`NONE`, `DWARF`, `SJLJ`, `SEH`,
`WASM`), modelo de desenrollado, endianidad, la anchura de
pointer/int/long/long long, alineación de pila, anchuras atómica y vectorial
máximas, tipo de `va_list`, niveles de ejecución (`USER`, `KERNEL`,
`HYPERVISOR`, `FIRMWARE`) y compatibilidad con TLS.

Las funciones integradas del destino llevan su propia retrollamada de
rebajado, que recibe un constructor de IR vivo:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI y convenciones de llamada

Un ABI clasifica las firmas de función:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

Los tipos de argumento son `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`,
`EXPAND`, `INDIRECT_ALIASED` y `COERCE_AND_EXPAND`; las banderas son `BYVAL`,
`REALIGN`, `INREG`, `SRET_AFTER_THIS`, `CAN_BE_FLATTENED`, `SIGN_EXTEND` y
`PADDING_INREG`. La coerción es `NONE`, `INTEGER`, `FLOAT` o `POINTER`, y
`COERCE_AND_EXPAND` aporta un arreglo de `NevercABICoercionElement`.

Una convención de llamada baja un nivel más y asigna las ubicaciones reales:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` es un valor LOCKSTEP: `RegisterNumber` solo significa
algo frente al esquema que nombra. Vea
[Convenciones de llamada personalizadas](custom-callconv/README.es.md#planes-materializados) y
[`pluginsdk/examples/CustomCallConvPlugin.c`] para el ejemplo completo.

## Rutas de generación de código

Una ruta se elige a partir de la `NevercTargetKey` canónica: identificador de
destino, partes del triple, CPU, CPU de ajuste, características, ABI,
convención de llamada, formato objeto, modelo de reubicación, modelo de
código, nivel de ejecución, anchura de puntero, endianidad y resumen de
esquema. Registre las aristas que sepa servir:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

Los tipos de producto son `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`,
`OBJECT_IMAGE` y `CUSTOM`. La ruta de grano fino es
`IR → MIR → MC → ObjectGraph → ObjectImage`.

Poner `NEVERC_CODEGEN_EDGE_COARSE` y aportar `CoarseLower` sustituye de una
sola vez todo el tramo `IR → ObjectImage`:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

Una ruta gruesa sigue pasando por `neverc.codegen.product_verify` y por la
confirmación transaccional de la salida. A `VerifyProduct` se la llama con las
obligaciones que el anfitrión espera que haya cumplido —`VERIFY_FINAL_IR`,
`VERIFY_TARGET_KEY`, `VERIFY_PRODUCT_KIND`, `VERIFY_PRODUCT_ID`,
`VERIFY_STRUCTURE`—, de modo que un proveedor no puede saltarse una puerta a
hurtadillas tomando un atajo.

## Construir MC

Un `MCUnit` contiene secciones, símbolos, expresiones, fragmentos,
instrucciones, operandos y fixups. La lectura es iteración first/next:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

La mutación es transaccional, igual que en todas partes:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

Los manejadores tienen ámbito de tarea y se comprueban por generación, así que
un manejador de una mutación abandonada se rechaza en lugar de reutilizarse.

Las banderas de sección son `ALLOCATED`, `EXECUTABLE`, `WRITABLE`,
`MERGEABLE` y `DEBUG`. Los enlaces de símbolo son `LOCAL`, `GLOBAL` y `WEAK`;
los tipos son `NONE`, `FUNCTION`, `OBJECT`, `SECTION` y `TLS`; las
definiciones son `UNDEFINED`, `SECTION`, `ABSOLUTE` y `COMMON`. Las
expresiones admiten los unarios `PLUS`, `MINUS`, `NOT` y los binarios `ADD`,
`SUBTRACT`, `MULTIPLY`, `DIVIDE`, `AND`, `OR`, `XOR`, `SHIFT_LEFT`,
`SHIFT_RIGHT`. Pase `NEVERC_MC_AUTOMATIC_OFFSET` allí donde quiera que el
anfitrión coloque algo por usted.

`RegisterSchema` publica un esquema MC de destino, y `GetSchemaToken` /
`GetSchemaTokenInfo` convierten un nombre en un token LOCKSTEP y viceversa.

## Observar la emisión

El flujo de emisión informa de diez tipos de evento en orden — uno por cada
fase `neverc.mc.emission.*`. La ABI también reserva
`NEVERC_MC_EMISSION_PRE_OBJECT_WRITE`; la escritura de objeto en sí es la fase
separada `neverc.object.pre_write`. Suscríbase como
observador y lea el evento:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, Frame->Input, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` dice qué partes del evento están rellenas: `HAS_SECTION`,
`HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT` y
`CAN_REPLACE_INSTRUCTION`. Compruebe la bandera antes de leer el campo
correspondiente: un evento que aún no tiene codificación no la tendrá solo
porque usted la pida.

`GetLayoutSection`, `GetLayoutFragment`, `GetLayoutSymbol` y
`GetLayoutFixup` dan direcciones y tamaños en cuanto `HAS_LAYOUT` está
puesta.

En `pre_instruction`, y solo cuando `CAN_REPLACE_INSTRUCTION` está puesta,
puede sustituir:

```c
const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
NevercMCInstHandle Instruction;
Emission->BeginInstructionReplacement(Emission->Context, Frame, Continuation,
                                       &MC, &Unit, &Instruction);
/* mutate Instruction through MC->BeginMutation / … / CommitMutation */
Emission->PublishInstructionReplacement(Emission->Context, Frame, Continuation,
                                         &OutResult->Output);
```

[`pluginsdk/examples/MCObserverPlugin.c`] es la versión de solo lectura de esto.

## Codificadores, decodificadores y disposición

Tres registros amplían el back-end de código máquina, todos indexados por
destino y resumen de esquema:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

Un codificador escribe a través de un sumidero en vez de devolver un búfer, lo
que deja la propiedad del lado del anfitrión:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

Un decodificador informa de `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`,
`_UNKNOWN` o `_FAIL`. Los tipos de fixup se describen a sí mismos mediante
`NevercMCFixupKindInfo` con las banderas `PC_RELATIVE`, `SIGNED`, `RELAXABLE`
y `TARGET`.

El back-end de ensamblador es dueño de la relajación. La disposición emite un
resumen de prueba, y **cualquier mutación posterior a la disposición invalida
esa prueba** y obliga a volver a disponer antes de poder escribir el objeto: el
mismo patrón de comprobación por generación que usa el grafo de enlazado.

## Ensamblador

Un proveedor de análisis consume bytes de origen y publica un `MCUnit`:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, Frame->Input, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, In.Source.Cursor, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame, In.Source.Cursor);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build into Unit … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, &Output);
```

Las fuentes son `NEVERC_ASSEMBLY_SOURCE_BUFFER` o
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. El ensamblador preprocesado (`.S`)
pasa primero por el preprocesador frontal normal y llega como tokens
renderizados; el ensamblador simple (`.s`) entra directamente al analizador
como búfer.

Un impresor va en sentido contrario: `GetPrintInput`, luego
`WritePrintOutput` sobre la transacción de salida provista, y después
`PublishAssemblyOutput`. Escribir en cualquier otro sitio no está admitido: la
verificación de análisis/impresión y la puerta de confirmación del anfitrión se
ejecutan antes de que los bytes sean visibles, así que una impresión fallida no
deja ningún archivo parcial.

## Grafos de objetos

`NevercObjectAPI` normaliza un archivo reubicable en secciones, símbolos,
reubicaciones y COMDAT. Los adaptadores integrados cubren ELF, COFF y Mach-O;
`RegisterFormat` añade otro.

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

La mutación sigue el patrón crear/reemplazar/mover/borrar para los cuatro
tipos de entidad, preparada dentro de `BeginMutation` … `CommitMutation` /
`AbandonMutation`.

Las banderas de sección son `ALLOCATED`, `EXECUTABLE`, `WRITABLE`,
`MERGEABLE`, `STRINGS`, `TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE` y `RETAIN`.
Los destinos de reubicación son `SYMBOL`, `SECTION`, `ABSOLUTE` o
`FORMAT_EXTENSION`.

Cada descriptor tiene un trío `ExtensionOwner` / `ExtensionVersion` /
`Extension`. Así es como un formato conserva datos para los que el grafo
normalizado no tiene campo: los bytes viajan con la entidad y vuelven al
escribir, en lugar de perderse en el viaje de ida y vuelta.

El adaptador ELF integrado registra los hechos nativos exactos en extensiones
etiquetadas: `NCSE v2` contiene el índice, la dirección, el tipo, las flags, el
desplazamiento de archivo y el tamaño de entrada de la sección; `NCSY v2`
contiene `st_info`, el `st_other` completo, `st_size` y un estado explícito de
nombre nativo vacío/no vacío; `NCRL v1` contiene el tipo de reubicación nativo
y su nombre oficial. Por ello, un símbolo ELF ordinario vacío permanece vacío
y nunca se reescribe como un nombre sintético `$symbol.N`; un símbolo fuente
que se llame literalmente `$symbol.N` sigue siendo un nombre ordinario no
vacío. El passthrough de una imagen nativa sin cambios puede conservar símbolos
anónimos exactamente. Una escritura integrada donde el grafo sea autoritativo
los rechaza antes de abrir el sink de salida, porque la sintaxis MC portable no
puede reconstruir la misma entrada anónima de la tabla de símbolos. Las
auditorías canónicas de release Android exigen los payloads etiquetados actuales
exactos y reproducen desde ellos la proyección estable del grafo.

### Registrar un formato

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` informa de una `Confidence` de 0 a
`NEVERC_OBJECT_PROBE_MAX_CONFIDENCE` (1000), del `NevercObjectArtifactKind`
que reconoció (`RELOCATABLE`, `ARCHIVE`, `EXECUTABLE_IMAGE`, `SHARED_IMAGE`,
`UNIVERSAL_BINARY`) y de un `ConsumedMinimum`: cuántos bytes necesitó para
estar seguro, limitado a `NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536).
Gana la mayor confianza.

A `Reader` se le entrega un grafo y una mutación abierta, y los rellena. A
`Writer` se le entregan el grafo, su prueba de disposición y el constructor
binario acotado.

### Políticas de escritura de Object Format 1.1

`NevercObjectFormatDescriptor.Header.Minor` anuncia la capacidad del provider;
no es un selector de modo global del anfitrión. Un descriptor 1.0 sigue siendo
totalmente compatible para probe, read y las escrituras predeterminadas
ordinarias. Su writer recibe `NevercObjectWriteRequest.Header.Minor == 0` y
`Header.Flags == 0`. Anuncie minor 1 solo si el writer entiende los request
flags 1.1; una escritura ordinaria sigue llevando flags cero y conserva el
comportamiento de salida anterior a 1.1.

Object Format 1.1 define estos bits en
`NevercObjectWriteRequest.Header.Flags`:

- `NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES` exige secciones `.strtab` y
  `.shstrtab` canónicas e independientes, y reasigna todos los índices
  dependientes. Se trata de una normalización canónica de tablas ELF, no de un enlace
  reubicable: se conservan intactos el orden de las secciones, los grupos
  COMDAT, los metadatos del linker, los símbolos duplicados y los alias, los
  registros de reubicación y todo contenido ajeno a las tablas de nombres.
  También se conservan las secciones `SHT_STRTAB` adicionales válidas y propias
  del formato; solo se reconstruyen la tabla de cadenas del `SHT_SYMTAB`
  seleccionado y la tabla designada por `e_shstrndx`. Con `DROP_DEBUG_INFO` solo
  se filtran las secciones de debug y los metadatos que hacen referencia a los
  índices eliminados de dichas secciones.
- `NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE` también convierte el ELF
  serializado final en la autoridad: elimina los mapping symbols sintetizados
  por el writer y vuelve a generar los nombres release desde las coordenadas
  reales de las secciones serializadas.
- `NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO` solicita eliminar las secciones de debug
  como parte de una de esas políticas ELF.

`NEVERC_OBJECT_WRITE_REQUEST_KNOWN_FLAGS` es la máscara completa de bits
conocidos. Las únicas combinaciones válidas son `0`, `CANONICAL_ELF_TABLES`,
`CANONICAL_ELF_TABLES | DROP_DEBUG_INFO`,
`CANONICAL_ELF_TABLES | ANDROID_KERNEL_RELEASE` y los tres bits juntos. Un bit
release o debug sin el bit canonical es inválido.

El anfitrión rechaza una combinación desconocida o inválida, o cualquier
solicitud especial a un provider minor-0, antes de abrir el sink de salida. Un
writer 1.1 también debe rechazar, no ignorar, cualquier flag desconocido o
inválido que reciba. Tras
el writer y cualquier interceptor `object.post_write`, la validación semántica
del anfitrión y el `object.final_verify` sellado vuelven a auditar los bytes
serializados y son la autoridad. Estos flags no prometen soporte general para
todos los formatos de terceros. Minor 1 significa que el writer entiende el
protocolo de flags: puede implementar una política ELF aplicable o devolver
explícitamente `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` cuando no sea aplicable o
no esté admitida; nunca debe ignorar silenciosamente la solicitud.

### Autoridad de escritura de una release Android finalizada

Cuando `--strip` finaliza un `.ko` de Android, la API genérica de objetos
mutables anterior se restringe a la ruta de escritura fiable establecida por
el host. Este límite tiene dos sellos de identidad independientes:

- antes de cualquier fase `ObjectGraph` reemplazable, el sello del grafo
  vincula el `section ID`, el `final ordinal` y el nombre exacto de cada sección
  lógica conservada, además del `symbol ID`, owner, clase, sección, valor,
  tamaño, binding, tipo y `st_other` completo de cada símbolo de nombre exacto;
- después de que el writer propiedad del host cree la baseline fiable de la
  imagen, el sello de imagen vincula el ordinal y nombre exacto de cada sección
  conservada, el total de entradas de `.symtab`, y el `slot` `.symtab` crudo y
  los atributos de cada símbolo de nombre exacto. El verificador completo de
  release recalcula de forma independiente cada nombre estructural de release.

| Binding | Comportamiento de la release Android finalizada |
|---|---|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED` antes del callback; no puede reemplazar la ruta de escritura fiable |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`; esta ruta exige el graph writer propiedad del host que establece la baseline fiable |
| `observer` | `READ_ONLY`; puede inspeccionar, pero no mutar ni reemplazar la salida |
| `neverc.object.post_write` `interceptor` | `VALIDATED`; su API mutable acotada solo puede cambiar payload fuera de la superficie ABI y de identidad verificada estructuralmente, y el resultado debe pasar las comprobaciones ABI de entrada, ambos sellos y el verificador completo de release |

La propiedad del merge finalizado también queda sellada por el host. Se
descarta cualquier `MergedImage` o byte independiente de un
`third-party ObjectMergeProvider`; el `host-owned graph writer` serializa el
grafo verificado y finalizado de ese provider. En sentido inverso,
`built-in finalized input serialization` omite las `external object phases` y
entrega al merger del host exactamente los `audited native bytes`; este paso
interno de entrada no elude la frontera de salida anterior.

La finalización solo se acepta con `Android module merge semantics`; también
exige tanto una `relocatable output request` como una
`relocatable driver configuration`, o falla `before routing`. En una release
Android relocatable finalizada, `frozen input format`,
`TargetKey.ObjectFormatID` y `frozen output format` deben compartir
`one format identity`. Una discrepancia se rechaza `before provider dispatch`,
por tanto también antes del route planning o de crear el sink; así, el
capability preflight y el graph-writer dispatch real no pueden observar
formatos diferentes.

El native-image passthrough rechaza todo `route-matching provider` reemplazable
y todos los interceptors. Un provider cuya ruta
target/CPU/features/object-format/execution-level no coincida no se ejecuta ni
bloquea la release; solo admite observers. Solo un rechazo o fallo de validación
`before sealed commit` cancela el staging y no publica ningún archivo. Un fallo
de observer `AFTER_COMMIT` se informa después de la publicación y no puede
revertir el archivo ya publicado.

### La tubería de escritura

1. sondear y leer los bytes en un ObjectGraph;
2. ejecutar los interceptores de grafo `object.pre_write`;
3. disponer y luego ejecutar `object.post_layout` (volver a disponer tras
   cualquier mutación);
4. escribir una imagen candidata acotada;
5. ejecutar los interceptores binarios `object.post_write`;
6. ejecutar el `object.final_verify` sellado y el `object.commit` atómico.

El estado de la imagen recorre `CANDIDATE` → `VERIFIED` → `COMMITTED`, o bien
`ABORTED` / `FAILED_PARTIAL`.

Los observadores reciben puentes de solo lectura; una mutación intentada desde
un observador se rechaza con `NEVERC_STATUS_POLICY_VIOLATION`. Los escritores y
los interceptores posteriores a la escritura solo obtienen el constructor
acotado `NevercMutableBinaryAPI`: `Reserve`, `Write`, `WriteAt`, `Tell`,
`ReadAt`, `Insert`, `Append`, `Resize`. Un desbordamiento, una retrollamada
fallida o una verificación fallida aborta la preparación, así que un fallo
nunca deja medio archivo en el disco.

[`pluginsdk/examples/ObjectRewritePlugin.c`] es una reescritura transaccional
completa.

## Reglas

- Compare el resumen de esquema antes de consumir cualquier valor LOCKSTEP de
  opcode, registro, operando, fixup, reubicación o convención de llamada.
- Mantenga el estado mutable en el estado de process, session y task que
  proporciona el anfitrión.
- No guarde en caché manejadores de tarea ni vistas prestadas después de que
  una retrollamada retorne.
- Invoque la continuación de un interceptor como mucho una vez, en el hilo de
  la retrollamada.
- Cada `BeginMutation` llega exactamente a una confirmación o a un abandono.
- Vuelva a disponer tras mutar un MCUnit o un ObjectGraph ya dispuesto; la
  prueba de disposición antigua está caducada y el anfitrión la rechazará.
- Compruebe `NevercMCEmissionEventInfo.Flags` antes de leer un campo del
  evento, y sustituya una instrucción solo cuando `CAN_REPLACE_INSTRUCTION`
  esté puesta.
- Escriba la salida únicamente a través de la transacción o el sumidero de
  bytes provistos.
- Devuelva el `NevercStatus` original en caso de fallo y no publique nada
  parcial.
- Declare los modelos de concurrencia y reentrada más estrechos que sean
  ciertos.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify` y `object.commit` están sellados. Solo observe.

Vea [`PluginTarget.h`], [`PluginMC.h`], [`PluginObject.h`] y
[`Schema/PhaseSchema.json`] para las declaraciones normativas; las clases de
entidad, operando, fixup y sección que emplean provienen de
[`Schema/MCSchema.json`] y [`Schema/ObjectSchema.json`], que generan
[`Schema/PluginMCSchema.inc`] y [`Schema/PluginObjectSchema.inc`]. Vea también
[`coverage.json`], que asocia cada una de estas fases estables con sus pruebas
positivas, negativas, de sustitución, de observador de solo lectura y de
puerta sellada.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/MCSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MCSchema.json
[`Schema/ObjectSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ObjectSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMCSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc
[`Schema/PluginObjectSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc
