**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC ビルトインランタイムシステム](../README.ja.md)

# コンパイル時文字列ハッシュ (`strhash`)

## 概要

NeverC はプレーン C 向けにコンパイル時／実行時の文字列ハッシュを提供します。API 名やコマンドトークンなどを整数ハッシュ比較で高速に振り分ける用途向けで、バイナリに平文の対照表を残す必要がありません。

- **レイヤー 1 — 明示的コンパイル時マクロ**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` は Sema で整数定数に畳み込み
- **レイヤー 2 — 実行時 + 任意 IR 畳み込み**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`。`-fstrhash-fold` で文字列リテラル引数の実行時呼び出しを定数化

両レイヤーは `-fstrhash-algo` で選んだ同一アルゴリズム（既定: FNV-1a 64-bit）を共有し、コンパイル時と実行時のハッシュが常に一致します。

---

## クイックスタート

### レイヤー 1: コンパイル時マクロ

```c
#include <neverc/strhash/strhash.h>

static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");

int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

### レイヤー 2: 自動ディスパッチ + 畳み込み

```c
#include <neverc/strhash/strhash.h>
uint64_t h = NC_STRHASH_AUTO(name);
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## レイヤー 1: `NC_STRHASH` / `NEVERC_STRHASH`

すべての文字列リテラル種別（通常、UTF-8、ワイド、UTF-16、UTF-32）をサポート。非リテラル引数はコンパイルエラー。変数には `NC_STRHASH_AUTO` または `neverc_strhash_rt` を使用。

### アルゴリズム

| フラグ値 | 説明 | 既定 |
|----------|------|------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **はい** |
| `xxhash64` | XXHash64（seed 0） | |

---

## レイヤー 2: `-fstrhash-fold`

`neverc_fnv_sum32a` / `neverc_fnv_sum64a` / `neverc_xxhash64` への定数文字列引数呼び出しを整数定数に畳み込みます。

| フラグ | 説明 | 既定 |
|--------|------|------|
| `-fstrhash-fold` | IR 畳み込みを有効化 | オフ |
| `-fno-strhash-fold` | 無効化 | — |
| `-fstrhash-algo=<algo>` | アルゴリズム選択 | `fnv64a` |

---

## カスタム実行時ハッシュ

```c
#define NC_STRHASH_HASH_FN(data, len) my_hash(data, len)
#include <neverc/strhash/strhash.h>
```

実行時パスのみ上書き。`NC_STRHASH()` は引き続き builtin / `-fstrhash-algo` を使用。

---

## コンパイラフラグリファレンス

| フラグ | 説明 |
|--------|------|
| `-fstrhash-algo=fnv32a` | FNV-1a 32-bit を使用 |
| `-fstrhash-algo=fnv64a` | FNV-1a 64-bit を使用（既定） |
| `-fstrhash-algo=xxhash64` | XXHash64（seed 0）を使用 |
| `-fstrhash-fold` | 定数文字列引数の実行時ハッシュ呼び出しを畳み込み |
| `-fno-strhash-fold` | IR 畳み込みを無効化 |
