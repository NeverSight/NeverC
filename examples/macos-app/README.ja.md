**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# macOS アプリケーション サンプル

NeverC でクロスコンパイルしたネイティブ macOS Mach-O 実行ファイル。sysctl、uname、Mach カーネル API を使用してシステムとプロセスの情報を取得するデモです。macOS、Windows、Linux のいずれからでもビルド可能 — Xcode 不要。

## ビルド

リポジトリから（デフォルトターゲット：`arm64-apple-macos`）：

```bash
cd examples/macos-app
neverc make
```

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
