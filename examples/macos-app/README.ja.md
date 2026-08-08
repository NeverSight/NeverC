**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# macOS アプリケーション サンプル

NeverC でクロスコンパイルしたネイティブ macOS Mach-O 実行ファイル。sysctl、uname、Mach カーネル API を使用してシステムとプロセスの情報を取得するデモです。macOS、Windows、Linux のいずれからでもビルド可能 — Xcode 不要。

## ビルド

リポジトリから（デフォルトターゲット：`arm64-apple-macos`）：

```bash
cd examples/macos-app
neverc make          # debug: -g（初回ビルドの既定値）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
```

Makefile は `PROFILE` を保持するため、以降の `neverc make` でも同じ
debug/release 選択が使われます。release は NeverC 組み込みの `--strip`
で、不要な静的シンボル名とデバッグメタデータを削除しつつ、ローダー/
動的 ABI に必要な名前は残します。詳細は
[リリースビルド](../../docs/release-builds/README.ja.md)。


Intel 向けビルド：

```bash
neverc make TARGET=x86_64-apple-macos
```

スタンドアロンの NeverC リリースを使用：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動ビルド（Make なし）

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## 実行

```bash
./macos-app
```

## 機能

- `uname` によるカーネル情報の取得
- `sysctl` によるハードウェア情報の取得（モデル、CPU 数、メモリサイズ、ページサイズ）
- プロセス情報の表示（`getpid`、`getppid`、`getuid`）
- Mach `host_info` によるホスト情報、`task_info` によるタスクメモリ統計の取得
