**Idiomas**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# API de IR de los plugins de NeverC

`PluginIR.h` expone la representación intermedia de LLVM mediante seis tablas de
capacidades y un esquema generado. Un plugin lee y reescribe la IR, registra
pasadas en cinco puntos estables de la cadena, define sus propios análisis o
sustituye por completo la generación de IR y la cadena de optimización, y todo
ello sin incluir una sola cabecera de LLVM.

Los códigos de operación, los géneros de tipo y las propiedades de instrucción
son **identificadores de esquema estables**, no valores de enumeración de LLVM.
Esa indirección es lo que permite que un plugin compilado hoy siga funcionando
cuando el anfitrión pase a una versión nueva de LLVM.

## Interfaces

```c
#include "neverc/Plugin/PluginIR.h"
```

| Interfaz | Tabla | Ranuras | Propósito |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | Leer y editar módulos, valores, tipos, constantes, metadatos, atributos |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | Construcción transaccional |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | Análisis nativos y de plugin |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | Sustituir el descenso SemanticUnit → IR |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | Sustituir toda la cadena de optimización |

Todas son `NEVERC_INTERFACE_STABLE` en el mayor 1. Negocie con los
`NEVERC_IR_*_API_MAJOR` / `_MINOR` correspondientes y compruebe que `TableSize`
llega hasta la última ranura que vaya a llamar, tal como hace
`pluginsdk/examples/FunctionPass.c`:

```c
Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &StructSize);
if (!Table ||
    StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                     sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

## Fases

Ocho fases de IR:

| Fase | Política |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **PUERTA SELLADA DEL ANFITRIÓN** |

Las cinco fases `pass.*` son a las que apunta `NevercIRPassDescriptor.Phase`.
`neverc.ir.final_verify` ejecuta el verificador de LLVM y nada puede
interceptarla, sustituirla ni saltársela, ni siquiera un proveedor de
optimización.

## El esquema

`Schema/PluginIRSchema.inc` se genera y lo incluye `PluginIR.h`. Publica un
resumen y estos conjuntos de constantes:

```c
#define NEVERC_IR_SCHEMA_CAPABILITY_MAJOR   UINT16_C(1)
#define NEVERC_IR_SCHEMA_DIGEST             "4302919d…"
#define NEVERC_IR_TYPE_KIND_COUNT           UINT32_C(22)
#define NEVERC_IR_VALUE_KIND_COUNT          UINT32_C(29)
#define NEVERC_IR_OPCODE_COUNT              UINT32_C(67)
#define NEVERC_IR_PREDICATE_COUNT           UINT32_C(26)
#define NEVERC_IR_LINKAGE_COUNT             UINT32_C(11)
#define NEVERC_IR_CALLING_CONVENTION_COUNT  UINT32_C(21)
#define NEVERC_IR_PROPERTY_COUNT            UINT32_C(23)
```

Los identificadores llevan su dominio en el byte alto —`0x41……` para tipos,
`0x42……` para géneros de valor, `0x43……` para códigos de operación, `0x49……`
para propiedades—, de modo que un valor usado en el lugar equivocado se rechaza
en vez de malinterpretarse.

## Descriptores y propiedad

Los descriptores de IR son pares opacos `{Owner, Value}` con alcance de una
tarea, y el anfitrión es dueño de todo lo que hay detrás.

- Nunca conserve un descriptor después de que acabe su devolución de llamada o
  su tarea.
- Nunca use un descriptor en otra sesión u otra tarea.
- Una sustitución confirmada invalida los descriptores de los objetos
  sustituidos.
- Una modificación abortada deja obsoletos los descriptores que ella creó.
- Los errores son `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE` o `WRONG_TYPE`,
  nunca un puntero de LLVM en crudo.

Las cadenas y vistas de bytes que devuelve una consulta están prestadas durante
la devolución de llamada. La única excepción es `ExportModule`, que devuelve un
`NevercIRSerializedBufferHandle` que debe entregar a
`ReleaseSerializedBuffer`.

## Recorrer un módulo

Las colecciones se leen mediante un cursor que lleva su propia generación, así
que una modificación a mitad del recorrido se detecta en lugar de omitir
entradas en silencio:

```c
NevercIRValueCursor Cursor = {0};
Cursor.Header = (NevercABITableHeader){sizeof(Cursor),
                                       NEVERC_IR_CORE_API_MAJOR,
                                       NEVERC_IR_CORE_API_MINOR, 0};
Core->BeginValueCursor(Core->Context, Task, Module,
                       NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);

NevercIRValueHandle Batch[32];
uint64_t Count = 0;
for (;;) {
  Core->CollectValueCursor(Core->Context, Task, &Cursor, Batch, 32, &Count);
  if (Count == 0)
    break;
  for (uint64_t I = 0; I != Count; ++I) {
    NevercStringView Name;
    Core->GetValueName(Core->Context, Task, Batch[I], &Name);
  }
}
```

Repita hasta que `Count` vuelva a cero. Las siete colecciones son
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS` y `BLOCK_INSTRUCTIONS`.

Todo lo demás es una consulta directa: `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*`, y a nivel de módulo `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly` con
sus asignadores.

## Tipos y constantes

Los tipos están internados, así que preguntar dos veces da el mismo descriptor:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` toma un género de esquema como `NEVERC_IR_TYPE_VOID`,
`_FLOAT`, `_DOUBLE` o `_TOKEN`; `GetArrayType`, `GetVectorType` (con una bandera
`Scalable`) y `GetStructType` (con nombre o literal, empaquetada o no) cubren el
resto.

Las constantes enteras y de coma flotante se construyen a partir de palabras de
64 bits little-endian, de modo que un `i128` no necesita ninguna vía especial:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant` y `GetGlobalAddressConstant` cubren los casos
sencillos; `CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression` y `CreateConstantGEPExpression` construyen
expresiones constantes.

## Propiedades de instrucción

En vez de un accesor por bandera, el detalle de una instrucción pasa por un
valor de propiedad etiquetado, indexado por identificador de esquema:

```c
typedef struct NevercIRPropertyValue {
  NevercABITableHeader Header;
  NevercIRPropertyValueKind Kind;   /* BOOL, UINT, ENUM, FLAGS, STRING, TYPE */
  uint32_t Reserved;
  uint64_t UnsignedValue;
  NevercIRTypeHandle TypeValue;
  NevercStringView StringValue;
} NevercIRPropertyValue;

NevercIRPropertyValue Value = {0};
Value.Header = /* … */;
Core->GetInstructionProperty(Core->Context, Task, Instruction,
                             NEVERC_IR_PROPERTY_ALIGNMENT, &Value);
```

Las 23 propiedades son `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`,
`DISJOINT`, `VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`,
`PREDICATE`, `CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP` y `NUSW`. Los órdenes atómicos van de
`NOT_ATOMIC` a `SEQUENTIALLY_CONSISTENT`; los géneros de llamada terminal son
`NONE`, `TAIL`, `MUST_TAIL` y `NO_TAIL`; las banderas fast-math son los siete
bits habituales, de `ALLOW_REASSOC` a `APPROX_FUNC`.

## Atributos

Los atributos son valores que se crean y luego se adjuntan, lo que mantiene
uniformes los cuatro géneros (`ENUM`, `INTEGER`, `STRING`, `TYPE`):

```c
NevercIRAttributeHandle NoInline;
Core->CreateEnumAttribute(Core->Context, Task, SV("noinline"), &NoInline);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION,
                           /*ParameterIndex=*/0, NoInline);

NevercBool Present = NEVERC_FALSE;
Core->HasFunctionAttribute(Core->Context, Task, Function, SV("noinline"),
                           &Present);
```

`pluginsdk/examples/CustomCallConvPlugin.c` usa esto junto con
`GetFunctionStringAttribute` para gobernar una convención de llamada definida
por datos.

## Modificación transaccional

Todo cambio estructural pasa por `NevercIRBuilderAPI`. La modificación es la
transacción; el constructor es un cursor dentro de ella.

```c
NevercIRMutationHandle Mutation;
NevercIRBuilderHandle Builder;

Builders->BeginMutation(Builders->Context, Task,
                        NEVERC_IR_MUTATION_SCOPE_FUNCTION, Function,
                        &Mutation);
Builders->CreateBuilder(Builders->Context, Task, Mutation, &Builder);
Builders->SetInsertBefore(Builders->Context, Task, Builder, Terminator);

NevercIRValueHandle Sum;
Builders->BuildBinary(Builders->Context, Task, Builder,
                      NEVERC_IR_OPCODE_ADD, Left, Right, SV("sum"), &Sum);

Status = Builders->CommitMutation(Builders->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Builders->AbortMutation(Builders->Context, Task, Mutation);

Builders->DestroyBuilder(Builders->Context, Task, Builder);
Builders->DestroyMutation(Builders->Context, Task, Mutation);
```

Los alcances son `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION` y `_LOOP`;
`ScopeRoot` nombra la función o la cabecera de bucle. La confirmación verifica el
candidato y publica atómicamente: si el verificador falla, el anfitrión deshace
los cambios y el módulo anterior sobrevive intacto.

Las llamadas de construcción son `BuildBinary`, `BuildUnary`, `BuildCompare`,
`BuildCast`, `BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`,
`BuildGetElementPtr`, `BuildCall`, `BuildPhi`, `BuildBranch`,
`BuildConditionalBranch`, `BuildUnreachable`, `BuildReturn` y
`BuildReturnVoid`. `SetDebugLocation` y `SetFastMathFlags` se aplican a todo lo
que el constructor emita después.

Fíjese en la asimetría: `AddPhiIncoming`, `CreateFunction` y `CreateBasicBlock`
toman la **modificación**, no el constructor, porque no están atadas a un punto
de inserción.

`DestroyMutation` es distinto de confirmar y abortar. Cada `BeginMutation`
necesita exactamente un `DestroyMutation`, terminase como terminase la
transacción.

## Pasadas

```c
NevercIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                                     NEVERC_IR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.function-pass");
Pass.Phase         = (NevercInterfaceID){
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW};
Pass.Level         = NEVERC_IR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Cacheable     = NEVERC_TRUE;
Pass.Run           = run_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

Los niveles son `MODULE`, `CGSCC`, `FUNCTION` y `LOOP`. La invocación solo lleva
los descriptores válidos para su nivel:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION y LOOP       */
  NevercIRValueHandle LoopHeader;               /* solo LOOP             */
  const NevercIRValueHandle *SCCFunctions;      /* solo CGSCC            */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

Los tres punteros de API llegan con la invocación, de modo que el cuerpo de una
pasada no necesita guardar ninguna tabla.

Informe de lo que sobrevivió mediante `OutPreserved`:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* o _NONE, o _CFG */
```

`NEVERC_IR_PRESERVE_CFG` significa que el grafo de flujo de control está intacto
aunque hayan cambiado instrucciones. Los análisis propios se preservan
listándolos en `CustomAnalyses`. No declare `PRESERVE_ALL` tras cambiar la IR: el
adaptador compara la generación del módulo y rechaza una declaración falsa.

Las pasadas de función y de bucle pueden ejecutarse a la vez, así que el estado
mutable del plugin debe casar con el `NevercConcurrencyModel` declarado.

## Análisis

Siete análisis nativos se pueden consultar por identificador:
`DOMINATOR_TREE`, `POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`,
`MEMORY_SSA`, `CALL_GRAPH` y `ALIAS`.

```c
NevercIRAnalysisResultHandle Loops;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_IR_ANALYSIS_LOOP_INFO, Function, &Loops);

uint64_t LoopCount = 0;
Analyses->GetLoopCount(Analyses->Context, Task, Loops, &LoopCount);
for (uint64_t I = 0; I != LoopCount; ++I) {
  NevercIRValueHandle Header;
  Analyses->GetLoopHeader(Analyses->Context, Task, Loops, I, &Header);
}
```

Cada uno tiene accesores tipados en lugar de un bloque opaco:
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind` (`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee`, y `Alias` (`NO`, `MAY`, `PARTIAL`,
`MUST`).

Un análisis de plugin se registra con su propio ciclo de vida:

```c
NevercIRAnalysisDescriptor Analysis = {0};
Analysis.Header          = /* … */;
Analysis.AnalysisID      = MyAnalysisID;
Analysis.Name            = SV("example.my-analysis");
Analysis.Level           = NEVERC_IR_PASS_LEVEL_FUNCTION;
Analysis.Dependencies    = Deps;
Analysis.DependencyCount = DepCount;
Analysis.Compute         = compute;
Analysis.Query           = query;
Analysis.Invalidate      = invalidate;
Analysis.Destroy         = destroy;
Analyses->RegisterAnalysis(Analyses->Context, RegistrarContext, &Analysis);
```

A `Invalidate` se le dice el motivo: `INVALIDATED_BY_PASS` o
`INVALIDATED_BY_PLAN_DESTROY`. Los resultados se guardan en caché por invocación
y se descartan según lo que preservara la pasada en curso. Los ciclos de
dependencia se rechazan al registrar, y modificar la IR desde una devolución de
llamada de análisis se deniega.

## Sustituir la generación y la optimización

`NevercIRGenAPI` sustituye a `neverc.ir.generate`:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … construir el módulo … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` parte de bitcode o de IR textual en lugar de un módulo vacío.
`NevercIROptimizationAPI` tiene la misma forma para `neverc.ir.optimize`, más
`GetInputModule` para alcanzar el módulo entrante y `RunBuiltinPipeline` para
delegar en la cadena nativa y luego posprocesar su resultado.

Ambas vías publican a través del anfitrión en vez de devolver un puntero, ambas
verifican la compatibilidad de destino y ambas conservan atómicamente el módulo
antiguo si la publicación falla. `neverc.ir.final_verify` se ejecuta igualmente
después.

## Ejemplos

| Archivo | Muestra |
|---|---|
| `pluginsdk/examples/FunctionPass.c` | Una pasada de función de solo lectura, con negociación de ABI incluida |
| `pluginsdk/examples/ExamplePlugin.c` | Una pasada de módulo que recorre funciones con un cursor de valores |
| `pluginsdk/examples/CustomCallConvPlugin.c` | Atributos y propiedades del punto de llamada |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Use el sufijo de módulo que CMake haya producido para su plataforma.

## Reglas

- Devuelva un `NevercStatus` desde cada devolución de llamada. El fallo de un
  plugin se convierte en un diagnóstico estructurado; nunca deje que una
  excepción cruce la frontera de C.
- Ponga a cero cada estructura de salida y fije su `Header` antes de la llamada
  que la rellena.
- No incruste valores numéricos de código de operación, tipo o propiedad. Use los
  nombres de `PluginIRSchema.inc` para que una revisión del esquema sea un error
  de compilación.
- Cada `BeginMutation` llega exactamente a un `DestroyMutation`, y cada
  `CreateBuilder` a exactamente un `DestroyBuilder`, también en las rutas de
  error.
- Libere lo que le entrega `ExportModule` con `ReleaseSerializedBuffer`.
- Nunca declare `NEVERC_IR_PRESERVE_ALL` después de modificar la IR.
- Dé por hecho que las pasadas de función y de bucle corren en paralelo, salvo
  que el plugin haya declarado `NEVERC_CONCURRENCY_SESSION_SERIAL`.
- `neverc.ir.final_verify` está sellada. Nada de lo que haga un plugin puede
  saltársela.

Consulte `PluginIR.h`, `Schema/PluginIRSchema.inc`, `Schema/PhaseSchema.json` y
`coverage.json` para las declaraciones normativas, las constantes de esquema,
las políticas de fase y las pruebas de los tests.
