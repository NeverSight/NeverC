**Idiomas**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API de AST y semántica de los plugins de NeverC

Tres tablas cubren el frontal del compilador. `NevercParserAPI` permite que un
plugin se haga cargo de un trozo del análisis sintáctico —una nueva forma de
declaración, una nueva sentencia— manejando un cursor de tokens con puntos de
control. `NevercASTAPI` lee el árbol y lo modifica de forma transaccional.
`NevercSemaAPI` se ocupa de la búsqueda de nombres, la construcción de tipos, la
clasificación de conversiones y la evaluación de constantes.

El AST se expone mediante un **esquema**, no mediante un reflejo en C de la
jerarquía de clases de Clang. Los nodos son descriptores opacos; se pide una
propiedad por identificador estable y se recibe un `NevercASTValue` etiquetado.
Esa indirección es lo que mantiene estable la superficie entre versiones de
LLVM.

## Interfaces

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| Interfaz | Tabla | Macros de versión |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

[`Schema/PluginASTSchema.inc`] suministra los identificadores de género de nodo,
de propiedad y de ranura hija; su mayor de capacidad debe ser igual a
`NEVERC_AST_API_MAJOR`.

## Fases

Siete fases sintácticas y siete semánticas, todas
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`:

| Sintaxis | Semántica |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` consume un flujo de tokens y produce una unidad AST;
`neverc.sema.analyze` consume esa unidad y produce una unidad semántica. Las
fases `extension.*` son los enganches para extensiones del lenguaje: el
anfitrión pregunta si algún plugin quiere atender esta construcción antes de
recurrir al comportamiento nativo.

## El modelo de esquema

Cada nodo es un `NevercASTNodeHandle`, con alias tipados
(`NevercDeclHandle`, `NevercStmtHandle`, `NevercExprHandle`,
`NevercTypeHandle`, `NevercAttrHandle`, `NevercDeclContextHandle`,
`NevercTypeLocHandle`). La navegación estructural es uniforme:

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind, .Domain, .Parent, .DeclContext, .SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

`Domain` es uno de `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`, `TYPE_LOC`
o `ATTR`.

Las propiedades se leen por identificador en un valor etiquetado:

```c
typedef struct NevercASTValue {
  NevercABITableHeader Header;
  NevercASTValueType Type;
  uint32_t Reserved;
  int64_t SignedValue;
  uint64_t UnsignedValue;
  NevercStringView StringValue;
  NevercSourceRange SourceRangeValue;
  NevercASTNodeHandle NodeValue;
} NevercASTValue;
```

`Type` selecciona qué miembro está vivo: `NEVERC_AST_VALUE_BOOL`, `I64`, `U64`,
`STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`, `TYPE_LOC`,
`ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX` o
`ALIGNMENT_OPERAND`. El esquema registra el modo de acceso de cada propiedad
(`READ_ONLY`, `READ_WRITE`, `BUILD_ONLY`) y su cardinalidad (`REQUIRED`,
`OPTIONAL`, `MANY`), de modo que intentar escribir en una propiedad de solo
lectura falla en la API en lugar de corromper el árbol.

Recorrer muchos nodos de una vez sale más barato con las llamadas por lotes, que
toman un paso de salida para escribir directamente en su propio arreglo de
estructuras:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## Accesores tipados

Para las construcciones que los plugins tocan más a menudo hay lectores directos
en lugar de búsquedas de propiedad:

| Llamada | Entrega |
|---|---|
| `GetTranslationUnit` | La declaración raíz |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | Nombre, tipo, tipo de retorno, cuerpo, número de parámetros, variádica, definición |
| `GetVarDeclInfo` | Nombre, tipo, inicializador, definición, almacenamiento global |
| `GetRecordDeclInfo` | Nombre, número de campos, completo, unión, miembro de arreglo flexible |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | Género de atributo, grafía, implícito, heredado |
| `GetDeclRefExprInfo` | Declaración referida y hallada, tipo |
| `GetCallExprInfo`, `GetCallExprArgument` | Llamado, llamado directo, tipo, argumentos |
| `GetBinaryOperatorInfo` | Izquierda, derecha, tipo, grafía y género del operador |
| `GetCompoundStmtInfo` | Número de sentencias |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | Anchura en bits y palabras little-endian |
| `GetTypeInfo`, `GetTypeElement` | Descripción completa del tipo |
| `GetBuiltinType` | Un tipo intrínseco por `NevercBuiltinTypeKind` |

`NevercTypeInfo` es la más rica de estas estructuras:

```c
typedef struct NevercTypeInfo {
  NevercABITableHeader Header;
  NevercTypeKind Kind;
  NevercTypeQualifierFlags QualifierFlags;  /* CONST, RESTRICT, VOLATILE, UNALIGNED */
  NevercTypeFlags Flags;                    /* CANONICAL, SUGARED, DEPENDENT,
                                               INCOMPLETE, FUNCTION, VARIADIC,
                                               HAS_KNOWN_LAYOUT, POINTER, ARRAY,
                                               VECTOR, ATOMIC */
  NevercTypeAddressSpaceKind AddressSpaceKind;
  uint32_t TargetAddressSpace;
  uint32_t Reserved;
  uint64_t SizeInBits;
  uint64_t AlignmentInBits;
  uint64_t ElementCount;
  NevercTypeHandle CanonicalType;
  NevercTypeHandle DesugaredType;
  NevercTypeHandle RelatedType;
  NevercStringView Name;
} NevercTypeInfo;
```

Los géneros de tipo intrínseco van de `NEVERC_BUILTIN_TYPE_VOID` y `_BOOL`,
subiendo la escalera de los enteros, hasta `_LONG_DOUBLE`; los géneros de
operador binario van de `NEVERC_BINARY_OPERATOR_MUL` a `_COMMA`.

## Construir y modificar

La construcción usa un constructor; la modificación, una transacción. Se
componen: primero se construye el nodo de reemplazo y luego se intercambia.

```c
NevercASTBuilderHandle Builder;
AST->CreateASTBuilder(AST->Context, Task, NodeKind, &Builder);

NevercASTValue Value = {0};
Value.Header = (NevercABITableHeader){sizeof(Value), NEVERC_AST_API_MAJOR,
                                      NEVERC_AST_API_MINOR, 0};
Value.Type          = NEVERC_AST_VALUE_U64;
Value.UnsignedValue = 1;
AST->ASTBuilderSetProperty(AST->Context, Task, Builder, PropertyID, &Value);
AST->ASTBuilderSetChild(AST->Context, Task, Builder, SlotID, 0, ChildNode);

NevercASTNodeHandle NewNode;
AST->ASTBuilderCommit(AST->Context, Task, Builder, &NewNode);
AST->DestroyASTBuilder(AST->Context, Task, Builder);
```

`ASTBuilderSetIntegerValue` toma una `NevercAPIntView` (palabras little-endian
más anchura en bits) para literales de más de 64 bits, y
`ASTBuilderSetBinaryOperatorKind` fija el operador de una expresión binaria.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* o AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

La confirmación verifica el árbol preparado y lo publica atómicamente. Una
confirmación fallida deja intacto el árbol anterior, y un aborto deja obsoletos
los descriptores que creó la modificación. [`pluginsdk/examples/ASTRewritePlugin.c`]
muestra el ciclo completo, incluida la interceptación del analizador.

## Eventos del ciclo de vida

En vez de sondear, suscríbase a los once puntos donde el frontal publica una
declaración:

```c
NevercASTLifecycleObserverDescriptor Observer = {0};
Observer.Header = /* … */;
Observer.Events =
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL) |
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TRANSLATION_UNIT);
Observer.Callback = on_lifecycle;
Observer.UserData = State;
AST->RegisterLifecycleObserver(AST->Context, Task, &Observer);
```

Los géneros son `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT` y `SEMA_END`; `NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` cubre los
once. El evento lleva la unidad de traducción, una declaración suelta y un
arreglo de declaraciones, todo de solo lectura y prestado durante la devolución
de llamada.

## Extensión del analizador sintáctico

Una extensión del analizador recibe un cursor de tokens con análisis
especulativo incorporado:

```c
NevercParserExtensionInput In = {0};
In.Header = /* … */;
Parser->GetExtensionInput(Parser->Context, Frame, Frame->Input, &In);

NevercParserCheckpointHandle Checkpoint;
Parser->CursorCheckpoint(Parser->Context, Task, In.Cursor, &Checkpoint);

NevercTokenHandle Token;
Parser->CursorPeek(Parser->Context, Task, In.Cursor, /*Offset=*/0, &Token);
if (!is_my_construct(Token)) {
  Parser->CursorRollback(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_UNHANDLED;
} else {
  Parser->CursorConsume(Parser->Context, Task, In.Cursor, &Token);
  /* … construir un nodo … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

`ExpectedResult`, en la entrada, le dice qué necesita el analizador:
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE` o `ATTRIBUTE`.
`CreateParsedAttribute` construye un atributo en forma GNU (`__attribute__`),
C23 (`[[…]]`) o `__declspec`.

Un proveedor para `neverc.syntax.parse` en sí publica una unidad AST entera:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` informa del `SemanticState` de la unidad. Una unidad publicada
como `NEVERC_AST_UNIT_UNANALYZED` se reproducirá a través del análisis
semántico; `NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` afirma que el proveedor ya
hizo ese trabajo.

## Consultas semánticas

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* o _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind es NOT_FOUND, FOUND o AMBIGUOUS; sigue Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo` y `GetScopeDeclaration` recorren la cadena de
ámbitos; las banderas de ámbito son `NEVERC_SEMA_SCOPE_FILE`, `FUNCTION`,
`RECORD` y `BLOCK`.

La evaluación de constantes devuelve un descriptor cuya información describe la
forma del valor:

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind: NONE, INDETERMINATE, INTEGER, FLOAT, FIXED_POINT,
   COMPLEX_INTEGER, COMPLEX_FLOAT, ADDRESS, VECTOR, ARRAY, STRUCT, UNION,
   ADDRESS_LABEL_DIFFERENCE. */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

Las conversiones se clasifican antes de aplicarse, de manera que un plugin puede
inspeccionar la decisión:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind abarca COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE y más;
   siguen SeqInfo.Viable y .RequiresDiagnostic. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType` y `GetBuiltinInfo`
completan la superficie de solo lectura.

## El arriendo de modificación

Todo lo que cambia el estado semántico —crear un tipo, aplicar una conversión,
emitir un diagnóstico semántico— necesita un arriendo. El arriendo es lo que
hace seguro el trabajo semántico concurrente:

```c
NevercSemaMutationLeaseHandle Lease;
Sema->AcquireMutationLease(Sema->Context, Task, &Lease);

NevercTypeHandle Pointer;
Sema->CreatePointerType(Sema->Context, Task, Lease, Pointee, &Pointer);

NevercExprHandle Converted;
Sema->ApplyImplicitConversion(Sema->Context, Task, Lease, Sequence,
                              Expression, NEVERC_SEMA_CONVERSION_ARGUMENT,
                              &Converted);

Sema->ReleaseMutationLease(Sema->Context, Task, Lease);
```

`CreateConstantArrayType`, `CreateFunctionType`, `CreateAtomicType`,
`CreateVectorType`, `CreateExplicitCast` y `EmitDiagnostic` toman todos el
arriendo. Los contextos de conversión son
`NEVERC_SEMA_CONVERSION_ASSIGNMENT`, `ARGUMENT`, `RETURN`, `INITIALIZATION` y
`EXPLICIT_CAST`.

## Fases de extensión semántica

Cada fase de extensión tiene su pareja de entrada/salida. El enganche de
expresiones, por ejemplo:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* o _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

La misma forma se aplica a `Statement`, `Declaration`, `Type`, `Lookup` y
`Conversion`. Devolver `NEVERC_SEMA_EXTENSION_UNHANDLED` deja correr el
comportamiento nativo.

Un proveedor para `neverc.sema.analyze` publica la unidad semántica:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` informa del `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` o `_HAS_ERROR`), de si la unidad fue
reproducida y de un resumen del verificador.

## Reglas

- Los descriptores de AST y de tipo tienen alcance de tarea. Nunca guarde uno
  más allá de la devolución de llamada.
- Cada constructor, modificación, resultado de búsqueda, secuencia de conversión
  y valor constante tiene su `Destroy*` correspondiente; llámelo también en la
  ruta de error.
- Una modificación semántica sin arriendo devuelve
  `NEVERC_STATUS_INVALID_STATE`.
- No modifique el árbol desde un observador del ciclo de vida: los observadores
  son de solo lectura. Use un interceptor en la fase correspondiente.
- Los identificadores de propiedad y de ranura hija son constantes del esquema.
  No incruste literales numéricos; use los nombres de [`PluginASTSchema.inc`] para
  que una revisión del esquema sea un error de compilación.
- Compruebe `HAS_KNOWN_LAYOUT` en `NevercTypeInfo.Flags` antes de fiarse de
  `SizeInBits` o `AlignmentInBits`.

Consulte [`PluginAST.h`], [`PluginSema.h`] y [`Schema/ASTSchema.json`] para las
declaraciones normativas, y [`pluginsdk/examples/ASTRewritePlugin.c`] para una
interceptación de analizador y una reescritura atómica de árbol que funcionan.

<!-- reference links -->
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
[`pluginsdk/examples/ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`Schema/ASTSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ASTSchema.json
[`Schema/PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
