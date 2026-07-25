**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC プラグイン ABI

NeverC のプラグインは、関数をちょうど 1 つだけエクスポートし、128 ビットのインタ
ーフェース ID でバージョン付きのケーパビリティテーブルをネゴシエートし、名前付き
コンパイラフェーズの凍結されたグラフに自分自身を接続する共有モジュールです。イン
ターフェース全体が純粋な C11 です。プラグインが LLVM ヘッダーをインクルードする
ことも、コンパイラをリンクすることも、C++ の型を境界越しに渡すこともありません。

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

`PluginCore.h` で宣言されるこのシグネチャが、リンケージ契約のすべてです。それ以外
のこと——IR を読む、オブジェクトグラフを書き換える、最適化パイプラインを差し替える
——はすべて、ID を指定してホストに要求するテーブル経由で到達します。

## ガイド

| ガイド | 扱う範囲 |
|---|---|
| [ドライバー API](driver.ja.md) | コマンドライン、ツールチェーン選択、アクショングラフ、ジョブグラフ |
| [ソースと I/O API](source.ja.md) | VFS プロバイダー、ソース位置、バッファー、出力シンク、依存関係 |
| [プリプロセッサー API](prep.ja.md) | トークン、マクロ、pragma、include、機能クエリ、39 種類のイベント |
| [AST と意味解析 API](ast-sema.ja.md) | パーサー拡張、AST 変更、名前探索、型、定数 |
| [IR API](ir.ja.md) | LLVM IR の読み取り、トランザクショナルな構築、解析、パス、プロバイダー |
| [MIR API](mir.ja.md) | マシン関数、レジスター、スタックフレーム、MIR パスと解析 |
| [ターゲット、MC、アセンブリ、オブジェクト](target-mc-object.ja.md) | ターゲット登録、呼び出し規約、MC エンコード、オブジェクトグラフ |
| [リンクと LTO API](link-lto.ja.md) | リンクグラフ、シンボル解決、GC/ICF、リンカーと LTO プロバイダー |
| [DynCode API](dyncode.ja.md) | フラットな位置独立イメージ、インポートの低位化、文字セットエンコード |
| [カスタム呼び出し規約](custom-callconv/README.ja.md) | データ駆動の呼び出し規約プラグイン |
| [フェーズカバレッジの根拠](coverage.json) | すべての安定フェーズに対するテストの対応付け |

## 実行モデル

ホストは 3 層の入れ子スコープでプラグインを駆動します。各スコープは、プラグイン自
身が確保して所有する不透明な状態ポインターをプラグインに渡します。したがって、正
しく書かれたプラグインにグローバルな可変状態は必要ありません。

| スコープ | コールバック | 意味 |
|---|---|---|
| Process | `ProcessBegin`、`Register`、`Destroy` | コンパイラプロセス 1 つ。ここでインターフェースを問い合わせ、ケーパビリティを登録します。 |
| Session | `SessionBegin`、`SessionEnd` | ドライバー呼び出し 1 回。 |
| Task | `TaskBegin`、`TaskEnd` | `NevercTaskKind` で識別される作業単位 1 つ。 |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

実質的に必須なのは `PluginID` と `Register` だけで、どのコールバックスロットも
`NULL` のままで構いません。タスク種別は `NEVERC_TASK_INVOCATION`、
`TRANSLATION_UNIT`、`LTO`、`LINK`、`CODEGEN`、`OBJECT`、`DYNCODE` です。

ホストはまず `ProcessBegin` を呼び、続いて `Register` をちょうど 1 回呼びます。
オプション、オブザーバー、インターセプター、プロバイダーを追加できるのは登録時だ
けで、その後フェーズグラフは凍結されます。

状態は事前に捕捉するのではなく、コールバックの中で取得します:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## フェーズ

フェーズとは、入力アーティファクトから出力アーティファクトへの、名前付きでバージ
ョン付きの遷移です。NeverC は **130 個の組み込みフェーズ**を提供し、さらにプラグ
イン定義フェーズ用に 8 つの拡張 ID ファミリーを予約しています:

| ドメイン | フェーズ数 | ドメイン | フェーズ数 |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

この 130 個はすべて ABI メジャー 1 において安定性ティア `stable` です。各フェーズ
はポリシーを宣言し、プラグインはそのポリシーが許す方法でのみ接続できます:

| ポリシーフラグ | フェーズ数 | プラグインができること |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | 読み取り専用の通知を受けるオブザーバーを登録する。 |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | フェーズをラップし、チェーンの残りを呼ぶかどうかを決める。 |
| `NEVERC_PHASE_REPLACEABLE` | 86 | 出力自体を供給するプロバイダーを登録する。 |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | 証明ハンドルを提供したうえで遷移をスキップする。 |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | 何もできない。検証器とコミットはホストの専有物。 |

14 個の封印されたゲートは `ir.final_verify`、`mir.final_verify`、
`codegen.product_verify`、`assembly.final_verify`、`assembly.commit`、
`object.final_verify`、`object.commit`、`link.image_verify`、
`link.side_outputs_verify`、`link.commit`、`dyncode.ir.final_verify`、
`dyncode.mir.final_verify`、`dyncode.verify`、`dyncode.commit` です。観測はでき
ますが、インターセプト・置換・スキップは決してできません。

オブザーバーは、フェーズが宣言した時点で配送されます:
`NEVERC_OBSERVER_BEFORE`、`NEVERC_OBSERVER_AFTER`、
`NEVERC_OBSERVER_AFTER_COMMIT`。インターセプターは
`NevercPhaseContinuation` を受け取り、コールバックスレッド上で `InvokeNext` を
**高々 1 回**呼んだうえで、`NevercPhaseResult.Action` に
`NEVERC_PHASE_CONTINUE`、`NEVERC_PHASE_REPLACE`、`NEVERC_PHASE_SKIP` のいずれか
を報告しなければなりません。

どのフェーズコールバックも同じフレームを受け取ります:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json` が、フェーズ ID・ポリシー・安定性ティア・検証器ゲートの
規範的な出典です。生成される `Schema/PluginPhaseSchema.inc` は、それらをコンパイ
ル時定数として公開します。フェーズ `neverc.ir.pass.pipeline_start` の場合:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT` と、ドメインごとの
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` 定数を使えば、プラグインはビルド時に前提と
したグラフをアサートできます。

## 完全な最小プラグイン

以下は `pluginsdk/templates/minimal/Plugin.c` そのままです。ロードされ、ABI をネ
ゴシエートし、何も登録せず、きれいにアンロードされます。このディレクトリをコピー
して、ここから育ててください。

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Register options, observers, interceptors, or providers here. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` は呼び出し側が所有するバッファーです。入口ではその
`Header.StructSize` が書き込み可能な容量を表します。プラグインはその範囲までしか
書き込まず、実際に生成したサイズを報告します。ディスクリプター自身の `Header` を
先に書き、それからコピーを切り詰めれば、この規則の両側を同時に満たせます。

## インターフェースのネゴシエーション

ケーパビリティテーブルはシンボルではなく 128 ビットのインターフェース ID で取得し
ます。ビルド時に前提としたメジャーバージョンと、動作可能な最小のマイナーバージョ
ンを指定してください:

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

呼び出す最後の関数のオフセットと `TableSize` を突き合わせること——これがこの ABI
を拡張可能にしている規則です。新しいホストはフィールドを末尾に追加し、古いプラグ
インは検証済みの前半部分より先を決して読まないので動き続けます。
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` マクロは、受け取った構造体に同
じ検査を適用します。同じシグネチャの `QueryInterface` は `NevercCoreAPI` にもある
ので、エントリー時ではなく後からネゴシエートすることもできます。

公開インターフェースと、そのテーブル、ID マクロ:

| インターフェースマクロの組 | テーブル | ヘッダー |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`、`..._SOURCE_LOCATION_*` | `NevercIOAPI`、`NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`、`..._PARSER_*` | `NevercASTAPI`、`NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`、`..._IR_BUILDER_*`、`..._IR_ANALYSIS_*`、`..._IR_PASS_*`、`..._IR_GEN_*`、`..._IR_OPTIMIZATION_*` | 6 つの IR テーブル | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`、`..._TARGET_ABI_*`、`..._CALLING_CONVENTION_*` | `NevercTargetAPI`、`NevercTargetABIAPI`、`NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`、`..._MIR_ANALYSIS_*`、`..._MIR_PASS_*`、`..._MIR_PROVIDER_*` | 4 つの MIR テーブル | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`、`..._MC_EMISSION_*`、`..._MC_PROVIDER_*`、`..._ASSEMBLY_PROVIDER_*` | 4 つの MC テーブル | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`、`..._OBJECT_FORMAT_*`、`..._OBJECT_PHASE_*` | 3 つのオブジェクトテーブル | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`、`..._LINK_REGISTRAR_*`、`..._LINK_PHASE_*` | 3 つのリンクテーブル | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`、`..._LTO_REGISTRAR_*` | `NevercLTOAPI`、`NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`、`..._DYNCODE_REGISTRAR_*`、`..._DYNCODE_PHASE_*` | 3 つの dyncode テーブル | `PluginDynCode.h` |

各ヘッダーは、`QueryInterface` に渡すべき対応する
`NEVERC_<DOMAIN>_API_MAJOR` と `_MINOR` も定義しています。

インターフェースは `NEVERC_INTERFACE_STABLE`（新しいホストは追加のみ可能）か、
`NEVERC_INTERFACE_LOCKSTEP`（完全一致が必要なターゲット固有スキーマ）のいずれかで
す。LOCKSTEP の値を利用する前にスキーマダイジェストを比較してください。

## 登録

`Register` は `NevercRegistrarAPI` と不透明な `RegistrarContext` を受け取ります:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

ドメイン別の登録関数——`NevercIRPassAPI.RegisterPass`、
`NevercTargetAPI.RegisterTarget`、`NevercObjectFormatAPI.RegisterFormat` など
——は、いずれも同じ `RegistrarContext` を第 2 引数に取ります。ホストはこれによっ
て、登録をあなたのプラグインに帰属させます。

プロバイダーはさらに、ビルドキャッシュが依拠する決定性の契約を宣言します:

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

## ビルド

集約ヘッダーを include するか、使うドメインだけを include します:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

NeverC 自身で共有モジュールをビルドする:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

インストール済み SDK に対して CMake でビルドする:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

pkg-config を使う:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

ホストに応じて `.so`、`.dylib`、`.dll` を使い分けてください。この SDK は LLVM も
NeverC ランタイムもリンクしません。`NevercPluginSDK::headers` はヘッダーオンリー
です。

## ロードと設定

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| オプション | 形式 | 目的 |
|---|---|---|
| `-fplugin=<path>` | 繰り返し可 | フルツールチェーンのプラグイン共有モジュールをロードする。 |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 繰り返し可 | 登録済みプラグインオプションに名前空間付きの値を渡す。 |
| `-fplugin-provider=<phase>:<plugin-id>` | 繰り返し可 | 置換可能フェーズをどのプラグインが提供するかを選ぶ。 |
| `-fplugin-pass=<dsopath>` | 繰り返し可 | C-ABI のアウトオブツリーなパスプラグインをロードする。 |
| `-fplugin-pass-arg=<key>=<value>` | 繰り返し可 | C-ABI パスプラグインに引数を渡す。 |

`<plugin-id>:` 修飾子を省略できるのは、有効なプラグインがちょうど 1 つのときだけ
です。プラグインが `RegisterOption` で登録したオプションは、宣言した綴りで直接
——flag 形式、joined 形式、separate 形式、複数引数形式で——受け付けられます。対応す
る `-fplugin=` のないプラグイン引数やプロバイダー選択は、黙って無視されるのではな
くハードエラーになります。

登録済みのオプションは、いつでも core テーブル経由で読み戻せます:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## ABI ルール

- ケーパビリティテーブルは `QueryInterface` で取得し、メジャーの一致を要求し、フ
  ィールドに触れる前に `StructSize` を確認する。
- すべての公開構造体の `Header` と予約領域を初期化する。構造体をゼロ埋めしてから
  `StructSize`、`Major`、`Minor`、`Flags` を設定する。
- ハンドルと借用ビューはスコープ付きの不透明値として扱う。タスクスコープのハンド
  ルをコールバックの外まで保持しない、別のセッションやタスクで使わない、ハンドル
  値を自作しない。
- すべてのコールバックから `NevercStatus` を返す。C++ 例外やホスト所有のポインタ
  ーを C 境界越しに漏らさない。
- 真実であるうちで最も狭い `NevercConcurrencyModel`（`SESSION_SERIAL`、
  `THREAD_SAFE`、`PROCESS_SERIAL`）と `NevercReentrancyModel`（`NONE`、
  `ALLOWED`）を宣言する。
- IR、MIR、AST、グラフ、アーティファクトの変更は、トランザクショナルなホスト API
  を通じて行う。変更を開始し、変更をステージし、コミットまたはアボートする。コミ
  ットは検証と公開をアトミックに行い、失敗したコミットは以前の状態をそのまま残す。
- ホストにメモリーを計上させたい場合は `NevercCoreAPI.Allocate` / `Reallocate` /
  `Deallocate` で確保する。
- 可変状態はホストが提供する process/session/task 状態に置く。グローバルな可変状
  態は `utils/plugin-api/check-global-state.py` が検査する。

公開構造体はすべて `NEVERC_ABI_PACK_BEGIN`（8 バイトパッキング）の下に配置され、
固定幅の型のみを使います。新しい関数は、独立にバージョン管理されるケーパビリティ
テーブルの末尾に追加されます。最初の ABI メジャー（`NEVERC_PLUGIN_ABI_MAJOR` =
1）の範囲内では、テーブルの安定した前半部分は変わりません。

## ステータスと診断

`NevercStatus` は `Code`、`Flags`、`Detail` ワードを持ちます。コードの全体像:

| コード | 意味 |
|---|---|
| `NEVERC_STATUS_OK` | 成功。 |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 必須のポインターまたは値が欠けているか不正。 |
| `NEVERC_STATUS_ABI_MISMATCH` | ネゴシエートされたテーブルが小さすぎるか、メジャーが異なる。 |
| `NEVERC_STATUS_MISSING_INTERFACE` | ホストが要求されたインターフェースを公開していない。 |
| `NEVERC_STATUS_VERSION_MISMATCH` | 要求されたメジャー/マイナーを満たせない。 |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | ディスクリプターが構造検証に失敗した。 |
| `NEVERC_STATUS_DUPLICATE_ID` | その ID はすでに登録済み。 |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | 宣言された依存関係が存在しない。 |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | 登録順序を満たせない。 |
| `NEVERC_STATUS_BUSY` | リソースが他所で保持されている。 |
| `NEVERC_STATUS_CANCELLED` | 協調的キャンセルが要求された。 |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | 予算または上限に達した。 |
| `NEVERC_STATUS_STALE_HANDLE` | ハンドルが指す対象より長く生き残った。 |
| `NEVERC_STATUS_WRONG_SESSION` | ハンドルが別のセッションで使われた。 |
| `NEVERC_STATUS_WRONG_SCOPE` | ハンドルがスコープ外で使われた。 |
| `NEVERC_STATUS_WRONG_TYPE` | ハンドルが別種のエンティティを指していた。 |
| `NEVERC_STATUS_INVALID_STATE` | 現在の状態でその操作は不正。 |
| `NEVERC_STATUS_POLICY_VIOLATION` | フェーズポリシーがその操作を禁じている。 |
| `NEVERC_STATUS_VERIFICATION_FAILED` | 封印されたホスト検証器が成果物を拒否した。 |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | ホストはここでその機能を提供できない。 |
| `NEVERC_STATUS_PLUGIN_FAILURE` | プラグインが一般的な失敗を報告した。 |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | プラグインのコールバックから例外が漏れた。 |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | 出力が部分的にしか書かれなかった。 |
| `NEVERC_STATUS_REENTRANCY_DENIED` | 再入呼び出しが拒否された。 |
| `NEVERC_STATUS_NOT_FOUND` | 指定されたエンティティが存在しない。 |

フラグビットは出力に何が起きたかを表します。これはビルドシステムがリトライの安全
性を判断するために必要な情報です: `NEVERC_STATUS_FLAG_RECOVERABLE`、
`_OUTPUT_ALREADY_COMMITTED`、`_OUTPUT_MAY_BE_PARTIAL`、
`_OUTPUT_RECOVERY_REQUIRED`、`_DURABILITY_UNCONFIRMED`。

問題の報告には `NevercCoreAPI.EmitDiagnostic` と、重大度（`NOTE`、`REMARK`、
`WARNING`、`ERROR`、`FATAL`）・コード・プラグイン ID・フェーズ ID・メッセージ・
注記・ソース位置・範囲・fix-it を持つ `NevercDiagnosticDescriptor` を使います。
高コストな処理の前には `CheckCancelled` を呼んでください。

## サンプル

すべてビルドする:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

各サンプルは 2 回コンパイルされます——1 回は設定されたホスト C コンパイラーで、も
う 1 回はビルドしたばかりの NeverC で——したがって ABI が両側から証明されます。モ
ジュールは `build-neverc/neverc/pluginsdk/examples/host/` に生成されます。

| サンプル | CMake ターゲット | 示す内容 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | オプション登録、フェーズ観測、ジョブのインターセプト |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | メモリー内ヘッダーを提供する VFS プロバイダー |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | パーサーのインターセプトとアトミックな AST 変更 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 値カーソルで関数リストを歩くモジュールレベル IR パス |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 安定した IR 関数パス |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | pre-emit フックに置く安定した MIR パス |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 読み取り専用の MC 出力イベント |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | トランザクショナルな ObjectGraph 書き換え |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | データ駆動の呼び出し規約 |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | dyncode パイプラインの観測 |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | dyncode の文字セットエンコードのインターセプト |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | CRT 依存ゼロのプラグイン |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI 呼び出しスループットのマイクロベンチマーク |

1 つロードしてみる:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 規範的な出典

| ファイル | 保証する内容 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | フェーズ ID、ポリシー、安定性、検証器ゲート |
| `pluginsdk/manifest/plugin.json` | ABI バージョン、インターフェース ID/バージョン/安定性、スキーマダイジェスト、対応ターゲット |
| `pluginsdk/abi/plugin.json` | ホスト ABI キーごとの、全公開構造体の実測サイズ・アライメント・フィールドオフセット |
| `docs/plugin-api/coverage.json` | 各安定フェーズを、肯定・否定・置換・オブザーバー・封印ゲートのテストに対応付ける |

したがって SDK はホストに対して機械的に検証でき、プラグインのビルドは、それがロー
ドされる先の ABI キーに対して自身の構造体レイアウトをアサートできます。
