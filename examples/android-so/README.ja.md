**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android 共有ライブラリ サンプル

NeverC で Android 向けにクロスコンパイルした ARM64 ネイティブ `.so` 共有ライブラリです。macOS、Windows、Linux からビルド可能。

## ビルド

```bash
cd examples/android-so
neverc make          # debug: -g（初回ビルドの既定）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
```

Makefile は `PROFILE` を保持するため、以降の `neverc make` でも同じ
debug/release 選択が使われます。release は NeverC 組み込みの `--strip`
で、不要な静的シンボル名とデバッグメタデータを削除しつつ、ローダー/
動的 ABI に必要な名前は残します。詳細は
[リリースビルド](../../docs/release-builds/README.ja.md)。

## 手動ビルド

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## 機能

- ゲームセキュリティ研究向けヘルパー関数: PID 取得、`/proc/self/maps` 読み取り、RWX メモリ確保、XOR バッファ暗号化
- `dlopen` で `liblog.so` を動的ロード
- `mmap` + `PROT_EXEC` で実行可能メモリを確保するデモ

