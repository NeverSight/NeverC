**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../README.md)

# `neverc runtime`

[GitHub Releases](https://github.com/NeverSight/NeverC/releases) から取得する
**クロスコンパイル runtime**（sysroot / SDK）を管理します。配置先はコンパイラ隣の
`<NeverC-root>/runtime/`（既定インストールでは `~/.neverc/runtime/`）。

`neverc-runtime-<target>.zip` を手で展開するより
`neverc runtime install …` を使ってください。

## 構文

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

別名: `upgrade` → `update`；`uninstall` → `remove`；`ls` → `list`。

## 利用可能なターゲット

| ターゲット | 配置（`runtime/` 配下） |
|------------|-------------------------|
| `windows-x64` | `windows/x64`（共有の `windows/shared` あり） |
| `windows-arm64` | `windows/arm64`（共有の `windows/shared` あり） |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## サブコマンド

### `install`

既定では**コンパイラのリリースタグ**で 1 ターゲットを導入します（または
`--version <tag>`）。アセット名: `neverc-runtime-<target>.zip`。

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

すでに導入済みの場合:

- 同じタグ → 報告して成功終了。
- 異なる / 不明なタグ → `[Y/n]` で再導入を確認。

### `install all`

コンパイラ版（または `--version`）でカタログ上の**未導入**ターゲットをすべて
導入します。導入済みはスキップされます。ピンを変えるときは単一ターゲットへ
改めて `install` してください。

```bash
neverc runtime install all
```

### `update` / `upgrade`

対話なしで 1 ターゲットを強制取得します。既定バージョンは **latest** です
（`install` がコンパイラタグに従うのと異なります）。`--version` でピン留めできます。

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

導入済みターゲットのディレクトリを削除し、`runtime/manifest.json` を更新します。

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

カタログ各ターゲットの導入状態（記録タグ付き）と、現在のコンパイラタグを表示します。

```bash
neverc runtime list
```

## バージョン規則

| コマンド | `--version` 省略時の既定 |
|----------|---------------------------|
| `install` / `install all` | コンパイラのリリースタグ |
| `update` | その runtime 資産を含む最新 release |

タグは `vMAJOR.MINOR.PATCH` 形式です。展開前に release の `SHA256SUMS` で検証します。

## `neverc update` との関係

- `neverc runtime …` は **sysroot のみ**変更します。
- [`neverc update`](../update/README.ja.md) は**コンパイラと導入済み runtime すべて**を
  1 回のトランザクションで同じタグへ揃えます。

`neverc update` でコンパイラを上げたあと、導入済み runtime はすでに揃っています。
**新しい**ターゲットだけ `runtime install` してください。

## 関連コマンド

| コマンド | 用途 |
|----------|------|
| [`neverc update`](../update/README.ja.md) | コンパイラ＋導入済み runtime の一括更新／ダウングレード |
| [`neverc build` / `make`](../build/README.ja.md) | これらの sysroot に依存するクロスコンパイル例のビルド |
| [Examples](../examples/README.ja.md) | `--target=…` を使うサンプル `Makefile` |
| `neverc runtime --help` | 組み込みの使い方要約 |
