**言語**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# プロトタイププラグイン API からの移行

未リリースのプロトタイププラグイン API — その `nevercGetPluginInfo` エントリポイント、
単一の `NevercHostAPI` vtable、`Register*Pass` 呼び出し、`NEVERC_INTERPOSE_*` フック、
そして `-fplugin-pass=` ローダー — は、最初の公開リリースより前にすべて削除されました。
最初の公開 ABI は [README.md](README.md) に記載されているフェーズベースの
ディスクリプタ ABI です。プラグインは `neverc_plugin_entry` をエクスポートし、
独立してバージョン管理される機能テーブルをネゴシエートします。

互換シムは存在せず、`v1`/`v2` の分岐もありません。プラグインの*ソース*を公開ヘッダに
対して再コンパイルしてください。本ページでは、すべてのプロトタイプ構成要素を、
第一バージョンでの置き換え先、意味的な変更点、または明示的な非継承として対応付けます。

## プロトタイプのバイナリは拒否される

プロトタイプの共有オブジェクトをロードすると、安定した診断メッセージとともに失敗します:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

どちらのエントリポイントもエクスポートしていないライブラリは
`plugin has no 'neverc_plugin_entry' entry` で失敗します。ソースを移植するまで、
何もロードされません。

## エントリポイント

| プロトタイプ | 最初の公開 ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

エントリポイントはもはや構造体を値で*返しません*。呼び出し側が用意した
`NevercPluginDescriptor` を、`OutPlugin->Header.StructSize` を尊重しつつ埋め、
`NevercStatus` を返します。サポートを表明する前に、必要な機能テーブルを
`Bootstrap` から照会してください。

## `NevercPluginInfo` のフィールド

| プロトタイプのフィールド | 第一バージョンでの対応 |
|---|---|
| `APIVersion` | `Descriptor.Header`（`StructSize`、`NEVERC_PLUGIN_ABI_MAJOR`、`NEVERC_PLUGIN_ABI_MINOR` を持つ `NevercABITableHeader`） |
| `PluginName` | `Descriptor.DisplayName`（`NevercStringView`）に加え、スコープごとの状態のキーとして使われる安定した逆 DNS 形式の `Descriptor.PluginID` |
| `PluginVersion` | `Descriptor.Version`（`NevercSemanticVersion`） |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)` と、ライフサイクルコールバック `ProcessBegin`、`SessionBegin`/`SessionEnd`、`TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(プロトタイプに相当なし)* | `Descriptor.Concurrency` と `Descriptor.Reentrancy` を偽りなく宣言する必要があります（例: `NEVERC_CONCURRENCY_SESSION_SERIAL`、`NEVERC_REENTRANCY_ALLOWED`） |

## ホストへのアクセス: 単一 vtable → 機能テーブル

プロトタイプは 200 を超えるエントリを持つ単一の `NevercHostAPI` vtable をすべての
コールバックに渡し、新しいフィールドを `NEVERC_API_FN` で保護していました。第一
バージョンでは、これを独立してバージョン管理され、必要に応じて照会する機能テーブルに
置き換えます:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

一致するメジャーバージョンを要求し、フィールドを読む前に `offsetof` で `TableSize` を
検査してください。インターフェースはドメインごとにスコープされています: Core、Driver、
Source、Prep、AST、Sema、IR、MIR、Target、MC、Object、Link、LTO、DynCode。

## 登録: `Register*Pass` + フック → オブザーバ／インターセプタ／プロバイダ

プロトタイプの登録はコールバックをフックに結び付けていました:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

第一バージョンでは `Register` の内部で、128 ビットの `NevercInterfaceID` で識別される
フェーズに対して型付きハンドラを登録します:

| プロトタイプの呼び出し | 第一バージョンのレジストラ呼び出し |
|---|---|
| 読み取り専用パス | `NEVERC_OBSERVER_BEFORE`／`NEVERC_OBSERVER_AFTER` ポイントを指定した `Registrar->RegisterObserver(NevercObserverDescriptor)` |
| フェーズをラップまたは短絡するパス | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`。`Continuation->InvokeNext` は高々 1 回だけ呼び、`OutResult->Action` を設定します |
| 組み込み変換を置き換えるパス | `REPLACEABLE` なフェーズ上での `Registrar->RegisterProvider(...)` |
| `-fplugin-pass-arg=` の読み取り | `Registrar->RegisterOption(NevercOptionDescriptor)` で本物のドライバオプションを宣言 |

プロトタイプの「`PRE_OPT` でのモジュールパス」は、IR フェーズ `neverc.ir.pass.pre_opt`
上のオブザーバ、インターセプタ、またはプロバイダになります。

## フック → フェーズの対応

| プロトタイプのフック | 第一バージョンのフェーズ（名前） |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | LTO フェーズ `neverc.link.lto_resolve` / `neverc.link.lto_generate`（[mir.md](mir.md) を参照） |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `BEFORE` / `AFTER` で観測する `neverc.link.layout` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*`（dyncode） | [dyncode.md](dyncode.md) の型付き dyncode フェーズ |

フェーズ ID、ポリシー、安定性ティア、検証ゲートの規範的な一覧は
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` です。実行可能なカバレッジ
契約は [coverage.json](coverage.json) です。かつて単一ポイントだったフックが、
それぞれ独自のポリシーと証明を持つ複数のフェーズ ID に対応する場合があります。

## パスコールバック、ハンドル、バイト編集

| プロトタイプ | 第一バージョン |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` など | コールバックは `NevercPhaseFrame` を受け取ります。IR/MIR/AST/グラフのオブジェクトは、該当する機能テーブルから取得する型付き・スコープ付き・不透明なハンドルです（[ir.md](ir.md)、[mir.md](mir.md)、[ast-sema.md](ast-sema.md)、[target-mc-object.md](target-mc-object.md) を参照） |
| 汎用の `NevercValueRef` | 型付き IR ハンドルに置き換えられ、削除されました |
| 生きた `Ref` のインプレース変更 | すべての変更はトランザクショナルなホスト API を経由します |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | 削除されました。dyncode のバイト編集は検査付きイメージビルダ（read/write/insert/append/resize）を使います。[dyncode.md](dyncode.md) を参照 |

ハンドルと借用ビューは、以前とまったく同じくコールバックのスコープ内でのみ有効です。
コールバックから戻った後にキャッシュしないでください。

## 削除された利便レイヤ

プロトタイプは汎用ヘルパーを vtable に同梱していました。これらは最初の公開 ABI の
一部では**ありません**:

| プロトタイプ | 第一バージョン |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | 継承されません。`Core->Allocate`／`Core->Deallocate` と独自のコンテナ、または型付きドメイン API を使ってください |
| `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` マクロ | 各ドメインの機能テーブルにある型付きイテレーションに置き換えられました |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | `RegisterOption` でオプションを宣言し、Driver API 経由で読み取ってください |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## ロードとコマンドライン

| プロトタイプ | 第一バージョン |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | `RegisterOption` で宣言したオプションの綴り（例: `--driver-trace` や `--my-opt=value`） |
| 2 つのローダー（`-fplugin` と `-fplugin-pass`） | 単一のローダー。モジュールは 1 つのローダーに渡されます |

## バージョニング

プロトタイプは、単調に増えていく単一の vtable と `NEVERC_API_FN` ガードに依存して
いました。第一バージョンでは各機能テーブルが独自にバージョン管理されます。一致する
メジャーバージョンを要求し、追加されたフィールドを読む前に `StructSize`／`TableSize`
を検査してください。最初の ABI メジャーの範囲内では、新しい関数はテーブルの安定
プレフィックスの後ろに追加されるため、より古いマイナーに対してビルドされたプラグインは
より新しいホストでも動作し続けます。

## 実例

`pluginsdk/examples/DriverTracePlugin.c` は第一バージョンの全体像を示しています:
`neverc_plugin_entry` ディスクリプタ、`ProcessBegin`／`Session`／`Task` のライフ
サイクル、実際の CLI フラグのための `RegisterOption`、`neverc.driver.raw_arguments`
上の `RegisterObserver`、そして `InvokeNext` をちょうど 1 回呼ぶ
`neverc.driver.execute_job` 上の `RegisterInterceptor` です。
`pluginsdk/examples/ExamplePlugin.c` は IR、MIR、object、link の各フェーズを
カバーしています。
