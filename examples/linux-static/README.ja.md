**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 完全静的リンクサンプル

NeverC で構築した自己完結型の静的リンク Linux 実行ファイル。ランタイム依存関係なし。

NeverC は `runtime/linux/` に Linux sysroot（Ubuntu 22.04、glibc 2.35）をバンドルしています。

## ビルド

```bash
cd examples/linux-static
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動ビルド

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## 実行

```bash
chmod +x static-demo
./static-demo
```

## 機能

- システム情報の表示
- 数学関数：`sqrt`、`sin`、`pow`、`log`
- 文字列操作、動的メモリ管理
