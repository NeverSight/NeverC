**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../README.md)

# `neverc runtime`

[GitHub Releases](https://github.com/NeverSight/NeverC/releases) から取得する
**クロスコンパイル runtime**（sysroot / SDK）を管理します。配置先はコンパイラ隣の
`<NeverC-root>/runtime/`（既定は `~/.neverc/runtime/`）。

`neverc-runtime-<target>.zip` を手で展開するより `neverc runtime install …` を使ってください。

## 構文

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

別名: `upgrade` → `update`、`uninstall` → `remove`、`ls` → `list`。

## 利用可能なターゲット

| ターゲット | `runtime/` 配下の配置 |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## 例

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## サブコマンド

- **`install`**: 既定では**コンパイラのリリースタグ**で 1 ターゲットを導入（または `--version`）。同タグなら成功終了、異なる場合は `[Y/n]` で再導入確認。
- **`install all`**: カタログ上の**未導入**ターゲットをすべて導入。導入済みはスキップ。
- **`update` / `upgrade`**: 対話なしで強制取得。既定は **latest**（`install` とは異なる）。
- **`remove` / `uninstall`**: ディレクトリ削除と `manifest.json` 更新。
- **`list` / `ls`**: 導入状態とコンパイラタグを表示。

## バージョン規則

| コマンド | `--version` 省略時の既定 |
|----------|---------------------------|
| `install` / `install all` | コンパイラのリリースタグ |
| `update` | その runtime 資産を含む最新 release |

タグは `vMAJOR.MINOR.PATCH`。展開前に `SHA256SUMS` で検証します。

## `neverc update` との関係

- `neverc runtime …` は **sysroot のみ**変更。
- [`neverc update`](../update/README.ja.md) はコンパイラと導入済み runtime を一括同期。

コンパイラ更新後、導入済み runtime は既に揃っています。**新しい**ターゲットだけ `runtime install` してください。

## 関連コマンド

| コマンド | 用途 |
|----------|------|
| [`neverc update`](../update/README.ja.md) | コンパイラ＋導入済み runtime の一括更新 |
| [`neverc build` / `make`](../build/README.ja.md) | クロスコンパイル例のビルド |
| [Examples](../examples/README.ja.md) | `--target=…` を使う Makefile |
| `neverc runtime --help` | 組み込みヘルプ |
