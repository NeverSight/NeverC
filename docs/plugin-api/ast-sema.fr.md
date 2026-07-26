**Langues**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← ABI de plugin NeverC](README.fr.md)

# API AST et sémantique des plugins NeverC

Trois tables couvrent la partie avant du compilateur. `NevercParserAPI` permet à
un plugin de reprendre à son compte un morceau d'analyse syntaxique — une
nouvelle forme de déclaration, une nouvelle instruction — en pilotant un curseur
de jetons doté de points de reprise. `NevercASTAPI` lit l'arbre et le modifie de
façon transactionnelle. `NevercSemaAPI` s'occupe de la recherche de noms, de la
construction de types, de la classification des conversions et de l'évaluation
des constantes.

L'AST est exposé par un **schéma**, et non par un miroir en C de la hiérarchie de
classes de Clang. Les nœuds sont des descripteurs opaques ; vous demandez une
propriété par identifiant stable et récupérez une `NevercASTValue` étiquetée.
C'est cette indirection qui rend la surface stable d'une version de LLVM à
l'autre.

## Interfaces

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| Interface | Table | Macros de version |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

[`Schema/PluginASTSchema.inc`] fournit les identifiants de genre de nœud, de
propriété et d'emplacement d'enfant ; son majeur de capacité doit être égal à
`NEVERC_AST_API_MAJOR`.

## Phases

Sept phases syntaxiques et sept phases sémantiques, toutes
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE` :

| Syntaxe | Sémantique |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` consomme un flux de jetons et produit une unité AST ;
`neverc.sema.analyze` consomme cette unité et produit une unité sémantique. Les
phases `extension.*` sont les points d'accroche des extensions de langage :
l'hôte demande si un plugin veut traiter cette construction avant de se rabattre
sur le comportement natif.

## Le modèle de schéma

Chaque nœud est un `NevercASTNodeHandle`, avec des alias typés
(`NevercDeclHandle`, `NevercStmtHandle`, `NevercExprHandle`,
`NevercTypeHandle`, `NevercAttrHandle`, `NevercDeclContextHandle`,
`NevercTypeLocHandle`). La navigation structurelle est uniforme :

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

`Domain` vaut `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`, `TYPE_LOC` ou
`ATTR`.

Les propriétés se lisent par identifiant dans une valeur étiquetée :

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

`Type` désigne le membre actif : `NEVERC_AST_VALUE_BOOL`, `I64`, `U64`,
`STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`, `TYPE_LOC`,
`ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX` ou
`ALIGNMENT_OPERAND`. Le schéma consigne pour chaque propriété son mode d'accès
(`READ_ONLY`, `READ_WRITE`, `BUILD_ONLY`) et sa cardinalité (`REQUIRED`,
`OPTIONAL`, `MANY`), si bien qu'écrire dans une propriété en lecture seule
échoue au niveau de l'API au lieu de corrompre l'arbre.

Parcourir de nombreux nœuds d'un coup revient moins cher avec les appels par
lot, qui prennent un pas de sortie afin d'écrire directement dans votre propre
tableau de structures :

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## Accesseurs typés

Pour les constructions que les plugins manipulent le plus, il existe des
lecteurs directs plutôt que des recherches de propriété :

| Appel | Fournit |
|---|---|
| `GetTranslationUnit` | La déclaration racine |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | Nom, type, type de retour, corps, nombre de paramètres, variadique, définition |
| `GetVarDeclInfo` | Nom, type, initialiseur, définition, stockage global |
| `GetRecordDeclInfo` | Nom, nombre de champs, complet, union, membre tableau flexible |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | Genre d'attribut, écriture, implicite, hérité |
| `GetDeclRefExprInfo` | Déclaration référencée et trouvée, type |
| `GetCallExprInfo`, `GetCallExprArgument` | Appelé, appelé direct, type, arguments |
| `GetBinaryOperatorInfo` | Gauche, droite, type, écriture et genre de l'opérateur |
| `GetCompoundStmtInfo` | Nombre d'instructions |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | Largeur en bits et mots petit-boutistes |
| `GetTypeInfo`, `GetTypeElement` | Description complète du type |
| `GetBuiltinType` | Un type intrinsèque par `NevercBuiltinTypeKind` |

`NevercTypeInfo` est la plus riche de ces structures :

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

Les genres de type intrinsèque vont de `NEVERC_BUILTIN_TYPE_VOID` et `_BOOL`,
en passant par l'échelle des entiers, jusqu'à `_LONG_DOUBLE` ; les genres
d'opérateur binaire vont de `NEVERC_BINARY_OPERATOR_MUL` à `_COMMA`.

## Construire et modifier

La construction passe par un constructeur, la modification par une transaction.
Les deux se composent : bâtissez d'abord le nœud de remplacement, puis
substituez-le.

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

`ASTBuilderSetIntegerValue` prend une `NevercAPIntView` (mots petit-boutistes
plus largeur en bits) pour les littéraux de plus de 64 bits, et
`ASTBuilderSetBinaryOperatorKind` fixe l'opérateur d'une expression binaire.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* ou AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

La validation vérifie l'arbre préparé et le publie atomiquement. Une validation
qui échoue laisse l'arbre précédent intact, et un abandon périme les
descripteurs que la modification avait créés.
[`pluginsdk/examples/ASTRewritePlugin.c`]
montre le cycle complet, interception de l'analyseur comprise.

## Événements de cycle de vie

Plutôt que d'interroger en boucle, abonnez-vous aux onze points où la partie
avant publie une déclaration :

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

Les genres sont `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT` et `SEMA_END` ; `NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` couvre
les onze. L'événement transporte l'unité de traduction, une déclaration unique
et un tableau de déclarations — le tout en lecture seule et emprunté pour la
durée du rappel.

## Extension de l'analyseur syntaxique

Une extension d'analyseur reçoit un curseur de jetons avec l'analyse spéculative
intégrée :

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
  /* … construire un nœud … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

`ExpectedResult`, sur l'entrée, vous dit ce que l'analyseur attend :
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE` ou `ATTRIBUTE`.
`CreateParsedAttribute` construit un attribut sous forme GNU
(`__attribute__`), C23 (`[[…]]`) ou `__declspec`.

Un fournisseur pour `neverc.syntax.parse` lui-même publie une unité AST
entière :

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` indique le `SemanticState` de l'unité. Une unité publiée comme
`NEVERC_AST_UNIT_UNANALYZED` sera rejouée à travers l'analyse sémantique ;
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` affirme que le fournisseur a déjà fait
ce travail.

## Requêtes sémantiques

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* ou _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind vaut NOT_FOUND, FOUND ou AMBIGUOUS ; suit Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo` et `GetScopeDeclaration` remontent la chaîne
des portées ; les drapeaux de portée sont `NEVERC_SEMA_SCOPE_FILE`,
`FUNCTION`, `RECORD` et `BLOCK`.

L'évaluation de constante renvoie un descripteur dont les informations
décrivent la forme de la valeur :

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind : NONE, INDETERMINATE, INTEGER, FLOAT, FIXED_POINT,
   COMPLEX_INTEGER, COMPLEX_FLOAT, ADDRESS, VECTOR, ARRAY, STRUCT, UNION,
   ADDRESS_LABEL_DIFFERENCE. */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

Les conversions sont classées avant d'être appliquées, si bien qu'un plugin peut
inspecter la décision :

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind couvre COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE et d'autres ;
   suivent SeqInfo.Viable et .RequiresDiagnostic. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType` et `GetBuiltinInfo`
complètent la surface en lecture seule.

## Le bail de modification

Tout ce qui change l'état sémantique — créer un type, appliquer une conversion,
émettre un diagnostic sémantique — exige un bail. C'est le bail qui rend sûr le
travail sémantique concurrent :

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
`CreateVectorType`, `CreateExplicitCast` et `EmitDiagnostic` prennent tous le
bail. Les contextes de conversion sont
`NEVERC_SEMA_CONVERSION_ASSIGNMENT`, `ARGUMENT`, `RETURN`, `INITIALIZATION` et
`EXPLICIT_CAST`.

## Phases d'extension sémantique

Chaque phase d'extension possède un couple entrée/sortie correspondant. Le point
d'accroche des expressions, par exemple :

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* ou _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

La même forme s'applique à `Statement`, `Declaration`, `Type`, `Lookup` et
`Conversion`. Renvoyer `NEVERC_SEMA_EXTENSION_UNHANDLED` laisse s'exécuter le
comportement natif.

Un fournisseur pour `neverc.sema.analyze` publie l'unité sémantique :

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` rapporte le `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` ou `_HAS_ERROR`), le fait que l'unité ait
été rejouée ou non, et un résumé du vérificateur.

## Règles

- Les descripteurs d'AST et de type ont la portée de la tâche. N'en conservez
  jamais un au-delà du rappel.
- Chaque constructeur, modification, résultat de recherche, séquence de
  conversion et valeur constante a un `Destroy*` correspondant ; appelez-le aussi
  sur le chemin d'erreur.
- Une modification sémantique sans bail renvoie
  `NEVERC_STATUS_INVALID_STATE`.
- Ne modifiez pas l'arbre depuis un observateur de cycle de vie — les
  observateurs sont en lecture seule. Utilisez un intercepteur sur la phase
  correspondante.
- Les identifiants de propriété et d'emplacement d'enfant sont des constantes du
  schéma. N'écrivez pas de littéraux numériques en dur ; utilisez les noms de
  [`PluginASTSchema.inc`] afin qu'une révision du schéma devienne une erreur de
  compilation.
- Vérifiez `HAS_KNOWN_LAYOUT` dans `NevercTypeInfo.Flags` avant de vous fier à
  `SizeInBits` ou `AlignmentInBits`.

Voir [`PluginAST.h`], [`PluginSema.h`] et [`Schema/ASTSchema.json`] pour les
déclarations normatives, [`Schema/PhaseSchema.json`] pour les politiques de
phase, et [`pluginsdk/examples/ASTRewritePlugin.c`] pour une interception
d'analyseur et une réécriture atomique d'arbre qui fonctionnent.

<!-- reference links -->
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
[`pluginsdk/examples/ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`Schema/ASTSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ASTSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
