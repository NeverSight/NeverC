**Sprachen**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# NeverC Plugin-API für AST und Semantik

Drei Tabellen decken das Frontend ab. `NevercParserAPI` lässt ein Plugin ein
Stück Syntaxanalyse übernehmen — eine neue Deklarationsform, eine neue
Anweisung —, indem es einen Token-Cursor mit Prüfpunkten steuert.
`NevercASTAPI` liest den Baum und verändert ihn transaktional. `NevercSemaAPI`
erledigt Namenssuche, Typkonstruktion, Konvertierungsklassifikation und
Konstantenauswertung.

Der AST wird über ein **Schema** offengelegt, nicht über eine C-Spiegelung von
Clangs Klassenhierarchie. Knoten sind opake Handles; man fragt eine Eigenschaft
über eine stabile ID ab und erhält einen getaggten `NevercASTValue` zurück.
Genau diese Indirektion hält die Oberfläche über LLVM-Versionen hinweg stabil.

## Schnittstellen

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| Schnittstelle | Tabelle | Versionsmakros |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

`Schema/PluginASTSchema.inc` liefert die IDs für Knotenarten, Eigenschaften und
Kind-Slots; sein Capability-Major muss gleich `NEVERC_AST_API_MAJOR` sein.

## Phasen

Sieben Syntaxphasen und sieben Semantikphasen, allesamt
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`:

| Syntax | Semantik |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` verbraucht einen Tokenstrom und erzeugt eine AST-Einheit;
`neverc.sema.analyze` verbraucht diese Einheit und erzeugt eine semantische
Einheit. Die `extension.*`-Phasen sind die Haken für Spracherweiterungen: der
Host fragt, ob ein Plugin dieses Konstrukt behandeln möchte, bevor er auf das
eingebaute Verhalten zurückfällt.

## Das Schemamodell

Jeder Knoten ist ein `NevercASTNodeHandle`, mit typisierten Aliasen
(`NevercDeclHandle`, `NevercStmtHandle`, `NevercExprHandle`,
`NevercTypeHandle`, `NevercAttrHandle`, `NevercDeclContextHandle`,
`NevercTypeLocHandle`). Die strukturelle Navigation ist einheitlich:

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

`Domain` ist eines von `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`,
`TYPE_LOC` oder `ATTR`.

Eigenschaften werden per ID in einen getaggten Wert gelesen:

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

`Type` bestimmt, welches Glied gültig ist: `NEVERC_AST_VALUE_BOOL`, `I64`,
`U64`, `STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`,
`TYPE_LOC`, `ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX` oder
`ALIGNMENT_OPERAND`. Das Schema hält für jede Eigenschaft den Zugriffsmodus
(`READ_ONLY`, `READ_WRITE`, `BUILD_ONLY`) und die Kardinalität (`REQUIRED`,
`OPTIONAL`, `MANY`) fest, sodass der Versuch, eine schreibgeschützte
Eigenschaft zu schreiben, an der API scheitert, statt den Baum zu beschädigen.

Viele Knoten auf einmal zu durchlaufen ist über die Stapelaufrufe günstiger; sie
nehmen eine Ausgabeschrittweite, damit Sie direkt in Ihr eigenes Strukturfeld
schreiben können:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## Typisierte Zugriffsfunktionen

Für die Konstrukte, die Plugins am häufigsten anfassen, gibt es direkte Leser
statt Eigenschaftsabfragen:

| Aufruf | Liefert |
|---|---|
| `GetTranslationUnit` | Die Wurzeldeklaration |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | Name, Typ, Rückgabetyp, Rumpf, Parameteranzahl, variadisch, Definition |
| `GetVarDeclInfo` | Name, Typ, Initialisierer, Definition, globale Speicherung |
| `GetRecordDeclInfo` | Name, Feldanzahl, vollständig, Union, flexibles Feldelement |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | Attributart, Schreibweise, implizit, geerbt |
| `GetDeclRefExprInfo` | Referenzierte und gefundene Deklaration, Typ |
| `GetCallExprInfo`, `GetCallExprArgument` | Aufgerufener, direkt Aufgerufener, Typ, Argumente |
| `GetBinaryOperatorInfo` | Links, rechts, Typ, Schreibweise und Art des Operators |
| `GetCompoundStmtInfo` | Anzahl der Anweisungen |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | Bitbreite und Little-Endian-Wörter |
| `GetTypeInfo`, `GetTypeElement` | Vollständige Typbeschreibung |
| `GetBuiltinType` | Ein eingebauter Typ nach `NevercBuiltinTypeKind` |

`NevercTypeInfo` ist die reichhaltigste dieser Strukturen:

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

Die eingebauten Typarten reichen von `NEVERC_BUILTIN_TYPE_VOID` und `_BOOL`
über die Ganzzahlleiter bis `_LONG_DOUBLE`, die binären Operatorarten von
`NEVERC_BINARY_OPERATOR_MUL` bis `_COMMA`.

## Bauen und verändern

Konstruktion läuft über einen Erbauer, Veränderung über eine Transaktion. Beide
greifen ineinander: bauen Sie zuerst den Ersatzknoten, tauschen Sie ihn dann
ein.

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

`ASTBuilderSetIntegerValue` nimmt für Literale breiter als 64 Bit eine
`NevercAPIntView` (Little-Endian-Wörter plus Bitbreite), und
`ASTBuilderSetBinaryOperatorKind` setzt den Operator eines binären Ausdrucks.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* oder AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

Das Festschreiben prüft den vorbereiteten Baum und veröffentlicht ihn atomar.
Ein gescheitertes Festschreiben lässt den vorherigen Baum unangetastet, und ein
Abbruch macht die von der Veränderung erzeugten Handles ungültig.
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
zeigt den gesamten Zyklus einschließlich Parser-Abfangen.

## Lebenszyklusereignisse

Statt zu pollen, abonnieren Sie die elf Punkte, an denen das Frontend eine
Deklaration veröffentlicht:

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

Die Arten sind `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT` und `SEMA_END`; `NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` deckt
alle elf ab. Das Ereignis trägt die Übersetzungseinheit, eine einzelne
Deklaration und ein Deklarationsfeld — alles schreibgeschützt und für die Dauer
des Rückrufs geliehen.

## Parser-Erweiterung

Eine Parser-Erweiterung erhält einen Token-Cursor mit eingebauter spekulativer
Analyse:

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
  /* … einen Knoten bauen … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

`ExpectedResult` in der Eingabe sagt Ihnen, was der Parser braucht:
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE` oder `ATTRIBUTE`.
`CreateParsedAttribute` baut ein Attribut in GNU- (`__attribute__`), C23-
(`[[…]]`) oder `__declspec`-Form.

Ein Anbieter für `neverc.syntax.parse` selbst veröffentlicht eine ganze
AST-Einheit:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` meldet den `SemanticState` der Einheit. Eine als
`NEVERC_AST_UNIT_UNANALYZED` veröffentlichte Einheit wird durch die semantische
Analyse erneut abgespielt; `NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` behauptet,
der Anbieter habe diese Arbeit bereits erledigt.

## Semantische Abfragen

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* oder _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind ist NOT_FOUND, FOUND oder AMBIGUOUS; danach Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo` und `GetScopeDeclaration` laufen die
Gültigkeitskette ab; die Bereichsflags sind `NEVERC_SEMA_SCOPE_FILE`,
`FUNCTION`, `RECORD` und `BLOCK`.

Die Konstantenauswertung liefert ein Handle, dessen Info die Gestalt des Werts
beschreibt:

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

Konvertierungen werden klassifiziert, bevor sie angewandt werden, sodass ein
Plugin die Entscheidung prüfen kann:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind reicht über COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE und weitere;
   danach folgen SeqInfo.Viable und .RequiresDiagnostic. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType` und `GetBuiltinInfo`
runden die schreibgeschützte Oberfläche ab.

## Die Veränderungspacht

Alles, was semantischen Zustand ändert — einen Typ erzeugen, eine Konvertierung
anwenden, eine semantische Diagnose ausgeben —, braucht eine Pacht (Lease). Die
Pacht ist es, die nebenläufige semantische Arbeit sicher macht:

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
`CreateVectorType`, `CreateExplicitCast` und `EmitDiagnostic` nehmen alle die
Pacht entgegen. Die Konvertierungskontexte sind
`NEVERC_SEMA_CONVERSION_ASSIGNMENT`, `ARGUMENT`, `RETURN`, `INITIALIZATION` und
`EXPLICIT_CAST`.

## Semantische Erweiterungsphasen

Jede Erweiterungsphase hat ein passendes Eingabe/Ausgabe-Paar. Der Haken für
Ausdrücke etwa:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* oder _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

Dieselbe Gestalt gilt für `Statement`, `Declaration`, `Type`, `Lookup` und
`Conversion`. `NEVERC_SEMA_EXTENSION_UNHANDLED` zurückzugeben lässt das
eingebaute Verhalten laufen.

Ein Anbieter für `neverc.sema.analyze` veröffentlicht die semantische Einheit:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` meldet den `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` oder `_HAS_ERROR`), ob die Einheit erneut
abgespielt wurde, und eine Zusammenfassung des Prüfers.

## Regeln

- AST- und Typ-Handles gelten nur innerhalb der Aufgabe. Bewahren Sie keines
  über den Rückruf hinaus auf.
- Jeder Erbauer, jede Veränderung, jedes Suchergebnis, jede Konvertierungsfolge
  und jeder Konstantenwert hat ein passendes `Destroy*`; rufen Sie es auch auf
  dem Fehlerpfad auf.
- Eine semantische Veränderung ohne Pacht liefert
  `NEVERC_STATUS_INVALID_STATE`.
- Verändern Sie den Baum nicht aus einem Lebenszyklus-Beobachter heraus —
  Beobachter sind schreibgeschützt. Nehmen Sie einen Interzeptor auf der
  entsprechenden Phase.
- Eigenschafts- und Kind-Slot-IDs sind Schemakonstanten. Schreiben Sie keine
  Zahlenliterale fest ein; verwenden Sie die Namen aus `PluginASTSchema.inc`,
  damit eine Schemaüberarbeitung zum Übersetzungsfehler wird.
- Prüfen Sie `NevercTypeInfo.Flags` auf `HAS_KNOWN_LAYOUT`, bevor Sie
  `SizeInBits` oder `AlignmentInBits` vertrauen.

Die normativen Deklarationen stehen in `PluginAST.h`, `PluginSema.h` und
`Schema/ASTSchema.json`; ein funktionierendes Parser-Abfangen samt atomarer
Baumumschreibung zeigt `pluginsdk/examples/ASTRewritePlugin.c`.
