**言語**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# Source と I/O のプラグイン API

最初の公開プラグイン ABI は、ソース入力、仮想ファイル、依存関係、コンパイラ出力を
`PluginSource.h` を通じて公開します。すべてのパスは正規化された VFS パスであり、
すべてのハンドルは現在の `TranslationUnit` タスクにスコープされます。

## Source フェーズ

安定した source パイプラインは次のとおりです。

1. `neverc.source.resolve_input` が要求された入力を検証し、正規化します。
2. `neverc.source.open` が、ホストとプラグインを合成した VFS 経由でそれを開きます。
3. `neverc.source.after_open` が、検証済みの `SourceUnit` に対する読み取り専用
   イベントを発行します。

`resolve_input` は観測可能かつ傍受可能です。`open` はさらに置換も可能です。ホストは
置換結果を `SourceUnit` として発行する前に必ず検証します。プラグインが
`after_open` を置換することはできません。

## VFS プロバイダ

プラグイン登録中に `NevercIOAPI` を照会し、`RegisterVFSProvider` を呼び出します。
プロバイダはまず `MatchesPath` に応答し、次に自身が担当する操作を実装します。
`NEVERC_VFS_RESULT_NOT_HANDLED` を返すと次のプロバイダに委譲されます。`HANDLED`
を返した場合、不正な状態や内容は暗黙のフォールバックではなく致命的エラーになります。

プロバイダが返すバッファはコールバックの間だけ借用されます。NeverC は受理した
バイト列をタスク所有のストレージへコピーします。プロバイダは、その結果が決定的で
キャッシュ可能かどうかを宣言しなければなりません。

ビルド可能な
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
の例は、ホスト VFS を迂回することなくメモリ上のヘッダを提供します。

## 出力シンクと依存関係

ファイル出力とメモリ出力は同じトランザクショナルなシンクを使用します。

- 候補に書き込む。
- finish を呼び出して検証の対象となる資格を与える。
- 封印されたホストゲートに検証させる。
- タスク成功時に原子的にコミットし、エラーやキャンセル時には中止する。

プラグインが宛先パスへ直接書き込んで公開することはありません。ロールバックできない
ストリーミング宛先は、原子的な候補を必要とする変換を拒否します。依存レコードは
正規化された VFS 識別子を使うため、ネイティブのファイルとプラグインが提供する
ファイルは同じ出自とキャッシュ意味論を持ちます。

## 安全規則

- コールバック終了後に source、file、buffer、sink、task の各ハンドルを保持しない。
- `NevercStringView` と `NevercByteView` は長さ付きビューとして扱う。
- データがコールバックより長く生存する必要がある場合はホストアロケータを使う。
- VFS 契約の背後でホストのファイルシステム API を使わない。
- コストの高いプロバイダ処理の前にキャンセルを確認する。
