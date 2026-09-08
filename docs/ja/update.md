**言語**: [English](../update.md) | [简体中文](../zh-CN/update.md) | [繁體中文](../zh-TW/update.md) | [日本語](update.md) | [한국어](../ko/update.md) | [Français](../fr/update.md) | [Deutsch](../de/update.md) | [Español](../es/update.md) | [Italiano](../it/update.md) | [Русский](../ru/update.md) | [العربية](../ar/update.md)

[← ドキュメント索引](README.md) · [← NeverC プロジェクト](project.md)

# `neverc update`

**リリースインストール**のコンパイラと、すでに導入済みのクロスコンパイル runtime を、
**ひとつの具体的なリリースタグ**へまとめて同期します。`neverc upgrade` は同義コマンドです。

`install.sh`（または `~/.neverc` への導入）後のアップグレード／ダウングレード向けです。
CMake/Ninja のソースビルドツリーは更新しません。PATH を切り替え再ビルドしてください。
詳しくは [ローカル開発](local-dev.md)。

## 構文

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

例:

```bash
neverc update                 # このホスト向けの最新の完全な release
neverc update v3389.1.2       # 正確なタグ（アップ／ダウングレード）
neverc update 3389.1.2        # 先頭の v は省略可
neverc upgrade                # neverc update と同じ
```

`-y` / `--yes` はスクリプト互換のため受け付けます。更新自体は非対話です。

## 同期対象

| コンポーネント | 動作 |
|----------------|------|
| コンパイラ（`bin/`、`lib/`、`pluginsdk/`） | ターゲットタグが現行と異なるとき置換 |
| `runtime/` 配下の導入済み runtime | **既に入っている**ターゲットだけ再取得し同一タグへ固定 |
| 未導入の runtime | 自動導入しない — [`neverc runtime install`](runtime.md) を使う |

## 安全モデル

1. `<install>/.neverc-update.lock` で排他ロックを取得。
2. ターゲットタグを解決（最新ホスト資産、または指定タグ）。
3. `SHA256SUMS` と必要アーカイブをダウンロードし検証。
4. ステージへ展開・検証してから本番へコミット。失敗時はロールバック。

問題のある runtime なら、古いタグへまとめて戻します:

```bash
neverc update v3389.0.1
```

## 制約

- リリースインストール根（通常 `~/.neverc`）のみ。ファイルシステムルートや CMake ビルドツリーは拒否。
- ホストは公開済みコンパイラ資産と一致している必要があります。
- Windows では、実行中の `neverc.exe` を置換するため短いヘルパープロセスを使うことがあります。

## 関連コマンド

| コマンド | 用途 |
|----------|------|
| [`neverc runtime`](runtime.md) | 個別 sysroot の導入／一覧／削除（コンパイラは変更しない） |
| [`neverc run`](run.md) | 一時バイナリをホストでコンパイル実行 |
| [`neverc build` / `make`](build.md) | サンプル／プロジェクトの Makefile を駆動 |
| `neverc update --help` | 組み込みヘルプ |
