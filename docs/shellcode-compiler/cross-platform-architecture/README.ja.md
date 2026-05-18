**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# NeverC Shellcode クロスプラットフォームアーキテクチャ概要

本ドキュメントは「1 セットの pass で macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel をカバー」する背後の設計原則を記述する。新プラットフォームやコンテキストへの拡張前にお読みください。

関連サブシステム文書：
- [README.md](../README.ja.md) — 概要、CLI オプション、クイックスタート
- [ir-pass-design.md](../ir-pass-design/README.ja.md) — IR 層 pass の責務と例
- [mir-pass-design.md](../mir-pass-design/README.ja.md) — MIR 層 prep pass + 難読化フック
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ja.md) — カーネルコンテキスト設計詳細
- [platform-extension-guide.md](../platform-extension-guide/README.ja.md) — 新プラットフォーム追加ステップガイド

---

## 1. 三次元マトリクス：OS × Arch × ExecutionLevel

全クロスプラットフォーム差分が **三次元マトリクス** に収束。各セルが `TargetDesc` テーブルエントリに対応：

```
                ┌──── arm64 ────┬──── x86_64 ────┐
     Darwin ────┤ User / Kernel │ User / Kernel  │  Mach-O
     Linux  ────┤ User / Kernel │ User / Kernel  │  ELF
     Android────┤ User / Kernel │ User / Kernel  │  ELF
     Windows────┤ User / Kernel │ User / Kernel  │  COFF
                └───────────────┴────────────────┘
```

8 (OS, arch) × 2 ExecutionLevel = **16 テーブルエントリ**。

**コア設計思想**：pass は常にテーブルから読み、`if (OS == Darwin)` 分岐は書かない。新プラットフォーム追加 = `describeTriple()` に 1 行 + 各抽出器 switch に 1 case。

## 2. パイプライン実行順序

`-fshellcode` アクティブ時、コンパイラは固定順序に従う。**各ステージに対応する難読化フック**あり。

2 つの主要不変量：
1. **バックエンド TableGen `.td` が命令記述の唯一の源**。
2. **shellcode に外部 reloc とデータセクションはない**。

## 3. グローバル PIC 戦略

`isPICDefaultForced()` が全 3 ToolChain で **無条件 true** を返す。

## 4. User / Kernel 直交次元

- **User**（デフォルト）：PEB ウォーク / syscall stub パイプライン。
- **Kernel**：SyscallStubPass / WinPEBImportPass 短絡；KernelImportPass 起動。

## 5. ユーザーモード「普通 C」サポートマトリクス

`-fshellcode` 使用時、大配列、浮動小数点定数、computed-goto、memcpy/strlen、`__int128` 除算、アトミック、POSIX/Win32 ヘッダ等が**ユーザー無意識で直接サポート**。

## 6. MIR 層：修正 / フォールバック / 抽出 3 段パイプライン

1. クロスプラットフォーム疑似命令クリーンアップ
2. Shellcode フレンドリー命令リライト（テーブル駆動）
3. 外部参照 / 定数プール監査

## 7. 抽出器層

`ObjectFormat` でディスパッチ。共通契約：「intra-`.text` PC-rel パッチを受入、それ以外すべて拒否」。

## 8. 難読化フックポイント

11 フック（6 IR + 3 MIR + 2 バイトレベル）が全パイプラインステージをカバー。

## 9. 新 (OS, Arch) エントリ追加

コスト：TargetDesc 1 行 + syscall テーブル + 抽出器 case + テスト。IR/MIR pass 変更ゼロ。

## 10. 非ゴール

- C++ / ObjC / Rust フロントエンド
- 32 ビット / ビッグエンディアン / ニッチ ISA
- shellcode への libc ランタイム組込み
- 絶対アドレス reloc
