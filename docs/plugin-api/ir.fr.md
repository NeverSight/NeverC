**Langues**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← ABI de plugin NeverC](README.fr.md)

# API IR des plugins NeverC

`PluginIR.h` expose la représentation intermédiaire de LLVM à travers six tables
de capacités et un schéma généré. Un plugin lit et réécrit l'IR, enregistre des
passes en cinq points stables de la chaîne, définit ses propres analyses, ou
remplace purement et simplement la génération d'IR et la chaîne d'optimisation —
sans inclure le moindre en-tête LLVM.

Les codes d'opération, les genres de type et les propriétés d'instruction sont
des **identifiants de schéma stables**, non des valeurs d'énumération LLVM.
C'est cette indirection qui permet à un plugin compilé aujourd'hui de continuer
à fonctionner quand l'hôte passe à une nouvelle version de LLVM.

## Interfaces

```c
#include "neverc/Plugin/PluginIR.h"
```

| Interface | Table | Créneaux | Rôle |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | Lire et éditer modules, valeurs, types, constantes, métadonnées, attributs |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | Construction transactionnelle |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | Analyses natives et de plugin |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | Remplacer l'abaissement SemanticUnit → IR |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | Remplacer toute la chaîne d'optimisation |

Chacune est `NEVERC_INTERFACE_STABLE` en majeur 1. Négociez avec les
`NEVERC_IR_*_API_MAJOR` / `_MINOR` correspondants et vérifiez que `TableSize`
atteint le dernier créneau que vous appelez, exactement comme le fait
`pluginsdk/examples/FunctionPass.c` :

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

## Phases

Huit phases IR :

| Phase | Politique |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **PORTE SCELLÉE DE L'HÔTE** |

Les cinq phases `pass.*` sont celles que vise `NevercIRPassDescriptor.Phase`.
`neverc.ir.final_verify` exécute le vérificateur LLVM et ne peut être
interceptée, remplacée ni sautée par quoi que ce soit — fournisseur
d'optimisation compris.

## Le schéma

`Schema/PluginIRSchema.inc` est généré et inclus par `PluginIR.h`. Il publie un
condensat et les jeux de constantes suivants :

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

Les identifiants portent leur domaine dans l'octet de poids fort — `0x41……`
pour les types, `0x42……` pour les genres de valeur, `0x43……` pour les codes
d'opération, `0x49……` pour les propriétés — de sorte qu'une valeur employée à
la mauvaise place est rejetée au lieu d'être mal interprétée.

## Descripteurs et propriété

Les descripteurs d'IR sont des paires opaques `{Owner, Value}` limitées à une
tâche, et l'hôte possède tout ce qui se trouve derrière.

- Ne conservez jamais un descripteur après la fin de son rappel ou de sa tâche.
- N'utilisez jamais un descripteur dans une autre session ou une autre tâche.
- Un remplacement validé invalide les descripteurs des objets remplacés.
- Une modification abandonnée périme les descripteurs qu'elle avait créés.
- Les erreurs sont `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE` ou
  `WRONG_TYPE` — jamais un pointeur LLVM brut.

Les chaînes et vues d'octets issues d'une requête sont empruntées pour la durée
du rappel. La seule exception est `ExportModule`, qui renvoie un
`NevercIRSerializedBufferHandle` à rendre à `ReleaseSerializedBuffer`.

## Parcourir un module

Les collections se lisent au moyen d'un curseur qui porte sa propre génération :
une modification en cours de parcours est ainsi détectée au lieu de sauter
silencieusement des entrées.

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

Répétez jusqu'à ce que `Count` revienne à zéro. Les sept collections sont
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS` et `BLOCK_INSTRUCTIONS`.

Tout le reste est une requête directe : `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*`, ainsi que les fonctions de module `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly` et
leurs mutateurs.

## Types et constantes

Les types sont internés : demander deux fois donne le même descripteur.

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` prend un genre de schéma tel que `NEVERC_IR_TYPE_VOID`,
`_FLOAT`, `_DOUBLE` ou `_TOKEN` ; `GetArrayType`, `GetVectorType` (avec un
drapeau `Scalable`) et `GetStructType` (nommée ou littérale, compactée ou non)
couvrent le reste.

Les constantes entières et flottantes se bâtissent à partir de mots de 64 bits
petit-boutistes, si bien qu'un `i128` ne demande aucun chemin particulier :

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant` et `GetGlobalAddressConstant` traitent les cas
simples ; `CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression` et `CreateConstantGEPExpression` bâtissent des
expressions constantes.

## Propriétés d'instruction

Plutôt qu'un accesseur par drapeau, le détail d'une instruction passe par une
valeur de propriété étiquetée, indexée par identifiant de schéma :

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

Les 23 propriétés sont `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`,
`DISJOINT`, `VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`,
`PREDICATE`, `CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP` et `NUSW`. Les ordres atomiques vont
de `NOT_ATOMIC` à `SEQUENTIALLY_CONSISTENT` ; les genres d'appel terminal sont
`NONE`, `TAIL`, `MUST_TAIL` et `NO_TAIL` ; les drapeaux fast-math sont les sept
bits habituels, de `ALLOW_REASSOC` à `APPROX_FUNC`.

## Attributs

Les attributs sont des valeurs que l'on crée puis que l'on attache, ce qui rend
uniformes les quatre genres (`ENUM`, `INTEGER`, `STRING`, `TYPE`) :

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

`pluginsdk/examples/CustomCallConvPlugin.c` s'en sert avec
`GetFunctionStringAttribute` pour piloter une convention d'appel définie par des
données.

## Modification transactionnelle

Tout changement structurel passe par `NevercIRBuilderAPI`. La modification est
la transaction ; le constructeur est un curseur à l'intérieur.

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

Les portées sont `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION` et `_LOOP` ;
`ScopeRoot` désigne la fonction ou l'en-tête de boucle. La validation vérifie le
candidat et publie atomiquement — en cas d'échec du vérificateur, l'hôte revient
en arrière et le module précédent survit intact.

Les appels de construction sont `BuildBinary`, `BuildUnary`, `BuildCompare`,
`BuildCast`, `BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`,
`BuildGetElementPtr`, `BuildCall`, `BuildPhi`, `BuildBranch`,
`BuildConditionalBranch`, `BuildUnreachable`, `BuildReturn` et
`BuildReturnVoid`. `SetDebugLocation` et `SetFastMathFlags` s'appliquent à tout
ce que le constructeur émet ensuite.

Notez l'asymétrie : `AddPhiIncoming`, `CreateFunction` et `CreateBasicBlock`
prennent la **modification**, pas le constructeur, car elles ne sont pas liées à
un point d'insertion.

`DestroyMutation` est distinct de la validation et de l'abandon. Chaque
`BeginMutation` réclame exactement un `DestroyMutation`, quelle que soit la
façon dont la transaction s'est terminée.

## Passes

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

Les niveaux sont `MODULE`, `CGSCC`, `FUNCTION` et `LOOP`. L'invocation ne
transporte que les descripteurs valides pour son niveau :

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION et LOOP      */
  NevercIRValueHandle LoopHeader;               /* LOOP seulement        */
  const NevercIRValueHandle *SCCFunctions;      /* CGSCC seulement       */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

Les trois pointeurs d'API arrivent avec l'invocation : le corps d'une passe n'a
donc aucune table à conserver.

Indiquez ce qui a survécu au moyen de `OutPreserved` :

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* ou _NONE, ou _CFG */
```

`NEVERC_IR_PRESERVE_CFG` signifie que le graphe de flot de contrôle est intact
même si des instructions ont changé. Les analyses personnalisées se préservent
en les listant dans `CustomAnalyses`. N'annoncez pas `PRESERVE_ALL` après avoir
modifié l'IR — l'adaptateur compare la génération du module et rejette une
annonce mensongère.

Les passes de fonction et de boucle peuvent s'exécuter en parallèle : l'état
mutable du plugin doit donc correspondre au `NevercConcurrencyModel` déclaré.

## Analyses

Sept analyses natives sont interrogeables par identifiant : `DOMINATOR_TREE`,
`POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`, `MEMORY_SSA`,
`CALL_GRAPH` et `ALIAS`.

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

Chacune dispose d'accesseurs typés plutôt que d'un bloc opaque :
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind` (`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee`, et `Alias` (`NO`, `MAY`,
`PARTIAL`, `MUST`).

Une analyse de plugin s'enregistre avec son propre cycle de vie :

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

`Invalidate` apprend pourquoi — `INVALIDATED_BY_PASS` ou
`INVALIDATED_BY_PLAN_DESTROY`. Les résultats sont mis en cache par invocation et
abandonnés selon ce que la passe en cours a préservé. Les cycles de dépendance
sont rejetés à l'enregistrement, et modifier l'IR depuis un rappel d'analyse est
refusé.

## Remplacer la génération et l'optimisation

`NevercIRGenAPI` remplace `neverc.ir.generate` :

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … construire le module … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` part d'un bitcode ou d'une IR textuelle au lieu d'un module vide.
`NevercIROptimizationAPI` a la même forme pour `neverc.ir.optimize`, avec en
plus `GetInputModule` pour atteindre le module entrant et `RunBuiltinPipeline`
pour déléguer à la chaîne native puis post-traiter son résultat.

Les deux voies publient par l'hôte plutôt que de renvoyer un pointeur, toutes
deux vérifient la compatibilité de cible, et toutes deux conservent atomiquement
l'ancien module si la publication échoue. `neverc.ir.final_verify` s'exécute
ensuite dans tous les cas.

## Exemples

| Fichier | Montre |
|---|---|
| `pluginsdk/examples/FunctionPass.c` | Une passe de fonction en lecture seule, négociation d'ABI comprise |
| `pluginsdk/examples/ExamplePlugin.c` | Une passe de module parcourant les fonctions avec un curseur de valeurs |
| `pluginsdk/examples/CustomCallConvPlugin.c` | Attributs et propriétés de site d'appel |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Utilisez le suffixe de module que CMake a produit pour votre plate-forme.

## Règles

- Renvoyez un `NevercStatus` depuis chaque rappel. L'échec d'un plugin devient un
  diagnostic structuré ; ne laissez jamais une exception franchir la frontière C.
- Mettez à zéro chaque structure de sortie et fixez son `Header` avant l'appel
  qui la remplit.
- N'écrivez pas en dur les valeurs numériques de code d'opération, de type ou de
  propriété. Utilisez les noms de `PluginIRSchema.inc` afin qu'une révision du
  schéma devienne une erreur de compilation.
- Chaque `BeginMutation` atteint exactement un `DestroyMutation`, et chaque
  `CreateBuilder` exactement un `DestroyBuilder`, y compris sur les chemins
  d'erreur.
- Libérez ce que vous donne `ExportModule` avec `ReleaseSerializedBuffer`.
- N'annoncez jamais `NEVERC_IR_PRESERVE_ALL` après avoir modifié l'IR.
- Supposez que les passes de fonction et de boucle tournent en parallèle, sauf
  si le plugin a déclaré `NEVERC_CONCURRENCY_SESSION_SERIAL`.
- `neverc.ir.final_verify` est scellée. Rien de ce que fait un plugin ne peut la
  contourner.

Voir `PluginIR.h`, `Schema/PluginIRSchema.inc`, `Schema/PhaseSchema.json` et
`coverage.json` pour les déclarations normatives, les constantes de schéma, les
politiques de phase et les preuves par les tests.
