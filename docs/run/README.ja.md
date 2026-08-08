**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../README.md)

# `neverc run`

C または NeverC プログラムを**一時実行ファイル**にコンパイルし、**ローカルホスト**で実行して終了ステータスを返し、その後アーティファクトを削除します。ワークフローは意図的に `go run` に近い設計です。

バイナリを残す、配布する、デバッガでデバッグする場合は、通常のコンパイル呼び出し（`neverc ... -o output`）を使ってください。

## 構文

```text
neverc run [コンパイラフラグ] file.c [file2.nc ...] [プログラム引数...]
neverc run [コンパイラ引数...] -- [プログラム引数...]
```

`neverc run --help` で組み込みサマリーも確認できます。

## 引数解析

`neverc run` は次の 2 つの規則のいずれかで、引数を**コンパイラ呼び出し**と任意の**プログラム引数**に分割します。

### 既定（Go 風）分割

1. 左から右へ走査し、`.c` または `.nc` で終わり `-` で始まらない最初の引数を探す。
2. **最初のソースより前と、連続する `.c`/`.nc` ソース**はすべてコンパイラへ渡す。
3. 連続ソース**の後**の引数は、一時プログラムの `argv` へ渡す。

例：

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run -DGENERATED=.c -O2 main.c argument
```

注意：

- run ソースとして扱われるのは `.c` と `.nc` のみ。`-DGENERATED=.c` のように `-` で始まる引数はコンパイラ側に残る。
- 複数ソースは通常のマルチファイルリンクと同様、1 つの一時バイナリにまとまる。

### 明示的 `--` 区切り

ソース一覧の**後**にコンパイラ引数（リンカフラグ、非ソース入力、`-x c -` など）が必要な場合は、`--` でコンパイラ尾部とプログラム引数を分ける：

```bash
neverc run hello.c helper.o -lm -- arg.c -x
neverc run hello.c -O1 -- x
```

`--` より前は `neverc` へそのまま転送（内部 `-o <temp>` を付加）。`--` より後はプログラム引数になる。

## 実行時の挙動

| 項目 | 挙動 |
|------|------|
| 作業ディレクトリ | 一時プログラムは**現在のディレクトリ**で実行。相対パスは通常バイナリと同じ |
| 環境 | 現在の環境を継承（`PATH`、エクスポート済み変数など） |
| 標準 I/O | stdin/stdout/stderr は一時プロセスに接続。パイプとリダイレクトも通常どおり |
| 終了ステータス | 成功時は**プログラム**の終了コード。コンパイル失敗時は**コンパイラ**の終了コードを返し、プログラムは実行しない |
| 一時ファイル | 実行ファイルは一意の `neverc-run-*` ディレクトリに置かれ、実行後に削除される（成功・失敗を問わず）。クリーンアップ失敗は別途報告される。 |

## 例

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -fbuiltin-string greet.c -- Alice "two words"
neverc run -O2 main.c util.nc -- --port 8080
neverc run app.c extra.o -lm -- --config prod.json
```

## 制限と注意

- **ホスト実行のみ。** 交叉コンパイルフラグ（`-target ...`）でコンパイルできても、一時バイナリは常に呼び出し元マシンで実行される。
- **永続アーティファクトなし。** 完了後にバイナリは削除される。デバッガを後から付ける必要がある場合は `neverc ... -o out` を使う。
- **同じ `neverc` ツールチェーン。** `run` を処理した `neverc` バイナリを再呼び出し、コンパイラフラグを（内部 `-o` を除き）そのまま転送する。
- **`.nc` ソース。** `.c` と同じ規則。`.nc` 向け言語拡張は自動的に有効。

## 関連コマンド

| コマンド | 用途 |
|---------|------|
| `neverc file.c -o out` | バイナリ保持、交叉コンパイル、ビルドスクリプト連携 |
| [`neverc build` / `neverc make`](../build/README.ja.md) | Makefile 駆動の GNU Make 互換ビルド |
| `neverc run --help` | 組み込み用法サマリー |
