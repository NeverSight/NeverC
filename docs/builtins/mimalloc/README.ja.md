**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 組み込みランタイムシステム](../README.ja.md)

# 組み込み `mimalloc` アロケータ

## 概要

NeverC は [mimalloc](https://github.com/microsoft/mimalloc)（Microsoft の高性能汎用メモリアロケータ）を LLVM bitcode マージにより、コンパイル済みバイナリに直接埋め込むことができます。有効にすると、`malloc`、`free`、`calloc`、`realloc` がコンパイル時に mimalloc の実装に透過的に置換されます。

**有効化：**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## 使い方

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # 基本
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # `string` と組み合わせ
neverc -fno-builtin-mimalloc main.c -o main                    # 無効化
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("mimalloc アロケータを使用中\n");
#endif
```

---

## プラットフォームサポート

| プラットフォーム | Triple | ステータス |
|----------------|--------|----------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | サポート |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | サポート |
| Android | `aarch64-linux-android` | サポート |
| macOS x86_64 | `x86_64-apple-macosx` | サポート |
| macOS AArch64 | `arm64-apple-macosx` | サポート |
| iOS | `arm64-apple-ios` | サポート |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | サポート |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | サポート |

---

## 自動抑制

| フラグ / モード | 理由 |
|----------------|------|
| `-fno-builtin` | CRT 関数のオーバーライドシナリオなし |
| `-mkernel` | カーネルモードにユーザー空間ヒープなし |
| `-fshellcode-mode` | HeapArenaPass で代替（arena + OS フォールバック） |
| `-ffreestanding` | オーバーライドする libc なし |

---

## ブートストラップビルド

```bash
ninja neverc                         # ステージ 1：空の bitcode プレースホルダー
ninja neverc-bootstrap-mimalloc-bc   # ステージ 2：OS 別 bitcode をコンパイル
ninja neverc                         # ステージ 3：実際の bitcode を埋め込み
```

---

## アーキテクチャ

mimalloc は LLVM bitcode としてコンパイラバイナリに埋め込まれます。ユーザーコンパイル時に Module Pass が最適化パイプライン前に bitcode をユーザー IR にマージします。OS ごとに別々にコンパイル（Linux `mmap`、macOS `vm_allocate`、Windows `VirtualAlloc`）され、マージ時に target triple に基づいて選択されます。**ホールアーカイブ**セマンティクス — 全関数がリンクされます。オーバーライドエントリは外部リンケージを維持し、内部ヘルパーは内部化されます。

---

## ファイル構成

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## コンパイラフラグリファレンス

| フラグ | 説明 |
|--------|------|
| `-fbuiltin-mimalloc` | `mimalloc` オーバーライド注入を有効化（ホステッドビルドではデフォルトオン） |
| `-fno-builtin-mimalloc` | `mimalloc` 注入を明示的に無効化 |

| マクロ | 値 | 定義条件 |
|--------|----|---------| 
| `__NEVERC_MIMALLOC__` | `1` | `-fbuiltin-mimalloc` がアクティブな場合 |
