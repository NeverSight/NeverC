**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC ドキュメント](../README.ja.md)

# NeverC 組み込みランタイムシステム

NeverC はオプトイン方式の組み込みランタイムで標準 C を拡張します。これらのランタイムは LLVM bitcode としてコンパイラバイナリに直接埋め込まれています。コンパイラフラグで有効にすると、対応するランタイムがコンパイル時にユーザーの IR にマージされます——外部ヘッダー、ライブラリ、リンク時の依存関係は不要です。

## 利用可能な組み込み機能

| 組み込み | フラグ | デフォルト | 説明 |
|---------|--------|----------|------|
| [**`string`**](string/README.ja.md) | `-fbuiltin-string` | オフ | 値セマンティクスの文字列型。ドットコールメソッド、自動メモリ管理、ネイティブ UTF-8 対応 |
| [**`mimalloc`**](mimalloc/README.ja.md) | `-fbuiltin-mimalloc` | **オン** | 高性能メモリアロケータ。`malloc`/`free`/`calloc`/`realloc` を透過的に置換 |

両方の組み込み機能はデフォルトでオフであり、明示的なオプトインが必要です。組み合わせて使用可能：

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## アーキテクチャ概要

すべての組み込み機能は同じ4層アーキテクチャを共有します：

1. **言語オプションとドライバフラグ** — `LangOptions.def` で `LangOption` を定義
2. **Foundation API** — `getEmbeddedBitcode()` と `isSupported()` を提供
3. **CMake ブートストラップ** — 2段階ブートストラップで bitcode を生成
4. **IR マージパス** — `PipelineStartEP` で bitcode をユーザーモジュールにマージ

---

## 組み込み機能間の設計の違い

| 側面 | `string` | `mimalloc` |
|------|----------|------------|
| **マージ戦略** | オンデマンド（BFS コールグラフ、未使用を削除） | ホールアーカイブ（全シンボル保持） |
| **プラットフォーム bitcode** | 単一（アーキテクチャ非依存） | OS 別（Linux / Darwin / Windows） |
| **シンボル処理** | すべて内部化 | オーバーライドエントリは外部リンケージ維持 |
| **プリプロセッサマクロ** | *（なし）* | `__NEVERC_MIMALLOC__` |
| **シェルコードモード** | 自動有効化、アリーナ書き換え | 抑制（HeapArenaPass がヒープ処理） |
| **最適化レベル** | `-O0`（bitcode コンパイル） | `-O2`（性能重要なアロケータ） |
| **DCE** | マージ前プルーニング + マージ後マーク＆スイープ | DCE なし（ホールアーカイブセマンティクス） |

---

## 安全インターロック

| 条件 | 効果 | 理由 |
|------|------|------|
| `-fno-builtin` | mimalloc を抑制 | CRT オーバーライドのシナリオなし |
| `-mkernel` | mimalloc を抑制 | カーネルにユーザー空間ヒープなし |
| `-fshellcode-mode` | mimalloc を抑制 | HeapArenaPass で代替（アリーナベース） |
| `-ffreestanding` | mimalloc を抑制 | オーバーライドする libc なし |

`string` 組み込みには独自の抑制ロジックがあります（シェルコードパイプライン内でアリーナ書き換えがヒープ割り当てを置換）。

### HeapArenaPass（シェルコードヒープ割り当て）

`-fshellcode-mode` がアクティブな場合、`mimalloc` は抑制されますが、`malloc`/`free`/`calloc`/`realloc` 呼び出しは `HeapArenaPass` によって自動的に書き換えられます（デフォルト有効）。このパスはハイブリッド戦略を使用します：

- **小さな割り当て（≤ 64 KB）**：`string` 組み込みランタイムと共有するスタック上アリーナから割り当て（バンプアロケータ + フリーリスト再利用）。
- **大きな割り当て（> 64 KB）またはアリーナ OOM**：OS アロケータにフォールバック：
  - **Windows**：`malloc`/`free` を PEB walk 経由で `msvcrt.dll` から解決（`-mshellcode-win-peb-import`）。
  - **Linux / macOS / Android**：`mmap`/`munmap` をネイティブシステムコールとしてインライン化（`-mshellcode-syscall`）。
  - **インポートパス未有効**：アリーナのみ；OOM は `NULL` を返す。

ドライバフラグで制御：

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass オン（デフォルト）
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass オフ（従来の動作）
```

---

## プリプロセッサマクロ

組み込み機能がアクティブな場合、対応するプリプロセッサマクロが定義されます：

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc がアクティブ — malloc/free が透過的にオーバーライド
#endif
```

---

## ファイル構成

```
neverc/
├── include/neverc/Foundation/
│   ├── LangOpts/LangOptions.def          # LANGOPT 宣言
│   └── Builtin/
│       ├── BuiltinString.h               # string API
│       ├── BuiltinMimalloc.h             # mimalloc API
│       └── ...
│
├── lib/Foundation/
│   ├── CMakeLists.txt                    # 全組み込みのブートストラップターゲット
│   └── Builtin/
│       ├── BuiltinString.cpp             # string bitcode 埋め込み
│       ├── BuiltinMimalloc.cpp           # mimalloc OS別 bitcode 埋め込み
│       ├── bin2c.py                      # .bc → C ヘッダー変換器（共有）
│       ├── gen_string_runtime.py         # string ソースジェネレータ
│       └── gen_mimalloc_source.py        # mimalloc ソースジェネレータ
│
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp                   # PipelineStartEP 登録
│   ├── StringRuntimeLinker.{h,cpp}       # string IR マージパス
│   └── MimallocRuntimeLinker.{h,cpp}     # mimalloc IR マージパス
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                        # addNeverCFeatureFlags()
│
└── lib/Compiler/Preprocessor/
    └── InitPreprocessor.cpp              # __NEVERC_MIMALLOC__ マクロ
```

---

## 新しい組み込み機能の追加

1. `LangOptions.def` に `LANGOPT` を追加
2. `Options.td.h` にドライバフラグを追加
3. Foundation API を作成（`BuiltinFoo.h` + `.cpp`）
4. ソースジェネレータを作成
5. CMake ブートストラップターゲットを追加
6. IR パスを作成し `PipelineStartEP` に登録
7. プリプロセッサマクロを定義
8. 安全チェックを追加
9. テストを追加
10. ドキュメントと i18n 翻訳を追加
