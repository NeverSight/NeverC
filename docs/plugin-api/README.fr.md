**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# ABI de plugins NeverC

Le premier ABI public de plugins de NeverC est une interface en C pur, organisée
par phases. Un plugin est un module partagé qui exporte une seule fonction,
négocie des tables de capacités versionnées et s'exécute dans des portées
Process, Session et Task explicites. Il n'inclut aucun en-tête LLVM, ne lie
jamais le compilateur et n'échange jamais de type C++ à travers la frontière.

L'API prototype non publiée et son point d'entrée `nevercGetPluginInfo` ont été
**supprimés**. Les binaires prototypes sont rejetés avec un diagnostic de
migration ; recompilez leurs sources avec les en-têtes publics. Voir
[Migration depuis l'API prototype](migration-from-prototype.fr.md) pour la
correspondance complète ancien → nouveau.

## Commencer ici

- [API Source et E/S](source.fr.md)
- [API du préprocesseur](prep.fr.md)
- [API AST et sémantique](ast-sema.fr.md)
- [API IR](ir.fr.md)
- [API MIR](mir.fr.md)
- [API Target, MC, assembleur et objet](target-mc-object.fr.md)
- [API DynCode](dyncode.fr.md)
- [Conventions d'appel personnalisées](custom-callconv/README.fr.md)
- [Migration depuis l'API prototype](migration-from-prototype.fr.md)
- [Preuves de couverture des phases](coverage.json)

## Modèle d'exécution

L'hôte pilote le plugin à travers trois portées imbriquées. Chaque portée remet
au plugin un pointeur d'état opaque que le plugin alloue et possède lui-même :
un plugin correctement écrit n'a donc besoin d'aucun état global mutable.

| Portée | Rappels | Signification |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Un processus du compilateur. C'est ici qu'on interroge les interfaces et enregistre les capacités. |
| Session | `SessionBegin`, `SessionEnd` | Une invocation du pilote. |
| Task | `TaskBegin`, `TaskEnd` | Une unité de travail, identifiée par `NevercTaskKind`. |

Les types de tâches sont `INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`,
`CODEGEN`, `OBJECT` et `DYNCODE`.

L'hôte appelle d'abord `ProcessBegin`, puis `Register` exactement une fois.
L'enregistrement est le seul endroit où ajouter options, observateurs,
intercepteurs et fournisseurs ; le graphe de phases est gelé ensuite.

## Phases

Une phase est une transition nommée et versionnée, d'un artefact d'entrée vers
un artefact de sortie. NeverC fournit **130 phases intégrées** réparties sur les
domaines pilote, source, préprocesseur, syntaxe, sémantique, IR, codegen, MIR,
MC, assembleur, objet, édition de liens et dyncode, plus 8 familles d'ID
d'extension réservées aux phases définies par des plugins.

Chaque phase annonce une politique, et un plugin ne peut s'y rattacher que selon
ce que cette politique autorise :

| Indicateur de politique | Ce que le plugin peut faire |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | Enregistrer un observateur pour une notification en lecture seule. |
| `NEVERC_PHASE_INTERCEPTABLE` | Envelopper la phase et décider d'appeler ou non le reste de la chaîne. |
| `NEVERC_PHASE_REPLACEABLE` | Enregistrer un fournisseur qui produit lui-même la sortie. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | Sauter la transition en fournissant un handle de preuve. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | Rien. Les vérificateurs et les validations appartiennent à l'hôte : ni remplacement, ni interception, ni saut. |

Les observateurs sont livrés aux points déclarés par la phase :
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` et
`NEVERC_OBSERVER_AFTER_COMMIT`.

Un intercepteur reçoit une `NevercPhaseContinuation`. Il doit appeler
`InvokeNext` **au plus une fois**, sur le thread du rappel, puis rapporter
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` ou `NEVERC_PHASE_SKIP` dans
`NevercPhaseResult.Action`.

La source normative des ID de phases, politiques, niveaux de stabilité et
barrières de vérification est
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`. Le fichier généré
`PluginPhaseSchema.inc` les expose sous forme de constantes de compilation
telles que `NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`.

## Un plugin minimal complet

Voici `pluginsdk/templates/minimal/Plugin.c`. Il se charge, négocie l'ABI,
n'enregistre rien et se décharge proprement : copiez ce répertoire et faites-le
grandir.

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
  /* Enregistrez ici options, observateurs, intercepteurs ou fournisseurs. */
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
ce nombre d'octets et rapporte la taille qu'il a réellement produite.

## Négociation des interfaces

Les tables de capacités s'obtiennent par un ID d'interface de 128 bits, non par
symbole. Demandez la version majeure avec laquelle vous avez compilé et la
version mineure minimale qui vous convient :

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

Vérifier `TableSize` par rapport au décalage de la dernière fonction appelée :
c'est la règle qui rend cet ABI extensible. Un hôte plus récent ajoute des
champs à la fin, et un plugin plus ancien continue de fonctionner parce qu'il ne
lit jamais au-delà du préfixe qu'il a validé. La macro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` applique le même test à une
structure reçue.

Les interfaces publiques et leurs en-têtes :

| Interface | Table | En-tête |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`, `..._SOURCE_LOCATION` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`, `..._PARSER` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`, `..._BUILDER`, `..._ANALYSIS`, `..._PASS`, `..._GEN`, `..._OPTIMIZATION` | Tables IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`, `..._TARGET_ABI`, `..._CALLING_CONVENTION` | Tables Target | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`, `..._MIR_ANALYSIS`, `..._MIR_PASS`, `..._MIR_PROVIDER` | Tables MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`, `..._MC_EMISSION`, `..._MC_PROVIDER`, `..._ASSEMBLY_PROVIDER` | Tables MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`, `..._OBJECT_FORMAT`, `..._OBJECT_PHASE` | Tables Object | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`, `..._LINK_REGISTRAR`, `..._LINK_PHASE` | Tables Link | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`, `..._LTO_REGISTRAR` | Tables LTO | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`, `..._DYNCODE_REGISTRAR`, `..._DYNCODE_PHASE` | Tables DynCode | `PluginDynCode.h` |

Une interface est soit STABLE (un hôte plus récent ne peut qu'ajouter), soit
LOCKSTEP (schémas spécifiques à une cible, qui doivent correspondre exactement).
Comparez l'empreinte du schéma avant de consommer des valeurs LOCKSTEP.

## Compilation

Incluez l'en-tête agrégé, ou seulement les domaines que vous utilisez :

```c
#include "neverc/Plugin/NevercPluginAPI.h"
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
runtime NeverC : `NevercPluginSDK::headers` est une cible d'en-têtes seuls.

## Chargement et configuration

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Option | Forme | Rôle |
|---|---|---|
| `-fplugin=<path>` | répétable | Charger un module partagé de plugin. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | répétable | Passer une valeur qualifiée à une option de plugin enregistrée. |
| `-fplugin-provider=<phase>:<plugin-id>` | répétable | Choisir quel plugin fournit une phase remplaçable. |

Le qualificateur `<plugin-id>:` ne peut être omis que si exactement un plugin est
actif. Les options qu'un plugin enregistre via `RegisterOption` sont aussi
acceptées directement sous leur orthographe déclarée, en forme flag, jointe,
séparée ou à arguments multiples. Fournir des arguments de plugin ou une
sélection de fournisseur sans `-fplugin=` est une erreur franche, et non un
silence.

## Règles d'ABI

- Interrogez les tables via `QueryInterface` ; exigez la même version majeure et
  vérifiez `StructSize` avant de toucher un champ.
- Initialisez le `Header` et les zones réservées de chaque structure publique.
  Mettez la structure à zéro, puis renseignez `StructSize`, `Major`, `Minor` et
  `Flags`.
- Traitez handles et vues empruntées comme des valeurs opaques à portée limitée.
  Ne conservez jamais un handle de portée tâche au-delà de son rappel, ne
  l'utilisez pas dans une autre session ou tâche, et ne fabriquez jamais une
  valeur de handle.
- Renvoyez un `NevercStatus` depuis chaque rappel. Ne laissez ni exception C++ ni
  pointeur appartenant à l'hôte franchir la frontière C.
- Déclarez le `NevercConcurrencyModel` (`SESSION_SERIAL`, `THREAD_SAFE`,
  `PROCESS_SERIAL`) et le `NevercReentrancyModel` (`NONE`, `ALLOWED`) les plus
  restrictifs qui soient **véridiques**.
- Effectuez les modifications d'IR, MIR, AST, graphes et artefacts via les API
  transactionnelles de l'hôte : ouvrir une mutation, préparer les changements,
  puis valider ou abandonner. La validation vérifie et publie atomiquement ;
  une validation échouée laisse l'état antérieur intact.
- Gardez l'état mutable dans les états process/session/task fournis par l'hôte.
  L'état global mutable est contrôlé par
  `utils/plugin-api/check-global-state.py`.

Les nouvelles fonctions sont ajoutées à la fin de tables versionnées de façon
indépendante. Le préfixe stable d'une table ne change pas au sein du premier
majeur d'ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Statut et diagnostics

`NevercStatus` porte un `Code`, des `Flags` et un mot `Detail`. Codes courants :

| Code | Signification |
|---|---|
| `NEVERC_STATUS_OK` | Succès. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Pointeur ou valeur requis manquant ou mal formé. |
| `NEVERC_STATUS_ABI_MISMATCH` | Table négociée trop petite, ou majeur différent. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | L'hôte n'offre pas la capacité demandée. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | Handle utilisé hors de sa validité. |
| `NEVERC_STATUS_POLICY_VIOLATION` | Opération non permise par la politique de la phase. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Un vérificateur scellé de l'hôte a rejeté le produit. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | Annulation coopérative ou limites de ressources. |

Les bits d'indicateurs (`RECOVERABLE`, `OUTPUT_ALREADY_COMMITTED`,
`OUTPUT_MAY_BE_PARTIAL`, `OUTPUT_RECOVERY_REQUIRED`, `DURABILITY_UNCONFIRMED`)
décrivent ce qui est arrivé à la sortie — exactement ce dont un système de build
a besoin pour décider si une nouvelle tentative est sûre.

Signalez les problèmes avec `NevercCoreAPI.EmitDiagnostic` et un
`NevercDiagnosticDescriptor` portant gravité, code, ID de plugin, ID de phase,
message, notes, position source, plages et corrections. Appelez
`CheckCancelled` avant tout travail coûteux.

## Exemples

Tout construire :

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Chaque exemple est compilé deux fois — une fois avec le compilateur C hôte
configuré, une fois avec le NeverC fraîchement construit — ce qui prouve l'ABI
des deux côtés. Les modules atterrissent dans
`build-neverc/neverc/pluginsdk/examples/host/`.

| Exemple | Cible CMake | Démontre |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Enregistrement d'options, observation de phases, interception de tâches |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Un fournisseur VFS servant un en-tête en mémoire |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Interception de l'analyseur et mutation atomique d'AST |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | Passe IR au niveau module parcourant la liste des fonctions avec un curseur de valeurs |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Une passe IR de fonction stable |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Une passe MIR stable au point pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Événements d'émission MC en lecture seule |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Réécriture transactionnelle d'ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Conventions d'appel pilotées par les données |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Observation du pipeline dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Interception de l'encodage de jeu de caractères dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Un plugin sans aucune dépendance CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Micro-benchmark du débit d'appels ABI |

En charger un :

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Sources normatives

| Fichier | Garanties |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | ID de phases, politiques, stabilité, barrières de vérification |
| `pluginsdk/manifest/plugin.json` | Version d'ABI, ID/versions/stabilité des interfaces, empreintes de schémas, cibles prises en charge |
| `pluginsdk/abi/plugin.json` | Taille, alignement et décalages mesurés de chaque structure publique, par clé d'ABI hôte |
| `docs/plugin-api/coverage.json` | Associe chaque phase stable à des tests positifs, négatifs, de remplacement, d'observateur et de barrière scellée |

Un SDK peut donc être validé mécaniquement contre un hôte, et la compilation
d'un plugin peut affirmer sa disposition de structures face à la clé d'ABI dans
laquelle il sera chargé.
