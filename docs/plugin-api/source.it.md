**Lingue**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API sorgenti e I/O dei plugin NeverC

[`PluginSource.h`] pubblica due tabelle. `NevercIOAPI` è il file system: provider
di file virtuali, letture, attraversamento di directory, sink di output e
registrazione delle dipendenze. `NevercSourceLocationAPI` riporta le posizioni
interne del compilatore a file, righe e testo così com'è scritto. Insieme
permettono a un plugin di servire un header che esiste solo in memoria, di
risolvere un'espansione di macro fino al punto in cui è scritta, o di produrre
un output collaterale che partecipa alla contabilità di durabilità della build.

## Interfacce

```c
#include "neverc/Plugin/PluginSource.h"
```

| Interfaccia | Tabella | Macro di versione |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` e `_MINOR` sono alias della coppia source-location.

## Le tre fasi sorgente

| Fase | Politica | Significato |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | Trasformare un input del driver in un input sorgente |
| `neverc.source.open` | più REPLACEABLE | Produrre l'unità sorgente per un input |
| `neverc.source.after_open` | OBSERVABLE | Notifica che un'unità è disponibile |

Poiché `neverc.source.open` è sostituibile, un provider può restituire un'unità
i cui byte ha sintetizzato lui stesso: è il modo supportato per iniettare codice
generato senza toccare il disco.

## Provider di file system virtuale

Un provider VFS rivendica un prefisso di percorso e risponde alle quattro
domande che il compilatore pone su un file.

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

Ogni callback riempie un risultato il cui `Disposition` dice se il provider ha
gestito la richiesta:

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

Restituire `NEVERC_VFS_RESULT_NOT_HANDLED` passa al provider successivo e alla
fine al file system reale. I tipi di file sono `NEVERC_VFS_FILE_UNKNOWN`,
`REGULAR`, `DIRECTORY`, `SYMLINK` e `OTHER`.

La registrazione avviene durante `Register`:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

Per un singolo file in memoria che deve esistere solo per una sessione, il
provider è superfluo:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`] è un provider completo e funzionante.

## Leggere file

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

`CopyBuffer` trasforma byte di vostra proprietà in un buffer dell'host,
`Canonicalize` risolve un percorso, e `GetWorkingDirectory` /
`SetWorkingDirectory` gestiscono la directory corrente del task. Le directory si
attraversano con `OpenDirectory`, `ReadDirectory` (che alla fine imposta
`OutHasEntry` a `NEVERC_FALSE`) e `CloseDirectory`.

I codici di errore di I/O sono riportati in `NevercStatus.Detail`:
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH` e `IO`.

## Scrivere output

Gli output sono transazionali. Si apre un sink, si scrive, poi si conclude per
ottenere un sigillo — una dimensione e un digest di 32 byte che il sistema di
build può verificare.

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

| Funzione | Scopo |
|---|---|
| `BeginMemoryOutput` | Sink appoggiato alla memoria, con nome logico |
| `BeginFileOutput` | Sink che atterra atomicamente su un percorso finale |
| `BeginStreamOutput` | Sink su `NEVERC_OUTPUT_STREAM_STDOUT` o `_STDERR` |
| `OutputWrite`, `OutputWriteAt` | Accodare, oppure scrivere a un offset |
| `OutputTell`, `OutputTruncate` | Controllo di posizione e dimensione |
| `OutputMetadataSet` | Allegare una coppia chiave/valore all'output |
| `OutputFinish` | Sigillare l'output e produrre un `NevercOutputSeal` |
| `OutputAbort` | Scartare tutto ciò che è stato scritto |
| `OutputGetSummary` | Ispezionare stato, flag, dimensione e digest in qualunque momento |

`NevercOutputSummary.State` attraversa `NEVERC_OUTPUT_OPEN`, `FINISHED`,
`COMMITTED`, `ABORTED` o `FAILED_PARTIAL`, e `Flags` registra `PUBLISHED`,
`DURABLE`, `MAY_BE_PARTIAL`, `RECOVERY_REQUIRED` e `DURABILITY_UNCONFIRMED`.
Sono le stesse informazioni che il driver espone in `NevercStatus.Flags`, così
un crash a metà scrittura si distingue da un fallimento pulito.

Un `SizeBudget` pari a zero significa illimitato; un budget diverso da zero fa
fallire uno sforamento con `NEVERC_STATUS_RESOURCE_EXHAUSTED` invece di
riempire un disco.

## Registrare le dipendenze

Se un plugin legge qualcosa che il sistema di build dovrebbe tracciare,
dichiaratelo. Altrimenti una build incrementale non ricostruirà quando quell'input
cambia.

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

I generi sono `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`,
`RESOURCE`, `TOOL` e `PLUGIN`.

## Posizioni sorgente

Una `NevercSourceLocation` è opaca. La tabella delle posizioni la trasforma in
qualcosa che si può stampare o confrontare.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind è NEVERC_SOURCE_LOCATION_FILE oppure _MACRO;
   seguono Info.FileOffset, Info.Line, Info.Column. */
```

Quattro trasformazioni si spostano fra le viste di una posizione, tutte con la
stessa firma `NevercTransformSourceLocationFn`:

| Funzione | Restituisce |
|---|---|
| `GetSpellingLocation` | Dove sono scritti davvero i caratteri del token |
| `GetExpansionLocation` | Dove l'espansione di macro compare nel sorgente |
| `GetFileLocation` | La posizione di file più vicina |
| `GetIncludeLocation` | L'`#include` che ha portato dentro il file |
| `GetTokenEnd` | Subito dopo l'ultimo carattere del token |

`GetPresumedLocation` applica le direttive `#line` e produce nome file, riga,
colonna e posizione di inclusione. `GetLocationFile` insieme a `GetFileInfo` dà
il percorso canonico, la dimensione, l'ora di modifica, l'ID univoco e se il
file è utente, di sistema o di sistema extern-C:

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

Gli intervalli si leggono con `GetRangeInfo` (che riporta `Begin`, `End` e se
l'intervallo è `NEVERC_SOURCE_RANGE_CHARACTER` o `_TOKEN`), i byte stessi con
`GetSourceText` o `GetCharacterData`.

Quando servono molte posizioni in una volta — una passata diagnostica su
un'intera funzione, per esempio — usate la forma a lotti invece di una chiamata
per posizione:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## Unità sorgente

La vista di un input e dei suoi byte a livello di fase:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind (FILE o BUFFER), .Language, .System, .Preprocessed */
```

Un provider per `neverc.source.open` risponde con un'unità appoggiata alla
memoria:

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

La cache si indicizza su `CanonicalIdentity`, quindi deve cambiare ogni volta
che cambia il contenuto. `GetSourceUnit` rilegge un'unità e riporta in più
`MemoryBacked`.

## Regole

- I buffer restituiti da `ReadFile`, `CopyBuffer` e `PathToBuffer` appartengono
  all'host; rilasciateli tutti con `ReleaseBuffer`.
- Ogni `OpenFileForRead` richiede una `CloseFile`; ogni `OpenDirectory` una
  `CloseDirectory`; ogni sink di output un `OutputFinish` o un `OutputAbort`.
- Le viste dentro `NevercFileInfo`, `NevercVFSStatus` e i risultati di posizione
  sono prestate solo per la durata della callback.
- La callback di un provider VFS gira sul thread del task e non deve richiamare
  il compilatore; rispondete con i dati che avete già.
- Dichiarate `Deterministic` e `Cacheable` in modo veritiero. Un provider che
  legge l'orologio o l'ambiente e dichiara determinismo produrrà una cache di
  build avvelenata.
- `AddMemoryFile` ha ambito di sessione; quando il contenuto dipende dal task,
  il provider è lo strumento giusto.

Vedere [`PluginSource.h`] per le dichiarazioni normative,
[`Schema/PhaseSchema.json`] per le tre fasi source e le loro policy, e
[`pluginsdk/examples/VirtualHeaderPlugin.c`] per un provider completo.

<!-- reference links -->
[`pluginsdk/examples/VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
