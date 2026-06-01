**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Hello World サンプル

NeverC を使用して Linux ELF にクロスコンパイルする最小限の C プログラム。macOS、Windows、Linux からビルド可能——ターゲットシステムのツールチェーン不要。

NeverC は `runtime/linux/` に Linux sysroot（Ubuntu 22.04、glibc 2.35）をバンドルしており、一回の呼び出しでプリプロセス、コンパイル、最適化（auto-LTO）、内蔵リンカによるリンクを完了します。

## ビルド

リポジトリから（デフォルトターゲット：`x86_64-linux-gnu`）：

```bash
cd examples/linux-hello
neverc make
```

AArch64 向けにビルド：

```bash
neverc make TARGET=aarch64-linux-gnu
```

スタンドアロンの NeverC リリースを使用：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動ビルド（Make を使用しない）

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## 実行

`hello` を Linux マシン（または Docker コンテナ）にコピーして実行：

```bash
chmod +x hello
./hello
```

## 機能

- コマンドライン引数付きの挨拶メッセージを表示
- バンドルされた libc の `printf`、`strncpy`、`strlen`、`atoi` をデモ
- 基本的な整数/文字演算を検証するための XOR 文字列変換
