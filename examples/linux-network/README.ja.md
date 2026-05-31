**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux ネットワークソケットサンプル

NeverC でクロスコンパイルした TCP クライアント/サーバーデモ。

NeverC は `runtime/linux/` に Linux sysroot（Ubuntu 22.04、glibc 2.35）をバンドルしています。

## ビルド

```bash
cd examples/linux-network
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## 手動ビルド

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -o network-demo main.c
```

## 実行

```bash
chmod +x network-demo
./network-demo
```

## 機能

- TCP サーバー（127.0.0.1）
- クライアント接続
- 3つのメッセージ送受信
- `socket`、`bind`、`listen`、`accept` のデモ
