**Languages**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← NeverC Plugin ABI](README.md)

# NeverC Plugin Link and LTO API

Linking is modelled as a **state machine over one graph**. [`PluginLink.h`]
exposes that graph — inputs, sections, atoms, symbols, edges, COMDATs,
imports, exports, unwind records, synthetics, and layout constraints — plus
the twenty phases that advance it from a list of files to a committed binary
image. [`PluginLTO.h`] covers the two phases in the middle where bitcode
becomes objects.

A plugin can observe every step, intercept most of them, replace a single
step, replace the whole link, or merge objects. It never sees an lld data
structure: the graph is a normalized projection that the ELF, COFF, and
Mach-O backends all map onto.

## Interfaces

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* includes PluginLink.h */
```

| Interface | Table | Purpose |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | Read and mutate the link graph (52 slots) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | Register linker, object-merge, and image-verifier providers |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | Reach the graph or image behind a `NevercArtifactHandle` |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | Read the LTO request, modules, and symbol resolutions |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | Register an LTO codegen provider |

All five are `NEVERC_INTERFACE_STABLE` at major 1, so a newer host may only
append. Pair each with its `NEVERC_LINK_API_MAJOR` / `NEVERC_LTO_API_MAJOR`
and check `TableSize` against the last slot you call.

## The state machine

`NevercLinkGraphInfo.State` is one of fourteen values, and thirteen of the
twenty phases exist purely to advance it by one step:

| Phase | Resulting `NEVERC_LINK_STATE_…` | Host verifier |
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

Each of those thirteen is
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`, so a
provider may supply the transition itself, and a plugin holding a valid
`NevercLinkProofHandle` may skip it.

The remaining seven are structural:

| Phase | Policy | Role |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Replace the entire link, `INITIAL` straight to a binary image |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | `-r` relocatable merge of ObjectGraphs |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | Last chance to touch the image bytes |
| `neverc.link.image_verify` | OBSERVABLE, **SEALED** | Host image verifier |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **SEALED** | Map files, dSYM, side artifacts |
| `neverc.link.commit` | OBSERVABLE, **SEALED** | Atomic publication of the output bundle |
| `neverc.link.after_commit` | OBSERVABLE | Post-commit notification |

The three sealed gates can be observed but never intercepted, replaced, or
skipped. `NEVERC_BUILTIN_LINK_PHASE_COUNT` is 20.

## Reaching the graph from a phase

`NevercLinkPhaseAPI` converts the frame's artifact into a usable handle:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` is the `NevercLinkAPI` bound to this task, so an observer
needs no separate `QueryInterface`. A provider publishes its result with
`PublishGraph`, and `GetImage` gives the same treatment to an image artifact,
yielding `NevercLinkPhaseImageInfo` with the image, the output bundle, and a
`NevercBinaryImageState` (`CANDIDATE`, `VERIFIED`, `COMMITTED`, `ABORTED`, or
`FAILED_PARTIAL`).

## Reading the graph

`NevercLinkGraphInfo` is the summary — target, format, state, generation,
seventeen entity counts, and a 32-byte `SemanticDigest`. Entities themselves
come back through one paging call per kind, all sharing a caller-owned page:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* caller-owned array you supply       */
  uint64_t ElementCapacity;  /* how many entries fit                */
  uint64_t ElementStride;    /* sizeof your element                 */
  uint64_t OutCount;         /* how many the host wrote             */
  uint64_t NextCursor;       /* pass back in to continue            */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

The host writes no more than `ElementCapacity` entries of `ElementStride`
bytes and never retains `Data`, so an on-stack array works:

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

Fifteen graph pagers follow that shape — `GetInputPage`, `GetArchivePage`,
`GetArchiveMemberPage`, `GetSharedLibraryPage`, `GetBitcodeModulePage`,
`GetSectionPage`, `GetAtomPage`, `GetSymbolPage`, `GetEdgePage`,
`GetComdatPage`, `GetImportPage`, `GetExportPage`, `GetUnwindPage`,
`GetSyntheticPage`, and `GetConstraintPage` — and two more,
`GetBinarySegmentPage` and `GetBinarySectionPage`, page an emitted image.
Each has a matching `Get…Info` for a single handle.

Every entity info carries a `NevercLinkOrigin`:

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

That is what makes a link auditable: for any atom in the output you can name
the input file, the archive member it was pulled from, the phase that created
it, and the plugin that last touched it.

### The entities

| Kind | Info struct | Notable fields |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Archive / member | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Shared library | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Bitcode module | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Section | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Atom | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Symbol | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Edge | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Import / export | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Unwind | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Synthetic | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Constraint | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

Atom flags are `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`, `ADDRESS_SIGNIFICANT`,
`TLS`, and `UNWIND`. Symbol bindings are `LOCAL`, `GLOBAL`, `WEAK`, and
`COMMON`; definitions are `UNDEFINED`, `DEFINED`, `ABSOLUTE`, `COMMON`, and
`SHARED`. Edge kinds are `RELOCATION`, `ASSOCIATION`, `KEEP_ALIVE`, `UNWIND`,
and `FORMAT_EXTENSION`. COMDAT selection covers `ANY`, `EXACT_MATCH`,
`SAME_SIZE`, `LARGEST`, `NEWEST`, and `NO_DUPLICATES`.

## Mutating the graph

Mutation is transactional and always scoped to one graph:

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

Commit stages onto a working copy, verifies it, and only then publishes and
bumps `Generation`. `AbandonMutation` discards everything. Committing while
the graph sits at `GC_COMPLETE`, for instance, re-runs the liveness verifier,
so a mutation that would strand a live atom is rejected rather than written.

### Mutations invalidate downstream state

This is the part that surprises people. Every staging call is classified, and
the classification determines the **earliest state that becomes invalid**;
the host must re-run every phase from there:

| Call | Earliest invalidated state |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

A mutation that touches several of these takes the minimum. Rebinding a
symbol after layout therefore throws away layout, relocation, and image
results — cheap during `gc`, expensive during `post_emit`. Mutate as early in
the state machine as your change allows.

`SetSymbolResolution` takes a small update record rather than a whole symbol,
which keeps a resolution change from accidentally rewriting a name or value:

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

## Skipping a phase with a proof

A `SKIPPABLE_WITH_PROOF` phase accepts a `NevercLinkProofHandle` instead of
running. The proof pins everything the skip depends on:

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

Because `GraphGeneration` and `SemanticDigest` are both recorded, any
committed mutation between issuing the proof and using it makes the proof
stale, and the host runs the phase for real.

## The binary image

After `emit_image` the product is a `NevercBinaryImageHandle`:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                      */
```

Output kinds are `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY`, and `BUNDLE`.
Segment flags are `READ`, `WRITE`, and `EXECUTE`.

`Image.Binary` and `Image.Builder` are the bounded transactional writer from
[`PluginObject.h`] — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`,
`Append`, `Resize`. A `post_emit` interceptor patching bytes must go through
it; writes past the reserved bound abort staging instead of growing the file.

## Providers

Register during `Register`, never later.

### Replacing the linker

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

The callback receives the request and the raw input set, and fills a
candidate:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs is a NevercRawLinkInput[], Inputs->OrderDigest pins order */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` carries the flags a linker actually branches on — `PIE`,
`STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`, `ALLOW_UNDEFINED`,
`WHOLE_ARCHIVE`, `DETERMINISTIC` — plus `EntrySymbol`, `InstallName`,
`Soname`, `ImageBase`, `PageSize`, `ThreadBudget`, search paths, and
libraries. Per-input flags are `WHOLE_ARCHIVE`, `AS_NEEDED`, `START_GROUP`,
`END_GROUP`, and `LAZY`.

On success the host adopts the candidate. On failure the provider still owns
whatever it created. The sealed verify and commit gates run either way.

### Merging objects and verifying images

`RegisterObjectMergeProvider` handles `-r`: the request carries the input
`NevercObjectMergeInput[]` and a pre-opened output graph and mutation, so the
provider writes into a host-owned transaction rather than building a file.

`RegisterBinaryImageVerifier` adds a read-only check that runs alongside the
host's own image verifier. It cannot replace it.

## LTO

`lto_resolve` produces the symbol resolutions; `lto_generate` turns bitcode
into objects. `NevercLTOAPI` reads both.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` and `GetResolutionPage` use the same
`NevercLinkEntityPage` protocol, filling `NevercLTOInputModuleInfo` and
`NevercLTOSymbolResolution`. Each resolution names the module, the symbol,
the corresponding `NevercLinkSymbolHandle`, and its flags:

| Flag | Meaning |
|---|---|
| `PREVAILING` | This module owns the definition. |
| `VISIBLE_TO_REGULAR_OBJECT` | A non-bitcode object can see it. |
| `EXPORTED` | Present in the dynamic symbol table. |
| `FINAL_DEFINITION` | No later definition can replace it. |
| `CAN_INLINE` | Inlining across the boundary is permitted. |
| `CAN_INTERNALIZE` | Internalization is permitted. |
| `LINKER_REDEFINED` | The linker overrode it. |
| `REFERENCED_BY_REGULAR_OBJECT` | A regular object references it. |

`NevercLTOOptions` selects `NEVERC_LTO_FULL` or `NEVERC_LTO_THIN`, the
optimization levels, `ThreadBudget`, `ThinBackendPartitions`, CPU and
features, and a cache scope of `DISABLED`, `TASK`, `LOCAL_SHARED`, or
`REMOTE_SHARED`. Option flags are `EMIT_OPTIMIZED_BITCODE`, `EMIT_INDEX`,
`SAVE_TEMPS`, `WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO`, and `DETERMINISTIC`.

### An LTO provider

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

`BuildCacheKey` writes into a caller-supplied `NevercMutableByteView` and
reports the size it needed, so the host can size the buffer and retry. It
must be a pure function of the request — deriving it from `RequestDigest` and
`ResolutionDigest` is the safe construction. Declaring `CACHEABLE` with a key
that ignores part of the request produces stale objects that survive a clean
rebuild.

`Codegen` fills a `NevercLTOProductCandidate`: an array of
`NevercLTOObjectProduct` (each naming its source module, ObjectGraph, and
artifact), optionally `OptimizedBitcode` and `ThinIndex`, and the `CacheKey`
it actually used.

## Rules

- Handles are task-scoped and host-owned. Never store one past the callback,
  never use it in another task, and never manufacture a value.
- `NevercLinkEntityPage.Data` is yours. The host writes at most
  `ElementCapacity × ElementStride` bytes and keeps no reference to it.
- Every `BeginMutation` reaches exactly one `CommitMutation` or
  `AbandonMutation`, including on the error path.
- Mutate as early in the state machine as the change allows; a late mutation
  silently invalidates every downstream phase.
- Do not mutate from an observer. Observers get a read-only bridge and the
  attempt is rejected with `NEVERC_STATUS_POLICY_VIOLATION`.
- Write image bytes only through `NevercBinaryImageInfo.Binary` and its
  builder. Overflow aborts staging rather than growing the output.
- Only claim `DETERMINISTIC` if the same request digest always produces
  byte-identical output, and only claim `CACHEABLE` if your cache key covers
  every input that can change that output.
- `image_verify`, `side_outputs_verify`, and `commit` are sealed. Observe
  them; do not try to intercept or skip them.

See [`PluginLink.h`] and [`PluginLTO.h`] for the normative declarations,
[`Schema/PhaseSchema.json`] for the twenty phase policies, and [`coverage.json`]
for the tests that pin each one.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
