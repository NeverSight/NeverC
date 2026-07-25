**言語**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC プラグイン IR API

最初の公開プラグイン ABI は、安定した C のテーブルを通じて LLVM IR を公開します。
プラグインは LLVM のヘッダをインクルードせず、NeverC のハンドルを LLVM オブジェクト
にキャストしてはいけません。

## インターフェイス

`neverc_plugin_entry` の中で `NevercBootstrapAPI.QueryInterface` を使って
インターフェイスを照会します。

- `NEVERC_INTERFACE_IR_CORE` — モジュール、型、値、CFG、メタデータ、属性、定数、
  シリアライズの各問い合わせ。
- `NEVERC_INTERFACE_IR_BUILDER` — トランザクショナルな IR の構築と変更。
- `NEVERC_INTERFACE_IR_ANALYSIS` — 組み込み解析とプラグイン定義の解析。
- `NEVERC_INTERFACE_IR_PASS` — Module、CGSCC、Function、Loop の各パス。
- `NEVERC_INTERFACE_IR_GEN` — SemanticUnit から IR への低下処理の置換。
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — 最適化パイプライン全体の置換。

必ずヘッダのメジャー／マイナーの組を要求し、返された `StructSize` がプラグインの
使う最後の関数ポインタまで届いていることを検証してください。新しいホストは
フィールドを追加することがあります。プラグインは未知の末尾を無視しなければなりま
せん。

## ハンドルと所有権

IR ハンドルは、タスクにスコープされた不透明な `{Owner, Value}` の組です。それらが
参照するオブジェクトはすべてホストが所有します。

- タスクスコープのハンドルを、コールバックやタスクの終了後に保持しない。
- ハンドルを別のセッションやタスクで使わない。
- コミットされた置換は、置き換えられたオブジェクトのハンドルを無効化する。
- 中止された変更は、その変更が作成したハンドルを失効させる。
- API は LLVM ポインタを公開せず、`NEVERC_STATUS_STALE_HANDLE`、`WRONG_OWNER`、
  `WRONG_TYPE` を返す。

問い合わせ呼び出しが返す文字列やバイトのビューは、API が解放可能なバッファを返すと
明記していない限り借用されたものです。

## IR の読み取り

`NevercIRCoreAPI` が提供するもの:

- モジュール識別子、トリプル、データレイアウト、インラインアセンブリ;
- 関数、グローバル、ブロック、命令、use、オペランドに対する安定した値カーソル;
- 安定した型 ID とオペコード ID;
- 関数、グローバル、命令、メタデータ、属性の各プロパティ;
- 整数、浮動小数点、集約、null、poison、undef の定数;
- ビットコードのエクスポート／インポートと、検証済みモジュール成果物。

コレクションカーソルは上限付きです。出力容量を渡し、返された個数が 0 になるまで収集
を繰り返してください。

## トランザクショナルな変更

構造的な変更はすべて `NevercIRBuilderAPI` を使います。

1. モジュールまたは関数の変更を開始する。
2. その変更に束縛されたビルダを作成する。
3. 挿入位置を設定し、命令、関数、ブロックを構築する。
4. 変更をコミットする。
5. ビルダと変更ハンドルを破棄する。

コミットは候補 IR を検証し、アトミックに公開します。検証器が失敗した場合、ホストは
変更をロールバックして以前のモジュールを保持します。`AbortMutation` は常に
ステージングされた変更を巻き戻します。

IR を変更した後に `NEVERC_IR_PRESERVE_ALL` を主張してはいけません。パスアダプタは
モジュール世代を確認し、矛盾する保存宣言を拒否します。

## パスのレベルとフェーズ

`NevercIRPassDescriptor.Level` がサポートするもの:

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

安定した挿入フェーズは `PRE_OPT`、`PIPELINE_START`、`OPTIMIZER_LAST`、`POST_OPT`、
`PRE_CODEGEN` です。呼び出しにはそのレベルで有効なハンドルだけが含まれます。関数
パスとループパスは並行実行される可能性があるため、可変のプラグイン状態は宣言した
並行性契約に従わなければなりません。

ホストは常に最終の封印された IR 検証器を実行します。プラグインがそのゲートを置換、
傍受、スキップすることはできません。

## 解析

組み込み解析の ID は、コールグラフ、支配木、後支配木、ループ情報、スカラー展開、
MemorySSA、エイリアス解析を対象とします。

プラグイン解析は依存関係とライフサイクルコールバックを宣言します。結果は呼び出し
ごとにキャッシュされ、パスの保存結果に従って無効化されます。再帰的な依存の循環や、
解析コールバックからの変更は拒否されます。

## 完全なプロバイダ

IR 生成プロバイダは組み込みの低下処理を置き換え、検証済みのモジュール成果物を発行
できます。最適化プロバイダは組み込みの最適化パイプライン全体を置き換えられます。
どちらの経路でも:

- 明示的なフェーズ入力を消費する;
- LLVM ポインタを返すのではなくホスト API を通じて公開する;
- ターゲット互換性とモジュールの妥当性を検証する;
- 公開に失敗した場合は古いモジュールをアトミックに保持する。

最適化プロバイダの後でも、最終検証器は必須のままです。

## 最小の例

`pluginsdk/examples/FunctionPass.c` は読み取り専用の関数パスです。
`pluginsdk/examples/ExamplePlugin.c` はモジュールの列挙を示し、
`pluginsdk/examples/CustomCallConvPlugin.c` は属性と呼び出し位置のプロパティを
実演します。

例をビルドしてロードする:

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

CMake が生成するプラットフォーム固有のモジュール拡張子を使ってください。

## 失敗の規則

すべてのコールバックから `NevercStatus` を返してください。プラグインの失敗は構造化
された診断になります。例外を C の境界越しに投げてはいけません。すべての出力
テーブルヘッダと予約フィールドを初期化し、必須ポインタが欠けている場合は
`INVALID_ARGUMENT` を返してください。

規範的な ABI 宣言、フェーズポリシー、テストの証拠は、`PluginIR.h`、
`PluginPhaseSchema.h`、`coverage.json` を参照してください。
