**言語**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**LLVM 上に構築されたセキュリティ研究向け C23 コンパイラ**

統合リンカ · Shellcode パイプライン · 組み込みランタイム（`string` · mimalloc）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#機能)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#windows-へのクロスコンパイル)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#機能)

[ドキュメント索引](../README.ja.md) · [Shellcode ガイド](../shellcode-compiler/README.ja.md) · [組み込み文字列](../builtins/string/README.ja.md)

</div>

---

> **注：** GitHub はリポジトリトップに常に英語の `README.md` を表示します（ブラウザ言語の自動切替なし）。上の言語リンクを使用し、[ドキュメント](../README.ja.md)・[shellcode ガイド](../shellcode-compiler/README.ja.md) ではページ内の言語欄とパンくずで同じ言語を維持してください。

## 概要

NeverC は標準 C をホスト型バイナリ、フリースタンディング実行ファイル、位置独立 shellcode にコンパイルします——すべて単一ツールチェーンから。**x86_64** と **AArch64**（リトルエンディアンのみ）をターゲットとします。

## 機能

- **[Shellcode コンパイラ](../shellcode-compiler/README.ja.md)** — 多段 IR/MIR パイプライン、クロスプラットフォーム抽出、インポート/システムコール低減、カーネルモード、バッドバイト監査、プラグインアーキテクチャ
- **統合リンカ** — 単一バイナリで COFF・ELF・Mach-O；外部 `ld` / `link.exe` 不要
- **クロスコンパイル** — macOS/Linux からバンドル MSVC SDK で Windows PE をビルド
- **[組み込みランタイム](../builtins/README.ja.md)** — コンパイラ埋め込みの LLVM bitcode ランタイム：[`string`](../builtins/string/README.ja.md)（値セマンティクス文字列、自動メモリ管理）と [`mimalloc`](../builtins/mimalloc/README.ja.md)（透過的高性能アロケータオーバーライド）
- **スリム LLVM ビルド** — x86_64 / AArch64 バックエンドのみ；C++/ObjC/OpenMP 経路を除去

## クイックサンプル

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());
    return 0;
}
```

> **注：** 組み込み **`string`** 型は通常のホスト型バイナリでは **`-fbuiltin-string`** が必要です。**`-fshellcode`** では自動的に有効になります。

```bash
# macOS arm64
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# macOS x86_64
neverc -fshellcode -target x86_64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin

# Linux arm64
neverc -fshellcode -target aarch64-linux-gnu -mshellcode-syscall hello.c -o hello.bin

# Android arm64
neverc -fshellcode -target aarch64-linux-android -mshellcode-syscall hello.c -o hello.bin

# Android x86_64
neverc -fshellcode -target x86_64-linux-android -mshellcode-syscall hello.c -o hello.bin

# Windows x86_64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin

# Windows arm64
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

詳細な設計、プラットフォームマトリクス、CLI リファレンス、例は **[ドキュメント索引](../README.ja.md)** を参照。

## ビルド

### 要件

- CMake 3.20+
- Ninja
- C++17 対応ホストコンパイラ（GCC、Clang、MSVC）

### 設定

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### ビルド

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` は自動検出され、存在すれば有効化されます。

### テスト

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### 検証

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Windows へのクロスコンパイル

[xwin](https://github.com/Jake-Shadle/xwin) SDK splat を `build-neverc/sdk/msvc/` に配置。

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows shellcode（`-fshellcode`、PEB インポート解決など）は [shellcode コンパイラドキュメント](../shellcode-compiler/README.ja.md) を参照。

## ライセンス

[AGPL-3.0](../../LICENSE)

LLVM コンポーネントは [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) ライセンスを維持します。
