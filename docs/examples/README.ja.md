**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../docs/i18n/README.ja.md)

# NeverC サンプル

NeverC のクロスプラットフォームコンパイル機能を示すビルド可能なサンプル集。すべて macOS / Linux からクロスコンパイル可能 — Windows ビルド環境不要。

---

## サンプル一覧

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [Windows カーネルドライバ](../../examples/windows-driver/README.ja.md) | 最小 WDM カーネルドライバ | macOS/Linux から `.sys` をクロスコンパイル、自動 LTO、内蔵リンカ |
| [Windows ドライバ + CET](../../examples/windows-driver-cet/README.ja.md) | Intel CET シャドウスタック付きカーネルドライバ | CET 対応カーネルコード、`/guard:ehcont` |
| [Windows ドライバ + 浮動小数点](../../examples/windows-driver-float/README.ja.md) | 浮動小数点/SIMD 付きカーネルドライバ | カーネルモード安全浮動小数点 |

### Linux

| サンプル | 説明 | 主要機能 |
|---------|------|--------|
| [Linux Hello World](../../examples/linux-hello/README.ja.md) | 最小限の C プログラム | macOS/Windows からのクロスコンパイル |
| [Linux POSIX](../../examples/linux-posix/README.ja.md) | POSIX システムプログラミング | pthreads、mmap、pipe、シグナル |
| [Linux 静的](../../examples/linux-static/README.ja.md) | 完全静的バイナリ | `-static` リンク |
| [Linux ネットワーク](../../examples/linux-network/README.ja.md) | TCP ソケットデモ | クライアント/サーバー |
| [Linux 数学 + zlib](../../examples/linux-math/README.ja.md) | 数学 + 圧縮 | 三角関数、zlib、CRC32 |

### Android

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [Android ELF](../../examples/android-elf/README.ja.md) | root 端末向けネイティブ ARM64 バイナリ | Android クロスコンパイル、dlopen/liblog、/proc 情報、root 検出 |

---

## クイックスタート

```bash
cd examples/<サンプル名>
make
```

コンパイラパス指定：`make NEVERC=/path/to/neverc`

すべてのサンプルは **neverc** をコンパイラとして使用し、内蔵リンカ経由で Windows PE バイナリ（`.sys`）を生成します。
