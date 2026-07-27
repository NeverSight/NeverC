**Langues** : [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← ABI de plugin NeverC](README.fr.md)

# API NeverC cible, MC, assembleur et objet du plugin

Le back-end tient en quatre en-têtes et vingt-neuf phases. [`PluginTarget.h`]
décrit une cible et les routes qui traversent la génération de code.
[`PluginMC.h`] construit et observe le code machine. L'analyse et l'impression
d'assembleur vivent dans le même en-tête. [`PluginObject.h`] transforme un
fichier relogeable en graphe normalisé, et inversement.

Ensemble, ils permettent à un plugin d'ajouter une cible, de remplacer une
étape d'abaissement ou toutes, de surveiller chaque instruction au moment de
son émission, de définir un dialecte d'assembleur ou de réécrire un fichier
objet — à travers un ABI C pur qui n'expose jamais un `MCInst`, un
`MCSection` ni un `object::ObjectFile` de LLVM.

## Interfaces

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| Interface | Table | Emplacements | Rôle |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | Lire et modifier un `MCUnit` ; enregistrer encodeurs, décodeurs, back-ends |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | Événements d'émission et instantanés de disposition |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | Remplacer MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | Remplacer l'analyseur ou l'imprimeur d'assembleur |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | Lire et modifier un ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## Deux niveaux de compatibilité

C'est la règle qui gouverne tout le reste de ce document.

**STABLE**, et sûr à coder en dur : les descripteurs indépendants de la cible,
les identifiants de phase, les identifiants d'artefact, les conteneurs MC et
ObjectGraph, les transactions de sortie et tous les contrats de rappel.

**LOCKSTEP**, et dangereux sans vérification : les schémas d'opcode, de
registre, d'opérande, de fixup, de relogement et de convention d'appel propres
à une cible. Leurs valeurs numériques n'ont de sens que face à une révision de
schéma bien précise.

Partout où apparaît une valeur LOCKSTEP, une empreinte de schéma l'accompagne.
Comparez-la avant de lire la valeur :

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC rejette lui aussi un schéma discordant avant d'invoquer un fournisseur,
donc cette vérification tient de la ceinture et des bretelles — mais un plugin
qui la saute et lit tout de même un opcode brut interprétera silencieusement
les instructions de travers.

## Les phases

Vingt-neuf, réparties en quatre domaines.

### `codegen` — routage (4)

| Phase | Politique |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — code machine (13)

`neverc.mc.encode`, `neverc.mc.decode` et `neverc.mc.layout` sont OBSERVABLE,
INTERCEPTABLE, REPLACEABLE.

`neverc.mc.emission.pre_instruction` est le seul événement d'émission qui soit
aussi REPLACEABLE — c'est là que l'on substitue une instruction. Les neuf
autres (`unit_begin`, `unit_end`, `section_change`, `post_instruction`,
`post_encode`, `fixup`, `relaxation_round`, `pre_layout`, `post_layout`) sont
en observation seule.

### `assembly` (4)

`neverc.assembly.parse` et `neverc.assembly.print` sont REPLACEABLE.
`neverc.assembly.final_verify` et `neverc.assembly.commit` sont SEALED.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write` et `post_layout` sont
REPLACEABLE ; `neverc.object.post_write` est seulement INTERCEPTABLE ;
`neverc.object.final_verify` et `neverc.object.commit` sont SEALED.

## Enregistrer une cible

`NevercTargetDescriptor` est le plus grand descripteur de l'ABI, parce qu'il
transporte tout ce que le front-end et le back-end doivent savoir :

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

`TripleMatchers` décide quand la cible est sélectionnée : chaque filtre nomme
une architecture, un fournisseur, un système d'exploitation et un
environnement, plus une `Priority` qui départage face aux cibles intégrées.

`Machine` est un `NevercTargetMachineDescriptor` — disposition des données,
CPU par défaut et de réglage, table des fonctionnalités, ABI, conventions
d'appel et formats objet pris en charge, espaces d'adressage, modèles de
relogement et de code (à la fois par défaut et sous forme de masque de
prise en charge), modèle d'exceptions (`NONE`, `DWARF`, `SJLJ`, `SEH`,
`WASM`), modèle de déroulement, boutisme, largeur de pointer/int/long/long
long, alignement de pile, largeurs atomique et vectorielle maximales, type de
`va_list`, niveaux d'exécution (`USER`, `KERNEL`, `HYPERVISOR`, `FIRMWARE`) et
prise en charge du TLS.

Les fonctions intégrées de la cible portent leur propre rappel d'abaissement,
qui reçoit un constructeur d'IR bien vivant :

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

## ABI et conventions d'appel

Un ABI classe les signatures de fonction :

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

Les genres d'argument sont `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`,
`EXPAND`, `INDIRECT_ALIASED` et `COERCE_AND_EXPAND` ; les drapeaux sont
`BYVAL`, `REALIGN`, `INREG`, `SRET_AFTER_THIS`, `CAN_BE_FLATTENED`,
`SIGN_EXTEND` et `PADDING_INREG`. La coercition vaut `NONE`, `INTEGER`,
`FLOAT` ou `POINTER`, et `COERCE_AND_EXPAND` fournit un tableau de
`NevercABICoercionElement`.

Une convention d'appel descend d'un cran et attribue les emplacements réels :

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

`Query->SchemaDigest` est une valeur LOCKSTEP — `RegisterNumber` n'a de sens
que face au schéma qu'il nomme. Voir
[Conventions d'appel personnalisées](custom-callconv/README.fr.md#plans-matérialisés) et
[`pluginsdk/examples/CustomCallConvPlugin.c`] pour l'exemple complet.

## Routes de génération de code

Une route est choisie à partir de la `NevercTargetKey` canonique :
identifiant de cible, parties du triplet, CPU, CPU de réglage,
fonctionnalités, ABI, convention d'appel, format objet, modèle de relogement,
modèle de code, niveau d'exécution, largeur de pointeur, boutisme et empreinte
de schéma. Enregistrez les arêtes que vous savez servir :

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

Les genres de produit sont `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`,
`OBJECT_IMAGE` et `CUSTOM`. La route à grain fin est
`IR → MIR → MC → ObjectGraph → ObjectImage`.

Poser `NEVERC_CODEGEN_EDGE_COARSE` et fournir `CoarseLower` remplace d'un seul
coup toute l'étendue `IR → ObjectImage` :

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

Une route grossière passe malgré tout par `neverc.codegen.product_verify` et
par la validation transactionnelle de la sortie. `VerifyProduct` est appelée
avec les obligations que l'hôte attend de vous — `VERIFY_FINAL_IR`,
`VERIFY_TARGET_KEY`, `VERIFY_PRODUCT_KIND`, `VERIFY_PRODUCT_ID`,
`VERIFY_STRUCTURE` — de sorte qu'un fournisseur ne peut pas discrètement
sauter une porte en prenant un raccourci.

## Construire du MC

Un `MCUnit` contient des sections, des symboles, des expressions, des
fragments, des instructions, des opérandes et des fixups. La lecture se fait
par itération first/next :

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

La mutation est transactionnelle, comme partout ailleurs :

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

Les handles ont la portée de la tâche et sont vérifiés par génération : un
handle issu d'une mutation abandonnée est donc rejeté plutôt que réutilisé.

Les drapeaux de section sont `ALLOCATED`, `EXECUTABLE`, `WRITABLE`,
`MERGEABLE` et `DEBUG`. Les liaisons de symbole sont `LOCAL`, `GLOBAL` et
`WEAK` ; les types sont `NONE`, `FUNCTION`, `OBJECT`, `SECTION` et `TLS` ; les
définitions sont `UNDEFINED`, `SECTION`, `ABSOLUTE` et `COMMON`. Les
expressions acceptent les opérateurs unaires `PLUS`, `MINUS`, `NOT` et
binaires `ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`, `AND`, `OR`, `XOR`,
`SHIFT_LEFT`, `SHIFT_RIGHT`. Passez `NEVERC_MC_AUTOMATIC_OFFSET` là où vous
voulez que l'hôte place quelque chose à votre place.

`RegisterSchema` publie un schéma MC de cible, et `GetSchemaToken` /
`GetSchemaTokenInfo` convertissent un nom en jeton LOCKSTEP et inversement.

## Observer l'émission

Le flux d'émission signale dix genres d'événements dans l'ordre — un par phase
`neverc.mc.emission.*`. L'ABI réserve aussi
`NEVERC_MC_EMISSION_PRE_OBJECT_WRITE` ; l'écriture d'objet elle-même est la
phase séparée `neverc.object.pre_write`. Abonnez-vous
comme observateur et lisez l'événement :

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` indique quelles parties de l'événement sont renseignées :
`HAS_SECTION`, `HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT` et
`CAN_REPLACE_INSTRUCTION`. Vérifiez le drapeau avant de lire le champ
correspondant — un événement qui n'a pas encore d'encodage n'en aura pas
davantage parce que vous l'avez demandé.

`GetLayoutSection`, `GetLayoutFragment`, `GetLayoutSymbol` et
`GetLayoutFixup` donnent adresses et tailles une fois `HAS_LAYOUT` posé.

À `pre_instruction`, et seulement quand `CAN_REPLACE_INSTRUCTION` est posé,
vous pouvez substituer :

```c
Emission->BeginInstructionReplacement(Emission->Context, Frame, &Builder);
/* build the replacement through the MC builder */
Emission->PublishInstructionReplacement(Emission->Context, Frame, NewInstr);
```

[`pluginsdk/examples/MCObserverPlugin.c`] en est la version en lecture seule.

## Encodeurs, décodeurs et disposition

Trois enregistrements étendent le back-end de code machine, tous indexés par
cible et par empreinte de schéma :

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

Un encodeur écrit à travers un puits plutôt que de renvoyer un tampon, ce qui
laisse la propriété du côté de l'hôte :

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

Un décodeur signale `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`, `_UNKNOWN` ou
`_FAIL`. Les genres de fixup se décrivent eux-mêmes via
`NevercMCFixupKindInfo` avec les drapeaux `PC_RELATIVE`, `SIGNED`,
`RELAXABLE` et `TARGET`.

Le back-end d'assemblage possède la relaxation. La disposition émet une
empreinte de preuve, et **toute mutation postérieure invalide cette preuve** et
force une nouvelle disposition avant que l'objet puisse être écrit — le même
schéma de vérification par génération que celui du graphe de liaison.

## Assembleur

Un fournisseur d'analyseur consomme des octets source et publie un `MCUnit` :

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, Unit, &Output);
```

Les sources sont soit `NEVERC_ASSEMBLY_SOURCE_BUFFER`, soit
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. L'assembleur prétraité (`.S`) passe
d'abord par le préprocesseur frontal normal et arrive sous forme de jetons
rendus ; l'assembleur simple (`.s`) entre directement dans l'analyseur comme
tampon.

Un imprimeur fait le chemin inverse — `GetPrintInput`, puis
`WritePrintOutput` dans la transaction de sortie fournie, puis
`PublishAssemblyOutput`. Écrire ailleurs n'est pas pris en charge : la
vérification d'analyse/impression et la porte de validation de l'hôte
s'exécutent avant que les octets ne deviennent visibles, si bien qu'une
impression ratée ne laisse aucun fichier partiel derrière elle.

## Graphes objet

`NevercObjectAPI` normalise un fichier relogeable en sections, symboles,
relogements et COMDAT. Les adaptateurs intégrés couvrent ELF, COFF et Mach-O ;
`RegisterFormat` en ajoute un autre.

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

La mutation suit le schéma create/replace/move/erase pour les quatre genres
d'entités, préparée à l'intérieur de `BeginMutation` … `CommitMutation` /
`AbandonMutation`.

Les drapeaux de section sont `ALLOCATED`, `EXECUTABLE`, `WRITABLE`,
`MERGEABLE`, `STRINGS`, `TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE` et `RETAIN`.
Les cibles de relogement sont `SYMBOL`, `SECTION`, `ABSOLUTE` ou
`FORMAT_EXTENSION`.

Chaque descripteur possède un triplet `ExtensionOwner` / `ExtensionVersion` /
`Extension`. C'est ainsi qu'un format conserve des données pour lesquelles le
graphe normalisé n'a pas de champ — les octets voyagent avec l'entité et
reviennent à l'écriture, au lieu d'être perdus par l'aller-retour.

### Enregistrer un format

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

`Probe` rapporte une `Confidence` de 0 à
`NEVERC_OBJECT_PROBE_MAX_CONFIDENCE` (1000), le `NevercObjectArtifactKind`
qu'il a reconnu (`RELOCATABLE`, `ARCHIVE`, `EXECUTABLE_IMAGE`,
`SHARED_IMAGE`, `UNIVERSAL_BINARY`) et un `ConsumedMinimum` — le nombre
d'octets qu'il lui a fallu pour en être sûr, plafonné à
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536). La plus grande confiance
l'emporte.

`Reader` reçoit un graphe et une mutation ouverte, et les remplit. `Writer`
reçoit le graphe, sa preuve de disposition et le constructeur binaire borné.

### Le pipeline d'écriture

1. sonder et lire les octets dans un ObjectGraph ;
2. exécuter les intercepteurs de graphe `object.pre_write` ;
3. disposer, puis exécuter `object.post_layout` (nouvelle disposition après
   toute mutation) ;
4. écrire une image candidate bornée ;
5. exécuter les intercepteurs binaires `object.post_write` ;
6. exécuter le `object.final_verify` scellé et le `object.commit` atomique.

L'état de l'image évolue en `CANDIDATE` → `VERIFIED` → `COMMITTED`, ou en
`ABORTED` / `FAILED_PARTIAL`.

Les observateurs reçoivent des ponts en lecture seule ; une mutation tentée
depuis un observateur est rejetée avec `NEVERC_STATUS_POLICY_VIOLATION`. Les
écrivains et les intercepteurs post-écriture n'obtiennent que le constructeur
borné `NevercMutableBinaryAPI` — `Reserve`, `Write`, `WriteAt`, `Tell`,
`ReadAt`, `Insert`, `Append`, `Resize`. Un dépassement, un rappel en échec ou
une vérification ratée annule la préparation, si bien qu'un échec ne laisse
jamais un demi-fichier sur le disque.

[`pluginsdk/examples/ObjectRewritePlugin.c`] est une réécriture transactionnelle
complète.

## Règles

- Comparez l'empreinte de schéma avant de consommer toute valeur LOCKSTEP
  d'opcode, de registre, d'opérande, de fixup, de relogement ou de convention
  d'appel.
- Gardez l'état mutable dans l'état process, session et task fourni par
  l'hôte.
- Ne mettez pas en cache les handles de tâche ni les vues empruntées après le
  retour d'un rappel.
- Invoquez la continuation d'un intercepteur au plus une fois, sur le thread
  du rappel.
- Chaque `BeginMutation` aboutit à exactement une validation ou un abandon.
- Redisposez après avoir muté un MCUnit ou un ObjectGraph déjà disposé ;
  l'ancienne preuve de disposition est périmée et l'hôte la rejettera.
- Vérifiez `NevercMCEmissionEventInfo.Flags` avant de lire un champ
  d'événement, et ne remplacez une instruction que si
  `CAN_REPLACE_INSTRUCTION` est posé.
- N'écrivez la sortie qu'à travers la transaction ou le puits d'octets fourni.
- Renvoyez le `NevercStatus` d'origine en cas d'échec et ne publiez rien de
  partiel.
- Déclarez les modèles de concurrence et de réentrance les plus étroits qui
  soient vrais.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify` et `object.commit` sont scellés. Observez seulement.

Voir [`PluginTarget.h`], [`PluginMC.h`], [`PluginObject.h`] et
[`Schema/PhaseSchema.json`] pour les déclarations normatives ; les genres
d'entité, d'opérande, de fixup et de section qu'ils emploient viennent de
[`Schema/MCSchema.json`] et [`Schema/ObjectSchema.json`], qui engendrent
[`Schema/PluginMCSchema.inc`] et [`Schema/PluginObjectSchema.inc`]. Voir
aussi [`coverage.json`], qui associe chacune de ces phases stables à ses
tests positifs, négatifs, de remplacement, d'observateur en lecture seule et
de porte scellée.

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
