**Langues** : [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# API MIR des plugins NeverC

`PluginMIR.h` expose la Machine IR : fonctions machine, blocs, instructions,
opérandes, registres virtuels et physiques, cadre de pile, réservoir de
constantes, tables de saut et opérandes mémoire. Un plugin accroche des passes
à neuf points d'ancrage stables de la génération de code, ou remplace
entièrement l'abaissement IR vers MIR.

Deux schémas se rencontrent ici. Le **schéma générique** est indépendant de la
cible et toujours disponible. Tout ce qui est propre à une cible — un vrai
opcode, un numéro de registre, une classe de registres — exige un **schéma de
cible** négocié, et chaque valeur qui en a besoin le signale par un drapeau
`RequiresTargetSchema`.

## Interfaces

```c
#include "neverc/Plugin/PluginMIR.h"
```

| Interface | Table | Emplacements | Rôle |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | Lire et modifier des fonctions machine |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | Vivacité, dominateurs, boucles, pression |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | Remplacer l'abaissement IR → MIR |

Les quatre sont `NEVERC_INTERFACE_STABLE` en majeure 1. Comparez le
`TableSize` renvoyé au décalage du dernier emplacement que vous utilisez et
ignorez tout ce qu'un hôte plus récent aurait ajouté au-delà.

## Phases

Dix phases MIR, dont neuf points d'ancrage pour passes :

| Phase | Quand |
|---|---|
| `neverc.mir.pass.post_isel` | Après la sélection d'instructions |
| `neverc.mir.pass.post_legalize` | Après la légalisation |
| `neverc.mir.pass.pre_scheduler` | Avant l'ordonnancement |
| `neverc.mir.pass.post_scheduler` | Après l'ordonnancement |
| `neverc.mir.pass.pre_regalloc` | Avant l'allocation de registres |
| `neverc.mir.pass.post_regalloc` | Après l'allocation de registres |
| `neverc.mir.pass.post_prolog_epilog` | Après l'insertion prologue/épilogue |
| `neverc.mir.pass.preemit` | Juste avant l'émission |
| `neverc.mir.pass.final` | Le dernier créneau pour les plugins |
| `neverc.mir.final_verify` | `MachineVerifier` **scellé** de l'hôte |

Les neuf points d'ancrage sont `OBSERVABLE | INTERCEPTABLE`. Les analyses
disponibles dépendent de l'endroit où vous vous accrochez : les intervalles de
vie n'existent pas avant l'allocation de registres, et les registres virtuels
ont disparu après.

`neverc.mir.final_verify` exécute le `MachineVerifier` de LLVM après le
dernier créneau de plugin. Aucun plugin ne peut le désactiver, le remplacer ni
le sauter.

## Le schéma

`Schema/PluginMIRSchema.inc` est généré et inclus par `PluginMIR.h` :

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

Quatre appels décrivent le schéma à l'exécution, chacun renvoyant une
`NevercMIRSchemaEntry` avec le nom canonique, la valeur LLVM sous-jacente et
l'indication qu'un schéma de cible est nécessaire :

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

Les autres sont `GetEntityInfo`, `GetOperandKindInfo` et
`GetMachinePropertyInfo`. `GetSchemaDigest` renvoie l'empreinte de la
correspondance réellement en usage — comparez-la à
`NEVERC_MIR_SCHEMA_DIGEST` avant de faire confiance à une valeur propre à la
cible.

## Lire la MIR

Le parcours se fait par liste doublement chaînée plutôt que par curseur :

```c
NevercMachineBasicBlockHandle Block;
MIR->GetFirstBasicBlock(MIR->Context, Task, Function, &Block);

while (!neverc_handle_is_null(Block)) {
  NevercMachineInstrHandle Instruction;
  MIR->GetFirstInstruction(MIR->Context, Task, Block, &Instruction);

  while (!neverc_handle_is_null(Instruction)) {
    NevercMIRInstructionInfo Info = {0};
    Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_MIR_API_MAJOR,
                                         NEVERC_MIR_API_MINOR, 0};
    MIR->GetInstructionInfo(MIR->Context, Task, Instruction, &Info);
    /* Info.StableOpcode, .TargetOpcode, .RequiresTargetSchema,
       .IsBranch, .IsCall, .IsReturn, .IsTerminator, .IsBarrier,
       .IsInlineAssembly, .IsDebugInstruction, .IsPseudo, .IsBundle,
       .Flags, .OperandCount, .MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` et `CollectInstructions` remplissent plutôt un tableau
borné, et `GetLastBasicBlock` / `GetPreviousInstruction` parcourent en arrière.
Les requêtes sur le graphe de flot sont `GetSuccessorCount` / `GetSuccessor`
(qui fournit une `NevercMIRCFGEdge` portant la probabilité de branchement sous
forme d'une paire numérateur/dénominateur), `GetPredecessorCount` /
`GetPredecessor`, et `GetLiveInCount` / `GetLiveIn`.

Les drapeaux d'instruction sont les 18 bits allant de `FRAME_SETUP` et
`FRAME_DESTROY`, en passant par le groupe fast-math, jusqu'à `NO_MERGE`,
`UNPREDICTABLE` et `NO_CONVERGENT`.

## Opérandes

Les 21 genres d'opérandes reviennent par une seule union étiquetée :

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number, .SubRegister, .Flags, .IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol, .Offset */
  break;
}
```

Les genres sont `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK` et `DBG_INSTR_REF`.

Les drapeaux d'opérande registre sont `DEF`, `IMPLICIT`, `KILL`, `DEAD`,
`UNDEF`, `EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ` et `DEBUG`. Les
immédiats flottants arrivent sous forme de `NevercMIRWordView` — des mots en
petit-boutiste plus une largeur en bits et l'une des sept sémantiques
flottantes, de `IEEE_HALF` à `PPC_DOUBLE_DOUBLE` — de sorte qu'aucun type
flottant de l'hôte n'est impliqué.

## Registres

Un registre virtuel se décrit par un type de bas niveau plus une affectation :

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* needs the target schema */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

Les genres d'affectation sont `NONE`, `GENERIC`, `CLASS` et `BANK` ; les
genres de types de bas niveau sont `INVALID`, `SCALAR`, `POINTER`, `VECTOR` et
`POINTER_VECTOR`, avec `IsScalable` pour les vecteurs extensibles.

Les requêtes def-use sont `GetRegisterDefCount` / `GetRegisterDef` et
`GetRegisterUseCount` / `GetRegisterUse` ; `ReplaceRegister` réécrit chaque
occurrence en une seule opération préparée. Les entrées vivantes au niveau de
la fonction associent un registre physique au registre virtuel dans lequel il
a été copié (`GetFunctionLiveIn`, `AddFunctionLiveIn`,
`RemoveFunctionLiveIn`), tandis que celles au niveau du bloc portent un masque
de voies (`AddBasicBlockLiveIn`, `RemoveBasicBlockLiveIn`).

## Le cadre de pile

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` place un objet à un décalage connu (avec
`IsImmutable` et `IsAliased`), et `CreateVariableSizedStackObject` gère
l'allocation dynamique. `SetFrameObjectSize`, `SetFrameObjectAlignment` et
`SetFrameObjectOffset` en ajustent un après coup.

`NevercMIRFrameObjectInfo` rapporte `Index`, `Flags`, `Size`, `Offset`,
`Alignment` et `StackID` ; les drapeaux de cadre sont `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD` et `PREALLOCATED`. L'état des
registres sauvegardés par l'appelé se lit avec `GetCalleeSaved` et se remplace
en bloc avec `SetCalleeSaved`.

## Réservoir de constantes, tables de saut, opérandes mémoire

Les entrées du réservoir de constantes portent leur valeur sous forme de
`NevercMIRWordView`, si bien qu'une entrée entière et une entrée flottante ont
la même forme :

```c
NevercMIRConstantPoolEntryDesc Desc = {0};
Desc.Header       = /* … */;
Desc.Kind         = NEVERC_MIR_CONSTANT_INTEGER;
Desc.Alignment    = 8;
Desc.Value.Data   = Words;
Desc.Value.Count  = 1;
Desc.Value.BitWidth = 64;

uint32_t Index = 0;
MIR->CreateConstantPoolEntry(MIR->Context, Task, Mutation, &Desc, &Index);
```

Les tables de saut se créent à partir d'un tableau de blocs de destination
avec l'un des sept genres d'entrée (`BLOCK_ADDRESS`,
`GP_REL64_BLOCK_ADDRESS`, `GP_REL32_BLOCK_ADDRESS`, `LABEL_DIFFERENCE32`,
`LABEL_DIFFERENCE64`, `INLINE`, `CUSTOM32`).

Les opérandes mémoire sont le descripteur le plus riche : drapeaux (`LOAD`,
`STORE`, `VOLATILE`, `NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT`, plus
trois drapeaux de cible), taille et alignement, un pointeur de l'un des neuf
genres (`IR_VALUE`, `FIXED_STACK`, `STACK`, `CONSTANT_POOL`, `JUMP_TABLE`,
`GOT`, `UNKNOWN_STACK`, `TARGET_CUSTOM`, `UNKNOWN`), les ordonnancements
atomiques de succès et d'échec, une portée de synchronisation, et des
références TBAA, alias-scope, no-alias et range. On en attache un avec
`AddInstructionMemoryOperand`.

## Mutation transactionnelle

Chaque changement est préparé dans une mutation liée à une seule fonction
machine :

```c
NevercMIRMutationHandle Mutation;
MIR->BeginMutation(MIR->Context, Task, Function, &Mutation);

NevercMIRInstructionOpcode Opcode = {0};
Opcode.StableOpcode = MyGenericOpcode;

NevercMachineInstrHandle New;
MIR->CreateInstruction(MIR->Context, Task, Mutation, Block,
                       /*InsertBefore=*/Terminator, Opcode, &New);

NevercMIROperandValue Op = {0};
Op.Header = /* … */;
Op.Kind   = NEVERC_MIR_OPERAND_IMMEDIATE;
Op.Payload.Immediate = 42;
MIR->AppendOperand(MIR->Context, Task, Mutation, New, &Op, &Operand);

Status = MIR->CommitMutation(MIR->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MIR->AbortMutation(MIR->Context, Task, Mutation);
MIR->EndMutation(MIR->Context, Task, Mutation);
```

La validation exécute un contrôle structurel préalable puis le vérificateur de
Machine IR. Des opérandes invalides, un graphe de flot cassé, des opcodes
génériques employés là où le schéma de cible en exige un vrai, ou une
revendication de propriété non prise en charge, provoquent toutes un retour en
arrière atomique. L'abandon restaure l'ordre des blocs, les instructions, les
opérandes, les arêtes du graphe de flot et les propriétés machine exactement
tels qu'ils étaient.

`EndMutation` libère le handle et est distinct de la validation et de
l'abandon — appelez-le dans les deux cas.

Les opérations préparées sont `CreateBasicBlock`, `MoveBasicBlock`,
`EraseBasicBlock`, `CreateInstruction`, `MoveInstruction`,
`EraseInstruction`, `AppendOperand`, `SetOperandValue`,
`SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, les appels sur les
registres et le cadre vus plus haut, ceux du réservoir de constantes et des
tables de saut, ceux des opérandes mémoire, et
`SetMachinePropertyWithProof`.

## Les propriétés machine exigent une preuve

Les onze propriétés machine — `IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`,
`NO_V_REGS`, `FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION` et `TRACKS_DEBUG_USER_VALUES` — se
lisent librement mais ne se posent jamais librement :

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

Une preuve est de deux genres. `INVALIDATION` efface une propriété dont votre
changement a rompu les hypothèses — c'est toujours accepté, car renoncer à une
garantie est sans danger. `STRUCTURAL_CHECK` demande à l'hôte de vérifier la
propriété avant de l'établir : revendiquer `IS_SSA` coûte donc une vraie
vérification, pas une simple promesse.

## Passes

```c
NevercMIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                                     NEVERC_MIR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.machine-pass");
Pass.Phase         = (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                                         NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
Pass.Level         = NEVERC_MIR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Run           = run_machine_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

C'est `pluginsdk/examples/MachinePass.c` mot pour mot. Les niveaux sont
`MODULE`, `FUNCTION` et `BASIC_BLOCK`. `RequiredAnalyses` et
`PreservedAnalyses` sont des tableaux de `NevercMIRBuiltinAnalysis`, et
`RequiredTargetSchemaDigest` fait que la passe refuse de s'exécuter face à un
schéma pour lequel elle n'a pas été construite.

L'invocation porte `Task`, `Phase`, `PassID`, `Level`, la `Function` et le
`BasicBlock` valides pour ce niveau, les tables `Core` et `Analyses`, et le
`TargetSchemaDigest` actif.

Signalez la préservation via `OutPreserved` — `NEVERC_MIR_PRESERVE_NONE`,
`_CFG` ou `_ALL`, plus une liste explicite dans `Analyses`. Revendiquer
`PRESERVE_ALL` après une mutation validée est rejeté.

Les passes de fonction peuvent s'exécuter dans des partitions parallèles de
génération de code ; les passes au niveau module s'exécutent aux barrières
sérialisées du pipeline. Les modèles de concurrence et de réentrance déclarés
par le plugin régissent toujours votre propre état.

## Analyses

Six analyses intégrées : `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO` et `REGISTER_PRESSURE`.

```c
NevercMIRAnalysisResultHandle Intervals;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Function,
                       &Intervals);

uint64_t SegmentCount = 0;
Analyses->GetLiveIntervalSegmentCount(Analyses->Context, Task, Intervals,
                                      Register, &SegmentCount);
for (uint64_t I = 0; I != SegmentCount; ++I) {
  NevercMIRLiveRangeSegment Segment;
  Analyses->GetLiveIntervalSegment(Analyses->Context, Task, Intervals,
                                   Register, I, &Segment);
  /* Segment.Start, Segment.End */
}
```

Également disponibles : `DominatorTreeDominates`, `GetLoopCount` /
`GetLoopHeader` / `GetLoopForBlock`, `GetSlotIndex`,
`IsRegisterLiveInBlock`, et `GetRegisterPressureSetCount` /
`GetRegisterPressure`.

La disponibilité dépend du point d'ancrage. Demander les intervalles de vie à
`post_isel` échoue avec `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` parce que
l'analyse LLVM sous-jacente n'existe pas encore. Une mutation validée invalide
les handles de résultat qu'elle affecte.

## Remplacer l'abaissement IR vers MIR

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module, .IR, .TargetID, .CompatibilityKey, .TargetSchemaDigest,
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … build the machine function … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

Le descripteur de couverture est ce qui garde honnête un fournisseur partiel :
déclarez seulement ce que vous avez réellement abaissé, et l'hôte s'occupe
lui-même du reste au lieu de laisser tomber silencieusement les globales, les
constructeurs, les informations de débogage ou les tables de déroulement.

## Exemple

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Utilisez le suffixe de module que CMake a produit pour votre plateforme.

## Règles

- Ne conservez pas de handles de tâche, de handles MIR ni de vues empruntées
  après le retour d'un rappel, et ne fabriquez jamais une valeur de handle ni
  un numéro d'opcode LLVM.
- Comparez `GetSchemaDigest` à votre empreinte compilée avant de consommer
  toute valeur dont le drapeau `RequiresTargetSchema` est posé.
- Ne modifiez qu'à l'intérieur d'une mutation. Chaque `BeginMutation` aboutit
  à exactement un `EndMutation`, après une validation ou un abandon.
- Ne revendiquez pas une propriété machine sans preuve, et préférez
  `INVALIDATION` à `STRUCTURAL_CHECK` quand votre changement y a renoncé.
- Ne revendiquez jamais `NEVERC_MIR_PRESERVE_ALL` après une mutation validée.
- Vérifiez que l'analyse dont vous avez besoin est bien disponible au point
  d'ancrage que vous avez choisi.
- Initialisez chaque en-tête de table et chaque champ réservé ; renvoyez des
  statuts à travers la frontière C et ne laissez jamais une exception C++ la
  franchir.
- `neverc.mir.final_verify` est scellée. Elle s'exécute quoi qu'il arrive.

Voir `PluginMIR.h`, `Schema/PluginMIRSchema.inc`, `Schema/PhaseSchema.json` et
`coverage.json` pour les déclarations normatives, les constantes de schéma,
les politiques de phase et les preuves de couverture.
