**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Linux POSIX API サンプル

NeverC を使用した POSIX システムプログラミングのクロスコンパイル：pthreads、mmap、pipe、シグナル処理。

NeverC は `runtime/linux/` に Linux sysroot（Ubuntu 22.04、glibc 2.35）をバンドルしています。

## ビルド

```bash
cd examples/linux-posix
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動ビルド

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## 実行

```bash
chmod +x posix-demo
./posix-demo
```

## 機能

- **pthreads**：4つのワーカースレッドを作成
- **mmap**：匿名メモリページの割り当て
- **pipe**：Unix パイプ経由のメッセージ送受信
- **signals**：`SIGUSR1` ハンドラの検証
