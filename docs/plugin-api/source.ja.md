**言語**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン ソース／I/O API

[`PluginSource.h`] は 2 枚のテーブルを公開します。`NevercIOAPI` はファイルシステム
そのもので、仮想ファイルプロバイダ、読み取り、ディレクトリ走査、出力シンク、
依存記録を担当します。`NevercSourceLocationAPI` はコンパイラ内部の位置をファイル、
行、綴り文字列へと引き戻します。この 2 つがあれば、プラグインはメモリ上にしか
存在しないヘッダを提供したり、マクロ展開をその綴り位置まで解決したり、ビルドの
永続性計算に参加するサイドバンド出力を書き出したりできます。

## インターフェイス

```c
#include "neverc/Plugin/PluginSource.h"
```

| インターフェイス | テーブル | バージョンマクロ |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` と `_MINOR` は source-location の組の別名です。

## 3 つのソースフェーズ

| フェーズ | ポリシー | 意味 |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE、INTERCEPTABLE | ドライバ入力をソース入力に変える |
| `neverc.source.open` | さらに REPLACEABLE | 入力に対するソースユニットを生成する |
| `neverc.source.after_open` | OBSERVABLE | ユニットが利用可能になったという通知 |

`neverc.source.open` は置換可能なので、プロバイダは自分で合成したバイト列を持つ
ユニットを返せます。これがディスクに触れずに生成コードを注入する、サポートされた
やり方です。

## 仮想ファイルシステムプロバイダ

VFS プロバイダはパスのプレフィックスを引き受け、コンパイラがファイルについて問う
4 つの質問に答えます。

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

各コールバックは結果を埋めます。その `Disposition` が、プロバイダがその要求を
処理したかどうかを表します:

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

`NEVERC_VFS_RESULT_NOT_HANDLED` を返すと次のプロバイダへ、最終的には実ファイル
システムへ落ちます。ファイル種別は `NEVERC_VFS_FILE_UNKNOWN`、`REGULAR`、
`DIRECTORY`、`SYMLINK`、`OTHER` です。

登録は `Register` の中で行います:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

1 セッションだけ存在すればよい単一のインメモリファイルなら、プロバイダは省略でき
ます:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`] が完全に動作するプロバイダです。

## ファイルの読み取り

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

`CopyBuffer` は自分が所有するバイト列をホストバッファに変え、`Canonicalize` は
パスを解決し、`GetWorkingDirectory` / `SetWorkingDirectory` はタスクのカレント
ディレクトリを扱います。ディレクトリの走査には `OpenDirectory`、`ReadDirectory`
（終端で `OutHasEntry` を `NEVERC_FALSE` にします）、`CloseDirectory` を使います。

I/O エラーコードは `NevercStatus.Detail` で報告されます:
`NEVERC_IO_ERROR_NOT_FOUND`、`PERMISSION_DENIED`、`NOT_DIRECTORY`、
`IS_DIRECTORY`、`INVALID_PATH`、`IO`。

## 出力の書き出し

出力はトランザクショナルです。シンクを開き、書き込み、finish してシール ——
サイズと 32 バイトのダイジェスト —— を得ます。ビルドシステムはこれを検証できます。

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

| 関数 | 用途 |
|---|---|
| `BeginMemoryOutput` | メモリに裏打ちされ、論理名を持つシンク |
| `BeginFileOutput` | 最終パスへアトミックに着地するシンク |
| `BeginStreamOutput` | `NEVERC_OUTPUT_STREAM_STDOUT` または `_STDERR` 上のシンク |
| `OutputWrite`、`OutputWriteAt` | 追記、またはオフセット指定の書き込み |
| `OutputTell`、`OutputTruncate` | 位置とサイズの制御 |
| `OutputMetadataSet` | 出力にキー／値の組を付ける |
| `OutputFinish` | 出力をシールし `NevercOutputSeal` を生成する |
| `OutputAbort` | 書いたものをすべて破棄する |
| `OutputGetSummary` | 状態、フラグ、サイズ、ダイジェストをいつでも確認する |

`NevercOutputSummary.State` は `NEVERC_OUTPUT_OPEN`、`FINISHED`、`COMMITTED`、
`ABORTED`、`FAILED_PARTIAL` の間を遷移し、`Flags` は `PUBLISHED`、`DURABLE`、
`MAY_BE_PARTIAL`、`RECOVERY_REQUIRED`、`DURABILITY_UNCONFIRMED` を記録します。
これらのフラグはドライバが `NevercStatus.Flags` で見せるのと同じ情報なので、書き
込み途中のクラッシュときれいな失敗とを区別できます。

`SizeBudget` が 0 なら上限なしです。0 以外の予算を与えると、超過はディスクを埋め
尽くす代わりに `NEVERC_STATUS_RESOURCE_EXHAUSTED` で失敗します。

## 依存関係の記録

ビルドシステムが追跡すべきものをプラグインが読んだなら、そう申告してください。
さもないと、その入力が変わってもインクリメンタルビルドは再構築しません。

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

種別は `NEVERC_INPUT_DEPENDENCY_SOURCE`、`INCLUDE`、`MODULE`、`RESOURCE`、
`TOOL`、`PLUGIN` です。

## ソース位置

`NevercSourceLocation` は不透明です。位置テーブルがそれを印字したり比較したり
できるものに変えます。

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind は NEVERC_SOURCE_LOCATION_FILE または _MACRO。
   続いて Info.FileOffset、Info.Line、Info.Column。 */
```

位置の各ビューを行き来する変換は 4 つあり、いずれも
`NevercTransformSourceLocationFn` のシグネチャを共有します:

| 関数 | 返すもの |
|---|---|
| `GetSpellingLocation` | トークンの文字が実際に書かれている場所 |
| `GetExpansionLocation` | マクロ展開がソース中に現れる場所 |
| `GetFileLocation` | 最も近いファイル位置 |
| `GetIncludeLocation` | そのファイルを引き込んだ `#include` |
| `GetTokenEnd` | トークン最後の文字の 1 つ先 |

`GetPresumedLocation` は `#line` 指令を適用し、ファイル名、行、桁、include 位置を
返します。`GetLocationFile` と `GetFileInfo` を組み合わせると、正規パス、サイズ、
更新時刻、一意 ID、そしてそのファイルがユーザ、システム、extern-C システムの
いずれであるかが得られます:

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

範囲は `GetRangeInfo` で読み取り（`Begin`、`End`、そしてその範囲が
`NEVERC_SOURCE_RANGE_CHARACTER` か `_TOKEN` かを報告します）、バイト列そのものは
`GetSourceText` または `GetCharacterData` で読みます。

一度に多数の位置が必要なとき —— たとえば関数全体を診断で走査する場合 —— は、
位置ごとに呼ぶのではなくバッチ形式を使ってください:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## ソースユニット

入力とそのバイト列をフェーズのレベルで見たものです:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path、.Kind（FILE または BUFFER）、.Language、.System、.Preprocessed */
```

`neverc.source.open` のプロバイダは、メモリに裏打ちされたユニットで応答します:

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

キャッシュのキーになるのは `CanonicalIdentity` なので、内容が変われば必ず変えな
ければなりません。`GetSourceUnit` はユニットを読み戻し、加えて `MemoryBacked` を
報告します。

## 規則

- `ReadFile`、`CopyBuffer`、`PathToBuffer` から得たバッファはホストの所有物です。
  一つ残らず `ReleaseBuffer` で解放してください。
- すべての `OpenFileForRead` に `CloseFile` を、すべての `OpenDirectory` に
  `CloseDirectory` を、すべての出力シンクに `OutputFinish` か `OutputAbort` を
  対応させてください。
- `NevercFileInfo`、`NevercVFSStatus`、位置結果の中のビューは、そのコールバックの
  間だけ借用されたものです。
- VFS プロバイダのコールバックはタスクスレッド上で動き、コンパイラを再び呼び出し
  てはいけません。すでに手元にあるデータだけで答えてください。
- `Deterministic` と `Cacheable` は正直に宣言してください。時計や環境を読みながら
  決定性を主張するプロバイダは、汚染されたビルドキャッシュを生みます。
- `AddMemoryFile` はセッションスコープです。内容がタスクに依存するなら、プロバイダ
  こそが正しい道具です。

規範的な宣言は [`PluginSource.h`] を、3 つの source フェーズとそのポリシーは
[`Schema/PhaseSchema.json`] を、完全なプロバイダの例は
[`pluginsdk/examples/VirtualHeaderPlugin.c`] を参照してください。

<!-- reference links -->
[`pluginsdk/examples/VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
