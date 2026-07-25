**言語**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# プリプロセッサのプラグイン API

`PluginPrep.h` は、安定したトークン、識別子、マクロ、プラグマ、トークンストリームの
スキーマを、NeverC や LLVM の C++ 型を漏らすことなく公開します。生成されたスキーマ
`Schema/PluginPrepSchema.inc` が、安定した数値種別、カテゴリ、綴り、構築可能性に
関する唯一の正典です。

## 拡張のレベル

プラグインは 3 つのレベルで参加できます。

- include、マクロ展開、条件分岐、プラグマ、ファイル遷移に関する読み取り専用の
  プリプロセッサイベント。
- トークン、include、マクロ、プラグマ、機能問い合わせの各フェーズに対する型付き
  インターセプタ。
- 検証済みの `TokenStream` を発行する完全な
  `neverc.prep.build_token_stream` プロバイダ。

トークンフェーズは、上限付きの置換、削除、展開をサポートします。ホストは展開予算を
強制し、置換を発行する前に綴り、位置、フラグ、EOF の配置、トークンの所有権を検証
します。

## トークンビルダ

`CreateTokenBuilder` で合成トークンを作成し、トークンのペイロードをちょうど 1 つ
設定し、タスクが所有する有効な位置を割り当ててから `TokenBuilderCommit` を呼び出し
ます。すべての経路でビルダを破棄してください。コミット済みのビルダは不変であり、
コミットが失敗した場合はトークンは発行されません。

トークンストリームは連続した不変のタスク成果物です。置換用のストリームは末尾に
EOF トークンをちょうど 1 つ含まなければならず、
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` を超えてはなりません。

## オブザーバとインターセプタの規則

オブザーバは読み取り専用のイベントデータを受け取り、プリプロセスに影響を与えること
はできません。インターセプタは共通の継続契約に従います。

- `InvokeNext` を最大 1 回呼び出してから `CONTINUE` を返す。または
- それを呼び出さず、検証済みの置換を発行する。

継続オブジェクトとすべてのプリプロセッサハンドルは、宣言されたコールバック／タスク
のスコープ内でのみ有効です。プラグインが作成したスレッドがそれらの値に触れる場合は、
コールバックが戻る前に join しなければなりません。

## 検証

トークン定義を変更したら、生成スキーマとカバレッジの検査を実行します。

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

`NEVERC_ENABLE_PLUGIN_FUZZERS=ON` を有効にすると、
`plugin-prep-token-builder-fuzzer` が不正なトークンビルダ、タスクハンドル、出力
容量、トークンストリームの問い合わせを検査します。
