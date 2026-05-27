**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC ビルトインランタイムシステム](../README.ja.md)

# コンパイル時文字列暗号化 (`xorstr`)

## 概要

NeverC は C コード向けの二層コンパイル時文字列暗号化を提供します。API 名、レジストリパス、デバッグメッセージなどの機密文字列がコンパイル済みバイナリに平文で残らないよう設計されています。

- **レイヤー 1 — 明示的マクロ**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` で文字列単位の精密制御
- **レイヤー 2 — 自動 IR パス**: `-fencrypt-call-strings` で関数呼び出しの全文字列引数を自動暗号化

両レイヤーともスタック割り当てバッファを使用（ヒープ不使用）、XOR 命令を使用しない復号アルゴリズム（アンチシグネチャ）、関数リターン前の volatile `memset` クリアを実装しています。

---

## クイックスタート

### レイヤー 1: 明示的マクロ

```c
#include <neverc/xorstr.h>

FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### レイヤー 2: 自動暗号化

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## レイヤー 1: `NC_XORSTR` / `NEVERC_XORSTR` マクロ

すべての文字列リテラル種別（通常、UTF-8、ワイド、UTF-16、UTF-32）をサポート。非リテラル引数はコンパイルエラーになります。

### アンチシグネチャ復号

XOR 命令を完全に回避し、数学的に等価な `a + b − 2 × (a & b)` を使用します。

---

## レイヤー 2: `-fencrypt-call-strings`

| フラグ | 説明 | デフォルト |
|--------|------|-----------|
| `-fencrypt-call-strings` | 自動暗号化を有効化 | オフ |
| `-fno-encrypt-call-strings` | 無効化 | — |
| `-fencrypt-call-strings-max-len=N` | N バイトを超える文字列をスキップ | 1024 |

---

## `.encrypt()` との比較

| 側面 | `NC_XORSTR()` | `.encrypt()` |
|------|---------------|--------------|
| **利用可能性** | 純 C（ヘッダー経由） | NeverC 構文拡張のみ |
| **メモリ** | スタック（`alloca`） | ヒープ（`NEVERC_STRING_ALLOC`） |
| **戻り値型** | `const char*` | `string`（値型） |
| **用途** | Win32 API、FFI | 汎用文字列操作 |

---

## コンパイラフラグリファレンス

| フラグ | 説明 |
|--------|------|
| `-fencrypt-call-strings` | 関数呼び出し引数の自動文字列暗号化を有効化 |
| `-fno-encrypt-call-strings` | 自動暗号化を無効化 |
| `-fencrypt-call-strings-max-len=N` | 自動暗号化の最大バイト長（デフォルト: 1024） |
| `-fstring-encrypt-key=0xHEX` | XOR キーシードを上書き |
