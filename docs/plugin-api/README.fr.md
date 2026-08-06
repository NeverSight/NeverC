**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index documentation](../README.fr.md) · [← Projet NeverC](../i18n/README.fr.md)

# ABI de plugin NeverC

Un plugin NeverC est un module partagé qui exporte exactement une fonction,
négocie des tables de capacités versionnées via un identifiant d'interface de
128 bits, et se rattache à un graphe figé de phases de compilation nommées.
Toute l'interface est en C11 pur. Un plugin n'inclut jamais d'en-tête LLVM, ne
lie jamais le compilateur et ne fait jamais franchir la frontière à un type
C++.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

Cette signature, déclarée dans [`PluginCore.h`], constitue à elle seule tout le
contrat d'édition de liens. Tout le reste — lire l'IR, réécrire un graphe
objet, remplacer le pipeline d'optimisation — s'atteint par des tables que
vous demandez à l'hôte par identifiant.

## Guides

| Guide | Contenu |
|---|---|
| [Plugins Python](python.fr.md) | Python embarqué facultatif, cycle de vie, options, observers en lecture seule, diagnostics et limites actuelles |
| [API Driver](driver.fr.md) | [Ligne de commande](driver.fr.md#arguments-bruts), [choix de la chaîne d'outils](driver.fr.md#sélection-de-la-chaîne-doutils), [graphe d'actions](driver.fr.md#le-graphe-dactions), [graphe de jobs](driver.fr.md#le-graphe-de-tâches) |
| [API Source et E/S](source.fr.md) | [Fournisseurs VFS](source.fr.md#fournisseurs-de-système-de-fichiers-virtuel), [positions source](source.fr.md#emplacements-source), [tampons](source.fr.md#lire-des-fichiers), [puits de sortie](source.fr.md#écrire-des-sorties), [dépendances](source.fr.md#enregistrer-les-dépendances) |
| [API Préprocesseur](prep.fr.md) | [Jetons](prep.fr.md#lire-les-jetons), [macros](prep.fr.md#identifiants-et-macros), [pragmas](prep.fr.md#pragmas-et-requêtes-de-fonctionnalité), [inclusions](prep.fr.md#rediriger-une-inclusion), [requêtes de fonctionnalités](prep.fr.md#pragmas-et-requêtes-de-fonctionnalité), [39 types d'événements](prep.fr.md#abonnement-aux-événements) |
| [API AST et sémantique](ast-sema.fr.md) | [Extension du parseur](ast-sema.fr.md#extension-de-lanalyseur-syntaxique), [mutation de l'AST](ast-sema.fr.md#construire-et-modifier), [recherche de noms](ast-sema.fr.md#requêtes-sémantiques), [types](ast-sema.fr.md#accesseurs-typés), [constantes](ast-sema.fr.md#requêtes-sémantiques) |
| [API IR](ir.fr.md) | [Lecture de l'IR LLVM](ir.fr.md#parcourir-un-module), [construction transactionnelle](ir.fr.md#modification-transactionnelle), [analyses](ir.fr.md#analyses), [passes](ir.fr.md#passes), [fournisseurs](ir.fr.md#remplacer-la-génération-et-loptimisation) |
| [API MIR](mir.fr.md) | [Fonctions machine](mir.fr.md#lire-la-mir), [registres](mir.fr.md#registres), [cadres de pile](mir.fr.md#le-cadre-de-pile), [passes et analyses MIR](mir.fr.md#passes) |
| [Cible, MC, assembleur, objet](target-mc-object.fr.md) | [Enregistrement de cible](target-mc-object.fr.md#enregistrer-une-cible), [conventions d'appel](target-mc-object.fr.md#abi-et-conventions-dappel), [encodage MC](target-mc-object.fr.md#encodeurs-décodeurs-et-disposition), [graphes objet](target-mc-object.fr.md#graphes-objet) |
| [API Link et LTO](link-lto.fr.md) | [Graphe de liaison](link-lto.fr.md#lire-le-graphe), [résolution de symboles](link-lto.fr.md#modifier-le-graphe), [GC/ICF](link-lto.fr.md#la-machine-à-états), [fournisseurs de lieur et de LTO](link-lto.fr.md#fournisseurs) |
| [API DynCode](dyncode.fr.md) | [Images plates indépendantes de la position](dyncode.fr.md#image-rapport-et-éditions-doctets-bornées), [abaissement des imports](dyncode.fr.md#références-externes-et-abaissement-des-imports), [encodage de jeu de caractères](dyncode.fr.md#image-rapport-et-éditions-doctets-bornées) |
| [Conventions d'appel personnalisées](custom-callconv/README.fr.md) | [Plugins de convention d'appel pilotés par les données](custom-callconv/README.fr.md#format-de-la-spec) |
| [Preuves de couverture des phases](coverage.json) | Correspondance des tests pour chaque phase stable |

## Modèle d'exécution

L'hôte pilote un plugin à travers trois portées imbriquées. Chaque portée
remet au plugin un pointeur d'état opaque que le plugin alloue et possède ; un
plugin correctement écrit n'a donc besoin d'aucun état global mutable.

| Portée | Rappels | Signification |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Un processus du compilateur. C'est ici qu'on interroge les interfaces et qu'on enregistre les capacités. |
| Session | `SessionBegin`, `SessionEnd` | Une invocation du driver. |
| Task | `TaskBegin`, `TaskEnd` | Une unité de travail, identifiée par `NevercTaskKind`. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

En pratique, seuls `PluginID` et `Register` sont obligatoires ; tout
emplacement de rappel peut rester `NULL`. Les types de tâche sont
`NEVERC_TASK_INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`,
`OBJECT` et `DYNCODE`.

L'hôte appelle d'abord `ProcessBegin`, puis `Register` exactement une fois.
L'enregistrement est le seul endroit où l'on peut ajouter des options, des
observateurs, des intercepteurs et des fournisseurs ; le graphe de phases est
figé ensuite.

L'état se récupère à l'intérieur d'un rappel plutôt qu'il ne se capture :

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## Phases

Une phase est une transition nommée et versionnée d'un artefact d'entrée vers
un artefact de sortie. NeverC fournit **130 phases intégrées**, plus 8
familles d'identifiants d'extension réservées aux phases définies par des
plugins :

| Domaine | Phases | Domaine | Phases |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

Les 130 sont toutes de niveau de stabilité `stable` dans l'ABI majeure 1.
Chaque phase annonce une politique, et un plugin ne peut s'y rattacher que
selon ce que cette politique autorise :

| Drapeau de politique | Phases | Ce qu'un plugin peut faire |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | Enregistrer un observateur pour une notification en lecture seule. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | Envelopper la phase et décider s'il faut appeler le reste de la chaîne. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | Enregistrer un fournisseur qui produit lui-même la sortie. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | Sauter la transition en fournissant un handle de preuve. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | Rien. Les vérificateurs et les commits appartiennent à l'hôte. |

Les 14 portes scellées sont `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify` et `dyncode.commit`. On peut les
observer, jamais les intercepter, les remplacer ni les sauter.

Les observateurs sont notifiés aux points que la phase déclare :
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` et
`NEVERC_OBSERVER_AFTER_COMMIT`. Un intercepteur reçoit une
`NevercPhaseContinuation` et doit appeler `InvokeNext` **au plus une fois**,
sur le thread du rappel, puis signaler `NEVERC_PHASE_CONTINUE`,
`NEVERC_PHASE_REPLACE` ou `NEVERC_PHASE_SKIP` dans
`NevercPhaseResult.Action`.

Chaque rappel de phase reçoit le même cadre :

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

[`Schema/PhaseSchema.json`] est la source normative des identifiants de phase,
des politiques, des niveaux de stabilité et des portes de vérification.
[`PluginPhaseSchema.h`] et le fichier généré [`Schema/PluginPhaseSchema.inc`]
qu'il inclut exposent chacun d'eux comme constante de compilation — pour la
phase `neverc.ir.pass.pipeline_start` :

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT` et les constantes par domaine
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` permettent à un plugin d'affirmer le
graphe contre lequel il a été compilé.

## Un plugin minimal complet

Voici [`pluginsdk/templates/minimal/Plugin.c`] tel quel. Il se charge, négocie
l'ABI, n'enregistre rien et se décharge proprement — copiez le répertoire et
faites-le grandir à partir de là.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Register options, observers, interceptors, or providers here. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` est un tampon appartenant à l'appelant. À l'entrée, son
`Header.StructSize` indique la capacité inscriptible ; le plugin écrit au plus
ce nombre d'octets et rapporte la taille qu'il a réellement produite. Écrire
d'abord le `Header` du descripteur lui-même, puis tronquer la copie, satisfait
les deux moitiés de cette règle.

## Négociation d'interface

Les tables de capacités s'obtiennent par identifiant d'interface de 128 bits,
et non par symbole. Demandez la version majeure contre laquelle vous avez
compilé et la version mineure la plus basse qui vous convient :

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

Comparer `TableSize` au décalage de la dernière fonction que vous appelez :
c'est la règle qui rend cet ABI extensible. Un hôte plus récent ajoute des
champs à la fin, et un plugin plus ancien continue de fonctionner parce qu'il
ne lit jamais au-delà du préfixe qu'il a vérifié. La macro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` applique le même test à une
structure que vous avez reçue. La même signature `QueryInterface` figure aussi
sur `NevercCoreAPI`, ce qui permet de négocier tardivement plutôt qu'à
l'entrée.

Les interfaces publiques, leurs tables et leurs macros d'identifiant :

| Paire de macros d'interface | Table | En-tête |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | [`PluginCore.h`] |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | [`PluginDriver.h`] |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | [`PluginSource.h`] |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | [`PluginPrep.h`] |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | [`PluginAST.h`] |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | [`PluginSema.h`] |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | six tables IR | [`PluginIR.h`] |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | [`PluginTarget.h`] |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | quatre tables MIR | [`PluginMIR.h`] |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | quatre tables MC | [`PluginMC.h`] |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | trois tables objet | [`PluginObject.h`] |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | trois tables de liaison | [`PluginLink.h`] |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | [`PluginLTO.h`] |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | trois tables dyncode | [`PluginDynCode.h`] |

Chaque en-tête définit aussi les `NEVERC_<DOMAIN>_API_MAJOR` et `_MINOR`
correspondants à passer à `QueryInterface`.

Une interface est soit `NEVERC_INTERFACE_STABLE` (un hôte plus récent ne peut
qu'ajouter), soit `NEVERC_INTERFACE_LOCKSTEP` (schémas spécifiques à une cible
qui doivent correspondre exactement). Comparez l'empreinte du schéma avant de
consommer des valeurs LOCKSTEP.

## Enregistrement

`Register` reçoit une `NevercRegistrarAPI` et un `RegistrarContext` opaque :

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

Chacun de ces appels prend `RegistrarContext` en premier argument et un descripteur
mis à zéro en second. L'appel que vous choisissez détermine la manière dont l'hôte
vous traite à la phase :

| Appel | Descripteur | Rappel | La phase doit déclarer |
|---|---|---|---|
| `RegisterObserver` | `NevercObserverDescriptor` | `NevercPhaseObserverFn` | `OBSERVABLE` |
| `RegisterInterceptor` | `NevercInterceptorDescriptor` | `NevercPhaseInterceptorFn` | `INTERCEPTABLE` |
| `RegisterProvider` | `NevercProviderDescriptor` | `NevercPhaseProviderFn` | `REPLACEABLE` |
| `RegisterPhase` | `NevercPhaseDescriptor` | — | un ID défini par le greffon |
| `RegisterOption` | `NevercOptionDescriptor` | `Validator` facultatif | — |
| `RegisterInterface` | arguments simples | — | — |

Un descripteur qui échoue à la validation structurelle est rejeté immédiatement avec
`NEVERC_STATUS_INVALID_DESCRIPTOR`. La vérification de la politique a lieu au moment
où l'hôte applique l'enregistrement : une phase inconnue, ou une phase qui ne
déclare pas la politique exigée par votre appel, y est refusée. Une porte scellée
n'accepte que des observateurs.

Les fonctions d'enregistrement propres à chaque domaine —
`NevercIRPassAPI.RegisterPass`, `NevercTargetAPI.RegisterTarget`,
`NevercObjectFormatAPI.RegisterFormat` et les autres — prennent ce même
`RegistrarContext` en deuxième argument : c'est ainsi que l'hôte attribue un
enregistrement à votre plugin.

### Observateurs

```c
typedef struct NevercObserverDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercObserverPoint Points;
  uint32_t Reserved;
  NevercPhaseObserverFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObserverDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseObserverFn)(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData);
```

`Points` est un masque de bits composé de `NEVERC_OBSERVER_BEFORE` (1),
`NEVERC_OBSERVER_AFTER` (2) et `NEVERC_OBSERVER_AFTER_COMMIT` (4) ; il doit être non
nul, et l'argument `Point` indique au rappel lequel s'est déclenché. Extrait de
[`pluginsdk/examples/DriverTracePlugin.c`] :

```c
NevercObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){
    sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Observer.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                                     NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
```

`UserData` vous est restitué tel quel. Définir `DestroyUserData` — présent sur
chaque descripteur de cette section — fait libérer cette mémoire par l'hôte lorsque
l'enregistrement disparaît, si bien qu'une allocation par enregistrement n'a pas à
être suivie dans `Destroy`.

### Intercepteurs

```c
typedef struct NevercInterceptorDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercPhaseInterceptorFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercInterceptorDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseInterceptorFn)(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);
```

La continuation est tout le reste de la chaîne, et le résultat est la façon dont
vous rapportez ce que vous en avez fait :

```c
typedef struct NevercPhaseContinuation {
  NevercABITableHeader Header;
  NevercInvokeNextFn InvokeNext;
  void *Context;
  uint64_t Generation;
} NevercPhaseContinuation;

typedef struct NevercPhaseResult {
  NevercABITableHeader Header;
  NevercPhaseAction Action;
  uint32_t Reserved;
  NevercArtifactHandle Output;
  NevercProofHandle Proof;
} NevercPhaseResult;
```

Les trois actions ne sont pas interchangeables. L'hôte confronte le résultat à ce
que vous avez réellement fait et fait échouer la chaîne avec
`NEVERC_STATUS_POLICY_VIOLATION` à la moindre discordance :

| `Action` | `InvokeNext` | `Output` | `Proof` | Exige en plus |
|---|---|---|---|---|
| `NEVERC_PHASE_CONTINUE` | appelé une fois | vide | vide | — |
| `NEVERC_PHASE_REPLACE` | non appelé | renseigné | vide | `REPLACEABLE` |
| `NEVERC_PHASE_SKIP` | non appelé | renseigné | renseigné | `SKIPPABLE_WITH_PROOF` |

`InvokeNext` ne peut être appelé qu'au plus une fois, et uniquement sur le fil du
rappel : un second appel est une violation de politique, et un appel depuis un autre
fil signale `NEVERC_STATUS_WRONG_SCOPE`. Un intercepteur qui renvoie `CONTINUE` sans
l'avoir appelé viole également la politique, car la phase ne produirait alors
silencieusement rien.

```c
NevercInterceptorDescriptor Interceptor = {0};
Interceptor.Header = (NevercABITableHeader){
    sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Interceptor.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                                        NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW};
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

### Fournisseurs

Un fournisseur remplace purement et simplement une phase ; il déclare donc aussi le
contrat de déterminisme dont dépend le cache de compilation :

```c
typedef struct NevercProviderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView ProviderID;
  NevercPhaseRoute Route;
  NevercBool Deterministic;
  NevercBool Cacheable;
  NevercBool FallbackSafe;
  uint32_t Reserved;
  NevercPhaseProviderFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercProviderDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseProviderFn)(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData);
```

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

`ProviderID` doit être un nom canonique : au plus 255 octets composés de minuscules,
de chiffres, de `.`, `_` et `-`, ne commençant ni ne finissant par un point et ne
contenant jamais `..`. Une seule majuscule suffit à faire refuser l'enregistrement.
`Route.Header` doit être initialisé comme n'importe quel autre en-tête de table.

Il n'y a pas de continuation : le rappel *est* la phase. Il doit signaler
`NEVERC_PHASE_REPLACE` avec un `Output` et un `Proof` vide — tout le reste est une
violation de politique.

`FallbackSafe` est le seul de ces drapeaux à avoir un effet à l'exécution au-delà de
la comptabilité. Lorsqu'il vaut `NEVERC_TRUE` et que le fournisseur échoue avec un
statut marqué `NEVERC_STATUS_FLAG_RECOVERABLE`, l'hôte peut écarter les effets
partiels et exécuter l'implémentation intégrée à la place. Laissez-le à
`NEVERC_FALSE` lorsqu'une tentative inachevée ne peut pas être annulée.

### Phases définies par un greffon

`RegisterPhase` ajoute une transition que l'hôte ne connaît pas : c'est précisément
ce à quoi les 8 familles d'ID d'extension sont réservées :

```c
typedef struct NevercPhaseDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView CanonicalName;
  NevercInterfaceID InputArtifact;
  NevercInterfaceID OutputArtifact;
  NevercPhasePolicy Policy;
  NevercObserverPoint ObserverPoints;
  uint32_t Reserved;
} NevercPhaseDescriptor;
```

`Phase`, `InputArtifact` et `OutputArtifact` doivent tous être non nuls, et `Policy`
doit être non nulle et ne contenir que des drapeaux connus. Déclarer
`ObserverPoints` sans `NEVERC_PHASE_OBSERVABLE` est refusé, tout comme combiner
`NEVERC_PHASE_SEALED_HOST_GATE` avec `INTERCEPTABLE`, `REPLACEABLE` ou
`SKIPPABLE_WITH_PROOF` — ce sont les invariants mêmes auxquels le graphe intégré est
confronté. Prenez l'ID dans la famille de votre domaine afin qu'il ne puisse pas
entrer en collision avec une future phase intégrée :

```c
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

/* NEVERC_EXTENSION_FAMILY_COUNT is 8; family 1 is "neverc.ir.extension". */
NevercInterfaceID MyPhase = {NEVERC_EXTENSION_FAMILY_1_ID_HIGH,
                             NEVERC_EXTENSION_FAMILY_1_ID_LOW_MIN};
```

Chaque famille publie `_NAMESPACE`, `_ID_HIGH`, `_ID_LOW_MIN` et `_ID_LOW_MAX`, et
la moitié basse vous revient, à répartir à l'intérieur de cette plage.

### Publier une interface pour d'autres greffons

`RegisterInterface` est le seul appel qui ne prend pas de descripteur. Il confie à
l'hôte une table qui vous appartient, de sorte qu'un autre greffon puisse y accéder
par le même `QueryInterface` que celui des interfaces intégrées :

```c
Registrar->RegisterInterface(RegistrarContext, MyInterfaceID,
                             NEVERC_INTERFACE_STABLE, &MyTable,
                             /* Compatibility = */ NULL);
```

Passez plutôt `NEVERC_INTERFACE_LOCKSTEP` lorsque la table transporte des valeurs de
schéma propres à la cible qui ne survivraient pas à un décalage de version. Une
interface lockstep doit fournir une `NevercCompatibilityKey`, qui arrime le
consommateur à une unique construction du producteur :

```c
typedef struct NevercCompatibilityKey {
  NevercABITableHeader Header;
  NevercStringView ProducerBuildID;   /* compare against Bootstrap->HostBuildID */
  NevercStringView TargetABIKey;
  uint32_t LLVMMajor;                 /* compare against Bootstrap->LLVMMajor   */
  uint32_t Reserved;
} NevercCompatibilityKey;
```

Les trois champs doivent être renseignés pour un enregistrement lockstep ; un
identifiant de construction vide, une clé ABI vide ou un majeur LLVM nul est rejeté
comme descripteur invalide.

## Compilation

Incluez l'en-tête global [`NevercPluginAPI.h`], ou seulement les domaines que
vous utilisez :

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

Construire un module partagé avec NeverC lui-même :

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Ou avec CMake contre un SDK installé :

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Ou avec pkg-config :

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Utilisez `.so`, `.dylib` ou `.dll` selon l'hôte. Le SDK ne lie ni LLVM ni le
runtime NeverC — `NevercPluginSDK::headers` ne contient que des en-têtes.

## Chargement et configuration

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Option | Forme | Rôle |
|---|---|---|
| `-fplugin=<path>` | répétable | Charger un module partagé de plugin pour toute la chaîne d'outils. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | répétable | Passer une valeur qualifiée à une option de plugin enregistrée. |
| `-fplugin-provider=<phase>:<plugin-id>` | répétable | Choisir quel plugin fournit une phase remplaçable. |
| `-fplugin-pass=<dsopath>` | répétable | Charger un plugin de passe hors arborescence à ABI C. |
| `-fplugin-pass-arg=<key>=<value>` | répétable | Passer un argument aux plugins de passe à ABI C. |

Le qualificateur `<plugin-id>:` ne peut être omis que si un seul plugin est
actif. Les options qu'un plugin enregistre avec `RegisterOption` sont aussi
acceptées directement sous l'orthographe déclarée, sous forme de drapeau,
jointe, séparée ou à plusieurs arguments. Un argument de plugin ou une
sélection de fournisseur sans `-fplugin=` correspondant est une erreur franche
plutôt qu'une opération silencieusement ignorée.

Une option enregistrée peut être relue à tout moment via la table core :

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## Règles d'ABI

- Obtenez les tables de capacités par `QueryInterface` ; exigez la majeure
  correspondante et vérifiez `StructSize` avant de toucher un champ.
- Initialisez le `Header` et l'espace réservé de chaque structure publique.
  Mettez la structure à zéro, puis renseignez `StructSize`, `Major`, `Minor`
  et `Flags`.
- Traitez les handles et les vues empruntées comme des valeurs opaques à
  portée limitée. Ne conservez jamais un handle de portée tâche au-delà de son
  rappel, ne l'utilisez jamais dans une autre session ou tâche, et ne
  fabriquez jamais une valeur de handle.
- Retournez un `NevercStatus` depuis chaque rappel. Ne laissez ni exception
  C++ ni pointeur appartenant à l'hôte franchir la frontière C.
- Déclarez le `NevercConcurrencyModel` le plus étroit qui soit vrai
  (`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`) ainsi que le
  `NevercReentrancyModel` (`NONE`, `ALLOWED`).
- Effectuez les modifications d'IR, de MIR, d'AST, de graphe et d'artefact via
  les API transactionnelles de l'hôte : ouvrez une mutation, préparez les
  changements, puis validez ou abandonnez. La validation vérifie et publie de
  façon atomique ; une validation échouée laisse l'état précédent intact.
- Allouez via `NevercCoreAPI.Allocate` / `Reallocate` / `Deallocate` quand
  l'hôte doit comptabiliser la mémoire.
- Gardez l'état mutable dans l'état process/session/task fourni par l'hôte.
  L'état global mutable est contrôlé par
  [`utils/plugin-api/check-global-state.py`].

Toutes les structures publiques sont disposées sous `NEVERC_ABI_PACK_BEGIN`
(alignement sur 8 octets) et n'utilisent que des types de largeur fixe. Les
nouvelles fonctions sont ajoutées à la fin de tables de capacités versionnées
indépendamment ; le préfixe stable d'une table ne change pas au sein de la
première majeure d'ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Statuts et diagnostics

`NevercStatus` porte un `Code`, des `Flags` et un mot `Detail`. L'ensemble
complet des codes :

| Code | Signification |
|---|---|
| `NEVERC_STATUS_OK` | Succès. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Un pointeur ou une valeur requise manquait ou était mal formé. |
| `NEVERC_STATUS_ABI_MISMATCH` | La table négociée est trop petite ou la majeure diffère. |
| `NEVERC_STATUS_MISSING_INTERFACE` | L'hôte ne publie pas l'interface demandée. |
| `NEVERC_STATUS_VERSION_MISMATCH` | La majeure/mineure demandée ne peut être satisfaite. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | Un descripteur a échoué à la validation structurelle. |
| `NEVERC_STATUS_DUPLICATE_ID` | Un identifiant était déjà enregistré. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | Une dépendance déclarée est absente. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | L'ordre d'enregistrement ne peut être satisfait. |
| `NEVERC_STATUS_BUSY` | Une ressource est détenue ailleurs. |
| `NEVERC_STATUS_CANCELLED` | Une annulation coopérative a été demandée. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | Un budget ou une limite a été atteint. |
| `NEVERC_STATUS_STALE_HANDLE` | Un handle a survécu à l'objet qu'il désignait. |
| `NEVERC_STATUS_WRONG_SESSION` | Un handle a été utilisé dans une autre session. |
| `NEVERC_STATUS_WRONG_SCOPE` | Un handle a été utilisé hors de sa portée. |
| `NEVERC_STATUS_WRONG_TYPE` | Un handle désignait un autre type d'entité. |
| `NEVERC_STATUS_INVALID_STATE` | L'opération n'est pas légale dans l'état actuel. |
| `NEVERC_STATUS_POLICY_VIOLATION` | La politique de la phase interdit l'opération. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Un vérificateur scellé de l'hôte a rejeté le produit. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | L'hôte ne peut pas offrir cette capacité ici. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | Le plugin a signalé un échec générique. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | Une exception s'est échappée d'un rappel de plugin. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | La sortie n'a été écrite qu'en partie. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | Un appel réentrant a été refusé. |
| `NEVERC_STATUS_NOT_FOUND` | L'entité nommée n'existe pas. |

Les bits de drapeau décrivent ce qui est arrivé à la sortie, ce dont un
système de compilation a besoin pour décider si une nouvelle tentative est
sûre : `NEVERC_STATUS_FLAG_RECOVERABLE`, `_OUTPUT_ALREADY_COMMITTED`,
`_OUTPUT_MAY_BE_PARTIAL`, `_OUTPUT_RECOVERY_REQUIRED` et
`_DURABILITY_UNCONFIRMED`.

Signalez les problèmes avec `NevercCoreAPI.EmitDiagnostic` et un
`NevercDiagnosticDescriptor` portant la sévérité (`NOTE`, `REMARK`,
`WARNING`, `ERROR`, `FATAL`), le code, l'identifiant du plugin, celui de la
phase, le message, les notes, la position source, les plages et les
corrections automatiques. Appelez `CheckCancelled` avant tout travail coûteux.

## Exemples

Tout compiler :

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Chaque exemple est compilé deux fois — une fois avec le compilateur C hôte
configuré et une fois avec le NeverC fraîchement construit — de sorte que
l'ABI est prouvé des deux côtés. Les modules atterrissent dans
`build-neverc/neverc/pluginsdk/examples/host/`.

| Exemple | Cible CMake | Montre |
|---|---|---|
| [`DriverTracePlugin.c`] | `neverc-plugin-example-driver-trace` | Enregistrement d'options, observation de phases, interception de jobs |
| [`VirtualHeaderPlugin.c`] | `neverc-plugin-example-virtual-header` | Un fournisseur VFS servant un en-tête en mémoire |
| [`ASTRewritePlugin.c`] | `neverc-plugin-example-ast-rewrite` | Interception du parseur et mutation atomique de l'AST |
| [`ExamplePlugin.c`] | `neverc-plugin-example-ir-overview` | Une passe IR au niveau module parcourant la liste des fonctions avec un curseur de valeurs |
| [`FunctionPass.c`] | `neverc-plugin-example-function-pass` | Une passe IR de fonction stable |
| [`MachinePass.c`] | `neverc-plugin-example-machine-pass` | Une passe MIR stable au point d'ancrage pre-emit |
| [`MCObserverPlugin.c`] | `neverc-plugin-example-mc-observer` | Événements d'émission MC en lecture seule |
| [`ObjectRewritePlugin.c`] | `neverc-plugin-example-object-rewrite` | Réécriture transactionnelle d'ObjectGraph |
| [`CustomCallConvPlugin.c`] | `neverc-plugin-example-custom-callconv` | Conventions d'appel pilotées par les données |
| [`DynCodeTracePlugin.c`] | `neverc-plugin-example-dyncode-trace` | Observation du pipeline dyncode |
| [`DynCodeEncoderPlugin.c`] | `neverc-plugin-example-dyncode-encoder` | Interception de l'encodage de jeu de caractères dyncode |
| [`CrtShimPlugin.c`] | `neverc-plugin-example-crt-shim` | Un plugin sans aucune dépendance à la CRT |
| [`BenchPlugin.c`] | `neverc-plugin-example-abi-bench` | Micro-benchmark du débit d'appels ABI |

En charger un :

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Sources normatives

| Fichier | Garanties |
|---|---|
| [`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`] | Identifiants de phase, politiques, stabilité, portes de vérification |
| [`pluginsdk/manifest/plugin.json`] | Version d'ABI, identifiants/versions/stabilité des interfaces, empreintes de schéma, cibles prises en charge |
| [`pluginsdk/abi/plugin.json`] | Taille, alignement et décalages de champs mesurés de chaque structure publique, par clé d'ABI hôte |
| [`docs/plugin-api/coverage.json`] | Associe chaque phase stable aux tests positifs, négatifs, de remplacement, d'observateur et de porte scellée |

Un SDK peut donc être validé mécaniquement contre un hôte, et une compilation
de plugin peut affirmer la disposition de ses structures contre la clé d'ABI
dans laquelle il sera chargé.

<!-- reference links -->
[`ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`BenchPlugin.c`]: ../../pluginsdk/examples/BenchPlugin.c
[`CrtShimPlugin.c`]: ../../pluginsdk/examples/CrtShimPlugin.c
[`CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`docs/plugin-api/coverage.json`]: coverage.json
[`DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
[`DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
[`ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`NevercPluginAPI.h`]: ../../neverc/include/neverc/Plugin/NevercPluginAPI.h
[`ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginCore.h`]: ../../neverc/include/neverc/Plugin/PluginCore.h
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`PluginDynCode.h`]: ../../neverc/include/neverc/Plugin/PluginDynCode.h
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`PluginPhaseSchema.h`]: ../../neverc/include/neverc/Plugin/PluginPhaseSchema.h
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`pluginsdk/abi/plugin.json`]: ../../pluginsdk/abi/plugin.json
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
[`pluginsdk/manifest/plugin.json`]: ../../pluginsdk/manifest/plugin.json
[`pluginsdk/templates/minimal/Plugin.c`]: ../../pluginsdk/templates/minimal/Plugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPhaseSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPhaseSchema.inc
[`utils/plugin-api/check-global-state.py`]: ../../utils/plugin-api/check-global-state.py
[`VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
