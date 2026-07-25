**語言**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛 Link 與 LTO API

連結被建模為**單一圖之上的狀態機**。[`PluginLink.h`] 公開這張圖——輸入、section、
atom、符號、edge、COMDAT、import、export、unwind 記錄、synthetic 與 layout 約束
——再加上把它從一組檔案推進到已提交二進位映像的二十個階段。[`PluginLTO.h`] 涵蓋中
間那兩個把 bitcode 變成目的檔的階段。

外掛可以觀察每一步、攔截其中大多數、替換單一步驟、替換整個連結，或合併目的檔。
它永遠看不到 lld 的資料結構：這張圖是一個正規化的投影，ELF、COFF 與 Mach-O 後端
都對映到它上面。

## 介面

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* 內含 PluginLink.h */
```

| 介面 | 表 | 用途 |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | 讀取與變更連結圖（52 個槽位） |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | 註冊連結器、目的檔合併與映像驗證器 Provider |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | 取得 `NevercArtifactHandle` 背後的圖或映像 |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | 讀取 LTO 請求、模組與符號解析結果 |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | 註冊 LTO codegen Provider |

五者在 major 1 都是 `NEVERC_INTERFACE_STABLE`，因此較新的宿主只能追加欄位。每個
都要搭配對應的 `NEVERC_LINK_API_MAJOR` / `NEVERC_LTO_API_MAJOR`，並以你會呼叫的
最後一個槽位來檢查 `TableSize`。

## 狀態機

`NevercLinkGraphInfo.State` 是十四個值之一，而二十個階段中有十三個純粹是為了把它
往前推進一步：

| 階段 | 產生的 `NEVERC_LINK_STATE_…` | 宿主驗證器 |
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

這十三個階段都是
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`，所以 Provider
可以自行提供該轉換，而持有有效 `NevercLinkProofHandle` 的外掛可以跳過它。

其餘七個是結構性的：

| 階段 | Policy | 角色 |
|---|---|---|
| `neverc.link.full` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE | 替換整個連結，從 `INITIAL` 直達二進位映像 |
| `neverc.link.object_merge` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE | ObjectGraph 的 `-r` 可重定位合併 |
| `neverc.link.post_emit` | OBSERVABLE、INTERCEPTABLE | 最後一次接觸映像位元組的機會 |
| `neverc.link.image_verify` | OBSERVABLE、**SEALED** | 宿主映像驗證器 |
| `neverc.link.side_outputs_verify` | OBSERVABLE、**SEALED** | map 檔、dSYM、附帶產物 |
| `neverc.link.commit` | OBSERVABLE、**SEALED** | 輸出 bundle 的原子性發布 |
| `neverc.link.after_commit` | OBSERVABLE | 提交後通知 |

這三個 sealed gate 可以被觀察，但永遠不能被攔截、替換或跳過。
`NEVERC_BUILTIN_LINK_PHASE_COUNT` 是 20。

## 從階段取得圖

`NevercLinkPhaseAPI` 把 frame 的產物轉成可用的控制代碼：

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link、.Graph、.Proof、.State、.Generation */
```

`GraphInfo.Link` 就是繫結到這個 task 的 `NevercLinkAPI`，所以觀察者不需要另外做
`QueryInterface`。Provider 以 `PublishGraph` 發布結果；`GetImage` 對映像產物做同
樣的事，回傳含有映像、輸出 bundle 與 `NevercBinaryImageState`（`CANDIDATE`、
`VERIFIED`、`COMMITTED`、`ABORTED` 或 `FAILED_PARTIAL`）的
`NevercLinkPhaseImageInfo`。

## 讀取圖

`NevercLinkGraphInfo` 是總覽——目標、格式、狀態、generation、十七個實體計數，以
及一個 32 位元組的 `SemanticDigest`。實體本身則透過每種各一個的分頁呼叫取得，全
都共用一個由呼叫端擁有的 page：

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* 由你提供、你擁有的陣列       */
  uint64_t ElementCapacity;  /* 能放幾筆                     */
  uint64_t ElementStride;    /* 你的元素的 sizeof            */
  uint64_t OutCount;         /* 宿主實際寫了幾筆             */
  uint64_t NextCursor;       /* 傳回去以繼續                 */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

宿主寫入的筆數不超過 `ElementCapacity`、每筆 `ElementStride` 位元組，而且絕不保
留 `Data`，因此堆疊上的陣列就夠用：

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
    /* Symbols[I].Name、.Binding、.Definition、.IsPrevailing、… */
  }
  Cursor = Page.NextCursor;
} while (Page.HasMore);
```

十五個圖分頁器都是這個形狀——`GetInputPage`、`GetArchivePage`、
`GetArchiveMemberPage`、`GetSharedLibraryPage`、`GetBitcodeModulePage`、
`GetSectionPage`、`GetAtomPage`、`GetSymbolPage`、`GetEdgePage`、
`GetComdatPage`、`GetImportPage`、`GetExportPage`、`GetUnwindPage`、
`GetSyntheticPage` 與 `GetConstraintPage`——另外兩個 `GetBinarySegmentPage` 與
`GetBinarySectionPage` 則對已產生的映像分頁。每一個都有對應的、針對單一控制代碼
的 `Get…Info`。

每個實體資訊都帶著一個 `NevercLinkOrigin`：

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

這正是讓連結可稽核的關鍵：對輸出中的任一 atom，你都能指出它的輸入檔、它是從哪個
封存成員被拉進來的、建立它的階段，以及最後動過它的外掛。

### 各種實體

| 種類 | Info 結構 | 值得注意的欄位 |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind`（OBJECT、ARCHIVE、SHARED_LIBRARY、BITCODE、SCRIPT、BLOB）、`Ordinal`、`ContentDigest`、`ReaderRoute` |
| Archive / member | `NevercLinkArchiveInfo`、`NevercLinkArchiveMemberInfo` | `Thin`、`Materialized`、`MaterializationReason` |
| Shared library | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Bitcode module | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Section | `NevercLinkSectionInfo` | `Kind`、`Flags`、`Alignment`、`Address`、`Size`、`Comdat` |
| Atom | `NevercLinkAtomInfo` | `Flags`、`Content`、`ZeroFillSize`、`FoldLeader` |
| Symbol | `NevercLinkSymbolInfo` | `Binding`、`Visibility`、`Definition`、`IsPrevailing`、`IsRoot` |
| Edge | `NevercLinkEdgeInfo` | `Kind`、`Offset`、`RelocationKind`、`Addend`、`TargetSymbol`、`TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`、`Selected` |
| Import / export | `NevercLinkImportInfo`、`NevercLinkExportInfo` | `Library`、`Symbol` |
| Unwind | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Synthetic | `NevercLinkSyntheticInfo` | `Role`、`Section`、`Atom` |
| Constraint | `NevercLinkConstraintInfo` | `Kind`、`SubjectID`、`Value`、`Required` |

Atom 旗標有 `LIVE`、`ROOT`、`SYNTHETIC`、`FOLDED`、`ADDRESS_SIGNIFICANT`、`TLS`
與 `UNWIND`。符號 binding 有 `LOCAL`、`GLOBAL`、`WEAK` 與 `COMMON`；definition 有
`UNDEFINED`、`DEFINED`、`ABSOLUTE`、`COMMON` 與 `SHARED`。Edge 種類有
`RELOCATION`、`ASSOCIATION`、`KEEP_ALIVE`、`UNWIND` 與 `FORMAT_EXTENSION`。COMDAT
selection 涵蓋 `ANY`、`EXACT_MATCH`、`SAME_SIZE`、`LARGEST`、`NEWEST` 與
`NO_DUPLICATES`。

## 變更圖

變更是交易式的，而且永遠限定在單一張圖上：

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

提交會先暫存到工作副本、驗證，然後才發布並遞增 `Generation`。`AbandonMutation`
會丟棄一切。舉例來說，在圖處於 `GC_COMPLETE` 時提交會重新執行 liveness 驗證器，
因此會讓存活 atom 變成孤兒的變更會被拒絕而非寫入。

### 變更會使下游狀態失效

這是最容易讓人意外的部分。每個暫存呼叫都會被歸類，而該分類決定了**最早失效的狀
態**；宿主必須從那裡開始重跑每一個階段：

| 呼叫 | 最早失效的狀態 |
|---|---|
| `RebindSymbol`、`RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`、`ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`、`ReplaceSynthetic`、`EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`、`ReplaceConstraint`、`EraseConstraint` | `LAYOUT_COMPLETE` |

一次動到多項的變更取其中最早者。因此在 layout 之後重新繫結符號，會丟掉 layout、
relocation 與映像結果——在 `gc` 期間很便宜，在 `post_emit` 期間很昂貴。請在你的
改動所允許的、狀態機中盡可能早的位置進行變更。

`SetSymbolResolution` 接受一筆小的更新記錄而非整個符號，這能避免一次解析變更意外
改寫了名稱或值：

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

## 以 proof 跳過階段

`SKIPPABLE_WITH_PROOF` 階段接受一個 `NevercLinkProofHandle` 來取代實際執行。這個
proof 釘住了跳過所依賴的一切：

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

由於 `GraphGeneration` 與 `SemanticDigest` 都被記錄下來，簽發 proof 到使用它之間
的任何已提交變更都會讓 proof 過期，宿主便會真正執行該階段。

## 二進位映像

`emit_image` 之後的產物是一個 `NevercBinaryImageHandle`：

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State、.OutputKind、.EntryAddress、.ImageBase、.Size、
   .SegmentCount、.SectionCount、.ImportCount、.ExportCount、
   .DynamicRelocationCount、.ContentDigest                    */
```

輸出種類有 `RELOCATABLE`、`EXECUTABLE`、`SHARED_LIBRARY` 與 `BUNDLE`。Segment 旗
標有 `READ`、`WRITE` 與 `EXECUTE`。

`Image.Binary` 與 `Image.Builder` 就是 [`PluginObject.h`] 裡那個有界的交易式寫入器
——`Reserve`、`Write`、`WriteAt`、`Tell`、`ReadAt`、`Insert`、`Append`、
`Resize`。`post_emit` 攔截器要修補位元組就必須經過它；寫超過保留邊界會中止暫存，
而不是把檔案撐大。

## Provider

只在 `Register` 期間註冊，絕不在之後。

### 替換連結器

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
Provider.VerifyImage  = my_verify;      /* 選用 */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

回呼會收到請求與原始輸入集合，並填出一個候選產物：

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target、->OutputKind、->OutputURI、->Options、->RequestDigest
     Inputs->Inputs 是 NevercRawLinkInput[]，Inputs->OrderDigest 釘住順序 */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` 攜帶連結器真正會據以分支的旗標——`PIE`、`STATIC`、
`GC_SECTIONS`、`ICF`、`EXPORT_DYNAMIC`、`ALLOW_UNDEFINED`、`WHOLE_ARCHIVE`、
`DETERMINISTIC`——外加 `EntrySymbol`、`InstallName`、`Soname`、`ImageBase`、
`PageSize`、`ThreadBudget`、搜尋路徑與程式庫。每個輸入的旗標則是 `WHOLE_ARCHIVE`、
`AS_NEEDED`、`START_GROUP`、`END_GROUP` 與 `LAZY`。

成功時宿主會採納該候選。失敗時 Provider 仍擁有它所建立的一切。無論如何，sealed
的 verify 與 commit gate 都會執行。

### 合併目的檔與驗證映像

`RegisterObjectMergeProvider` 處理 `-r`：請求攜帶輸入的
`NevercObjectMergeInput[]` 以及一個已預先開啟的輸出圖與變更，因此 Provider 是寫
進宿主擁有的交易裡，而不是自己去建一個檔案。

`RegisterBinaryImageVerifier` 新增一項唯讀檢查，與宿主自己的映像驗證器並行執行。
它無法取代宿主的驗證器。

## LTO

`lto_resolve` 產生符號解析結果；`lto_generate` 把 bitcode 變成目的檔。
`NevercLTOAPI` 兩者都能讀。

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest、.LinkGraph、.Target、.OutputFormat、.Options、
   .Modules、.Resolutions、.ResolutionDigest、.RequestDigest */
```

`GetModulePage` 與 `GetResolutionPage` 使用相同的 `NevercLinkEntityPage` 協定，
分別填入 `NevercLTOInputModuleInfo` 與 `NevercLTOSymbolResolution`。每筆解析都指
明模組、符號、對應的 `NevercLinkSymbolHandle`，以及它的旗標：

| 旗標 | 意義 |
|---|---|
| `PREVAILING` | 此模組擁有該定義。 |
| `VISIBLE_TO_REGULAR_OBJECT` | 非 bitcode 的目的檔看得到它。 |
| `EXPORTED` | 出現在動態符號表中。 |
| `FINAL_DEFINITION` | 之後的定義都不能取代它。 |
| `CAN_INLINE` | 允許跨邊界內聯。 |
| `CAN_INTERNALIZE` | 允許內部化。 |
| `LINKER_REDEFINED` | 連結器覆寫了它。 |
| `REFERENCED_BY_REGULAR_OBJECT` | 有一般目的檔引用它。 |

`NevercLTOOptions` 可選 `NEVERC_LTO_FULL` 或 `NEVERC_LTO_THIN`、各最佳化層級、
`ThreadBudget`、`ThinBackendPartitions`、CPU 與特性，以及 `DISABLED`、`TASK`、
`LOCAL_SHARED` 或 `REMOTE_SHARED` 的快取範圍。選項旗標有
`EMIT_OPTIMIZED_BITCODE`、`EMIT_INDEX`、`SAVE_TEMPS`、
`WHOLE_PROGRAM_VISIBILITY`、`UNIFIED_LTO` 與 `DETERMINISTIC`。

### 一個 LTO Provider

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

`BuildCacheKey` 寫進呼叫端提供的 `NevercMutableByteView`，並回報它所需的大小，讓
宿主可以據此配置緩衝區後重試。它必須是請求的純函式——由 `RequestDigest` 與
`ResolutionDigest` 推導出來是安全的作法。若宣告了 `CACHEABLE`，但 key 忽略了請求
的一部分，就會產生連乾淨重建都殺不掉的過期目的檔。

`Codegen` 填出一個 `NevercLTOProductCandidate`：一個
`NevercLTOObjectProduct` 陣列（每筆指明其來源模組、ObjectGraph 與產物），可選的
`OptimizedBitcode` 與 `ThinIndex`，以及它實際使用的 `CacheKey`。

## 規則

- 控制代碼的範圍是 task，且由宿主擁有。絕不在回呼之後保存、絕不在另一個 task 中
  使用，也絕不自行捏造值。
- `NevercLinkEntityPage.Data` 是你的。宿主最多寫入
  `ElementCapacity × ElementStride` 位元組，且不會保留任何對它的參考。
- 每個 `BeginMutation` 都要恰好走到一個 `CommitMutation` 或 `AbandonMutation`，
  錯誤路徑上也一樣。
- 在你的改動所允許的、狀態機中盡可能早的位置變更；太晚的變更會悄悄地讓每個下游
  階段失效。
- 不要從觀察者變更。觀察者拿到的是唯讀橋接，嘗試變更會以
  `NEVERC_STATUS_POLICY_VIOLATION` 被拒絕。
- 只能透過 `NevercBinaryImageInfo.Binary` 與其 builder 寫入映像位元組。溢位會中止
  暫存，而不是讓輸出變大。
- 只有在相同 request digest 永遠產生位元組完全相同的輸出時，才宣告
  `DETERMINISTIC`；只有在你的 cache key 涵蓋所有可能改變該輸出的輸入時，才宣告
  `CACHEABLE`。
- `image_verify`、`side_outputs_verify` 與 `commit` 是 sealed 的。觀察它們即可，
  不要嘗試攔截或跳過。

規範宣告請見 [`PluginLink.h`] 與 [`PluginLTO.h`]，二十個階段的 policy 見
[`Schema/PhaseSchema.json`]，逐一釘住它們的測試見 [`coverage.json`]。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
