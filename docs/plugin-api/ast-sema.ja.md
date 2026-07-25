**言語**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# AST・パーサ・セマンティクスのプラグイン API

`PluginAST.h` と `PluginSema.h` は、フロントエンドのツリーとセマンティック
パイプラインへの、タスクスコープで純粋な C のアクセス手段を提供します。安定した
ノード ID、プロパティ ID、子スロット ID は NeverC の具体的な AST 定義から生成され
ます。プラグインが C++ の `Decl`、`Stmt`、`Type`、`Sema` のポインタを受け取ること
はありません。

## AST ノードの読み取りと構築

`NevercASTAPI` を使って、ノード情報、スキーマプロパティ、子、親、宣言コンテキスト、
型、属性、および代表的な具体ノードの詳細を照会します。バッチ API では要素数、容量、
ストライドを明示的に指定する必要があります。

`NevercASTBuilder` はスキーマで宣言されたノード種別のみを構築します。必須の
プロパティと子スロットはコミット時に検証されます。コミットが成功するとタスク所有の
ノードが発行され、失敗した場合は部分的に見えるノードは残りません。コミットの成否に
かかわらず、すべてのビルダを破棄してください。

## アトミックな変更

AST の変更は `BeginASTMutation`、ステージングされた操作、`CommitASTMutation` を
使って行います。ホストはツリーを変更する前に、所有権、スロットの互換性、多重度、
親リンク、循環、意味的不変条件を検証します。`AbortASTMutation` はステージングした
操作をすべて破棄します。ネイティブの `TreeMutationListener` 通知は、コミットが成功
した後にのみ送られます。

ビルド可能な [`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
は、組み込みパーサを呼び出し、整数リテラルを構築し、変数の初期化子をアトミックに
置き換えるパーサインターセプタを示します。

## パーサと Sema の置換

`neverc.syntax.parse` は検証済みのトークンストリームを `ASTUnit` に写像します。
`neverc.sema.analyze` は AST 成果物を `SemanticUnit` に写像します。いずれのフェーズ
にも型付きインターセプタとプロバイダがあります。フロントエンドの一部だけを置き換える
場合は、宣言、文、式、型名、属性、名前探索、変換、キーワードといった細粒度の拡張
フェーズを引き続き利用できます。

組み込みの融合パーサ／Sema 経路は、置換実装とまったく同じ成果物契約を発行します。
セマンティックリプレイが受け付けるのは、NeverC がスコープ、名前探索、再宣言、型検査
の状態を再構築できるノード種別だけです。未対応の具体種別に遭遇した場合は
`NEVERC_STATUS_UNSUPPORTED_AST_KIND` を返し、部分的にリプレイされたツリーを意味的に
完全であるとみなすことは決してありません。

## ライフサイクルとクリーンアップ

AST と Sema のライフサイクルオブザーバは、ホストの `TreeConsumer` ブリッジを通じて
ソース順に配送されます。構文エラー、プラグインエラー、キャンセルが起きても
begin/end イベントは対のまま保たれます。タスクハンドルは、最後の読み取り専用 end
イベントとクリーンアップコールバックが実行された後にのみ無効になります。

## 検証

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

`NEVERC_ENABLE_PLUGIN_FUZZERS=ON` を有効にすると、`plugin-ast-mutation-fuzzer` が
プロパティのデコード、不正なビルダ、偽造ハンドル、変更のロールバックを検査します。
