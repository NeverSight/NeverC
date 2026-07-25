**Langues**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← ABI de plugin NeverC](README.fr.md)

# API Link et LTO des plugins NeverC

L'édition de liens est modélisée comme une **machine à états sur un seul
graphe**. `PluginLink.h` expose ce graphe — entrées, sections, atomes, symboles,
arêtes, COMDAT, imports, exports, enregistrements de déroulement, éléments
synthétiques et contraintes de disposition — ainsi que les vingt phases qui le
font passer d'une liste de fichiers à une image binaire validée.
`PluginLTO.h` couvre les deux phases intermédiaires où le bitcode devient des
objets.

Un plugin peut observer chaque étape, en intercepter la plupart, remplacer une
seule étape, remplacer toute l'édition de liens, ou fusionner des objets. Il ne
voit jamais une structure de données de lld : le graphe est une projection
normalisée sur laquelle se rabattent les backends ELF, COFF et Mach-O.

## Interfaces

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* inclut PluginLink.h */
```

| Interface | Table | Rôle |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | Lire et modifier le graphe de liaison (52 emplacements) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | Enregistrer des fournisseurs d'édition de liens, de fusion d'objets et de vérification d'image |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | Atteindre le graphe ou l'image derrière un `NevercArtifactHandle` |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | Lire la requête LTO, les modules et les résolutions de symboles |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | Enregistrer un fournisseur de génération de code LTO |

Les cinq sont `NEVERC_INTERFACE_STABLE` en majeure 1 : un hôte plus récent ne
peut qu'ajouter. Associez chacune à son `NEVERC_LINK_API_MAJOR` /
`NEVERC_LTO_API_MAJOR` et vérifiez `TableSize` par rapport au dernier
emplacement que vous appelez.

## La machine à états

`NevercLinkGraphInfo.State` prend l'une de quatorze valeurs, et treize des vingt
phases n'existent que pour la faire avancer d'un cran :

| Phase | `NEVERC_LINK_STATE_…` résultant | Vérificateur hôte |
|---|---|---|
| — | `INITIAL` | — |
| `neverc.link.input_probe` | `INPUT_PROBED` | `verify_input_probe` |
| `neverc.link.read_inputs` | `INPUTS_READ` | `verify_inputs` |
| `neverc.link.lto_resolve` | `LTO_RESOLUTION_READY` | |
| `neverc.link.lto_generate` | `LTO_GENERATED` | |
| `neverc.link.resolve_symbols` | `SYMBOLS_RESOLVED` | |
| `neverc.link.select_comdat` | `COMDAT_SELECTED` | |
| `neverc.link.gc` | `GC_COMPLETE` | `verify_liveness` |
| `neverc.link.icf` | `ICF_COMPLETE` | |
| `neverc.link.synthesize` | `SYNTHETICS_READY` | |
| `neverc.link.relax_thunks` | `THUNKS_RELAXED` | `verify_relaxation` |
| `neverc.link.layout` | `LAYOUT_COMPLETE` | `verify_layout` |
| `neverc.link.relocate` | `RELOCATIONS_APPLIED` | |
| `neverc.link.emit_image` | `IMAGE_EMITTED` | |

Chacune de ces treize est
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF` : un
fournisseur peut donc assurer la transition lui-même, et un plugin détenant un
`NevercLinkProofHandle` valide peut la sauter.

Les sept restantes sont structurelles :

| Phase | Politique | Rôle |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Remplacer toute l'édition de liens, d'`INITIAL` directement à une image binaire |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Fusion relogeable `-r` d'ObjectGraphs |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | Dernière occasion de toucher aux octets de l'image |
| `neverc.link.image_verify` | OBSERVABLE, **SCELLÉE** | Vérificateur d'image de l'hôte |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **SCELLÉE** | Fichiers de map, dSYM, artefacts annexes |
| `neverc.link.commit` | OBSERVABLE, **SCELLÉE** | Publication atomique du bundle de sortie |
| `neverc.link.after_commit` | OBSERVABLE | Notification après validation |

Les trois portes scellées peuvent être observées, jamais interceptées,
remplacées ou sautées. `NEVERC_BUILTIN_LINK_PHASE_COUNT` vaut 20.

## Atteindre le graphe depuis une phase

`NevercLinkPhaseAPI` convertit l'artefact du frame en un handle exploitable :

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` est le `NevercLinkAPI` lié à cette tâche : un observateur n'a
donc pas besoin d'un `QueryInterface` distinct. Un fournisseur publie son
résultat avec `PublishGraph`, et `GetImage` fait de même pour un artefact
d'image, en renvoyant un `NevercLinkPhaseImageInfo` avec l'image, le bundle de
sortie et un `NevercBinaryImageState` (`CANDIDATE`, `VERIFIED`, `COMMITTED`,
`ABORTED` ou `FAILED_PARTIAL`).

## Lire le graphe

`NevercLinkGraphInfo` est le résumé — cible, format, état, génération, dix-sept
compteurs d'entités et un `SemanticDigest` de 32 octets. Les entités
elles-mêmes reviennent via un appel de pagination par espèce, tous partageant
une page appartenant à l'appelant :

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* tableau que vous fournissez et possédez */
  uint64_t ElementCapacity;  /* combien d'entrées tiennent              */
  uint64_t ElementStride;    /* sizeof de votre élément                 */
  uint64_t OutCount;         /* combien l'hôte en a écrites             */
  uint64_t NextCursor;       /* à repasser pour continuer               */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

L'hôte n'écrit pas plus de `ElementCapacity` entrées de `ElementStride` octets
et ne conserve jamais `Data` : un tableau sur la pile suffit donc :

```c
NevercLinkSymbolInfo Symbols[64];
NevercLinkEntityPage Page = {0};
uint64_t Cursor = 0;

do {
  Page.Header = (NevercABITableHeader){sizeof(Page), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
  Page.Data            = Symbols;
  Page.ElementCapacity = 64;
  Page.ElementStride   = sizeof(Symbols[0]);
  Status = Link->GetSymbolPage(Link->Context, Task, Graph, Cursor, &Page);
  if (Status.Code != NEVERC_STATUS_OK)
    break;
  for (uint64_t I = 0; I != Page.OutCount; ++I) {
    /* Symbols[I].Name, .Binding, .Definition, .IsPrevailing, … */
  }
  Cursor = Page.NextCursor;
} while (Page.HasMore);
```

Quinze pagineurs de graphe suivent cette forme — `GetInputPage`,
`GetArchivePage`, `GetArchiveMemberPage`, `GetSharedLibraryPage`,
`GetBitcodeModulePage`, `GetSectionPage`, `GetAtomPage`, `GetSymbolPage`,
`GetEdgePage`, `GetComdatPage`, `GetImportPage`, `GetExportPage`,
`GetUnwindPage`, `GetSyntheticPage` et `GetConstraintPage` — et deux autres,
`GetBinarySegmentPage` et `GetBinarySectionPage`, paginent une image émise.
Chacun possède un `Get…Info` correspondant pour un handle unique.

Chaque info d'entité porte un `NevercLinkOrigin` :

```c
typedef struct NevercLinkOrigin {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkArchiveMemberHandle ArchiveMember;
  NevercObjectGraphHandle ObjectGraph;
  uint64_t ObjectEntityID;
  NevercInterfaceID CreatedByPhase;
  NevercStringView CreatedByProvider;
  NevercInterfaceID LastMutationPhase;
  NevercStringView LastMutationPlugin;
} NevercLinkOrigin;
```

C'est ce qui rend une édition de liens auditable : pour n'importe quel atome de
la sortie, vous pouvez nommer le fichier d'entrée, le membre d'archive dont il a
été extrait, la phase qui l'a créé et le plugin qui y a touché en dernier.

### Les entités

| Espèce | Structure Info | Champs notables |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Archive / membre | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Bibliothèque partagée | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Module bitcode | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Section | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Atome | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Symbole | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Arête | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Import / export | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Déroulement | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Synthétique | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Contrainte | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

Les drapeaux d'atome sont `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`,
`ADDRESS_SIGNIFICANT`, `TLS` et `UNWIND`. Les liaisons de symboles sont
`LOCAL`, `GLOBAL`, `WEAK` et `COMMON` ; les définitions `UNDEFINED`, `DEFINED`,
`ABSOLUTE`, `COMMON` et `SHARED`. Les espèces d'arêtes sont `RELOCATION`,
`ASSOCIATION`, `KEEP_ALIVE`, `UNWIND` et `FORMAT_EXTENSION`. La sélection COMDAT
couvre `ANY`, `EXACT_MATCH`, `SAME_SIZE`, `LARGEST`, `NEWEST` et
`NO_DUPLICATES`.

## Modifier le graphe

La mutation est transactionnelle et toujours portée par un seul graphe :

```c
NevercLinkMutationHandle Mutation;
Link->BeginMutation(Link->Context, Task, Graph, &Mutation);

Link->SetSymbolRoot(Link->Context, Task, Mutation, Symbol, NEVERC_TRUE);
Link->ReplaceAtomContent(Link->Context, Task, Mutation, Atom,
                         (NevercByteView){Bytes, Length},
                         /*ZeroFillSize=*/0);

Status = Link->CommitMutation(Link->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Link->AbandonMutation(Link->Context, Task, Mutation);
```

La validation prépare une copie de travail, la vérifie, et seulement alors
publie et incrémente `Generation`. `AbandonMutation` jette tout. Valider alors
que le graphe est à `GC_COMPLETE`, par exemple, relance le vérificateur de
vivacité : une mutation qui isolerait un atome vivant est rejetée plutôt
qu'écrite.

### Les mutations invalident l'état en aval

C'est la partie qui surprend. Chaque appel de préparation est classifié, et la
classification détermine **l'état le plus précoce qui devient invalide** ;
l'hôte doit rejouer toutes les phases à partir de là :

| Appel | État invalidé le plus tôt |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

Une mutation qui en touche plusieurs prend le minimum. Relier un symbole après
la disposition jette donc les résultats de disposition, de relogement et
d'image — bon marché pendant `gc`, coûteux pendant `post_emit`. Modifiez le
plus tôt possible dans la machine à états, dans la mesure où votre changement
le permet.

`SetSymbolResolution` prend un petit enregistrement de mise à jour plutôt qu'un
symbole entier, ce qui évite qu'un changement de résolution réécrive
accidentellement un nom ou une valeur :

```c
NevercLinkSymbolResolutionUpdate Update = {0};
Update.Header = (NevercABITableHeader){sizeof(Update), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
Update.Binding      = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
Update.Visibility   = NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
Update.Definition   = NEVERC_LINK_SYMBOL_DEFINED;
Update.IsPrevailing = NEVERC_TRUE;
Update.IsExported   = NEVERC_FALSE;
Link->SetSymbolResolution(Link->Context, Task, Mutation, Symbol, &Update);
```

## Sauter une phase avec une preuve

Une phase `SKIPPABLE_WITH_PROOF` accepte un `NevercLinkProofHandle` au lieu de
s'exécuter. La preuve épingle tout ce dont dépend le saut :

```c
typedef struct NevercLinkProofInfo {
  NevercABITableHeader Header;
  NevercLinkProofHandle Proof;
  NevercLinkGraphHandle Graph;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercInterfaceID OutputArtifact;
  uint8_t RouteDigest[32];
  uint8_t SemanticDigest[32];
  uint64_t ImageBase;
  uint64_t EntryAddress;
} NevercLinkProofInfo;
```

Comme `GraphGeneration` et `SemanticDigest` sont tous deux enregistrés, toute
mutation validée entre l'émission de la preuve et son usage la périme, et l'hôte
exécute la phase pour de bon.

## L'image binaire

Après `emit_image`, le produit est un `NevercBinaryImageHandle` :

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                     */
```

Les types de sortie sont `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY` et
`BUNDLE`. Les drapeaux de segment sont `READ`, `WRITE` et `EXECUTE`.

`Image.Binary` et `Image.Builder` sont l'écrivain transactionnel borné de
`PluginObject.h` — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`,
`Append`, `Resize`. Un intercepteur `post_emit` qui corrige des octets doit
passer par lui ; les écritures au-delà de la borne réservée abandonnent la
préparation au lieu d'agrandir le fichier.

## Fournisseurs

Enregistrez pendant `Register`, jamais après.

### Remplacer l'éditeur de liens

```c
NevercLinkerProviderDescriptor Provider = {0};
Provider.Header = (NevercABITableHeader){sizeof(Provider),
                                         NEVERC_LINK_REGISTRAR_API_MAJOR,
                                         NEVERC_LINK_REGISTRAR_API_MINOR, 0};
Provider.ProviderID   = SV("com.example.my-linker");
Provider.TargetID     = MyTargetID;
Provider.InputFormat  = ELFFormatID;
Provider.OutputFormat = ELFFormatID;
Provider.OutputKind   = NEVERC_LINK_OUTPUT_EXECUTABLE;
Provider.Flags        = NEVERC_LINK_PROVIDER_DETERMINISTIC |
                        NEVERC_LINK_PROVIDER_CACHEABLE;
Provider.Link         = my_link;
Provider.VerifyImage  = my_verify;      /* facultatif */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

Le callback reçoit la requête et l'ensemble d'entrées brut, et remplit un
candidat :

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs est un NevercRawLinkInput[], Inputs->OrderDigest fixe l'ordre */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` porte les drapeaux sur lesquels un éditeur de liens
branche réellement — `PIE`, `STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`,
`ALLOW_UNDEFINED`, `WHOLE_ARCHIVE`, `DETERMINISTIC` — plus `EntrySymbol`,
`InstallName`, `Soname`, `ImageBase`, `PageSize`, `ThreadBudget`, les chemins de
recherche et les bibliothèques. Les drapeaux par entrée sont `WHOLE_ARCHIVE`,
`AS_NEEDED`, `START_GROUP`, `END_GROUP` et `LAZY`.

En cas de succès, l'hôte adopte le candidat. En cas d'échec, le fournisseur
reste propriétaire de ce qu'il a créé. Les portes scellées de vérification et de
validation s'exécutent dans les deux cas.

### Fusionner des objets et vérifier des images

`RegisterObjectMergeProvider` gère `-r` : la requête porte les
`NevercObjectMergeInput[]` d'entrée ainsi qu'un graphe de sortie et une mutation
déjà ouverts, si bien que le fournisseur écrit dans une transaction appartenant
à l'hôte au lieu de construire un fichier.

`RegisterBinaryImageVerifier` ajoute une vérification en lecture seule qui
s'exécute à côté du vérificateur d'image de l'hôte. Elle ne peut pas le
remplacer.

## LTO

`lto_resolve` produit les résolutions de symboles ; `lto_generate` transforme le
bitcode en objets. `NevercLTOAPI` lit les deux.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` et `GetResolutionPage` utilisent le même protocole
`NevercLinkEntityPage`, en remplissant `NevercLTOInputModuleInfo` et
`NevercLTOSymbolResolution`. Chaque résolution nomme le module, le symbole, le
`NevercLinkSymbolHandle` correspondant, et ses drapeaux :

| Drapeau | Signification |
|---|---|
| `PREVAILING` | Ce module possède la définition. |
| `VISIBLE_TO_REGULAR_OBJECT` | Un objet non bitcode peut le voir. |
| `EXPORTED` | Présent dans la table des symboles dynamiques. |
| `FINAL_DEFINITION` | Aucune définition ultérieure ne peut le remplacer. |
| `CAN_INLINE` | L'inlining à travers la frontière est permis. |
| `CAN_INTERNALIZE` | L'internalisation est permise. |
| `LINKER_REDEFINED` | L'éditeur de liens l'a redéfini. |
| `REFERENCED_BY_REGULAR_OBJECT` | Un objet ordinaire le référence. |

`NevercLTOOptions` sélectionne `NEVERC_LTO_FULL` ou `NEVERC_LTO_THIN`, les
niveaux d'optimisation, `ThreadBudget`, `ThinBackendPartitions`, le CPU et les
fonctionnalités, ainsi qu'une portée de cache parmi `DISABLED`, `TASK`,
`LOCAL_SHARED` ou `REMOTE_SHARED`. Les drapeaux d'options sont
`EMIT_OPTIMIZED_BITCODE`, `EMIT_INDEX`, `SAVE_TEMPS`,
`WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO` et `DETERMINISTIC`.

### Un fournisseur LTO

```c
NevercLTOProviderDescriptor Provider = {0};
Provider.Header = /* … */;
Provider.ProviderID    = SV("com.example.my-lto");
Provider.TargetID      = MyTargetID;
Provider.Flags         = NEVERC_LTO_PROVIDER_THIN |
                         NEVERC_LTO_PROVIDER_DETERMINISTIC |
                         NEVERC_LTO_PROVIDER_CACHEABLE;
Provider.BuildCacheKey = my_cache_key;
Provider.Codegen       = my_codegen;
LTORegistrar->RegisterProvider(LTORegistrar->Context, RegistrarContext,
                               &Provider);
```

`BuildCacheKey` écrit dans un `NevercMutableByteView` fourni par l'appelant et
signale la taille dont il aurait eu besoin, si bien que l'hôte peut
dimensionner le tampon et réessayer. Ce doit être une fonction pure de la
requête — la dériver de `RequestDigest` et `ResolutionDigest` est la
construction sûre. Déclarer `CACHEABLE` avec une clé qui ignore une partie de la
requête produit des objets périmés qui survivent à une reconstruction propre.

`Codegen` remplit un `NevercLTOProductCandidate` : un tableau de
`NevercLTOObjectProduct` (chacun nommant son module source, son ObjectGraph et
son artefact), éventuellement `OptimizedBitcode` et `ThinIndex`, et la
`CacheKey` réellement utilisée.

## Règles

- Les handles sont portés par la tâche et appartiennent à l'hôte. N'en stockez
  jamais un au-delà du callback, ne l'utilisez pas dans une autre tâche et ne
  fabriquez jamais une valeur.
- `NevercLinkEntityPage.Data` est à vous. L'hôte écrit au plus
  `ElementCapacity × ElementStride` octets et n'en garde aucune référence.
- Chaque `BeginMutation` atteint exactement un `CommitMutation` ou
  `AbandonMutation`, y compris sur le chemin d'erreur.
- Modifiez le plus tôt possible dans la machine à états ; une mutation tardive
  invalide silencieusement toutes les phases en aval.
- Ne modifiez pas depuis un observateur. Les observateurs reçoivent un pont en
  lecture seule et la tentative est rejetée avec
  `NEVERC_STATUS_POLICY_VIOLATION`.
- N'écrivez les octets de l'image que via `NevercBinaryImageInfo.Binary` et son
  constructeur. Un dépassement abandonne la préparation au lieu d'agrandir la
  sortie.
- Ne revendiquez `DETERMINISTIC` que si la même empreinte de requête produit
  toujours une sortie identique octet par octet, et `CACHEABLE` que si votre clé
  de cache couvre toute entrée susceptible de changer cette sortie.
- `image_verify`, `side_outputs_verify` et `commit` sont scellées. Observez-les ;
  n'essayez pas de les intercepter ni de les sauter.

Voir `PluginLink.h` et `PluginLTO.h` pour les déclarations normatives,
`Schema/PhaseSchema.json` pour les politiques des vingt phases, et
`coverage.json` pour les tests qui épinglent chacune d'elles.
