**言語**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン Link / LTO API

リンクは**1 つのグラフ上の状態機械**としてモデル化されています。
[`PluginLink.h`] はそのグラフ — 入力、セクション、アトム、シンボル、エッジ、
COMDAT、インポート、エクスポート、アンワインドレコード、合成物、レイアウト制約
— に加えて、ファイルの一覧からコミット済みバイナリイメージまで進める 20 個の
フェーズを公開します。[`PluginLTO.h`] はその中間、ビットコードがオブジェクトに
なる 2 つのフェーズを扱います。

プラグインはすべての手順を観察し、ほとんどをインターセプトし、単一の手順を置き
換え、リンク全体を置き換え、あるいはオブジェクトをマージできます。lld のデータ
構造を目にすることは決してありません。このグラフは、ELF・COFF・Mach-O のバック
エンドがいずれも写像する正規化された射影です。

## インターフェース

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* PluginLink.h を含む */
```

| インターフェース | テーブル | 用途 |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | リンクグラフの読み取りと変更（52 スロット） |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | リンカ、オブジェクトマージ、イメージ検証プロバイダの登録 |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | `NevercArtifactHandle` の背後のグラフやイメージへの到達 |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | LTO 要求・モジュール・シンボル解決の読み取り |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | LTO コード生成プロバイダの登録 |

5 つとも major 1 では `NEVERC_INTERFACE_STABLE` なので、新しいホストは追加しか
できません。それぞれ対応する `NEVERC_LINK_API_MAJOR` /
`NEVERC_LTO_API_MAJOR` と組み合わせ、呼び出す最後のスロットに対して `TableSize`
を確認してください。

## 状態機械

`NevercLinkGraphInfo.State` は 14 個の値のいずれかであり、20 フェーズのうち 13
はそれを 1 段階進めるためだけに存在します:

| フェーズ | 結果の `NEVERC_LINK_STATE_…` | ホスト検証器 |
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

この 13 個はいずれも
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF` であり、
プロバイダが遷移そのものを提供でき、有効な `NevercLinkProofHandle` を持つ
プラグインはそれをスキップできます。

残る 7 つは構造的なものです:

| フェーズ | ポリシー | 役割 |
|---|---|---|
| `neverc.link.full` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE | リンク全体を置換し、`INITIAL` から一気にバイナリイメージへ |
| `neverc.link.object_merge` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE | ObjectGraph の `-r` 再配置可能マージ |
| `neverc.link.post_emit` | OBSERVABLE、INTERCEPTABLE | イメージのバイトに触れる最後の機会 |
| `neverc.link.image_verify` | OBSERVABLE、**SEALED** | ホストのイメージ検証器 |
| `neverc.link.side_outputs_verify` | OBSERVABLE、**SEALED** | マップファイル、dSYM、付随成果物 |
| `neverc.link.commit` | OBSERVABLE、**SEALED** | 出力バンドルのアトミックな公開 |
| `neverc.link.after_commit` | OBSERVABLE | コミット後の通知 |

3 つの封印ゲートは観察できますが、インターセプト・置換・スキップは一切できま
せん。`NEVERC_BUILTIN_LINK_PHASE_COUNT` は 20 です。

## フェーズからグラフへ到達する

`NevercLinkPhaseAPI` はフレームのアーティファクトを使えるハンドルに変換します:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link、.Graph、.Proof、.State、.Generation */
```

`GraphInfo.Link` はこのタスクに束縛された `NevercLinkAPI` なので、オブザーバは
別途 `QueryInterface` を行う必要がありません。プロバイダは `PublishGraph` で
結果を公開します。`GetImage` はイメージアーティファクトに対して同じことを行い、
イメージ、出力バンドル、`NevercBinaryImageState`（`CANDIDATE`、`VERIFIED`、
`COMMITTED`、`ABORTED`、`FAILED_PARTIAL`）を持つ
`NevercLinkPhaseImageInfo` を返します。

## グラフを読む

`NevercLinkGraphInfo` は要約です — ターゲット、フォーマット、状態、世代、15 個の
エンティティ数、そして 32 バイトの `SemanticDigest`。エンティティ自体は種類ごと
に 1 つのページング呼び出しで返り、いずれも呼び出し側所有のページを共有します:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* あなたが用意し所有する配列   */
  uint64_t ElementCapacity;  /* 何件入るか                   */
  uint64_t ElementStride;    /* あなたの要素の sizeof        */
  uint64_t OutCount;         /* ホストが書いた件数           */
  uint64_t NextCursor;       /* 続けるには渡し戻す           */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

ホストは `ElementCapacity` 件を超えず、1 件あたり `ElementStride` バイトを書き、
`Data` を保持しません。したがってスタック上の配列で十分です:

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

15 個のグラフページャがこの形をとります — `GetInputPage`、`GetArchivePage`、
`GetArchiveMemberPage`、`GetSharedLibraryPage`、`GetBitcodeModulePage`、
`GetSectionPage`、`GetAtomPage`、`GetSymbolPage`、`GetEdgePage`、
`GetComdatPage`、`GetImportPage`、`GetExportPage`、`GetUnwindPage`、
`GetSyntheticPage`、`GetConstraintPage`。さらに `GetBinarySegmentPage` と
`GetBinarySectionPage` の 2 つが、生成済みイメージをページングします。それぞれ
単一ハンドル向けの `Get…Info` が対になっています。

すべてのエンティティ情報は `NevercLinkOrigin` を持ちます:

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

これがリンクを監査可能にしている要素です。出力中のどのアトムについても、入力
ファイル、それが引き出されたアーカイブメンバ、それを作ったフェーズ、そして最後
に触れたプラグインを名指しできます。

### エンティティ一覧

| 種類 | Info 構造体 | 主なフィールド |
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

アトムのフラグは `LIVE`、`ROOT`、`SYNTHETIC`、`FOLDED`、
`ADDRESS_SIGNIFICANT`、`TLS`、`UNWIND` です。シンボルの binding は `LOCAL`、
`GLOBAL`、`WEAK`、`COMMON`、definition は `UNDEFINED`、`DEFINED`、`ABSOLUTE`、
`COMMON`、`SHARED`。エッジの種類は `RELOCATION`、`ASSOCIATION`、`KEEP_ALIVE`、
`UNWIND`、`FORMAT_EXTENSION`。COMDAT の選択は `ANY`、`EXACT_MATCH`、
`SAME_SIZE`、`LARGEST`、`NEWEST`、`NO_DUPLICATES` を含みます。

## グラフを変更する

変更はトランザクショナルで、常に 1 つのグラフにスコープされます:

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

コミットは作業コピーへステージし、検証し、そのうえで初めて公開して
`Generation` を進めます。`AbandonMutation` はすべてを破棄します。たとえばグラフ
が `GC_COMPLETE` にあるときにコミットすると liveness 検証器が再実行されるので、
生存アトムを孤立させるような変更は書き込まれず拒否されます。

### 変更は下流の状態を無効化する

ここが意外に思われる点です。すべてのステージング呼び出しは分類され、その分類が
**無効になる最も早い状態**を決めます。ホストはそこから先のすべてのフェーズを再
実行しなければなりません:

| 呼び出し | 無効化される最も早い状態 |
|---|---|
| `RebindSymbol`、`RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`、`ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`、`ReplaceSynthetic`、`EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`、`ReplaceConstraint`、`EraseConstraint` | `LAYOUT_COMPLETE` |

複数に触れる変更は最小のものを採ります。したがってレイアウト後にシンボルを再
束縛すると、レイアウト・再配置・イメージの結果が捨てられます。`gc` の間なら安く、
`post_emit` の間なら高くつきます。変更内容が許すかぎり、状態機械の早い段階で変更
してください。

`SetSymbolResolution` はシンボル全体ではなく小さな更新レコードを取ります。これに
より、解決の変更がうっかり名前や値を書き換えてしまうことを防ぎます:

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

## 証明によるフェーズのスキップ

`SKIPPABLE_WITH_PROOF` フェーズは、実行の代わりに `NevercLinkProofHandle` を受け
取ります。証明はスキップが依存するすべてを固定します:

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

`GraphGeneration` と `SemanticDigest` の両方が記録されるため、証明の発行から使用
までの間にコミットされた変更があれば証明は陳腐化し、ホストはそのフェーズを実際に
実行します。

## バイナリイメージ

`emit_image` の後、成果物は `NevercBinaryImageHandle` です:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State、.OutputKind、.EntryAddress、.ImageBase、.Size、
   .SegmentCount、.SectionCount、.ImportCount、.ExportCount、
   .DynamicRelocationCount、.ContentDigest                    */
```

出力種別は `RELOCATABLE`、`EXECUTABLE`、`SHARED_LIBRARY`、`BUNDLE`。セグメント
フラグは `READ`、`WRITE`、`EXECUTE` です。

`Image.Binary` と `Image.Builder` は [`PluginObject.h`] の有界トランザクショナル
ライタです — `Reserve`、`Write`、`WriteAt`、`Tell`、`ReadAt`、`Insert`、
`Append`、`Resize`。バイトをパッチする `post_emit` インターセプタは必ずこれを
経由しなければなりません。予約境界を越える書き込みはファイルを拡張せず、ステージ
ングを中止します。

## プロバイダ

登録は `Register` の間のみ。あとから登録することはできません。

### リンカを置き換える

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
Provider.VerifyImage  = my_verify;      /* 省略可 */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

コールバックは要求と生の入力集合を受け取り、候補を埋めます:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target、->OutputKind、->OutputURI、->Options、->RequestDigest
     Inputs->Inputs は NevercRawLinkInput[]、Inputs->OrderDigest が順序を固定 */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` はリンカが実際に分岐に使うフラグを運びます — `PIE`、
`STATIC`、`GC_SECTIONS`、`ICF`、`EXPORT_DYNAMIC`、`ALLOW_UNDEFINED`、
`WHOLE_ARCHIVE`、`DETERMINISTIC` — さらに `EntrySymbol`、`InstallName`、
`Soname`、`ImageBase`、`PageSize`、`ThreadBudget`、探索パス、ライブラリ。入力
ごとのフラグは `WHOLE_ARCHIVE`、`AS_NEEDED`、`START_GROUP`、`END_GROUP`、
`LAZY` です。

成功するとホストが候補を採用します。失敗した場合、作成したものはプロバイダの所有
のままです。封印された検証とコミットのゲートはいずれの場合も実行されます。

### オブジェクトのマージとイメージの検証

`RegisterObjectMergeProvider` は `-r` を扱います。要求は入力の
`NevercObjectMergeInput[]` と、あらかじめ開かれた出力グラフおよびミューテーション
を運ぶので、プロバイダはファイルを組み立てるのではなくホスト所有のトランザク
ションに書き込みます。

`RegisterBinaryImageVerifier` は、ホスト自身のイメージ検証器と並んで走る読み取り
専用のチェックを追加します。置き換えることはできません。

## LTO

`lto_resolve` がシンボル解決を生み、`lto_generate` がビットコードをオブジェクトに
変えます。`NevercLTOAPI` はその両方を読みます。

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest、.LinkGraph、.Target、.OutputFormat、.Options、
   .Modules、.Resolutions、.ResolutionDigest、.RequestDigest */
```

`GetModulePage` と `GetResolutionPage` は同じ `NevercLinkEntityPage` プロトコルを
使い、`NevercLTOInputModuleInfo` と `NevercLTOSymbolResolution` を埋めます。各
解決はモジュール、シンボル、対応する `NevercLinkSymbolHandle`、そしてフラグを
示します:

| フラグ | 意味 |
|---|---|
| `PREVAILING` | このモジュールが定義を所有する。 |
| `VISIBLE_TO_REGULAR_OBJECT` | ビットコードでないオブジェクトから見える。 |
| `EXPORTED` | 動的シンボルテーブルに存在する。 |
| `FINAL_DEFINITION` | 後続のどの定義も置き換えられない。 |
| `CAN_INLINE` | 境界を越えたインライン化が許される。 |
| `CAN_INTERNALIZE` | 内部化が許される。 |
| `LINKER_REDEFINED` | リンカが上書きした。 |
| `REFERENCED_BY_REGULAR_OBJECT` | 通常のオブジェクトが参照している。 |

`NevercLTOOptions` は `NEVERC_LTO_FULL` か `NEVERC_LTO_THIN`、各最適化レベル、
`ThreadBudget`、`ThinBackendPartitions`、CPU と機能、そして `DISABLED`、`TASK`、
`LOCAL_SHARED`、`REMOTE_SHARED` のキャッシュスコープを選びます。オプションフラグ
は `EMIT_OPTIMIZED_BITCODE`、`EMIT_INDEX`、`SAVE_TEMPS`、
`WHOLE_PROGRAM_VISIBILITY`、`UNIFIED_LTO`、`DETERMINISTIC` です。

### LTO プロバイダ

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

`BuildCacheKey` は呼び出し側が用意した `NevercMutableByteView` に書き込み、必要
だったサイズを報告します。ホストはそれに合わせてバッファを確保し再試行できます。
これは要求の純粋関数でなければならず、`RequestDigest` と `ResolutionDigest` から
導出するのが安全な作りです。要求の一部を無視するキーで `CACHEABLE` を宣言すると、
クリーンな再ビルドでも生き残る陳腐なオブジェクトが生まれます。

`Codegen` は `NevercLTOProductCandidate` を埋めます: `NevercLTOObjectProduct` の
配列（各要素が元のモジュール、ObjectGraph、アーティファクトを示す）、任意で
`OptimizedBitcode` と `ThinIndex`、そして実際に使った `CacheKey` です。

## ルール

- ハンドルはタスクスコープでホスト所有です。コールバックを越えて保持せず、別の
  タスクで使わず、値を捏造しないでください。
- `NevercLinkEntityPage.Data` はあなたのものです。ホストは最大
  `ElementCapacity × ElementStride` バイトを書き、参照を保持しません。
- すべての `BeginMutation` は、エラー経路も含めてちょうど 1 回の
  `CommitMutation` か `AbandonMutation` に到達します。
- 変更内容が許すかぎり状態機械の早い段階で変更してください。遅い変更は下流の
  すべてのフェーズを黙って無効化します。
- オブザーバから変更してはいけません。オブザーバには読み取り専用のブリッジが
  渡され、試みは `NEVERC_STATUS_POLICY_VIOLATION` で拒否されます。
- イメージのバイトは `NevercBinaryImageInfo.Binary` とそのビルダを通してのみ書き
  ます。あふれた場合、出力を拡張せずステージングを中止します。
- 同じ要求ダイジェストが常にバイト単位で同一の出力を生むときだけ
  `DETERMINISTIC` を主張し、キャッシュキーがその出力を変えうるすべての入力を
  カバーするときだけ `CACHEABLE` を主張してください。
- `image_verify`、`side_outputs_verify`、`commit` は封印されています。観察は
  できますが、インターセプトやスキップを試みないでください。

規範的な宣言は [`PluginLink.h`] と [`PluginLTO.h`]、20 フェーズのポリシーは
[`Schema/PhaseSchema.json`]、それぞれを固定するテストは [`coverage.json`] を参照して
ください。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
