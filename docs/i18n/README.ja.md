**言語**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**AI フレンドリーなセキュリティ研究向け C23 コンパイラ — LLVM 上に構築**

統合リンカ · DynCode パイプライン · 組み込みランタイム（`string` · `mimalloc` · `xorstr` · `strhash`）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#機能)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#機能)

[ドキュメント索引](../README.ja.md) · [DynCode ガイド](../dyncode-compiler/README.ja.md) · [組み込みランタイム](../builtins/README.ja.md) · [プラグイン API](../plugin-api/README.ja.md) · [ロードマップ](../roadmap/README.ja.md)

</div>

---

> **注：** GitHub はリポジトリトップに常に英語の `README.md` を表示します（ブラウザ言語の自動切替なし）。上の言語リンクを使用し、[ドキュメント](../README.ja.md)・[dyncode ガイド](../dyncode-compiler/README.ja.md) ではページ内の言語欄とパンくずで同じ言語を維持してください。

## 概要

NeverC は標準 C をホスト型バイナリ、フリースタンディング実行ファイル、位置独立 dyncode にコンパイルします——すべて単一ツールチェーンから。**x86_64** と **AArch64**（リトルエンディアンのみ）をターゲットとします。将来のリリースでは **EVM**（Ethereum スマートコントラクト）と **Solana eBPF**（オンチェーンプログラム）をコンパイルターゲットとして追加予定です。

## なぜ NeverC？

C は既に最もシンプルなシステムプログラミング言語です。NeverC はそれをさらにシンプルにします：

- **純粋な C23、それだけ** — テンプレートなし、RAII なし、演算子オーバーロードなし、隠れた制御フローなし。読んだ通りに実行されます。
- **組み込み `string`** — 値セマンティクスの文字列型。`+`、`==`、`.starts_with()` と自動解放に対応——C++ 不要。
- **例外なし** — エラー処理は常に明示的。スタック巻き戻しなし、パフォーマンスの予測不能な低下なし。
- **単一バイナリ** — コンパイラ + リンカ + ランタイムが一つの実行ファイルに。外部依存ゼロ。
- **LLM フレンドリー** — 最小限の文法と決定的なセマンティクスにより、AI が生成する NeverC コードは C++ より正しくコンパイルされやすい。
- **真のクロスコンパイル** — macOS や Linux から Windows PE、Linux ELF、macOS Mach-O、Android ELF、dyncode をビルド——VM 不要、デュアルブート不要、SDK 探し不要。各プラットフォーム SDK はコンパイラに内蔵。
- **ゼロフリクションで拡張可能** — たった1つの C ヘッダーと 130 の名前付きコンパイルフェーズで、IR 最適化から最終バイナリ出力まであらゆる段階に介入する[コンパイラプラグイン](../plugin-api/README.ja.md)が書ける——LLVM の知識不要。
- **セキュリティ研究を組み込み済み** — DynCode コンパイル、コンパイル時文字列暗号化、クロスプラットフォーム PE 生成がコンパイラにネイティブ統合——外部スクリプトによる後付けではありません。

## 機能

- **[DynCode コンパイラ](../dyncode-compiler/README.ja.md)** — 多段 IR/MIR パイプライン、クロスプラットフォーム抽出、インポート/システムコール低減、カーネルモード、バッドバイト監査、プラグインアーキテクチャ
- **統合リンカ** — 単一バイナリで COFF・ELF・Mach-O；外部 `ld` / `link.exe` 不要
- **クロスコンパイル** — 任意のホストから Windows PE、Linux ELF、macOS Mach-O、Android ELF をビルド（各プラットフォーム SDK 内蔵）
- **[組み込みランタイム](../builtins/README.ja.md)** — コンパイラ埋め込みの LLVM bitcode ランタイム：[`string`](../builtins/string/README.ja.md)（値セマンティクス文字列、自動メモリ管理）、[`mimalloc`](../builtins/mimalloc/README.ja.md)（透過的高性能アロケータオーバーライド、カーネルと freestanding を除きデフォルト有効）、[`xorstr`](../builtins/xorstr/README.ja.md)（コンパイル時文字列暗号化、シグネチャ対策の復号）、[`strhash`](../builtins/strhash/README.ja.md)（コンパイル時文字列ハッシュ、実行時と同一アルゴリズム）
- **[プラグイン API](../plugin-api/README.ja.md)** — アウトオブツリープラグイン用純粋 C ABI；単一ヘッダー SDK、LLVM/CRT 依存ゼロ、ドライバー・プリプロセッサー・AST・IR・MIR・MC・オブジェクト・リンク・LTO・dyncode の各フェーズを網羅
- **[`.nc` 拡張子](../nc-extension/README.ja.md)** — `.nc` ファイル拡張子ですべての NeverC 機能（`string`、Rust スタイル整数型）を自動有効化、追加フラグ不要
- **スリム LLVM ビルド** — x86_64 / AArch64 バックエンドのみ；C++/ObjC/OpenMP 経路を除去

## クイックサンプル

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **注：** 組み込み **`string`** 型は `.c` ファイルでは **`-fbuiltin-string`** が必要です。[**`.nc` ファイル**](../nc-extension/README.ja.md) または **`-fdyncode`** モードでは自動的に有効になります。

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

詳細な設計、プラットフォームマトリクス、CLI リファレンス、例は **[ドキュメント索引](../README.ja.md)** を参照。ビルド可能なサンプルは **[examples](../examples/README.ja.md)** を参照。

## インストール

**Linux x64/arm64** および **macOS arm64** では、次の 1 コマンドで最新 release をインストールできます：

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

インストーラはプラットフォーム向け release アーカイブをダウンロードし、`SHA256SUMS` で検証して `~/.neverc` に展開し、shell の `PATH` 先頭に `~/.neverc/bin` を追加します。

特定バージョンを指定する場合：

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

インストール確認：

```bash
neverc --version
neverc hello.c -o hello -fbuiltin-string
```

**Windows x64/arm64** は [GitHub Releases](https://github.com/NeverSight/NeverC/releases) から手動でダウンロードしてください。macOS arm64 バイナリは Apple Developer ID 署名済みで公証されています。

任意のインストール環境変数：

| 変数 | 用途 |
|------|------|
| `NEVERC_INSTALL_DIR` | インストール先（既定：`~/.neverc`） |
| `NEVERC_VERSION` | Release タグ（例：`v3389.1.2`、既定：最新） |
| `NEVERC_NO_MODIFY_PATH=1` | shell 設定ファイルを変更しない |

クロスコンパイル用 sysroot（Windows SDK、Linux sysroot など）は、コンパイラが `PATH` に入った後に必要に応じてインストールします：

```bash
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

## ソースからビルド

ビルド要件、ビルドコマンド、Windows クロスコンパイル、PATH 設定、release インストールとツリー内ビルドの切り替えについては **[ローカル開発](../local-dev/README.ja.md)** を参照してください。

## コントリビューション

NeverC は**設計上 C のみ**（C23）です。C++ / Objective-C / CUDA などの言語フロントエンドは
スコープ外であり、それらを追加する Pull Request はクローズされます。C++ 向けの LLVM
ツールチェーンが必要な場合は [llvm-msvc](https://github.com/backengineering/llvm-msvc) を
検討してください。

言語・ABI・ランタイムに関する大規模な変更は、Pull Request の前にまず issue を開いて
スコープを相談してください。

既定の開発ブランチは **`dev`** です。作業前に clone して checkout し、Pull Request は `dev` 向けに送ってください。

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## ライセンス

[AGPL-3.0](../../LICENSE)

LLVM コンポーネントは [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) ライセンスを維持します。
