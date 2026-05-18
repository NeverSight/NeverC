**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC プロジェクト](../README.ja.md)

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
| [Plugin Interface](shellcode-compiler/plugin-interface/README.ja.md) | 難読化・エンコードプラグイン |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.ja.md) | `TargetDesc` と抽出器 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.ja.md) | 新プラットフォームの追加 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.ja.md) | shellcode の観点から見た ARM64 命令 |
| [Roadmap](shellcode-compiler/roadmap/README.ja.md) | 予定作業 |
| [Progress](shellcode-compiler/progress/README.ja.md) | 実装状況 |

---

## 組み込み文字列型

NeverC は C 言語向けの組み込み `string` 値型を提供します。`std::string` の使いやすさと `QString` レベルの Unicode サポートを C にもたらします。`-fbuiltin-string` で有効化（`-fshellcode` モードでは自動有効）。

**[組み込み文字列 →](builtin-string/README.ja.md)**
