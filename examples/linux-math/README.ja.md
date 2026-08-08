**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Linux 数学 + zlib サンプル

数学ライブラリ関数と zlib 圧縮のデモ。`-lm` と `-lz` を使用。

NeverC は `runtime/linux/` に Linux sysroot（Ubuntu 22.04、glibc 2.35）をバンドルしています。

## ビルド

```bash
cd examples/linux-math
neverc make          # debug: -g（初回ビルドの既定値）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
```

Makefile は `PROFILE` を保持するため、以降の `neverc make` でも同じ
debug/release 選択が使われます。release は NeverC 組み込みの `--strip`
で、不要な静的シンボル名とデバッグメタデータを削除しつつ、ローダー/
動的 ABI に必要な名前は残します。詳細は
[リリースビルド](../../docs/release-builds/README.ja.md)。


AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動ビルド

```bash
neverc --target=x86_64-linux-gnu -Wall -lm -lz -o math-demo main.c
```

## 実行

```bash
chmod +x math-demo
./math-demo
```

## 機能

- 三角関数：sin/cos/tan
- 特殊関数：`exp`、`tgamma`、`erf`
- zlib 圧縮・展開・CRC32
