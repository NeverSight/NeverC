**Sprachen**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC Plugin-Link- und LTO-API

Das Linken ist als **Zustandsmaschine über einem einzigen Graphen** modelliert.
`PluginLink.h` legt diesen Graphen offen — Eingaben, Sections, Atome, Symbole,
Kanten, COMDATs, Imports, Exports, Unwind-Datensätze, Synthetics und
Layout-Constraints — dazu die zwanzig Phasen, die ihn von einer Dateiliste zu
einem committeten Binärabbild führen. `PluginLTO.h` deckt die beiden Phasen in
der Mitte ab, in denen Bitcode zu Objekten wird.

Ein Plugin kann jeden Schritt beobachten, die meisten abfangen, einen einzelnen
Schritt ersetzen, den gesamten Link ersetzen oder Objekte zusammenführen. Es
sieht nie eine lld-Datenstruktur: Der Graph ist eine normalisierte Projektion,
auf die die ELF-, COFF- und Mach-O-Backends allesamt abbilden.

## Schnittstellen

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* enthält PluginLink.h */
```

| Schnittstelle | Tabelle | Zweck |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | Link-Graph lesen und verändern (52 Slots) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | Linker-, Objektmerge- und Image-Verifier-Provider registrieren |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | Graph oder Image hinter einem `NevercArtifactHandle` erreichen |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | LTO-Anfrage, Module und Symbolauflösungen lesen |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | LTO-Codegen-Provider registrieren |

Alle fünf sind bei Major 1 `NEVERC_INTERFACE_STABLE`, ein neuerer Host darf also
nur anhängen. Kombinieren Sie jede mit ihrem `NEVERC_LINK_API_MAJOR` /
`NEVERC_LTO_API_MAJOR` und prüfen Sie `TableSize` gegen den letzten Slot, den
Sie aufrufen.

## Die Zustandsmaschine

`NevercLinkGraphInfo.State` ist einer von vierzehn Werten, und dreizehn der
zwanzig Phasen existieren allein, um ihn um einen Schritt weiterzubringen:

| Phase | Resultierender `NEVERC_LINK_STATE_…` | Host-Verifier |
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

Jede dieser dreizehn ist
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`, ein Provider
darf den Übergang also selbst liefern, und ein Plugin mit gültigem
`NevercLinkProofHandle` darf ihn überspringen.

Die verbleibenden sieben sind strukturell:

| Phase | Policy | Rolle |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Den gesamten Link ersetzen, von `INITIAL` direkt zum Binärabbild |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Relozierbares `-r`-Merge von ObjectGraphs |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | Letzte Gelegenheit, die Bytes des Abbilds anzufassen |
| `neverc.link.image_verify` | OBSERVABLE, **VERSIEGELT** | Image-Verifier des Hosts |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **VERSIEGELT** | Map-Dateien, dSYM, Nebenartefakte |
| `neverc.link.commit` | OBSERVABLE, **VERSIEGELT** | Atomare Veröffentlichung des Ausgabebündels |
| `neverc.link.after_commit` | OBSERVABLE | Benachrichtigung nach dem Commit |

Die drei versiegelten Gates lassen sich beobachten, aber niemals abfangen,
ersetzen oder überspringen. `NEVERC_BUILTIN_LINK_PHASE_COUNT` ist 20.

## Aus einer Phase an den Graphen kommen

`NevercLinkPhaseAPI` verwandelt das Artefakt des Frames in ein nutzbares Handle:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` ist die an diesen Task gebundene `NevercLinkAPI`, ein Observer
braucht also kein eigenes `QueryInterface`. Ein Provider veröffentlicht sein
Ergebnis mit `PublishGraph`; `GetImage` tut dasselbe für ein Image-Artefakt und
liefert ein `NevercLinkPhaseImageInfo` mit dem Abbild, dem Ausgabebündel und
einem `NevercBinaryImageState` (`CANDIDATE`, `VERIFIED`, `COMMITTED`, `ABORTED`
oder `FAILED_PARTIAL`).

## Den Graphen lesen

`NevercLinkGraphInfo` ist die Zusammenfassung — Ziel, Format, Zustand,
Generation, siebzehn Entitätszähler und ein 32-Byte-`SemanticDigest`. Die
Entitäten selbst kommen über je einen Paging-Aufruf pro Art zurück, alle mit
einer Seite im Besitz des Aufrufers:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* Array, das Sie stellen und besitzen */
  uint64_t ElementCapacity;  /* wie viele Einträge hineinpassen     */
  uint64_t ElementStride;    /* sizeof Ihres Elements               */
  uint64_t OutCount;         /* wie viele der Host geschrieben hat  */
  uint64_t NextCursor;       /* zum Fortsetzen zurückgeben          */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

Der Host schreibt höchstens `ElementCapacity` Einträge zu je `ElementStride`
Bytes und behält `Data` nie, ein Array auf dem Stack genügt also:

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

Fünfzehn Graph-Pager folgen dieser Form — `GetInputPage`, `GetArchivePage`,
`GetArchiveMemberPage`, `GetSharedLibraryPage`, `GetBitcodeModulePage`,
`GetSectionPage`, `GetAtomPage`, `GetSymbolPage`, `GetEdgePage`,
`GetComdatPage`, `GetImportPage`, `GetExportPage`, `GetUnwindPage`,
`GetSyntheticPage` und `GetConstraintPage` — und zwei weitere,
`GetBinarySegmentPage` und `GetBinarySectionPage`, pagen ein emittiertes
Abbild. Zu jedem gibt es ein passendes `Get…Info` für ein einzelnes Handle.

Jede Entitätsinfo trägt einen `NevercLinkOrigin`:

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

Das macht einen Link prüfbar: Zu jedem Atom in der Ausgabe können Sie die
Eingabedatei benennen, das Archivmitglied, aus dem es gezogen wurde, die Phase,
die es erzeugt hat, und das Plugin, das es zuletzt angefasst hat.

### Die Entitäten

| Art | Info-Struktur | Bemerkenswerte Felder |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Archiv / Mitglied | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Shared Library | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Bitcode-Modul | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Section | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Atom | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Symbol | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Kante | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Import / Export | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Unwind | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Synthetic | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Constraint | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

Atom-Flags sind `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`, `ADDRESS_SIGNIFICANT`,
`TLS` und `UNWIND`. Symbolbindungen sind `LOCAL`, `GLOBAL`, `WEAK` und
`COMMON`; Definitionen sind `UNDEFINED`, `DEFINED`, `ABSOLUTE`, `COMMON` und
`SHARED`. Kantenarten sind `RELOCATION`, `ASSOCIATION`, `KEEP_ALIVE`, `UNWIND`
und `FORMAT_EXTENSION`. Die COMDAT-Auswahl umfasst `ANY`, `EXACT_MATCH`,
`SAME_SIZE`, `LARGEST`, `NEWEST` und `NO_DUPLICATES`.

## Den Graphen verändern

Mutationen sind transaktional und immer auf einen Graphen beschränkt:

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

Der Commit staged auf eine Arbeitskopie, verifiziert sie und veröffentlicht erst
dann, wobei `Generation` erhöht wird. `AbandonMutation` verwirft alles. Ein
Commit, während der Graph etwa bei `GC_COMPLETE` steht, lässt den
Liveness-Verifier erneut laufen, sodass eine Mutation, die ein lebendes Atom
abhängen würde, abgewiesen statt geschrieben wird.

### Mutationen invalidieren nachgelagerte Zustände

Das ist der Teil, der überrascht. Jeder Staging-Aufruf wird klassifiziert, und
die Klassifikation bestimmt den **frühesten Zustand, der ungültig wird**; der
Host muss ab dort jede Phase erneut ausführen:

| Aufruf | Frühester invalidierter Zustand |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

Eine Mutation, die mehrere davon berührt, nimmt das Minimum. Ein Symbol nach dem
Layout neu zu binden wirft daher Layout-, Relokations- und Image-Ergebnisse weg
— billig während `gc`, teuer während `post_emit`. Mutieren Sie so früh in der
Zustandsmaschine, wie Ihre Änderung es zulässt.

`SetSymbolResolution` nimmt einen kleinen Aktualisierungsdatensatz statt eines
ganzen Symbols; so kann eine Auflösungsänderung nicht versehentlich einen Namen
oder Wert überschreiben:

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

## Eine Phase mit einem Beweis überspringen

Eine `SKIPPABLE_WITH_PROOF`-Phase akzeptiert ein `NevercLinkProofHandle`,
anstatt zu laufen. Der Beweis fixiert alles, wovon das Überspringen abhängt:

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

Da sowohl `GraphGeneration` als auch `SemanticDigest` festgehalten werden, macht
jede committete Mutation zwischen Ausstellung und Verwendung den Beweis
veraltet, und der Host führt die Phase tatsächlich aus.

## Das Binärabbild

Nach `emit_image` ist das Produkt ein `NevercBinaryImageHandle`:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                     */
```

Ausgabearten sind `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY` und `BUNDLE`.
Segment-Flags sind `READ`, `WRITE` und `EXECUTE`.

`Image.Binary` und `Image.Builder` sind der begrenzte transaktionale Schreiber
aus `PluginObject.h` — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`,
`Insert`, `Append`, `Resize`. Ein `post_emit`-Interceptor, der Bytes patcht,
muss über ihn gehen; Schreibvorgänge über die reservierte Grenze hinaus brechen
das Staging ab, statt die Datei wachsen zu lassen.

## Provider

Registrieren Sie während `Register`, niemals später.

### Den Linker ersetzen

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
Provider.VerifyImage  = my_verify;      /* optional */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

Der Callback erhält die Anfrage und die rohe Eingabemenge und füllt einen
Kandidaten:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs ist ein NevercRawLinkInput[], Inputs->OrderDigest fixiert die
     Reihenfolge */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` trägt die Flags, nach denen ein Linker tatsächlich
verzweigt — `PIE`, `STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`,
`ALLOW_UNDEFINED`, `WHOLE_ARCHIVE`, `DETERMINISTIC` — dazu `EntrySymbol`,
`InstallName`, `Soname`, `ImageBase`, `PageSize`, `ThreadBudget`, Suchpfade und
Bibliotheken. Flags pro Eingabe sind `WHOLE_ARCHIVE`, `AS_NEEDED`,
`START_GROUP`, `END_GROUP` und `LAZY`.

Bei Erfolg übernimmt der Host den Kandidaten. Bei Misserfolg gehört alles
Erzeugte weiterhin dem Provider. Die versiegelten Verify- und Commit-Gates
laufen in beiden Fällen.

### Objekte zusammenführen und Abbilder prüfen

`RegisterObjectMergeProvider` behandelt `-r`: Die Anfrage trägt die
eingehenden `NevercObjectMergeInput[]` sowie einen bereits geöffneten
Ausgabegraphen und eine Mutation, sodass der Provider in eine Transaktion des
Hosts schreibt, statt eine Datei zu bauen.

`RegisterBinaryImageVerifier` fügt eine Nur-Lese-Prüfung hinzu, die neben dem
Image-Verifier des Hosts läuft. Ersetzen kann sie ihn nicht.

## LTO

`lto_resolve` erzeugt die Symbolauflösungen; `lto_generate` verwandelt Bitcode
in Objekte. `NevercLTOAPI` liest beides.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` und `GetResolutionPage` verwenden dasselbe
`NevercLinkEntityPage`-Protokoll und füllen `NevercLTOInputModuleInfo` sowie
`NevercLTOSymbolResolution`. Jede Auflösung benennt das Modul, das Symbol, das
zugehörige `NevercLinkSymbolHandle` und ihre Flags:

| Flag | Bedeutung |
|---|---|
| `PREVAILING` | Dieses Modul besitzt die Definition. |
| `VISIBLE_TO_REGULAR_OBJECT` | Ein Nicht-Bitcode-Objekt kann sie sehen. |
| `EXPORTED` | In der dynamischen Symboltabelle vorhanden. |
| `FINAL_DEFINITION` | Keine spätere Definition kann sie ersetzen. |
| `CAN_INLINE` | Inlining über die Grenze hinweg ist erlaubt. |
| `CAN_INTERNALIZE` | Internalisierung ist erlaubt. |
| `LINKER_REDEFINED` | Der Linker hat sie überschrieben. |
| `REFERENCED_BY_REGULAR_OBJECT` | Ein reguläres Objekt referenziert sie. |

`NevercLTOOptions` wählt `NEVERC_LTO_FULL` oder `NEVERC_LTO_THIN`, die
Optimierungsstufen, `ThreadBudget`, `ThinBackendPartitions`, CPU und Features
sowie einen Cache-Scope aus `DISABLED`, `TASK`, `LOCAL_SHARED` oder
`REMOTE_SHARED`. Options-Flags sind `EMIT_OPTIMIZED_BITCODE`, `EMIT_INDEX`,
`SAVE_TEMPS`, `WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO` und `DETERMINISTIC`.

### Ein LTO-Provider

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

`BuildCacheKey` schreibt in eine vom Aufrufer bereitgestellte
`NevercMutableByteView` und meldet die benötigte Größe, sodass der Host den
Puffer dimensionieren und erneut versuchen kann. Es muss eine reine Funktion der
Anfrage sein — die Ableitung aus `RequestDigest` und `ResolutionDigest` ist die
sichere Konstruktion. `CACHEABLE` mit einem Schlüssel zu deklarieren, der einen
Teil der Anfrage ignoriert, erzeugt veraltete Objekte, die einen sauberen
Neubau überleben.

`Codegen` füllt einen `NevercLTOProductCandidate`: ein Array von
`NevercLTOObjectProduct` (jedes benennt sein Quellmodul, den ObjectGraph und das
Artefakt), optional `OptimizedBitcode` und `ThinIndex` sowie den tatsächlich
verwendeten `CacheKey`.

## Regeln

- Handles sind task-gebunden und gehören dem Host. Speichern Sie keines über den
  Callback hinaus, verwenden Sie keines in einem anderen Task und erfinden Sie
  nie einen Wert.
- `NevercLinkEntityPage.Data` gehört Ihnen. Der Host schreibt höchstens
  `ElementCapacity × ElementStride` Bytes und behält keine Referenz darauf.
- Jedes `BeginMutation` erreicht genau ein `CommitMutation` oder
  `AbandonMutation`, auch auf dem Fehlerpfad.
- Mutieren Sie so früh in der Zustandsmaschine, wie die Änderung es zulässt;
  eine späte Mutation invalidiert stillschweigend jede nachgelagerte Phase.
- Mutieren Sie nicht aus einem Observer heraus. Observer erhalten eine
  Nur-Lese-Brücke, und der Versuch wird mit
  `NEVERC_STATUS_POLICY_VIOLATION` abgewiesen.
- Schreiben Sie Image-Bytes nur über `NevercBinaryImageInfo.Binary` und dessen
  Builder. Ein Überlauf bricht das Staging ab, statt die Ausgabe zu vergrößern.
- Beanspruchen Sie `DETERMINISTIC` nur, wenn derselbe Request-Digest immer
  bytegleiche Ausgabe erzeugt, und `CACHEABLE` nur, wenn Ihr Cache-Schlüssel
  jede Eingabe abdeckt, die diese Ausgabe ändern kann.
- `image_verify`, `side_outputs_verify` und `commit` sind versiegelt. Beobachten
  Sie sie; versuchen Sie nicht, sie abzufangen oder zu überspringen.

Die normativen Deklarationen stehen in `PluginLink.h` und `PluginLTO.h`, die
Policies der zwanzig Phasen in `Schema/PhaseSchema.json` und die Tests, die
jede einzelne festnageln, in `coverage.json`.
