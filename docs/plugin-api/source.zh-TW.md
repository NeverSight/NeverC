**語言**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛原始碼與 I/O API

[`PluginSource.h`] 發布兩張表。`NevercIOAPI` 就是檔案系統：虛擬檔案 Provider、
讀取、目錄走訪、輸出 sink 與相依記錄。`NevercSourceLocationAPI` 把編譯器內部的
位置映射回檔案、行號與拼寫文字。有了這兩者，外掛可以提供一個只存在於記憶體中
的標頭檔、把巨集展開解析到它的拼寫位置，或是寫出一份參與建置持久性核算的旁路
輸出。

## 介面

```c
#include "neverc/Plugin/PluginSource.h"
```

| 介面 | 表 | 版本巨集 |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` 與 `_MINOR` 是 source-location 那一對的別名。

## 三個原始碼階段

| 階段 | 政策 | 意義 |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | 把驅動程式輸入變成原始碼輸入 |
| `neverc.source.open` | 另加 REPLACEABLE | 為某個輸入產出原始碼單元 |
| `neverc.source.after_open` | OBSERVABLE | 通知：某個單元已就緒 |

由於 `neverc.source.open` 可取代，Provider 可以交回一個位元組由它自己合成的單
元，這正是注入產生程式碼而不碰磁碟的受支援做法。

## 虛擬檔案系統 Provider

一個 VFS Provider 認領某個路徑前綴，並回答編譯器關於檔案的四個問題。

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

每個回呼都要填一個結果，其中的 `Disposition` 說明這個 Provider 是否處理了該請
求：

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

回傳 `NEVERC_VFS_RESULT_NOT_HANDLED` 會落到下一個 Provider，最終落到真實檔案系
統。檔案類型有 `NEVERC_VFS_FILE_UNKNOWN`、`REGULAR`、`DIRECTORY`、`SYMLINK`、
`OTHER`。

在 `Register` 期間註冊：

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

如果只要一個僅在單一 session 內存在的記憶體檔案，可以完全跳過 Provider：

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`] 是一個完整可運作的 Provider。

## 讀取檔案

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

`CopyBuffer` 把你自己持有的位元組轉成主機緩衝區，`Canonicalize` 解析路徑，
`GetWorkingDirectory` / `SetWorkingDirectory` 處理任務的目前目錄。目錄以
`OpenDirectory`、`ReadDirectory`（走到結尾時會把 `OutHasEntry` 設為
`NEVERC_FALSE`）與 `CloseDirectory` 走訪。

I/O 錯誤碼回報在 `NevercStatus.Detail` 中：`NEVERC_IO_ERROR_NOT_FOUND`、
`PERMISSION_DENIED`、`NOT_DIRECTORY`、`IS_DIRECTORY`、`INVALID_PATH`、`IO`。

## 寫出輸出

輸出是交易式的。你開啟一個 sink、寫入，然後 finish 以取得一份封緘（seal）──
一個大小與一組 32 位元組摘要，建置系統可以據此驗證。

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

| 函式 | 用途 |
|---|---|
| `BeginMemoryOutput` | 由記憶體支撐、以邏輯名稱命名的 sink |
| `BeginFileOutput` | 最終原子性落到某個路徑的 sink |
| `BeginStreamOutput` | 位於 `NEVERC_OUTPUT_STREAM_STDOUT` 或 `_STDERR` 的 sink |
| `OutputWrite`、`OutputWriteAt` | 附加寫入，或在指定位移寫入 |
| `OutputTell`、`OutputTruncate` | 位置與大小控制 |
| `OutputMetadataSet` | 為輸出附加一組鍵/值 |
| `OutputFinish` | 封緘輸出並產生 `NevercOutputSeal` |
| `OutputAbort` | 丟棄所有已寫入的內容 |
| `OutputGetSummary` | 隨時檢視狀態、旗標、大小與摘要 |

`NevercOutputSummary.State` 會在 `NEVERC_OUTPUT_OPEN`、`FINISHED`、
`COMMITTED`、`ABORTED`、`FAILED_PARTIAL` 之間移動，而 `Flags` 記錄
`PUBLISHED`、`DURABLE`、`MAY_BE_PARTIAL`、`RECOVERY_REQUIRED` 與
`DURABILITY_UNCONFIRMED`。這些旗標與驅動程式在 `NevercStatus.Flags` 中呈現的是
同一份資訊，因此寫入中途的當機可以和乾淨的失敗區分開來。

`SizeBudget` 為零表示不設上限；非零的預算會讓超量以
`NEVERC_STATUS_RESOURCE_EXHAUSTED` 失敗，而不是把磁碟塞滿。

## 記錄相依

如果外掛讀了某個建置系統應該追蹤的東西，就要說出來。否則增量建置在那個輸入改變
時不會重建。

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

種類有 `NEVERC_INPUT_DEPENDENCY_SOURCE`、`INCLUDE`、`MODULE`、`RESOURCE`、
`TOOL`、`PLUGIN`。

## 原始碼位置

`NevercSourceLocation` 是不透明的。位置表把它變成你可以列印或比較的東西。

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind 是 NEVERC_SOURCE_LOCATION_FILE 或 _MACRO；
   接著是 Info.FileOffset、Info.Line、Info.Column。 */
```

有四個轉換在一個位置的各種視角之間移動，它們共用
`NevercTransformSourceLocationFn` 簽章：

| 函式 | 回傳 |
|---|---|
| `GetSpellingLocation` | token 的字元實際寫在哪裡 |
| `GetExpansionLocation` | 巨集展開在原始碼中出現的位置 |
| `GetFileLocation` | 最接近的檔案位置 |
| `GetIncludeLocation` | 把該檔案拉進來的那個 `#include` |
| `GetTokenEnd` | token 最後一個字元的下一個位置 |

`GetPresumedLocation` 會套用 `#line` 指示，產生檔名、行、欄與 include 位置。
`GetLocationFile` 搭配 `GetFileInfo` 可取得正規路徑、大小、修改時間、唯一 ID，
以及該檔案屬於使用者、系統，還是 extern-C 系統：

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

範圍用 `GetRangeInfo` 讀取（它會回報 `Begin`、`End`，以及該範圍是
`NEVERC_SOURCE_RANGE_CHARACTER` 還是 `_TOKEN`），位元組本身則用
`GetSourceText` 或 `GetCharacterData`。

當你一次需要很多個位置時──例如對整個函式做一輪診斷──請用批次形式，而不是每
個位置呼叫一次：

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## 原始碼單元

輸入及其位元組在階段層級的視角：

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path、.Kind（FILE 或 BUFFER）、.Language、.System、.Preprocessed */
```

`neverc.source.open` 的 Provider 以一個由記憶體支撐的單元作答：

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

快取是以 `CanonicalIdentity` 作為鍵，所以只要內容改變，它就必須跟著改變。
`GetSourceUnit` 會把單元讀回來，並額外回報 `MemoryBacked`。

## 規則

- 來自 `ReadFile`、`CopyBuffer` 與 `PathToBuffer` 的緩衝區屬於主機；每一個都要
  用 `ReleaseBuffer` 釋放。
- 每個 `OpenFileForRead` 都要有 `CloseFile`；每個 `OpenDirectory` 都要有
  `CloseDirectory`；每個輸出 sink 都要有 `OutputFinish` 或 `OutputAbort`。
- `NevercFileInfo`、`NevercVFSStatus` 與位置結果裡的 view 只在該回呼期間借用。
- VFS Provider 的回呼在任務執行緒上執行，且不得回頭呼叫編譯器；只能用你手上已
  有的資料作答。
- 誠實宣告 `Deterministic` 與 `Cacheable`。一個會讀時鐘或環境變數卻宣稱具決定
  性的 Provider，會產生一份被汙染的建置快取。
- `AddMemoryFile` 的範圍是 session；當內容取決於任務時，Provider 才是正確的工
  具。

規範性宣告請見 [`PluginSource.h`]，完整的 Provider 範例請見
[`pluginsdk/examples/VirtualHeaderPlugin.c`]。

<!-- reference links -->
[`pluginsdk/examples/VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
