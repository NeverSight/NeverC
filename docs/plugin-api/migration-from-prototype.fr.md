**Langues** : [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# Migration depuis l'API de plugin prototype

L'API de plugin prototype jamais publiée — son point d'entrée
`nevercGetPluginInfo`, l'unique vtable `NevercHostAPI`, les appels
`Register*Pass`, les hooks `NEVERC_INTERPOSE_*` et le chargeur
`-fplugin-pass=` — a été supprimée avant la première version publique. Le
premier ABI public est l'ABI à descripteur fondé sur les phases documenté dans
[README.md](README.md) : les plugins exportent `neverc_plugin_entry` et
négocient des tables de capacités versionnées indépendamment.

Il n'existe aucune couche de compatibilité ni de séparation `v1`/`v2`.
Recompilez le *code source* du plugin contre les en-têtes publics ; cette page
associe chaque construction du prototype à son remplacement de première
version, à un changement sémantique, ou à une non-reprise explicite.

## Les binaires prototypes sont rejetés

Le chargement d'un objet partagé prototype échoue avec un diagnostic stable :

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

Une bibliothèque qui n'exporte aucun des deux points d'entrée échoue avec
`plugin has no 'neverc_plugin_entry' entry`. Rien n'est chargé tant que le code
source n'est pas porté.

## Point d'entrée

| Prototype | Premier ABI public |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

Le point d'entrée ne *renvoie* plus une structure par valeur. Il remplit un
`NevercPluginDescriptor` fourni par l'appelant, en respectant
`OutPlugin->Header.StructSize`, et renvoie un `NevercStatus`. Interrogez les
tables de capacités dont vous avez besoin auprès de `Bootstrap` avant
d'annoncer leur prise en charge.

## Champs de `NevercPluginInfo`

| Champ du prototype | Correspondance en première version |
|---|---|
| `APIVersion` | `Descriptor.Header` (`NevercABITableHeader` avec `StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`), plus un `Descriptor.PluginID` stable en DNS inversé servant de clé pour l'état de chaque portée |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`, plus les rappels de cycle de vie `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(pas d'équivalent prototype)* | `Descriptor.Concurrency` et `Descriptor.Reentrancy` doivent être déclarés sans complaisance (par exemple `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## Accès à l'hôte : une vtable → des tables de capacités

Le prototype transmettait à chaque rappel une unique vtable `NevercHostAPI` de
plus de 200 entrées et protégeait les nouveaux champs par `NEVERC_API_FN`. La
première version la remplace par des tables de capacités versionnées
indépendamment, interrogées à la demande :

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

Exigez la version majeure correspondante et vérifiez `TableSize` avec
`offsetof` avant de lire un champ. Les interfaces sont délimitées par domaine :
Core, Driver, Source, Prep, AST, Sema, IR, MIR, Target, MC, Object, Link, LTO
et DynCode.

## Enregistrement : `Register*Pass` + hooks → observateurs/intercepteurs/fournisseurs

L'enregistrement prototype rattachait un rappel à un hook :

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

La première version enregistre, à l'intérieur de `Register`, un gestionnaire
typé sur une phase identifiée par un `NevercInterfaceID` de 128 bits :

| Appel prototype | Appel du registrar en première version |
|---|---|
| passe en lecture seule | `Registrar->RegisterObserver(NevercObserverDescriptor)` avec les points `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` |
| passe qui enveloppe ou court-circuite une phase | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)` ; appelez `Continuation->InvokeNext` au plus une fois et renseignez `OutResult->Action` |
| passe qui remplace une transformation intégrée | `Registrar->RegisterProvider(...)` sur une phase `REPLACEABLE` |
| lecture de `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)` pour déclarer une véritable option du pilote |

Une « passe de module à `PRE_OPT` » du prototype devient un observateur, un
intercepteur ou un fournisseur sur la phase IR `neverc.ir.pass.pre_opt`.

## Correspondance hook → phase

| Hook prototype | Phase de première version (nom) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | phases LTO `neverc.link.lto_resolve` / `neverc.link.lto_generate` (voir [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `neverc.link.layout` observée en `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | les phases dyncode typées de [dyncode.md](dyncode.md) |

La liste normative des identifiants de phase, des politiques, des niveaux de
stabilité et des portes de vérification est
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` ; le contrat de
couverture exécutable est [coverage.json](coverage.json). Un hook qui
constituait autrefois un point unique peut correspondre à plusieurs
identifiants de phase, chacun avec sa politique et sa preuve.

## Rappels de passe, handles et éditions d'octets

| Prototype | Première version |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` et similaires | les rappels reçoivent un `NevercPhaseFrame` ; les objets IR/MIR/AST/graphe sont des handles typés, délimités et opaques obtenus depuis la table de capacités concernée (voir [ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md)) |
| `NevercValueRef` générique | supprimé au profit de handles IR typés |
| modification sur place d'un `Ref` vivant | tous les changements passent par les API hôtes transactionnelles |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | supprimé ; les éditions d'octets dyncode utilisent le constructeur d'image vérifié (read/write/insert/append/resize), voir [dyncode.md](dyncode.md) |

Les handles et les vues empruntées ne sont valides que dans la portée du
rappel, exactement comme auparavant ; ne les mettez pas en cache après le
retour du rappel.

## Couches de commodité supprimées

Le prototype embarquait des utilitaires génériques dans la vtable. Ils ne font
**pas** partie du premier ABI public :

| Prototype | Première version |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | non repris ; utilisez `Core->Allocate`/`Core->Deallocate` avec vos propres conteneurs, ou les API de domaine typées |
| macros `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` | remplacées par l'itération typée de la table de capacités de chaque domaine |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | déclarez les options avec `RegisterOption` et lisez-les via l'API Driver |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## Chargement et ligne de commande

| Prototype | Première version |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | l'orthographe d'option que vous déclarez dans `RegisterOption` (par exemple `--driver-trace` ou `--my-opt=value`) |
| deux chargeurs (`-fplugin` et `-fplugin-pass`) | un seul chargeur ; un module est confié à un unique chargeur |

## Versionnement

Le prototype reposait sur une vtable unique croissant de façon monotone,
assortie de gardes `NEVERC_API_FN`. Dans la première version, chaque table de
capacités est versionnée pour elle-même : exigez la majeure correspondante et
vérifiez `StructSize`/`TableSize` avant de lire un champ ajouté. Les nouvelles
fonctions sont ajoutées après le préfixe stable d'une table au sein de la
première majeure d'ABI, si bien qu'un plugin construit contre une mineure
antérieure continue de fonctionner avec un hôte plus récent.

## Exemple complet

`pluginsdk/examples/DriverTracePlugin.c` montre la forme complète de la
première version : le descripteur `neverc_plugin_entry`, le cycle de vie
`ProcessBegin`/`Session`/`Task`, un `RegisterOption` pour un véritable
indicateur de ligne de commande, un `RegisterObserver` sur
`neverc.driver.raw_arguments`, et un `RegisterInterceptor` sur
`neverc.driver.execute_job` qui appelle `InvokeNext` exactement une fois.
`pluginsdk/examples/ExamplePlugin.c` couvre les phases IR, MIR, object et link.
