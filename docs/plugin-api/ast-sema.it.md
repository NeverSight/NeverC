**Lingue**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# API AST e semantica dei plugin NeverC

Tre tabelle coprono il front end. `NevercParserAPI` consente a un plugin di
farsi carico di un pezzo di analisi sintattica — una nuova forma di
dichiarazione, un nuovo statement — guidando un cursore di token dotato di punti
di ripristino. `NevercASTAPI` legge l'albero e lo modifica in modo
transazionale. `NevercSemaAPI` si occupa di ricerca dei nomi, costruzione dei
tipi, classificazione delle conversioni e valutazione delle costanti.

L'AST è esposto tramite uno **schema**, non tramite un rispecchiamento in C
della gerarchia di classi di Clang. I nodi sono handle opachi: si chiede una
proprietà per ID stabile e si riceve un `NevercASTValue` etichettato. È questa
indirezione a rendere la superficie stabile fra le versioni di LLVM.

## Interfacce

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| Interfaccia | Tabella | Macro di versione |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

`Schema/PluginASTSchema.inc` fornisce gli ID di genere di nodo, di proprietà e di
slot figlio; il suo major di capacità deve essere uguale a
`NEVERC_AST_API_MAJOR`.

## Fasi

Sette fasi sintattiche e sette semantiche, tutte
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`:

| Sintassi | Semantica |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` consuma un flusso di token e produce un'unità AST;
`neverc.sema.analyze` consuma quell'unità e produce un'unità semantica. Le fasi
`extension.*` sono i ganci per le estensioni di linguaggio: l'host chiede se
qualche plugin voglia gestire questo costrutto prima di ripiegare sul
comportamento nativo.

## Il modello a schema

Ogni nodo è un `NevercASTNodeHandle`, con alias tipizzati
(`NevercDeclHandle`, `NevercStmtHandle`, `NevercExprHandle`,
`NevercTypeHandle`, `NevercAttrHandle`, `NevercDeclContextHandle`,
`NevercTypeLocHandle`). La navigazione strutturale è uniforme:

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

`Domain` è uno fra `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`, `TYPE_LOC`
e `ATTR`.

Le proprietà si leggono per ID dentro un valore etichettato:

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

`Type` sceglie quale membro sia vivo: `NEVERC_AST_VALUE_BOOL`, `I64`, `U64`,
`STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`, `TYPE_LOC`,
`ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX` o
`ALIGNMENT_OPERAND`. Lo schema registra per ogni proprietà la modalità di
accesso (`READ_ONLY`, `READ_WRITE`, `BUILD_ONLY`) e la cardinalità
(`REQUIRED`, `OPTIONAL`, `MANY`), così un tentativo di scrivere una proprietà in
sola lettura fallisce a livello di API invece di corrompere l'albero.

Percorrere molti nodi in una volta costa meno con le chiamate a lotti, che
prendono un passo di uscita in modo da scrivere direttamente nel vostro array di
strutture:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## Accessori tipizzati

Per i costrutti che i plugin toccano più spesso esistono lettori diretti anziché
ricerche di proprietà:

| Chiamata | Restituisce |
|---|---|
| `GetTranslationUnit` | La dichiarazione radice |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | Nome, tipo, tipo di ritorno, corpo, numero di parametri, variadica, definizione |
| `GetVarDeclInfo` | Nome, tipo, inizializzatore, definizione, memorizzazione globale |
| `GetRecordDeclInfo` | Nome, numero di campi, completo, unione, membro array flessibile |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | Genere di attributo, grafia, implicito, ereditato |
| `GetDeclRefExprInfo` | Dichiarazione riferita e trovata, tipo |
| `GetCallExprInfo`, `GetCallExprArgument` | Chiamato, chiamato diretto, tipo, argomenti |
| `GetBinaryOperatorInfo` | Sinistra, destra, tipo, grafia e genere dell'operatore |
| `GetCompoundStmtInfo` | Numero di statement |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | Ampiezza in bit e parole little-endian |
| `GetTypeInfo`, `GetTypeElement` | Descrizione completa del tipo |
| `GetBuiltinType` | Un tipo intrinseco tramite `NevercBuiltinTypeKind` |

`NevercTypeInfo` è la più ricca fra queste:

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

I generi di tipo intrinseco vanno da `NEVERC_BUILTIN_TYPE_VOID` e `_BOOL`, su
per la scala degli interi, fino a `_LONG_DOUBLE`; i generi di operatore binario
da `NEVERC_BINARY_OPERATOR_MUL` a `_COMMA`.

## Costruire e modificare

La costruzione usa un builder, la modifica una transazione. I due si compongono:
prima si costruisce il nodo sostitutivo, poi lo si scambia.

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

`ASTBuilderSetIntegerValue` prende una `NevercAPIntView` (parole little-endian
più ampiezza in bit) per i letterali più larghi di 64 bit, mentre
`ASTBuilderSetBinaryOperatorKind` imposta l'operatore di un'espressione binaria.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* oppure AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

Il commit verifica l'albero in staging e lo pubblica atomicamente. Un commit
fallito lascia intatto l'albero precedente, e un abort rende obsoleti gli handle
creati da quella modifica.
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
mostra l'intero ciclo, intercettazione del parser inclusa.

## Eventi di ciclo di vita

Invece di interrogare a ripetizione, sottoscrivete gli undici punti in cui il
front end pubblica una dichiarazione:

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

I generi sono `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT` e `SEMA_END`; `NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` li copre
tutti e undici. L'evento porta l'unità di traduzione, una singola dichiarazione
e un array di dichiarazioni — tutto in sola lettura e prestato per la durata
della callback.

## Estensione del parser

Un'estensione del parser riceve un cursore di token con l'analisi speculativa
già incorporata:

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
  /* … costruire un nodo … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

`ExpectedResult`, nell'ingresso, vi dice che cosa serve al parser:
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE` o `ATTRIBUTE`.
`CreateParsedAttribute` costruisce un attributo in forma GNU
(`__attribute__`), C23 (`[[…]]`) o `__declspec`.

Un provider per `neverc.syntax.parse` stesso pubblica un'intera unità AST:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` riporta il `SemanticState` dell'unità. Un'unità pubblicata come
`NEVERC_AST_UNIT_UNANALYZED` verrà ripercorsa dall'analisi semantica;
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` afferma che il provider ha già svolto
quel lavoro.

## Interrogazioni semantiche

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* oppure _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind è NOT_FOUND, FOUND o AMBIGUOUS; segue Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo` e `GetScopeDeclaration` risalgono la catena
degli ambiti; i flag di ambito sono `NEVERC_SEMA_SCOPE_FILE`, `FUNCTION`,
`RECORD` e `BLOCK`.

La valutazione delle costanti restituisce un handle le cui informazioni
descrivono la forma del valore:

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

Le conversioni vengono classificate prima di essere applicate, così un plugin
può ispezionare la decisione:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind spazia su COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE e altri;
   seguono SeqInfo.Viable e .RequiresDiagnostic. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType` e `GetBuiltinInfo`
completano la superficie in sola lettura.

## Il contratto di modifica

Tutto ciò che cambia lo stato semantico — creare un tipo, applicare una
conversione, emettere una diagnostica semantica — richiede un contratto
(lease). È il contratto a rendere sicuro il lavoro semantico concorrente:

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
`CreateVectorType`, `CreateExplicitCast` ed `EmitDiagnostic` prendono tutti il
contratto. I contesti di conversione sono
`NEVERC_SEMA_CONVERSION_ASSIGNMENT`, `ARGUMENT`, `RETURN`, `INITIALIZATION` ed
`EXPLICIT_CAST`.

## Fasi di estensione semantica

Ogni fase di estensione ha una coppia ingresso/uscita corrispondente. Il gancio
delle espressioni, per esempio:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* oppure _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

La stessa forma vale per `Statement`, `Declaration`, `Type`, `Lookup` e
`Conversion`. Restituire `NEVERC_SEMA_EXTENSION_UNHANDLED` lascia scorrere il
comportamento nativo.

Un provider per `neverc.sema.analyze` pubblica l'unità semantica:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` riporta il `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` oppure `_HAS_ERROR`), se l'unità sia stata
ripercorsa, e un riepilogo del verificatore.

## Regole

- Gli handle di AST e di tipo hanno ambito di task. Non conservatene mai uno
  oltre la callback.
- Ogni builder, modifica, risultato di ricerca, sequenza di conversione e valore
  costante ha il suo `Destroy*`; chiamatelo anche sul percorso di errore.
- Una modifica semantica senza contratto restituisce
  `NEVERC_STATUS_INVALID_STATE`.
- Non modificate l'albero da un osservatore di ciclo di vita: gli osservatori
  sono in sola lettura. Usate un intercettore sulla fase corrispondente.
- Gli ID di proprietà e di slot figlio sono costanti dello schema. Non
  incorporate letterali numerici; usate i nomi di `PluginASTSchema.inc`, così una
  revisione dello schema diventa un errore di compilazione.
- Controllate `HAS_KNOWN_LAYOUT` in `NevercTypeInfo.Flags` prima di fidarvi di
  `SizeInBits` o `AlignmentInBits`.

Vedere `PluginAST.h`, `PluginSema.h` e `Schema/ASTSchema.json` per le
dichiarazioni normative, e `pluginsdk/examples/ASTRewritePlugin.c` per
un'intercettazione del parser e una riscrittura atomica dell'albero
funzionanti.
