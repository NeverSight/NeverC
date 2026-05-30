**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC プロジェクト](i18n/README.ja.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# NeverC ドキュメント

各サブシステムの設計ノート、APIリファレンス、ガイド。

---

## Shellcode コンパイラ

Shellcode コンパイルパイプラインは NeverC の主要な研究領域です。アーキテクチャ、CLI オプション、プラットフォームマトリクス、例は次を参照：

**[Shellcode コンパイラ →](shellcode-compiler/README.ja.md)**

| ドキュメント | 説明 |
|-------------|------|
| [README](shellcode-compiler/README.ja.md) | 概要、クイックスタート、サポートターゲット |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.ja.md) | IR → オブジェクト → 抽出の設計 |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.ja.md) | 各 IR パスの設計意図 |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.ja.md) | バックエンド MIR パス |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.ja.md) | Ring-0 コンパイル |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.ja.md) | `TargetDesc` と抽出器 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.ja.md) | 新プラットフォームの追加 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.ja.md) | shellcode の観点から見た ARM64 命令 |
| [Roadmap](shellcode-compiler/roadmap/README.ja.md) | 予定作業 |
| [Progress](shellcode-compiler/progress/README.ja.md) | 実装状況 |

---

## `.nc` ファイル拡張子

NeverC は `.nc` をネイティブソースファイル拡張子として認識します。`.nc` を使用すると、すべての NeverC 言語拡張（`-fneverc-types`、`-fbuiltin-string`）が自動的に有効化されます — 追加フラグ不要。

**[`.nc` 拡張子 →](nc-extension/README.ja.md)**

---

## 組み込みランタイム

NeverC は LLVM bitcode として埋め込まれた組み込みランタイムで標準 C を拡張します。各 `-fbuiltin-<name>` フラグで制御。`.nc` ファイルでは `string` が自動有効化。

**[組み込みランタイムシステム →](builtins/README.ja.md)**

| 組み込み | フラグ | 説明 |
|---------|--------|------|
| [組み込み文字列](builtins/string/README.ja.md) | `-fbuiltin-string` | 値セマンティクス `string` 型、ドットコールメソッド、自動メモリ管理、ネイティブ UTF-8 |
| [組み込み mimalloc](builtins/mimalloc/README.ja.md) | `-fbuiltin-mimalloc` | `malloc`/`free`/`calloc`/`realloc` の透過的 `mimalloc` 高性能アロケータオーバーライド |
| [文字列暗号化 (xorstr)](builtins/xorstr/README.ja.md) | `-fencrypt-call-strings` | コンパイル時文字列暗号化、スタック割り当て XOR 復号、アンチシグネチャ |
