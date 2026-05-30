**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# ロードマップ

本ドキュメントは、計画中・進行中・意図的に延期した機能を追跡します。

## 現在の状態

NeverC の shellcode パイプラインは以下をカバー：

- 11+ 個の専用パスを持つ完全な LLVM IR パイプライン
- COFF / ELF / Mach-O エクストラクタ
- Win32 PEB-walk インポート解決（ROR-13 ハッシュ、6 DLL バケット）
- ダイレクト syscall 低レベル化（Darwin `svc #0x80`、Linux `svc #0` / `syscall`）
- カーネルモードサポート（Windows、Linux）
- 設定可能なプロファイルによるバッドバイト監査
- バッドバイトリライターと文字セットエンコーダーのプラグイン SDK
- サイズ / アライメント / パディング制約（`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=`）
- IR、MIR、バイトストリーム 3 層で計 11 個の難読化フック

## 完了済み（2026-04）

1. **サイズ / アライメント / パディング制約** — ビルトイン。`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=` は `finalizeShellcodeBytes` の末尾で実行。ドライバは矛盾する設定を拒否（例：パッドバイトがバッドバイトセット内にある、または align/max-length なしの pad）。

2. **バッドバイトリライターインターフェース** — スケルトンはビルトイン、ビルトイン戦略なし。`Plugin.h::registerBadByteRewriteStrategy` が SDK を公開。`-fshellcode-bad-byte-rewrite` / `-fno-...` でファイナライズチェーンがリライターを呼び出すかを制御。無効時は監査のみにフォールバック。ダウンストリームライブラリが Capstone ベースまたはカスタムリライト戦略を登録。

3. **文字セットエンコーダーインターフェース** — スケルトンはビルトイン、ビルトイン文字セットなし。`Plugin.h::registerCharsetEncoder` が `(name, Encode, Stub, IsCharsetMember)` タプルを公開。`-fshellcode-charset=<name>` 設定時、ファイナライズステージが `.text` を `Stub(target) || Encode(text, target)` に置換し、全出力バイトを文字セットに対して検証。印字可能 / 英数字 / カスタムエンコーダーはダウンストリームライブラリが登録。

## 計画中 — プラグインレイヤー（フック経由）

以下の機能は **意図的にビルトインしない**。戦略 / 難読化レイヤーに属し、フックとプラグインインターフェースを通じてサードパーティプラグインが提供する設計。

| 機能 | フックポイント | 備考 |
|------|--------------|------|
| アンチディスアセンブリ | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | 命令プレフィックス干渉、ジャンプ並び替え、ジャンクバイト挿入 |
| ポリモーフィズム | `RunAfterFinalMIR` / `RunPostExtract` | シードベースのコンパイルごとの出力変化 |
| ステージドエンコーダー（XOR / RC4 / 自己復号） | `RunPostExtract` / `RunPostFinalize` | コンパイル時スタブ生成 + ペイロード暗号化 |
| 間接 syscall（Halos / Tartarus / Recycled Gate） | IR レベルプラグインまたは `RunPostExtract` | ランタイム ntdll ガジェットスキャン |
| スリープマスク / コールスタックスプーフィング | IR パスプラグイン | Ekko / FOLIAGE / Cronos パターン |
| ETW / AMSI パッチ | IR パスプラグイン | ランタイムパッチシーケンス |
| モジュールストンプ / アンフック | IR パスプラグイン | メモリ操作パターン |

## プラグインフック概要

3 層で計 11 フック：

**IR 層（6 フック、`ModulePassManager &` を受け取り）**：
- `RunBeforePrep` — shellcode パスの前
- `RunAfterPrep` — リンケージ統一後
- `RunBeforeInlining` — AlwaysInliner 前の最後の機会
- `RunAfterInlining` — IR が 1 関数に完全フラット化
- `RunAfterStackify` — コード生成前の最終 IR 形状
- `RunAfterFinalIR` — `AllBlrPass` 後、絶対最後の IR フック

**MIR 層（3 フック、`TargetPassConfig &` を受け取り）**：
- `RunBeforePreEmit` — レジスタ割り当て済み、CFI/EH 疑似命令あり
- `RunAfterPreEmit` — `MIRPrepPass` クリーンアップ後、最終バイトに最も近い
- `RunAfterFinalMIR` — LLVM `addPreEmitPass2()` 後、AsmPrinter 直前

**バイトストリーム層（2 フック、`SmallVectorImpl<uint8_t> &` を受け取り）**：
- `RunPostExtract` — プレファイナライズ、リライター/エンコーダー/監査/サイジングが処理
- `RunPostFinalize` — ポストファイナライズ、ディスク書き込み直前；NeverC は以降の監査を行わない

## ファイナライズパイプライン

各エクストラクタは `.bin` 書き込み前に `finalizeShellcodeBytes` を呼び出す：

```
applyPostExtractObfuscationHook       (C Plugin API: NEVERC_HOOK_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (ビルトインハード監査)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationHook      (C Plugin API: NEVERC_HOOK_SC_POST_FINALIZE)
```

使用法とコード例は [Plugin API ドキュメント](../../plugin-api/README.ja.md) を参照。

## 計画なし

- **クロスランゲージフロントエンド** — NeverC は自身の C23 フロントエンドのみを受け入れる。IR パイプラインはフロントエンドから分離されているが、外部 bitcode（例：`rustc` や `zig` から）の受け入れはプロジェクト目標ではない。
