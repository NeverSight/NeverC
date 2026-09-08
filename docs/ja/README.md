**言語**: [English](../README.md) | [简体中文](../zh-CN/README.md) | [繁體中文](../zh-TW/README.md) | [日本語](README.md) | [한국어](../ko/README.md) | [Français](../fr/README.md) | [Deutsch](../de/README.md) | [Español](../es/README.md) | [Italiano](../it/README.md) | [Русский](../ru/README.md) | [العربية](../ar/README.md)

[← NeverC プロジェクト](project.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# NeverC ドキュメント

各サブシステムの設計ノート、APIリファレンス、ガイド。

---

## DynCode コンパイラ

DynCode コンパイルパイプラインは NeverC の主要な研究領域です。アーキテクチャ、CLI オプション、プラットフォームマトリクス、例は次を参照：

**[DynCode コンパイラ →](dyncode-compiler.md)**

| ドキュメント | 説明 |
|-------------|------|
| [README](dyncode-compiler.md) | 概要、クイックスタート、サポートターゲット |
| [Pipeline & PIC](dyncode-compiler-pipeline-and-pic.md) | IR → オブジェクト → 抽出の設計 |
| [IR Pass Design](dyncode-compiler-ir-pass-design.md) | 各 IR パスの設計意図 |
| [MIR Pass Design](dyncode-compiler-mir-pass-design.md) | バックエンド MIR パス |
| [Kernel-Mode DynCode](dyncode-compiler-kernel-mode-dyncode.md) | Ring-0 コンパイル |
| [Cross-Platform Architecture](dyncode-compiler-cross-platform-architecture.md) | `TargetDesc` と抽出器 |
| [Platform Extension Guide](dyncode-compiler-platform-extension-guide.md) | 新プラットフォームの追加 |
| [ARM64 Assembly Tutorial](dyncode-compiler-arm64-assembly-tutorial.md) | dyncode の観点から見た ARM64 命令 |
| [Roadmap](dyncode-compiler-roadmap.md) | 予定作業 |
| [Progress](dyncode-compiler-progress.md) | 実装状況 |

---

## `.nc` ファイル拡張子

NeverC は `.nc` をネイティブソースファイル拡張子として認識します。`.nc` を使用すると、すべての NeverC 言語拡張（`-fneverc-types`、`-fbuiltin-string`）が自動的に有効化されます — 追加フラグ不要。

**[`.nc` 拡張子 →](nc-extension.md)**

---

## 組み込みランタイム

NeverC は LLVM bitcode として埋め込まれた組み込みランタイムで標準 C を拡張します。各 `-fbuiltin-<name>` フラグで制御。`.nc` ファイルでは `string` が自動有効化。

**[組み込みランタイムシステム →](builtins.md)**

| 組み込み | フラグ | 説明 |
|---------|--------|------|
| [組み込み文字列](builtins-string.md) | `-fbuiltin-string` | 値セマンティクス `string` 型、ドットコールメソッド、自動メモリ管理、ネイティブ UTF-8 |
| [組み込み mimalloc](builtins-mimalloc.md) | `-fbuiltin-mimalloc` | `malloc`/`free`/`calloc`/`realloc` の透過的 `mimalloc` 高性能アロケータオーバーライド |
| [文字列暗号化 (xorstr)](builtins-xorstr.md) | `-fencrypt-call-strings` | インスタンス別暗号化、必須 late seal、呼び出し箇所別の最終展開、volatile スタッククリア |
| [文字列ハッシュ (strhash)](builtins-strhash.md) | `-fstrhash-algo` / `-fstrhash-fold` | コンパイル時文字列ハッシュ、実行時と同一アルゴリズム、任意 IR 畳み込み |

---

## プラグイン API

NeverC は純粋な C ABI を通じてツールチェーン全体を公開します。プラグインは共有モジュール（`.dll` / `.so` / `.dylib`）であり、コマンドライン解析から最終的なリンク済みイメージまで、130 の名前付きコンパイルフェーズのいずれにも、オブザーバー・インターセプター・置換プロバイダーとして接続できます。SDK はヘッダーのみで、LLVM ヘッダーもコンパイラへのリンクも不要です。

**[プラグイン API →](plugin-api.md)**

| ドキュメント | 説明 |
|-------------|------|
| [README](plugin-api.md) | エントリーポイント、フェーズ、インターフェース交渉、登録、ABI 規則 |
| [Python プラグイン](plugin-api-python.md) | 任意の埋め込み Python、ライフサイクル、オプション、read-only observer、診断、制限 |
| [ドライバー API](plugin-api-driver.md) | コマンドライン、ツールチェーン選択、アクショングラフ、ジョブグラフ |
| [ソースと I/O API](plugin-api-source.md) | VFS プロバイダー、ソース位置、バッファー、出力シンク、依存関係 |
| [プリプロセッサー API](plugin-api-prep.md) | トークン、マクロ、pragma、include、機能クエリ、39 種類のイベント |
| [AST と意味解析 API](plugin-api-ast-sema.md) | パーサー拡張、AST 変更、名前探索、型、定数 |
| [IR API](plugin-api-ir.md) | LLVM IR の読み取り、トランザクショナルな構築、解析、パス、プロバイダー |
| [MIR API](plugin-api-mir.md) | マシン関数、レジスター、スタックフレーム、MIR パスと解析 |
| [ターゲット、MC、アセンブリ、オブジェクト](plugin-api-target-mc-object.md) | ターゲット登録、呼び出し規約、MC エンコード、オブジェクトグラフ |
| [リンクと LTO API](plugin-api-link-lto.md) | リンクグラフ、シンボル解決、GC/ICF、リンカーと LTO プロバイダー |
| [DynCode API](plugin-api-dyncode.md) | フラットな位置独立イメージ、インポートの低位化、文字セットエンコード |
| [カスタム呼び出し規約](plugin-api-custom-callconv.md) | データ駆動の呼び出し規約プラグイン |

---

## ロードマップ

NeverC プロジェクトの主要な計画方向：標準ライブラリ、EVM スマートコントラクトバックエンド、Solana eBPF バックエンド。

**[ロードマップ →](roadmap.md)**

| 機能 | 説明 |
|------|------|
| 標準ライブラリ (`std`) | Go スタイルのバッテリー同梱パッケージ：`fmt`、`os`、`io`、`net`、`crypto`、`encoding`、`sync` など |
| 難読化プラグインスイート (`neverc-obfuscation`) | ファーストパーティ VM、MBA、制御フロー平坦化、ポリモーフィックエンジン、アンチタンパープラグイン |
| UI コンポーネントライブラリ (`neverc-ui`) | Qt 風クロスプラットフォーム UI、HTML/JS/CSS レンダラー、ドラッグ＆ドロップデザイナー、AI ネイティブワークフロー |
| IDE & 言語ツール (`neverc-ide`) | `.nc` ファイル用 VSCode 拡張 + スタンドアロン IDE、IntelliSense、デバッグ、dyncode パイプライン可視化 |
| EVM スマートコントラクト | C を EVM バイトコードにコンパイル——Solidity の代わりに C でスマートコントラクトを記述 |
| Solana eBPF | C を Solana eBPF バイトコードにコンパイル——C でオンチェーンプログラム開発 |

---

## CLI ツール

単一コンパイル以外のユーザー向けコマンド。

| ドキュメント | 説明 |
|--------------|------|
| [`neverc run`](run.md) | 一時バイナリをコンパイル・ローカル実行・削除（`go run` 風） |
| [`neverc translate`](translate.md) | C++ を NeverC に変換する |
| [`neverc update`](update.md) | リリースインストールのアップ／ダウングレード（コンパイラと導入済み runtime を同一タグへ） |
| [`neverc runtime`](runtime.md) | クロスコンパイル sysroot の導入・一覧・更新・削除 |
| [`neverc build` / `neverc make`](build.md) | サンプル／プロジェクト Makefile 向け GNU Make 互換ドライバ |
| [リリースバイナリと `--strip`](release-builds.md) | 実行時不要シンボルとソースデバッグを削除し、`.ko` にはカーネル対応の構造的シンボル改名を適用（hash でも encryption でもありません） |

---

## Windows セキュリティターゲット

| ドキュメント | 説明 |
|--------------|------|
| [VBS エンクレーブ DLL](vbs-enclave.md) | Microsoft 互換の VBS エンクレーブイメージをリンク、検証、処理、署名して読み込みます |

---

## ローカル開発

NeverC をソースからビルドし、PATH 設定を含むローカル開発環境をセットアップします。

**[ローカル開発 →](local-dev.md)**

---

## サンプル

NeverC のクロスプラットフォームコンパイル機能を示すビルド可能なサンプル。macOS / Linux からクロスコンパイル可能。

**[サンプル →](examples.md)**

---

## 帰属表示と出典の引用

**[帰属表示ガイド →](attribution.md)**
