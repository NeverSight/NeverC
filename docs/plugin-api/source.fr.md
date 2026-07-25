**Langues**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# API Source et E/S des plugins NeverC

`PluginSource.h` publie deux tables. `NevercIOAPI` est le système de fichiers :
fournisseurs de fichiers virtuels, lectures, parcours de répertoires, puits de
sortie et enregistrement des dépendances. `NevercSourceLocationAPI` ramène les
positions internes du compilateur vers des fichiers, des lignes et le texte tel
qu'il est écrit. À elles deux, elles permettent à un plugin de servir un en-tête
qui n'existe qu'en mémoire, de résoudre une expansion de macro jusqu'à son
emplacement d'écriture, ou d'écrire une sortie annexe qui participe à la
comptabilité de durabilité de la compilation.

## Interfaces

```c
#include "neverc/Plugin/PluginSource.h"
```

| Interface | Table | Macros de version |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` et `_MINOR` sont des alias de la paire
source-location.

## Les trois phases source

| Phase | Politique | Signification |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | Transformer une entrée du pilote en entrée source |
| `neverc.source.open` | plus REPLACEABLE | Produire l'unité source correspondant à une entrée |
| `neverc.source.after_open` | OBSERVABLE | Notification qu'une unité est disponible |

Comme `neverc.source.open` est remplaçable, un fournisseur peut rendre une unité
dont il a synthétisé lui-même les octets : c'est la façon prise en charge
d'injecter du code généré sans toucher au disque.

## Fournisseurs de système de fichiers virtuel

Un fournisseur VFS revendique un préfixe de chemin et répond aux quatre
questions que le compilateur pose au sujet d'un fichier.

```c
typedef struct NevercVFSProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercStringView RoutePrefix;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint64_t Reserved;
  NevercVFSPathPredicateFn MatchesPath;
  NevercVFSProviderStatusFn Status;
  NevercVFSProviderOpenReadFn OpenRead;
  NevercVFSProviderReadDirectoryFn ReadDirectory;
  NevercVFSProviderCanonicalizeFn Canonicalize;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercVFSProviderDescriptor;
```

Chaque rappel remplit un résultat dont le champ `Disposition` indique si le
fournisseur a traité la requête :

```c
static NevercStatus NEVERC_CALL
open_read(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  static const char Header[] = "#define GENERATED 1\n";
  if (!path_matches(Path)) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition   = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Status.Type   = NEVERC_VFS_FILE_REGULAR;
  OutResult->Status.Size   = sizeof(Header) - 1;
  OutResult->Content.Data  = (const uint8_t *)Header;
  OutResult->Content.Length = sizeof(Header) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}
```

Renvoyer `NEVERC_VFS_RESULT_NOT_HANDLED` fait passer au fournisseur suivant,
puis finalement au vrai système de fichiers. Les types de fichier sont
`NEVERC_VFS_FILE_UNKNOWN`, `REGULAR`, `DIRECTORY`, `SYMLINK` et `OTHER`.

L'enregistrement a lieu pendant `Register` :

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

Pour un unique fichier en mémoire qui ne doit exister que le temps d'une
session, le fournisseur est superflu :

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
est un fournisseur complet et fonctionnel.

## Lire des fichiers

```c
NevercVFSStatus Status;
IO->Stat(IO->Context, Task, Path, &Status);

NevercFileHandle File;
IO->OpenFileForRead(IO->Context, Task, Path, &File);

NevercBufferHandle Buffer;
IO->ReadFile(IO->Context, Task, File, /*Offset=*/0, /*Length=*/Status.Size,
             &Buffer);

NevercBufferView View;
IO->GetBufferView(IO->Context, Task, Buffer, &View);
/* View.Data / View.Length / View.NullTerminated */

IO->ReleaseBuffer(IO->Context, Task, Buffer);
IO->CloseFile(IO->Context, Task, File);
```

`CopyBuffer` transforme des octets qui vous appartiennent en tampon hôte,
`Canonicalize` résout un chemin, et `GetWorkingDirectory` /
`SetWorkingDirectory` gèrent le répertoire courant de la tâche. Les répertoires
se parcourent avec `OpenDirectory`, `ReadDirectory` (qui met `OutHasEntry` à
`NEVERC_FALSE` à la fin) et `CloseDirectory`.

Les codes d'erreur d'E/S sont rapportés dans `NevercStatus.Detail` :
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH` et `IO`.

## Écrire des sorties

Les sorties sont transactionnelles. On ouvre un puits, on écrit, puis on
termine pour obtenir un sceau — une taille et un condensat de 32 octets que le
système de compilation peut vérifier.

```c
NevercOutputSinkHandle Sink;
IO->BeginFileOutput(IO->Context, Task, SV("out.json"), /*SizeBudget=*/0, &Sink);
IO->OutputWrite(IO->Context, Task, Sink, Bytes);
IO->OutputMetadataSet(IO->Context, Task, Sink, SV("content-type"),
                      SV("application/json"));

NevercOutputSeal Seal = {0};
Seal.Header = (NevercABITableHeader){sizeof(Seal), NEVERC_IO_API_MAJOR,
                                     NEVERC_IO_API_MINOR, 0};
IO->OutputFinish(IO->Context, Task, Sink, &Seal);
```

| Fonction | Rôle |
|---|---|
| `BeginMemoryOutput` | Puits adossé à la mémoire, nommé logiquement |
| `BeginFileOutput` | Puits qui atterrit atomiquement à un chemin final |
| `BeginStreamOutput` | Puits sur `NEVERC_OUTPUT_STREAM_STDOUT` ou `_STDERR` |
| `OutputWrite`, `OutputWriteAt` | Ajouter, ou écrire à un décalage donné |
| `OutputTell`, `OutputTruncate` | Contrôle de la position et de la taille |
| `OutputMetadataSet` | Attacher une paire clé/valeur à la sortie |
| `OutputFinish` | Sceller la sortie et produire un `NevercOutputSeal` |
| `OutputAbort` | Jeter tout ce qui a été écrit |
| `OutputGetSummary` | Inspecter à tout moment état, drapeaux, taille et condensat |

`NevercOutputSummary.State` traverse `NEVERC_OUTPUT_OPEN`, `FINISHED`,
`COMMITTED`, `ABORTED` ou `FAILED_PARTIAL`, et `Flags` enregistre `PUBLISHED`,
`DURABLE`, `MAY_BE_PARTIAL`, `RECOVERY_REQUIRED` et `DURABILITY_UNCONFIRMED`.
Ces drapeaux portent la même information que celle exposée par le pilote dans
`NevercStatus.Flags` : un plantage en cours d'écriture se distingue donc d'un
échec propre.

Un `SizeBudget` nul signifie « sans limite » ; un budget non nul fait échouer un
dépassement avec `NEVERC_STATUS_RESOURCE_EXHAUSTED` au lieu de remplir un
disque.

## Enregistrer les dépendances

Si un plugin lit quelque chose que le système de compilation devrait suivre,
dites-le. Sinon une compilation incrémentale ne se relancera pas quand cette
entrée changera.

```c
NevercDependencyDescriptor Dependency = {0};
Dependency.Header = (NevercABITableHeader){sizeof(Dependency),
                                           NEVERC_IO_API_MAJOR,
                                           NEVERC_IO_API_MINOR, 0};
Dependency.CanonicalPath = SV("/etc/mytool/rules.txt");
Dependency.ContentDigest = Digest;
Dependency.Kind          = NEVERC_INPUT_DEPENDENCY_RESOURCE;
Dependency.System        = NEVERC_FALSE;
Dependency.ProviderID    = SV("com.example.myplugin");

NevercDependencyHandle Handle;
IO->RecordDependency(IO->Context, Task, &Dependency, &Handle);
```

Les genres sont `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`,
`RESOURCE`, `TOOL` et `PLUGIN`.

## Emplacements source

Un `NevercSourceLocation` est opaque. La table des emplacements en fait quelque
chose que l'on peut afficher ou comparer.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind vaut NEVERC_SOURCE_LOCATION_FILE ou _MACRO ;
   viennent ensuite Info.FileOffset, Info.Line, Info.Column. */
```

Quatre transformations circulent entre les vues d'un emplacement ; toutes
partagent la signature `NevercTransformSourceLocationFn` :

| Fonction | Renvoie |
|---|---|
| `GetSpellingLocation` | Là où les caractères du jeton sont réellement écrits |
| `GetExpansionLocation` | Là où l'expansion de macro apparaît dans la source |
| `GetFileLocation` | L'emplacement de fichier le plus proche |
| `GetIncludeLocation` | Le `#include` qui a amené le fichier |
| `GetTokenEnd` | Juste après le dernier caractère du jeton |

`GetPresumedLocation` applique les directives `#line` et fournit un nom de
fichier, une ligne, une colonne et un emplacement d'inclusion.
`GetLocationFile` associé à `GetFileInfo` donne le chemin canonique, la taille,
la date de modification, l'identifiant unique, et si le fichier est utilisateur,
système ou système extern-C :

```c
typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;      /* {Device, File} */
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;
```

Les plages se lisent avec `GetRangeInfo` (qui rapporte `Begin`, `End`, et si la
plage est `NEVERC_SOURCE_RANGE_CHARACTER` ou `_TOKEN`), et les octets eux-mêmes
avec `GetSourceText` ou `GetCharacterData`.

Quand beaucoup d'emplacements sont nécessaires d'un coup — une passe de
diagnostic sur une fonction entière, par exemple — utilisez la forme par lot
plutôt qu'un appel par emplacement :

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## Unités source

La vue d'une entrée et de ses octets au niveau des phases :

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind (FILE ou BUFFER), .Language, .System, .Preprocessed */
```

Un fournisseur pour `neverc.source.open` répond par une unité adossée à la
mémoire :

```c
NevercMemorySourceUnitDescriptor Unit = {0};
Unit.Header = (NevercABITableHeader){sizeof(Unit),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Unit.LogicalPath      = SV("/virtual/generated.c");
Unit.CanonicalIdentity = SV("com.example:generated:v1");
Unit.Content          = Bytes;
Unit.ProviderID       = SV("com.example.myplugin");
Unit.Deterministic    = NEVERC_TRUE;
Unit.Cacheable        = NEVERC_TRUE;

NevercArtifactHandle Output;
Source->CreateMemorySourceUnit(Source->Context, Frame, Frame->Input, &Unit,
                               &Output);
```

C'est sur `CanonicalIdentity` que le cache s'indexe : cette valeur doit donc
changer dès que le contenu change. `GetSourceUnit` relit une unité et rapporte
en outre `MemoryBacked`.

## Règles

- Les tampons issus de `ReadFile`, `CopyBuffer` et `PathToBuffer` appartiennent
  à l'hôte ; libérez chacun d'eux avec `ReleaseBuffer`.
- Chaque `OpenFileForRead` exige un `CloseFile` ; chaque `OpenDirectory` un
  `CloseDirectory` ; chaque puits de sortie un `OutputFinish` ou un
  `OutputAbort`.
- Les vues contenues dans `NevercFileInfo`, `NevercVFSStatus` et les résultats
  d'emplacement ne sont empruntées que pour la durée du rappel.
- Le rappel d'un fournisseur VFS s'exécute sur le fil de la tâche et ne doit pas
  rappeler le compilateur ; répondez à partir des données que vous détenez déjà.
- Déclarez `Deterministic` et `Cacheable` honnêtement. Un fournisseur qui lit
  l'horloge ou l'environnement tout en revendiquant le déterminisme produira un
  cache de compilation empoisonné.
- `AddMemoryFile` a la portée d'une session ; lorsque le contenu dépend de la
  tâche, le fournisseur est l'outil approprié.

Voir `PluginSource.h` pour les déclarations normatives et
`pluginsdk/examples/VirtualHeaderPlugin.c` pour un fournisseur complet.
