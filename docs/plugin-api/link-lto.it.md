**Lingue**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API di link e LTO dei plugin NeverC

Il linking è modellato come una **macchina a stati su un solo grafo**.
`PluginLink.h` espone quel grafo — input, sezioni, atomi, simboli, archi,
COMDAT, import, export, record di unwind, sintetici e vincoli di layout —
insieme alle venti fasi che lo portano da un elenco di file a un'immagine
binaria committata. `PluginLTO.h` copre le due fasi centrali in cui il bitcode
diventa oggetti.

Un plugin può osservare ogni passo, intercettarne la maggior parte, sostituire
un singolo passo, sostituire l'intero link oppure fondere oggetti. Non vede mai
una struttura dati di lld: il grafo è una proiezione normalizzata su cui i
backend ELF, COFF e Mach-O si mappano tutti.

## Interfacce

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* include PluginLink.h */
```

| Interfaccia | Tabella | Scopo |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | Leggere e modificare il grafo di link (52 slot) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | Registrare provider di linker, merge di oggetti e verifica immagine |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | Raggiungere il grafo o l'immagine dietro un `NevercArtifactHandle` |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | Leggere la richiesta LTO, i moduli e le risoluzioni dei simboli |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | Registrare un provider di generazione codice LTO |

Tutte e cinque sono `NEVERC_INTERFACE_STABLE` alla major 1, quindi un host più
recente può solo aggiungere. Abbinate ciascuna al proprio
`NEVERC_LINK_API_MAJOR` / `NEVERC_LTO_API_MAJOR` e verificate `TableSize`
rispetto all'ultimo slot che chiamate.

## La macchina a stati

`NevercLinkGraphInfo.State` è uno di quattordici valori, e tredici delle venti
fasi esistono unicamente per farlo avanzare di un passo:

| Fase | `NEVERC_LINK_STATE_…` risultante | Verificatore host |
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

Ciascuna di queste tredici è
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`, quindi un
provider può fornire la transizione stessa e un plugin che possiede un
`NevercLinkProofHandle` valido può saltarla.

Le restanti sette sono strutturali:

| Fase | Policy | Ruolo |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Sostituire l'intero link, da `INITIAL` direttamente a un'immagine binaria |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Merge rilocabile `-r` di ObjectGraph |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | Ultima occasione per toccare i byte dell'immagine |
| `neverc.link.image_verify` | OBSERVABLE, **SIGILLATA** | Verificatore d'immagine dell'host |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **SIGILLATA** | File di map, dSYM, artefatti collaterali |
| `neverc.link.commit` | OBSERVABLE, **SIGILLATA** | Pubblicazione atomica del bundle di output |
| `neverc.link.after_commit` | OBSERVABLE | Notifica dopo il commit |

I tre cancelli sigillati si possono osservare, ma mai intercettare, sostituire o
saltare. `NEVERC_BUILTIN_LINK_PHASE_COUNT` vale 20.

## Raggiungere il grafo da una fase

`NevercLinkPhaseAPI` converte l'artefatto del frame in un handle utilizzabile:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` è la `NevercLinkAPI` legata a questo task, quindi un observer
non ha bisogno di un `QueryInterface` separato. Un provider pubblica il proprio
risultato con `PublishGraph`, e `GetImage` fa lo stesso per un artefatto
immagine, restituendo un `NevercLinkPhaseImageInfo` con l'immagine, il bundle di
output e un `NevercBinaryImageState` (`CANDIDATE`, `VERIFIED`, `COMMITTED`,
`ABORTED` o `FAILED_PARTIAL`).

## Leggere il grafo

`NevercLinkGraphInfo` è il riepilogo: target, formato, stato, generazione,
diciassette conteggi di entità e un `SemanticDigest` di 32 byte. Le entità
stesse tornano tramite una chiamata di paginazione per specie, tutte
condividendo una pagina di proprietà del chiamante:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* array che fornite e possedete   */
  uint64_t ElementCapacity;  /* quante voci ci stanno           */
  uint64_t ElementStride;    /* sizeof del vostro elemento      */
  uint64_t OutCount;         /* quante ne ha scritte l'host     */
  uint64_t NextCursor;       /* ripassatelo per continuare      */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

L'host non scrive più di `ElementCapacity` voci da `ElementStride` byte e non
trattiene mai `Data`, perciò basta un array sullo stack:

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

Quindici paginatori di grafo seguono questa forma — `GetInputPage`,
`GetArchivePage`, `GetArchiveMemberPage`, `GetSharedLibraryPage`,
`GetBitcodeModulePage`, `GetSectionPage`, `GetAtomPage`, `GetSymbolPage`,
`GetEdgePage`, `GetComdatPage`, `GetImportPage`, `GetExportPage`,
`GetUnwindPage`, `GetSyntheticPage` e `GetConstraintPage` — e altri due,
`GetBinarySegmentPage` e `GetBinarySectionPage`, paginano un'immagine emessa.
Ciascuno ha un corrispondente `Get…Info` per un singolo handle.

Ogni info di entità porta un `NevercLinkOrigin`:

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

È questo che rende verificabile un link: per qualunque atomo dell'output potete
nominare il file di input, il membro d'archivio da cui è stato estratto, la fase
che l'ha creato e il plugin che l'ha toccato per ultimo.

### Le entità

| Specie | Struttura Info | Campi notevoli |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Archivio / membro | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Libreria condivisa | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Modulo bitcode | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Sezione | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Atomo | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Simbolo | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Arco | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Import / export | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Unwind | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Sintetico | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Vincolo | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

I flag di atomo sono `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`,
`ADDRESS_SIGNIFICANT`, `TLS` e `UNWIND`. I binding dei simboli sono `LOCAL`,
`GLOBAL`, `WEAK` e `COMMON`; le definizioni `UNDEFINED`, `DEFINED`, `ABSOLUTE`,
`COMMON` e `SHARED`. Le specie di arco sono `RELOCATION`, `ASSOCIATION`,
`KEEP_ALIVE`, `UNWIND` e `FORMAT_EXTENSION`. La selezione COMDAT copre `ANY`,
`EXACT_MATCH`, `SAME_SIZE`, `LARGEST`, `NEWEST` e `NO_DUPLICATES`.

## Modificare il grafo

La mutazione è transazionale e sempre limitata a un solo grafo:

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

Il commit mette in staging su una copia di lavoro, la verifica e solo allora
pubblica e incrementa `Generation`. `AbandonMutation` scarta tutto. Fare commit
mentre il grafo è a `GC_COMPLETE`, per esempio, riesegue il verificatore di
liveness, così una mutazione che lascerebbe orfano un atomo vivo viene respinta
anziché scritta.

### Le mutazioni invalidano lo stato a valle

Questa è la parte che sorprende. Ogni chiamata di staging viene classificata, e
la classificazione determina lo **stato più precoce che diventa invalido**;
l'host deve rieseguire ogni fase a partire da lì:

| Chiamata | Stato invalidato più precoce |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

Una mutazione che ne tocca più d'una prende il minimo. Ricollegare un simbolo
dopo il layout butta quindi via i risultati di layout, rilocazione e immagine:
economico durante `gc`, costoso durante `post_emit`. Mutate il più presto
possibile nella macchina a stati, per quanto la vostra modifica lo consenta.

`SetSymbolResolution` prende un piccolo record di aggiornamento invece di un
simbolo intero, così un cambio di risoluzione non riscrive per sbaglio un nome o
un valore:

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

## Saltare una fase con una prova

Una fase `SKIPPABLE_WITH_PROOF` accetta un `NevercLinkProofHandle` invece di
eseguirsi. La prova fissa tutto ciò da cui il salto dipende:

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

Poiché vengono registrati sia `GraphGeneration` sia `SemanticDigest`, qualsiasi
mutazione committata tra l'emissione della prova e il suo utilizzo la rende
obsoleta, e l'host esegue la fase per davvero.

## L'immagine binaria

Dopo `emit_image` il prodotto è un `NevercBinaryImageHandle`:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                     */
```

I tipi di output sono `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY` e `BUNDLE`.
I flag di segmento sono `READ`, `WRITE` ed `EXECUTE`.

`Image.Binary` e `Image.Builder` sono lo scrittore transazionale limitato di
`PluginObject.h`: `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`,
`Append`, `Resize`. Un interceptor di `post_emit` che applica patch ai byte deve
passare di lì; le scritture oltre il limite riservato interrompono lo staging
invece di far crescere il file.

## Provider

Registrate durante `Register`, mai dopo.

### Sostituire il linker

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
Provider.VerifyImage  = my_verify;      /* facoltativo */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

La callback riceve la richiesta e l'insieme grezzo degli input, e riempie un
candidato:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs è un NevercRawLinkInput[], Inputs->OrderDigest fissa l'ordine */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` porta i flag su cui un linker davvero ramifica — `PIE`,
`STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`, `ALLOW_UNDEFINED`,
`WHOLE_ARCHIVE`, `DETERMINISTIC` — più `EntrySymbol`, `InstallName`, `Soname`,
`ImageBase`, `PageSize`, `ThreadBudget`, percorsi di ricerca e librerie. I flag
per singolo input sono `WHOLE_ARCHIVE`, `AS_NEEDED`, `START_GROUP`,
`END_GROUP` e `LAZY`.

In caso di successo l'host adotta il candidato. In caso di fallimento il
provider resta proprietario di ciò che ha creato. I cancelli sigillati di
verifica e commit girano comunque.

### Fondere oggetti e verificare immagini

`RegisterObjectMergeProvider` gestisce `-r`: la richiesta porta i
`NevercObjectMergeInput[]` di ingresso e un grafo di output e una mutazione già
aperti, così il provider scrive in una transazione di proprietà dell'host invece
di costruire un file.

`RegisterBinaryImageVerifier` aggiunge un controllo di sola lettura che gira
accanto al verificatore d'immagine dell'host. Non può sostituirlo.

## LTO

`lto_resolve` produce le risoluzioni dei simboli; `lto_generate` trasforma il
bitcode in oggetti. `NevercLTOAPI` legge entrambe.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` e `GetResolutionPage` usano lo stesso protocollo
`NevercLinkEntityPage`, riempiendo `NevercLTOInputModuleInfo` e
`NevercLTOSymbolResolution`. Ogni risoluzione nomina il modulo, il simbolo, il
corrispondente `NevercLinkSymbolHandle` e i suoi flag:

| Flag | Significato |
|---|---|
| `PREVAILING` | Questo modulo possiede la definizione. |
| `VISIBLE_TO_REGULAR_OBJECT` | Un oggetto non bitcode può vederla. |
| `EXPORTED` | Presente nella tabella dei simboli dinamici. |
| `FINAL_DEFINITION` | Nessuna definizione successiva può sostituirla. |
| `CAN_INLINE` | L'inlining attraverso il confine è permesso. |
| `CAN_INTERNALIZE` | L'internalizzazione è permessa. |
| `LINKER_REDEFINED` | Il linker l'ha sovrascritta. |
| `REFERENCED_BY_REGULAR_OBJECT` | Un oggetto normale la referenzia. |

`NevercLTOOptions` seleziona `NEVERC_LTO_FULL` o `NEVERC_LTO_THIN`, i livelli di
ottimizzazione, `ThreadBudget`, `ThinBackendPartitions`, CPU e feature, e un
ambito di cache tra `DISABLED`, `TASK`, `LOCAL_SHARED` o `REMOTE_SHARED`. I flag
di opzione sono `EMIT_OPTIMIZED_BITCODE`, `EMIT_INDEX`, `SAVE_TEMPS`,
`WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO` e `DETERMINISTIC`.

### Un provider LTO

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

`BuildCacheKey` scrive in un `NevercMutableByteView` fornito dal chiamante e
riporta la dimensione di cui aveva bisogno, così l'host può dimensionare il
buffer e riprovare. Deve essere una funzione pura della richiesta: derivarla da
`RequestDigest` e `ResolutionDigest` è la costruzione sicura. Dichiarare
`CACHEABLE` con una chiave che ignora parte della richiesta produce oggetti
obsoleti che sopravvivono a una ricostruzione pulita.

`Codegen` riempie un `NevercLTOProductCandidate`: un array di
`NevercLTOObjectProduct` (ciascuno che nomina il modulo sorgente, l'ObjectGraph
e l'artefatto), facoltativamente `OptimizedBitcode` e `ThinIndex`, e la
`CacheKey` effettivamente usata.

## Regole

- Gli handle hanno scope di task e appartengono all'host. Non conservatene mai
  uno oltre la callback, non usatelo in un altro task e non inventate mai un
  valore.
- `NevercLinkEntityPage.Data` è vostro. L'host scrive al massimo
  `ElementCapacity × ElementStride` byte e non ne trattiene alcun riferimento.
- Ogni `BeginMutation` raggiunge esattamente un `CommitMutation` o
  `AbandonMutation`, anche sul percorso d'errore.
- Mutate il più presto possibile nella macchina a stati; una mutazione tardiva
  invalida in silenzio ogni fase a valle.
- Non mutate da un observer. Gli observer ricevono un ponte in sola lettura e il
  tentativo viene respinto con `NEVERC_STATUS_POLICY_VIOLATION`.
- Scrivete i byte dell'immagine solo tramite `NevercBinaryImageInfo.Binary` e il
  suo builder. Un overflow interrompe lo staging invece di far crescere
  l'output.
- Dichiarate `DETERMINISTIC` solo se lo stesso digest di richiesta produce
  sempre un output identico byte per byte, e `CACHEABLE` solo se la vostra
  chiave di cache copre ogni input che può cambiare quell'output.
- `image_verify`, `side_outputs_verify` e `commit` sono sigillate. Osservatele;
  non provate a intercettarle o a saltarle.

Vedete `PluginLink.h` e `PluginLTO.h` per le dichiarazioni normative,
`Schema/PhaseSchema.json` per le policy delle venti fasi e `coverage.json` per i
test che fissano ciascuna di esse.
