**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 共有ライブラリ サンプル

NeverC で Android 向けにクロスコンパイルした ARM64 ネイティブ `.so` 共有ライブラリです。macOS、Windows、Linux からビルド可能。

## ビルド

```bash
cd examples/android-so
make
```

## 手動ビルド

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## 機能

- ゲームセキュリティ研究向けヘルパー関数: PID 取得、`/proc/self/maps` 読み取り、RWX メモリ確保、XOR バッファ暗号化
- `dlopen` で `liblog.so` を動的ロード
- `mmap` + `PROT_EXEC` で実行可能メモリを確保するデモ

