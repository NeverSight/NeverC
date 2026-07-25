**Sprachen**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC Plugin-API für Quelltext und E/A

`PluginSource.h` veröffentlicht zwei Tabellen. `NevercIOAPI` ist das
Dateisystem: Anbieter virtueller Dateien, Lesevorgänge, Verzeichnisdurchläufe,
Ausgabesenken und Abhängigkeitseinträge. `NevercSourceLocationAPI` bildet
compilerinterne Positionen zurück auf Dateien, Zeilen und den geschriebenen
Text. Zusammen erlauben sie einem Plugin, einen nur im Speicher existierenden
Header bereitzustellen, eine Makroexpansion bis zu ihrer Schreibstelle
aufzulösen oder eine Nebenausgabe zu schreiben, die an der
Dauerhaftigkeitsbuchführung des Builds teilnimmt.

## Schnittstellen

```c
#include "neverc/Plugin/PluginSource.h"
```

| Schnittstelle | Tabelle | Versionsmakros |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` und `_MINOR` sind Aliase des source-location-Paares.

## Die drei Quelltextphasen

| Phase | Richtlinie | Bedeutung |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | Eine Treibereingabe in eine Quelltexteingabe verwandeln |
| `neverc.source.open` | zusätzlich REPLACEABLE | Die Quelltexteinheit zu einer Eingabe erzeugen |
| `neverc.source.after_open` | OBSERVABLE | Mitteilung, dass eine Einheit verfügbar ist |

Da `neverc.source.open` ersetzbar ist, kann ein Anbieter eine Einheit
zurückgeben, deren Bytes er selbst erzeugt hat — das ist der unterstützte Weg,
generierten Code einzuschleusen, ohne die Platte zu berühren.

## Anbieter für virtuelle Dateisysteme

Ein VFS-Anbieter beansprucht ein Pfadpräfix und beantwortet die vier Fragen, die
der Compiler zu einer Datei stellt.

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

Jeder Rückruf füllt ein Ergebnis, dessen `Disposition` angibt, ob der Anbieter
die Anfrage bearbeitet hat:

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

`NEVERC_VFS_RESULT_NOT_HANDLED` zurückzugeben reicht die Anfrage an den nächsten
Anbieter weiter und schließlich an das echte Dateisystem. Die Dateitypen sind
`NEVERC_VFS_FILE_UNKNOWN`, `REGULAR`, `DIRECTORY`, `SYMLINK` und `OTHER`.

Registriert wird während `Register`:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

Für eine einzelne Datei im Speicher, die nur eine Sitzung lang existieren muss,
kann man den Anbieter ganz weglassen:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
ist ein vollständig funktionierender Anbieter.

## Dateien lesen

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

`CopyBuffer` verwandelt Bytes, die Ihnen gehören, in einen Host-Puffer,
`Canonicalize` löst einen Pfad auf, und `GetWorkingDirectory` /
`SetWorkingDirectory` verwalten das aktuelle Verzeichnis der Aufgabe.
Verzeichnisse durchläuft man mit `OpenDirectory`, `ReadDirectory` (das am Ende
`OutHasEntry` auf `NEVERC_FALSE` setzt) und `CloseDirectory`.

Die E/A-Fehlercodes werden in `NevercStatus.Detail` gemeldet:
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH` und `IO`.

## Ausgaben schreiben

Ausgaben sind transaktional. Man öffnet eine Senke, schreibt und schließt dann
ab, um ein Siegel zu erhalten — eine Größe und einen 32-Byte-Digest, den das
Build-System prüfen kann.

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

| Funktion | Zweck |
|---|---|
| `BeginMemoryOutput` | Speichergestützte, logisch benannte Senke |
| `BeginFileOutput` | Senke, die atomar an einem endgültigen Pfad landet |
| `BeginStreamOutput` | Senke auf `NEVERC_OUTPUT_STREAM_STDOUT` oder `_STDERR` |
| `OutputWrite`, `OutputWriteAt` | Anhängen oder an einem Offset schreiben |
| `OutputTell`, `OutputTruncate` | Steuerung von Position und Größe |
| `OutputMetadataSet` | Ein Schlüssel/Wert-Paar an die Ausgabe heften |
| `OutputFinish` | Die Ausgabe versiegeln und `NevercOutputSeal` erzeugen |
| `OutputAbort` | Alles Geschriebene verwerfen |
| `OutputGetSummary` | Zustand, Flags, Größe und Digest jederzeit einsehen |

`NevercOutputSummary.State` durchläuft `NEVERC_OUTPUT_OPEN`, `FINISHED`,
`COMMITTED`, `ABORTED` oder `FAILED_PARTIAL`, und `Flags` hält `PUBLISHED`,
`DURABLE`, `MAY_BE_PARTIAL`, `RECOVERY_REQUIRED` und `DURABILITY_UNCONFIRMED`
fest. Diese Flags tragen dieselbe Information, die der Treiber in
`NevercStatus.Flags` sichtbar macht — ein Absturz mitten im Schreiben lässt sich
also von einem sauberen Fehlschlag unterscheiden.

Ein `SizeBudget` von null bedeutet unbegrenzt; ein Budget ungleich null lässt
eine Überschreitung mit `NEVERC_STATUS_RESOURCE_EXHAUSTED` scheitern, statt eine
Platte vollzuschreiben.

## Abhängigkeiten festhalten

Wenn ein Plugin etwas liest, das das Build-System verfolgen sollte, dann sagen
Sie es. Sonst baut ein inkrementeller Build nicht neu, wenn sich diese Eingabe
ändert.

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

Die Arten sind `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`,
`RESOURCE`, `TOOL` und `PLUGIN`.

## Quelltextpositionen

Eine `NevercSourceLocation` ist opak. Die Positionstabelle macht daraus etwas,
das man ausgeben oder vergleichen kann.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind ist NEVERC_SOURCE_LOCATION_FILE oder _MACRO;
   danach folgen Info.FileOffset, Info.Line, Info.Column. */
```

Vier Transformationen bewegen sich zwischen den Sichten auf eine Position; alle
teilen die Signatur `NevercTransformSourceLocationFn`:

| Funktion | Liefert |
|---|---|
| `GetSpellingLocation` | Wo die Zeichen des Tokens tatsächlich stehen |
| `GetExpansionLocation` | Wo die Makroexpansion im Quelltext auftaucht |
| `GetFileLocation` | Die nächstgelegene Dateiposition |
| `GetIncludeLocation` | Das `#include`, das die Datei hereingeholt hat |
| `GetTokenEnd` | Eine Stelle hinter dem letzten Zeichen des Tokens |

`GetPresumedLocation` wendet `#line`-Direktiven an und liefert Dateiname, Zeile,
Spalte und Einbindungsposition. `GetLocationFile` zusammen mit `GetFileInfo`
liefert den kanonischen Pfad, die Größe, den Änderungszeitpunkt, die eindeutige
ID und ob die Datei eine Benutzer-, System- oder extern-C-Systemdatei ist:

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

Bereiche liest man mit `GetRangeInfo` (das `Begin`, `End` und die Angabe
meldet, ob der Bereich `NEVERC_SOURCE_RANGE_CHARACTER` oder `_TOKEN` ist), die
Bytes selbst mit `GetSourceText` oder `GetCharacterData`.

Wenn viele Positionen auf einmal gebraucht werden — etwa ein Diagnoselauf über
eine ganze Funktion —, nehmen Sie die Stapelform statt eines Aufrufs je
Position:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## Quelltexteinheiten

Die Sicht auf eine Eingabe und ihre Bytes auf Phasenebene:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind (FILE oder BUFFER), .Language, .System, .Preprocessed */
```

Ein Anbieter für `neverc.source.open` antwortet mit einer speichergestützten
Einheit:

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

Der Cache schlüsselt über `CanonicalIdentity`, dieser Wert muss sich also
ändern, sobald sich der Inhalt ändert. `GetSourceUnit` liest eine Einheit zurück
und meldet zusätzlich `MemoryBacked`.

## Regeln

- Puffer aus `ReadFile`, `CopyBuffer` und `PathToBuffer` gehören dem Host; geben
  Sie jeden einzelnen mit `ReleaseBuffer` frei.
- Jedes `OpenFileForRead` braucht ein `CloseFile`; jedes `OpenDirectory` ein
  `CloseDirectory`; jede Ausgabesenke ein `OutputFinish` oder `OutputAbort`.
- Sichten in `NevercFileInfo`, `NevercVFSStatus` und in Positionsergebnissen
  sind nur für die Dauer des Rückrufs geliehen.
- Der Rückruf eines VFS-Anbieters läuft auf dem Aufgaben-Thread und darf den
  Compiler nicht zurückrufen; antworten Sie aus Daten, die Sie bereits haben.
- Deklarieren Sie `Deterministic` und `Cacheable` wahrheitsgemäß. Ein Anbieter,
  der Uhr oder Umgebung liest und dennoch Determinismus behauptet, erzeugt einen
  vergifteten Build-Cache.
- `AddMemoryFile` hat Sitzungsgültigkeit; hängt der Inhalt von der Aufgabe ab,
  ist ein Anbieter das richtige Werkzeug.

Die normativen Deklarationen stehen in `PluginSource.h`, ein vollständiger
Anbieter in `pluginsdk/examples/VirtualHeaderPlugin.c`.
