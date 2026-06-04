**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md)

# NeverC ロードマップ

このドキュメントは、既存の shellcode コンパイラと組み込みランタイムを超えた NeverC プロジェクトの主要な計画方向を概説します。

---

## 1. 標準ライブラリ (`std`)

NeverC は Go の標準ライブラリをモデルにした包括的な標準ライブラリを提供します——外部依存なしで一般的なシステムプログラミングのニーズをカバーするバッテリー同梱パッケージです。

### 計画中のパッケージ

| パッケージ | 説明 |
|-----------|------|
| `fmt` | フォーマット済み I/O（printf ファミリー + 型安全拡張） |
| `os` | OS 操作：環境変数、プロセス管理、ファイルパーミッション |
| `io` | Reader/Writer インターフェース、バッファ I/O、パイプユーティリティ |
| `fs` | ファイルシステム操作：ウォーク、glob、一時ファイル、アトミック書き込み |
| `net` | TCP/UDP ソケット、DNS 解決、HTTP クライアント/サーバー |
| `net/http` | HTTP/1.1 および HTTP/2 クライアントとサーバー |
| `crypto` | ハッシュ（SHA-256、SHA-512、BLAKE3）、HMAC、AES、ChaCha20、RSA、Ed25519 |
| `encoding` | JSON、Base64、Hex、CSV、バイナリ（リトル/ビッグエンディアン） |
| `sync` | Mutex、RWLock、WaitGroup、Once、アトミック操作 |
| `time` | モノトニック/ウォールクロック、持続時間、タイマー、フォーマット |
| `bytes` | バイトスライス操作、バッファ |
| `math` | 数学定数、基本関数、乱数生成 |
| `sort` | ジェネリックソートと検索 |
| `container` | 連結リスト、ヒープ、リングバッファ |
| `log` | レベル付き構造化ログ |
| `flag` | コマンドラインフラグ解析 |
| `path` | パス操作（POSIX および Windows） |
| `regexp` | 正規表現マッチング（RE2 構文） |
| `compress` | gzip、zlib、zstd、lz4 |
| `hash` | CRC32、CRC64、FNV、xxHash |
| `unicode` | Unicode テーブル、ケースフォールディング、UTF-8/UTF-16 変換 |

### 設計原則

- **純粋な C23** — すべてのパッケージが標準 NeverC/C23 としてコンパイル；隠れた C++ やプラットフォーム固有アセンブリなし
- **外部依存ゼロ** — 標準ライブラリは既存の `string` や `mimalloc` 組み込みと同様に、LLVM bitcode としてコンパイラに埋め込み
- **クロスプラットフォーム** — すべてのパッケージが macOS、Linux、Windows（x86_64 / AArch64）で動作
- **Shellcode 互換** — フリースタンディングモードで意味のあるパッケージ（例：`crypto`、`encoding`、`bytes`）は `-fshellcode` で動作

---

## 2. Obfuscation Plugin Suite (`neverc-obfuscation`)

NeverC will ship a first-party suite of code obfuscation plugins — reference implementations that demonstrate the Plugin API's full capabilities while providing production-grade code protection out of the box.

### Planned Plugins

| Plugin | Hook Point | Description |
|--------|-----------|-------------|
| Junk Code Insertion | `RunAfterFinalMIR` | Insert semantically dead but syntactically valid instruction sequences between real basic blocks |
| Opaque Predicates | `RunBeforePreEmit` | Insert always-true/always-false branches guarded by number-theoretic invariants; adds dead paths that confuse analysis |
| Control Flow Flattening | `RunAfterStackify` | Scatter basic blocks into a switch-dispatched loop; destroys natural CFG structure for decompilers |
| Anti-Tamper | `RunPostFinalize` | Embed self-integrity checks (CRC/hash of code sections) that trigger failure on patching |
| Polymorphic Engine | `RunPostExtract` | Seed-based output variation — each compilation produces functionally equivalent but structurally different code; defeats signature-based detection |
| MBA (Mixed Boolean Arithmetic) | `RunAfterInlining` | Replace arithmetic/boolean expressions with equivalent but opaque MBA forms (e.g., `x + y` → `(x ^ y) + 2 * (x & y)` chains); resists symbolic execution |
| VM (Code Virtualization) | `RunAfterFinalIR` | Convert functions into custom bytecode executed by an embedded interpreter; defeats static disassembly and signature matching |

### Design Principles

- **Pure Plugin API** — every obfuscation ships as a `.dll` / `.so` / `.dylib` plugin; no compiler fork required
- **Composable** — plugins stack: apply MBA first, then flatten, then virtualize — each pass is independent
- **Configurable** — per-function annotations (`__attribute__((obfuscate("vm")))`) to selectively protect hot paths without whole-program overhead
- **Auditable** — each plugin logs its transformations for security review; before/after IR diff output available via `-fshellcode-dump-ir`
- **Shellcode-compatible** — all plugins work in `-fshellcode` mode; generated code remains position-independent

---

## 3. UI コンポーネントライブラリ (`neverc-ui`)

NeverC は Qt に着想を得たクロスプラットフォーム UI コンポーネントライブラリを提供します——ただし HTML/JS/CSS フロントエンドレンダリングエンジンを採用し、AI によるインターフェース設計に本質的に適合します。

### 目標

- **コンポーネントベースアーキテクチャ** — ウィンドウ、ボタン、テキスト入力、リスト、ツリー、テーブル、メニュー、ダイアログ、タブ、レイアウトコンテナを C のファーストクラス型として提供
- **HTML/JS/CSS レンダラー** — 組み込み軽量ブラウザエンジンで UI をレンダリング；開発者は C ロジックを書き、ビジュアル層は標準 Web 技術を使用
- **ドラッグ＆ドロップ ビジュアルデザイナー** — NeverC 互換の C コードを生成する GUI ビルダー；レイアウトコードの手書き不要で迅速なプロトタイピング
- **AI ネイティブ設計ワークフロー** — LLM は C ビジネスロジックと HTML/CSS レイアウトをワンパスで生成可能；ビジュアル層は世界で最も広く理解された UI 言語を使用
- **ネイティブルック＆フィール** — CSS 変数とシステムフォント/カラー検出によるプラットフォーム適応テーマ（macOS、Windows、Linux）
- **軽量組み込み** — レンダラーは組み込みランタイムとして提供（`string` / `mimalloc` と同様）；Electron 級のオーバーヘッドなし
- **イベントシステム** — ユーザーインタラクション（クリック、入力、リサイズ、ドラッグ、キーボード、カスタムイベント）用の C コールバック関数
- **データバインディング** — C 構造体と UI 状態間の宣言的バインディング；変更は自動伝播
- **カスタムレンダリング** — ゲーム UI、データ可視化、カスタムウィジェット用の raw canvas/WebGL へのエスケープハッチ

### なぜ C の UI ライブラリに HTML/CSS を？

- すべての AI モデルはすでに HTML/CSS を知っている——UI コード生成に専門的なトレーニング不要
- Web 技術は最も実戦テスト済みのレイアウトシステム；flexbox、grid、テキストレンダリングの再発明不要
- セキュリティ研究ツール（ダッシュボード、ヘックスビューア、パケットインスペクタ）はプロプライエタリなウィジェット API を学ばずにリッチなスタイルインターフェースの恩恵を受ける
- ビジュアルデザイナーがエクスポートする HTML テンプレートは NeverC アプリとスタンドアロンブラウザの両方で動作し、高速な反復が可能

---

## 4. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **Shellcode mode** — syntax-aware features for `-fshellcode` pipelines: bad-byte highlighting, shellcode size display, target-specific completions
- **Plugin API integration** — plugin hook point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual shellcode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, shellcode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want shellcode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---


## 5. EVM スマートコントラクトバックエンド

NeverC は C ソースコードを EVM（Ethereum Virtual Machine）バイトコードにコンパイルすることをサポートします——開発者が Solidity の代わりに C でスマートコントラクトを書けるようになります。

### 目標

- **新 LLVM バックエンドターゲット** — `evm` ターゲットトリプル（例：`neverc --target=evm hello.c -o contract.bin`）
- **ABI 互換** — Solidity 互換の ABI 記述子を生成し、既存の Ethereum ツール（Hardhat、Foundry、ethers.js）と連携
- **ストレージレイアウト** — C 構造体を決定的レイアウトで EVM ストレージスロットにマッピング
- **組み込み EVM プリミティブ** — `msg.sender`、`msg.value`、`block.number`、`tx.origin` を組み込み変数またはイントリンシクスとして提供
- **payable / view / pure 修飾子** — Solidity の可視性セマンティクスにマッピングする関数属性
- **イベント発行** — アノテーション付き関数呼び出しから `LOG0`–`LOG4` オペコードを生成
- **Gas 最適化** — gas コストを最小化する IR パス（スタックスケジューリング、定数畳み込み、デッドストレージ除去）
- **revert / require** — カスタムエラーメッセージ付きエラー処理プリミティブ

### なぜ C で EVM を？

- Solidity の構文は JavaScript 開発者には親しみやすいが、システムプログラマには馴染みがない；C は普遍的
- NeverC の既存 IR 最適化パイプラインは多くのケースで `solc` よりコンパクトなバイトコードを生成可能
- セキュリティ研究者はすでに C で考える——C コントラクトに対する監査ツールや fuzzer を C で書くのは自然
- プラグイン API によりコンパイル時にカスタム gas 分析や脆弱性検出パスが可能

---

## 6. Solana eBPF バックエンド

NeverC は C ソースコードを Solana の eBPF バイトコードにコンパイルすることをサポートします——C でのオンチェーンプログラム開発を実現します。

### 目標

- **eBPF ターゲット** — `sbf`（Solana BPF）ターゲットトリプル（例：`neverc --target=sbf-solana hello.c -o program.so`）
- **Solana ランタイムバインディング** — Solana システムコール用組み込みヘッダー：`sol_invoke_signed`、`sol_log`、`sol_memcpy`、アカウント情報構造体
- **アカウントモデル** — C 構造体で Solana アカウントデータをオーバーレイ、自動シリアライズ/デシリアライズ
- **CPI（クロスプログラム呼び出し）** — 他のオンチェーンプログラムを呼び出すための型安全ラッパー
- **PDA（プログラム派生アドレス）** — PDA 導出と検証の組み込み関数
- **計算バジェット認識** — 推定計算ユニットがプログラム制限を超えた場合にコンパイラ警告
- **Anchor 互換** — Anchor ベースのフロントエンドとの相互運用のためのオプション IDL 生成

### なぜ C で Solana を？

- Solana のランタイムは eBPF を実行する——C は BPF ターゲットの最も自然なソース言語
- 既存の C ベース BPF ツールチェーン（clang + solana-bpf）はセットアップが複雑；NeverC はすべてを単一バイナリにバンドル
- パフォーマンスクリティカルなプログラムは C のゼロオーバーヘッド抽象と NeverC の最適化パスの恩恵を受ける
- shellcode コンパイル経験（位置独立、最小ランタイムコード）はオンチェーンプログラムの制約に直接マッピング

---

## タイムライン

これらの機能は研究・設計段階にあります。具体的なリリース日は未定です。進捗はこのドキュメントで更新され、プロジェクトのリリースページで発表されます。

| 機能 | ステータス |
|------|-----------|
| 標準ライブラリ (`std`) | 研究 / 設計 |
| Obfuscation Plugin Suite (`neverc-obfuscation`) | Research / Design |
| UI コンポーネントライブラリ (`neverc-ui`) | 研究 / 設計 |
| IDE & 言語ツール (`neverc-ide`) | 研究 / 設計 |
| EVM スマートコントラクトバックエンド | 研究 / 設計 |
| Solana eBPF バックエンド | 研究 / 設計 |
