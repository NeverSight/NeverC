**言語**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# DynCode プラグイン

`-fdyncode` は 1 つの翻訳単位を、コードに再配置が一切なくデータセクションも持たない
フラットで位置独立なイメージ（`.bin`）へコンパイルします。対象は macOS、Linux、
Android、Windows 上の arm64／x86_64 で、実行レベルはユーザーモードとカーネル
モードのいずれかです。プラグインは、C をそのイメージへ変換する型付きフェーズを、
他のドメインと同じ純粋な C ABI を通じて観測・インターセプト・置換します。LLVM の
C++ オブジェクト、STL 型、例外、そして API テーブルが寿命を明示しないホスト
ポインタは一切登場しません。

## インターフェース

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| インターフェース | テーブル | スロット | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | リクエスト、イメージ、レポート、およびセクション／シンボル／再配置／外部参照の各マップを読む |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`、`RegisterImportProvider`、`RegisterExtractor`、`RegisterCharsetEncoder`、`RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`、`GetRequest`、`GetImage`、`GetReport` |

3 つとも メジャー 1 において `NEVERC_INTERFACE_STABLE` です。フェーズコールバックの
内側では `NevercDynCodePhaseAPI` が入口となり、フレームを他方のテーブルが消費する
ハンドルへ変換します:

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

4 つのマップ族 — セクションマップ、シンボルマップ、再配置、外部参照 — はいずれも
同じ first/next/info の三点セットで走査します。たとえば `GetFirstRelocation`、
`GetNextRelocation`、`GetRelocationInfo` です。これにより、プラグインはレポートの
JSON を解析することなく、抽出が下した判断を読み取れます。

## DynCode は `main()` の後処理ではなくコンパイル成果物である

`-fdyncode` はドライバ DAG における通常の Action／Job です。コンパイルジョブは
検証済みのインメモリ `ObjectGraph` を発行し、`-dyncode-extract` ジョブがそのグラフを
消費してユーザーの `-o` イメージを書き出します。`-###`、フェーズ表示、ジョブグラフの
いずれもこの抽出ジョブを示すため、プラグインがモードを知るために書き換え済みの argv を
再構築する必要はまったくありません。凍結されたリクエストはタスクローカルに
インプロセスのコード生成と共有されます。`getCurrentDynCodeOptions()` も、プロセス
グローバルなモードフラグも、一時オブジェクトの往復もありません。

ちょうど 1 つの翻訳単位が 1 つのイメージへ低位化されます。複数入力、`-c/-S/-E`、
および非対応のトリプルは、安定した診断とともに事前に拒否されます。

## 互換性ティア

フェーズ ID、成果物 ID、リクエスト／レポート／イメージのコンテナ、そしてコールバック
契約は、初回リリースの STABLE ABI です。ターゲット固有の再配置種別と、オブジェクト
形式のセクション／シンボルスキーマは LOCKSTEP です。これらを消費する前に、ターゲット
スキーマ ID とダイジェストを比較してください。NeverC はスキーマが一致しない場合、
プロバイダを呼び出す前に拒否します。

## 凍結されたリクエスト

ジョブ開始時に、ドライバはコマンドラインを不変の `DynCodeRequest` へ正規化して
凍結します。子タスクはそのスナップショットを借用するだけで、変更することはありません。
リクエストは、ターゲットキーとオブジェクト形式、実行レベル（ユーザー／カーネル）、
エントリポリシー（明示シンボル、既定の候補リスト、オフセット 0 要件）、PIC／
セクションポリシー、外部参照ポリシー、禁止バイト集合／プロファイルと書き換えフラグ、
charset プロバイダ ID、そして最大長・アラインメント・パディングバイトを保持します。

## 型付きフェーズグラフ

DynCode は 34 フェーズからなる固定グラフです。30 個の通常の遷移は
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE` で、4 個は
`OBSERVABLE | SEALED_HOST_GATE` です。封印されたゲートは、IR 最終検証、MIR 最終検証、
イメージ検証、そしてコミットです。プラグインは任意のフェーズを観測でき、置換可能な
遷移をインターセプタで包むことも、そのプロバイダを丸ごと置き換えることもできますが、
封印ゲートを置換・スキップ・迂回することは決してできません。また、無効化された変換を
「呼ばれないコールバック」として表現することもできません。無効化された変換は明示的な
no-op プロバイダを実行し、その等価な出力をホストの検証器が引き続き証明します。

フェーズは順に次のとおりです:

1. リクエストの凍結;
2. IR 変換群 — prepare、間接分岐の低位化、メモリ intrinsic の低位化（ヒープ前と
   ヒープ後）、文字列ランタイムの低位化、ヒープアリーナ、3 つの `compiler_rt` 位置
   （pre／post／final）、syscall／PEB／カーネルインポートの低位化、2 つの
   `data_to_text` 位置（pre／post）、インライン最適化、文字列の確定、stackify、
   全 `blr` 化、そして封印された IR 最終検証;
3. MIR prepare 変換と、封印された MIR 最終検証;
4. オブジェクトインポート — 検証済み `ObjectGraph` をタスクに束縛する;
5. 抽出 — 計画、レイアウト、再配置、そして候補イメージの構築;
6. 範囲限定のバイナリフェーズ群 — post-extract、禁止バイトの書き換え、charset
   エンコード、サイズ／アラインメント／パディング、そして pre-verify;
7. 封印されたイメージ検証;
8. 封印されたコミット。

ID、ポリシー、安定性ティア、ゲートの規範的な情報源は
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`] です。実行可能なカバレッジ
契約は [`docs/plugin-api/coverage.json`] です。

## 組み込み変換もまたプロバイダである

すべての組み込み IR／MIR パスは型付きプロバイダとして包まれており、LLVM のパス
オブジェクトが C ABI をまたいで公開されることはありません。フェーズを置き換えると
組み込みプロバイダは動作しません。合格するテストは、単に登録が成功したことではなく、
振る舞いまたはトレースそのものを証明します。`mem_intrin`、`compiler_rt`、
`data_to_text` の各フェーズは複数の位置に現れますが、各位置はそれぞれ固有の証明を
持つ別個のフェーズ ID です。したがって再実行は冪等であり、隠れたパス状態に依存する
ことはありません。

## 通常オブジェクトの入力は ObjectGraph だけ

抽出は、ターゲットのコード生成経路が生成した検証済み `ObjectGraph` をちょうど 1 つだけ
消費します。`dyncode.object.import` はそのグラフを束縛し、ターゲットキーと由来を
検査します。ディスクからバイトを読み直すことも、2 度目のオブジェクト解析を走らせる
こともありません。カスタムオブジェクト形式は、`ObjectGraph` として読み込めて、対応する
再配置プロバイダとターゲットプロバイダを備えた時点で DynCode に参加できます。複数
オブジェクトと LTO のグラフ集合は、凍結時に安定した `CAPABILITY_UNAVAILABLE` で
拒否されます。

## 外部参照とインポートの低位化

リクエストの許可済み外部集合は「プロバイダがこれを処理してよい」という意味にすぎず、
未解決の再配置がフラットイメージまで生き残ることを許すものでは決してありません。
すべての外部参照は最終的に次のいずれかになります: IR／MIR で消去される、イメージ内の
シンボルへ解決される、宣言済みかつ検証器に受理されたランタイムリゾルバ契約へ変換
される、あるいはハードエラー。syscall スタブ、PEB インポート、カーネルインポートが
3 つの組み込み `ImportProvider` で、それぞれターゲット／レベル／シンボルのマッチャと、
生成する ABI 契約を宣言します。プラグインは `ImportProvider` を追加できますが、置換の
由来、エントリ ABI の変化、リゾルバのパラメータ、および残存参照を返さなければ
なりません。

## イメージ、レポート、範囲限定のバイト編集

抽出は `DynCodeImage` と `DynCodeReport` を生成します。イメージは、範囲限定のバイト
ビルダに加えて、エントリのオフセット／シンボル、ソースセクションとソースシンボルの
出力マップ、再配置の処理結果、および外部／ランタイム契約のレコードを備えます。
すべてのバイト編集はビルダの検査付き read/write/insert/append/resize API を経由し、
`uint8_t **` は存在しません。編集はイメージ世代を更新し、変更範囲と重なる
再配置／PIC／エントリの証明を無効化します。

レポートは不変かつ決定的な監査成果物です: リクエスト／経路／入力／出力のダイジェスト、
フェーズごとのプロバイダジャーナル、採用／却下されたセクションとその理由、エントリの
選択、パッチ済み／却下／ランタイム契約となった再配置、残存する外部参照、サイズ／
アラインメント／パディング、禁止バイトのスキャン、そして検証器のチェックリスト。
`-fdyncode-report=<path>` はその正規形 JSON を書き出します。詳細診断も、別に数え直す
のではなく同じレポートから描画されます。

禁止バイトの書き換えチェーンは凍結されたトポロジカル順で実行され、各ステップは変更
レコードを返します。charset エンコーダは厳密な安定 ID で選択され、デコーダスタブ、
エンコード済みペイロード、エントリ更新、ターゲット証明を返します。未知または曖昧な ID は
ハードエラーです。書き換えを無効化すると明示的な no-op ステップが選択されます —
最終監査は変わらず実行されます。

## 最終検証器と finalize 後のタイミング

書き込み可能なフェーズはすべて、封印された最終検証器より前に完了します。検証器が
確認するのは、未処理の外部再配置／参照が残っていないこと、禁止されたデータ／TLS／
アンワインド／デバッグ／メタデータの各セクションが存在しないこと、エントリが存在し
正しくアラインされ（必要な場合は）オフセット 0 にあること、すべての再配置サイトが
範囲内にあり現在のイメージバイトに対応する PIC 証明を伴うこと、セクション／シンボル
マップが重複しないこと、長さ／アラインメント／パディングの規則が満たされること、そして
デコーダ・ヘッダ・パディングを含む最終バイト列に禁止バイトが含まれないことです。
いずれかが失敗すると、構造化された診断を返して出力バンドル全体を破棄します。

監査の後に書き込み可能なフックはありません。バイト変換が実行可能領域に触れる場合、
凍結された経路は対応するバイナリ検証器ケイパビリティを提供しなければならず、ホストは
それを呼び出して、最終的で不変なイメージに対する PIC 証明を再発行します。

## ドライバオプション

`-fdyncode` がモードを有効にします。`-fdyncode-entry=` はエントリシンボルを選びます。
`-fdyncode-bad-bytes=` ／ `-fdyncode-bad-byte-profile=` は禁止バイトを設定し、
`-fdyncode-bad-byte-rewrite`（既定は有効）が書き換えチェーンを選択、
`-fdyncode-charset=` は登録済みエンコーダを選びます。`-fdyncode-max-length=`、
`-fdyncode-align=`、`-fdyncode-pad=` が最終サイズを制約します。
`-fdyncode-keep-obj=` は中間の再配置可能オブジェクトを保存し、`-fdyncode-report=` は
監査レポートを書き出します。`-mdyncode-context=user|kernel` は実行レベルを選択します。

## 並行性と失敗に関する規則

- 可変状態はホストが提供するプロセス／セッション／タスクの各スコープに置いてください。
  現在のプラグインや現在のオプションを指すシングルトンは決して使わないこと。
- コールバックが戻った後にタスクハンドルや借用ビューをキャッシュしないでください。
- インターセプタの継続は、コールバックスレッド上で高々 1 回だけ呼び出してください。
- 元の `NevercStatus` を返してください。`REPLACE` を宣言した処理が失敗しても、組み込み
  プロバイダへ黙ってフォールバックすることはありません。
- 並行性モデルと再入モデルは、偽りのない範囲で最も狭いものを宣言してください。

読み取り専用のフェーズトレーサは [`pluginsdk/examples/DynCodeTracePlugin.c`] を、
charset エンコーダは [`pluginsdk/examples/DynCodeEncoderPlugin.c`] を参照してください。

<!-- reference links -->
[`docs/plugin-api/coverage.json`]: coverage.json
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`pluginsdk/examples/DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
