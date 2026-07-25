**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC プラグイン ABI

NeverC の最初の公開プラグイン ABI は、純粋な C によるフェーズベースのインター
フェースです。プラグインは共有モジュールであり、関数を 1 つだけエクスポートし、
バージョン管理された機能テーブルをネゴシエートして、明示的な Process / Session /
Task スコープの中で実行されます。LLVM ヘッダーを include することはなく、
コンパイラをリンクすることもなく、境界を越えて C++ 型をやり取りすることも
ありません。

未リリースのプロトタイプ API とその `nevercGetPluginInfo` エントリポイントは
**削除されました**。プロトタイプのバイナリは移行診断とともに拒否されます。
公開ヘッダーに対してソースを再コンパイルしてください。旧 API から新 API への
完全な対応表は
[プロトタイプ API からの移行](migration-from-prototype.ja.md)を参照してください。

## ここから始める

- [Source と I/O API](source.ja.md)
- [プリプロセッサ API](prep.ja.md)
- [AST と意味解析 API](ast-sema.ja.md)
- [IR API](ir.ja.md)
- [MIR API](mir.ja.md)
- [Target / MC / アセンブリ / オブジェクト API](target-mc-object.ja.md)
- [DynCode API](dyncode.ja.md)
- [カスタム呼び出し規約](custom-callconv/README.ja.md)
- [プロトタイプ API からの移行](migration-from-prototype.ja.md)
- [フェーズカバレッジの証拠](coverage.json)

## 実行モデル

ホストは 3 段の入れ子スコープでプラグインを駆動します。各スコープはプラグインに
不透明な状態ポインタを渡し、その確保と所有はプラグイン自身が行います。したがって
正しく書かれたプラグインにグローバルな可変状態は必要ありません。

| スコープ | コールバック | 意味 |
|---|---|---|
| Process | `ProcessBegin`、`Register`、`Destroy` | コンパイラプロセス 1 つ。ここでインターフェースを問い合わせ、機能を登録します。 |
| Session | `SessionBegin`、`SessionEnd` | ドライバ呼び出し 1 回。 |
| Task | `TaskBegin`、`TaskEnd` | 作業単位 1 つ。`NevercTaskKind` で識別されます。 |

Task の種別は `INVOCATION`、`TRANSLATION_UNIT`、`LTO`、`LINK`、`CODEGEN`、
`OBJECT`、`DYNCODE` です。

ホストはまず `ProcessBegin` を呼び、続いて `Register` をちょうど 1 回呼びます。
オプション、オブザーバ、インターセプタ、Provider を追加できるのは登録時だけで、
その後フェーズグラフは凍結されます。

## フェーズ

フェーズとは、入力アーティファクトから出力アーティファクトへの、名前とバージョン
を持つ遷移です。NeverC は driver、source、プリプロセッサ、構文、意味解析、IR、
codegen、MIR、MC、アセンブリ、オブジェクト、リンク、dyncode の各ドメインにわたり
**130 個の組み込みフェーズ**を備え、さらにプラグイン定義フェーズ用に 8 つの
拡張 ID ファミリを予約しています。

各フェーズはポリシーを宣言しており、プラグインはそのポリシーが許す方法でのみ
接続できます。

| ポリシーフラグ | プラグインができること |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | オブザーバを登録し、読み取り専用で通知を受ける。 |
| `NEVERC_PHASE_INTERCEPTABLE` | フェーズをラップし、チェーンの残りを呼ぶかどうかを決める。 |
| `NEVERC_PHASE_REPLACEABLE` | Provider を登録し、出力自体を供給する。 |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | proof ハンドルを提供したうえで遷移をスキップする。 |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 何もできない。検証とコミットはホスト専有であり、置換・傍受・スキップは不可能。 |

オブザーバはフェーズが宣言した時点で配送されます:
`NEVERC_OBSERVER_BEFORE`、`NEVERC_OBSERVER_AFTER`、
`NEVERC_OBSERVER_AFTER_COMMIT`。

インターセプタは `NevercPhaseContinuation` を受け取ります。`InvokeNext` は
コールバックスレッド上で**最大 1 回**だけ呼び、その後
`NevercPhaseResult.Action` に `NEVERC_PHASE_CONTINUE`、
`NEVERC_PHASE_REPLACE`、`NEVERC_PHASE_SKIP` のいずれかを報告しなければ
なりません。

フェーズ ID、ポリシー、安定性ティア、検証ゲートの規範的な情報源は
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` です。生成される
`PluginPhaseSchema.inc` が、それらを
`NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW` のようなコンパイル時定数として
公開します。

## 完全な最小プラグイン

以下は `pluginsdk/templates/minimal/Plugin.c` です。ロードされ、ABI を
ネゴシエートし、何も登録せず、きれいにアンロードされます。このディレクトリを
コピーして育ててください。

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
  /* ここでオプション、オブザーバ、インターセプタ、Provider を登録します。 */
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

`OutPlugin` は呼び出し側が所有するバッファです。入口では
`Header.StructSize` が書き込み可能な容量を表します。プラグインはその容量を
超えない範囲で書き込み、実際に生成したサイズを報告します。

## インターフェースのネゴシエーション

機能テーブルはシンボルではなく 128 ビットのインターフェース ID で取得します。
コンパイル時に使った major と、動作可能な最小の minor を要求してください。

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

呼び出す最後の関数のオフセットに対して `TableSize` を検査すること——これが
この ABI を拡張可能にしているルールです。新しいホストはフィールドを末尾に
追加し、古いプラグインは検証済みのプレフィックスより先を決して読まないため、
そのまま動作し続けます。受け取った構造体に対して同じ検査を行うマクロが
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` です。

公開インターフェースとそのヘッダー:

| インターフェース | テーブル | ヘッダー |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`、`..._SOURCE_LOCATION` | `NevercIOAPI`、`NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`、`..._PARSER` | `NevercASTAPI`、`NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`、`..._BUILDER`、`..._ANALYSIS`、`..._PASS`、`..._GEN`、`..._OPTIMIZATION` | IR 各テーブル | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`、`..._TARGET_ABI`、`..._CALLING_CONVENTION` | Target 各テーブル | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`、`..._MIR_ANALYSIS`、`..._MIR_PASS`、`..._MIR_PROVIDER` | MIR 各テーブル | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`、`..._MC_EMISSION`、`..._MC_PROVIDER`、`..._ASSEMBLY_PROVIDER` | MC 各テーブル | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`、`..._OBJECT_FORMAT`、`..._OBJECT_PHASE` | Object 各テーブル | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`、`..._LINK_REGISTRAR`、`..._LINK_PHASE` | Link 各テーブル | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`、`..._LTO_REGISTRAR` | LTO 各テーブル | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`、`..._DYNCODE_REGISTRAR`、`..._DYNCODE_PHASE` | DynCode 各テーブル | `PluginDynCode.h` |

インターフェースは STABLE（新しいホストは追加のみ可能）か LOCKSTEP
（ターゲット固有スキーマで完全一致が必須）のいずれかです。LOCKSTEP の値を使う
前にスキーマダイジェストを比較してください。

## ビルド

集約ヘッダー、あるいは使用するドメインだけを include します。

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

NeverC 自身で共有モジュールをビルドする場合:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

インストール済み SDK に対して CMake でビルドする場合:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

pkg-config を使う場合:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

ホストに応じて `.so`、`.dylib`、`.dll` を使い分けてください。SDK は LLVM も
NeverC ランタイムもリンクしません。`NevercPluginSDK::headers` はヘッダーのみの
ターゲットです。

## ロードと設定

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| オプション | 形式 | 目的 |
|---|---|---|
| `-fplugin=<path>` | 繰り返し可 | プラグイン共有モジュールをロードする。 |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 繰り返し可 | 登録済みプラグインオプションに名前空間付きの値を渡す。 |
| `-fplugin-provider=<phase>:<plugin-id>` | 繰り返し可 | 置換可能フェーズをどのプラグインが提供するか選択する。 |

`<plugin-id>:` 修飾子を省略できるのは、有効なプラグインがちょうど 1 つの場合
だけです。プラグインが `RegisterOption` で登録したオプションは、宣言した綴りで
直接指定することもでき、flag / joined / separate / 複数引数の各形式に対応します。
`-fplugin=` なしでプラグイン引数や Provider 選択を与えるのは、黙って無視される
のではなくハードエラーになります。

## ABI ルール

- 機能テーブルは `QueryInterface` 経由で取得し、major の一致を要求し、
  フィールドに触れる前に `StructSize` を検査すること。
- すべての公開構造体の `Header` と予約領域を初期化すること。まず構造体をゼロ
  クリアし、その後 `StructSize`、`Major`、`Minor`、`Flags` を設定します。
- ハンドルと借用ビューはスコープ付きの不透明値として扱うこと。タスクスコープの
  ハンドルをコールバックの外へ持ち越さず、別の session や task で使わず、
  ハンドル値を自作しないこと。
- すべてのコールバックから `NevercStatus` を返すこと。C++ 例外やホスト所有の
  ポインタを C 境界の外へ出さないこと。
- `NevercConcurrencyModel`（`SESSION_SERIAL`、`THREAD_SAFE`、
  `PROCESS_SERIAL`）と `NevercReentrancyModel`（`NONE`、`ALLOWED`）は、
  **最も狭く、かつ正直に**宣言すること。
- IR、MIR、AST、グラフ、アーティファクトの変更は必ずトランザクショナルな
  ホスト API 経由で行うこと。mutation を開始し、変更をステージし、commit または
  abort します。commit は検証してアトミックに公開し、失敗した commit は直前の
  状態をそのまま残します。
- 可変状態はホストが提供する process / session / task 状態に置くこと。
  グローバルな可変状態は `utils/plugin-api/check-global-state.py` が検査します。

新しい関数は、独立してバージョン管理される機能テーブルの末尾に追加されます。
最初の ABI メジャー（`NEVERC_PLUGIN_ABI_MAJOR` = 1）の範囲内では、テーブルの
安定プレフィックスは変化しません。

## ステータスと診断

`NevercStatus` は `Code`、`Flags`、`Detail` ワードを持ちます。主なコード:

| コード | 意味 |
|---|---|
| `NEVERC_STATUS_OK` | 成功。 |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 必須のポインタや値が欠落、または不正。 |
| `NEVERC_STATUS_ABI_MISMATCH` | ネゴシエートしたテーブルが小さすぎる、または major が異なる。 |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | ホストが要求された機能を提供していない。 |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | ハンドルが有効範囲外で使用された。 |
| `NEVERC_STATUS_POLICY_VIOLATION` | フェーズポリシーがその操作を許可していない。 |
| `NEVERC_STATUS_VERIFICATION_FAILED` | ホストの封印された検証器が成果物を拒否した。 |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | 協調的キャンセルまたはリソース上限。 |

フラグビット（`RECOVERABLE`、`OUTPUT_ALREADY_COMMITTED`、
`OUTPUT_MAY_BE_PARTIAL`、`OUTPUT_RECOVERY_REQUIRED`、
`DURABILITY_UNCONFIRMED`）は出力に何が起きたかを示します。これはビルド
システムが「リトライして安全か」を判断するために必要な情報です。

問題の報告には `NevercCoreAPI.EmitDiagnostic` と、重大度、コード、プラグイン
ID、フェーズ ID、メッセージ、ノート、ソース位置、範囲、fix-it を持つ
`NevercDiagnosticDescriptor` を使います。重い処理の前には `CheckCancelled` を
呼んでください。

## サンプル

すべてビルドする:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

各サンプルは 2 回コンパイルされます——設定されたホスト C コンパイラで 1 回、
ビルドしたての NeverC で 1 回——これにより ABI が両側から実証されます。
モジュールは `build-neverc/neverc/pluginsdk/examples/host/` に生成されます。

| サンプル | CMake ターゲット | 示す内容 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | オプション登録、フェーズ観測、ジョブ傍受 |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | メモリ上のヘッダーを提供する VFS provider |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | パーサ傍受とアトミックな AST 変更 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 値カーソルで関数リストを走査するモジュールレベル IR パス |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 安定した IR 関数パス |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | pre-emit フックでの安定した MIR パス |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 読み取り専用の MC 発行イベント |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | トランザクショナルな ObjectGraph 書き換え |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | データ駆動の呼び出し規約 |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | dyncode パイプラインの観測 |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | dyncode 文字集合エンコードの傍受 |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | CRT 依存ゼロのプラグイン |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI 呼び出しスループットのマイクロベンチマーク |

ロードする:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 規範的な情報源

| ファイル | 保証する内容 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | フェーズ ID、ポリシー、安定性、検証ゲート |
| `pluginsdk/manifest/plugin.json` | ABI バージョン、インターフェース ID/バージョン/安定性、スキーマダイジェスト、対応ターゲット |
| `pluginsdk/abi/plugin.json` | 公開構造体ごとの実測サイズ・アラインメント・フィールドオフセット（ホスト ABI キー別） |
| `docs/plugin-api/coverage.json` | 各安定フェーズを正常系・異常系・置換・オブザーバ・封印ゲートのテストに対応付け |

したがって SDK はホストに対して機械的に検証でき、プラグインのビルドも、
ロード先となる ABI キーに対して自身の構造体レイアウトを表明できます。
