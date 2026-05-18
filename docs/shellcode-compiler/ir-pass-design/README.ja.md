**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# IR パス設計 — 原則、パイプライン、前後比較

> 本ドキュメントは shellcode コンパイルパイプラインの各パスの**設計理由**を説明する。実装詳細は `.cpp` ソースの英語コメントにある。

---

## 0. コアアイデア

shellcode の目標を一文で：**`.o` 中の reloc になりうるものをすべて除去し、`mmap(RWX)` + `memcpy` + `blr` できる純粋な命令列だけを残す。**

この制約をユーザーに漏らしたくない — ユーザーは普通の C を書き、パイプラインが reloc を生む IR コンストラクトを内部で処理する。

| パス | 機能 | 実行タイミング |
|------|------|---------------|
| ZeroRelocPass (Prep) | リンケージ統一 / always_inline 強制 / ハードブロッカー拒否 | PipelineStart |
| IndirectBrPass | computed-goto `indirectbr` → `switch` | PipelineStart |
| MemIntrinPass | `@llvm.mem*` + 明示 mem*/str*/abs → 内部バイトループヘルパ | PipelineStart |
| StringRuntimePass | 組込み `string` 型 → スタック arena バリアント | PipelineStart |
| CompilerRtPass | `__udivti3` 族 + i128 div/rem → 内部 128 ビット長除算 | PipelineStart |
| SyscallStubPass | libc extern → TargetDesc テーブル駆動カーネルトラップラッパ | PipelineStart |
| WinPEBImportPass | Win32 extern → PEB モジュールウォーク + PE エクスポートリゾルバ | PipelineStart |
| KernelImportPass | Ring-0 extern → リゾルバ支援間接呼出（カーネルのみ） | PipelineStart |
| Data2TextPass (Phase 1) | 定数 GV → 即値 / スタックチャンクストア | PipelineStart |
| *(LLVM 標準最適化)* | AlwaysInliner / SROA / InstCombine / SLP | O-level |
| Data2TextPass (Phase 2) | SROA 残留ベクトル store 分割、遅延定数消費 | OptimizerLast |
| ZeroRelocPass (Stackify) | 変更可能グローバル → エントリ alloca + 最終検証 | OptimizerLast |
| AllBlrPass (optional) | 直接呼出 → 間接呼出 | OptimizerLast |
| MIRPrepPass | MIR キャッチオール：CFI/EH/XRay/SEH 疑似命令除去 | Before addPreEmitPass |
| ShellcodeExtractor | `.o` 走査の最終監査 + flat `.bin` 出力 | 後処理 |

---

## 1. ZeroRelocPass

### 1.1 Prep — リンケージ統一
全非エントリ関数 → `internal` + `alwaysinline`。ハードブロッカー拒否。`_Thread_local` を static に静かに降格。

### 1.2 Stackify — グローバル変数除去
変更可能 GV → エントリの `alloca`。最終検証で残存 GV を拒否。

### 1.3 `placeEntryFirst`
エントリを `.bin` のオフセット 0 に配置。

## 2. IndirectBrPass
GCC computed-goto → `switch`（ゼロ reloc）。

## 3. SyscallStubPass
libc extern → TargetDesc 駆動インラインアセンブリ。POSIX 互換レイヤ。K&R 自動修正。

## 4. WinPEBImportPass
Win32 extern → PEB ウォークリゾルバ（~190 API、6 DLL）。Windows POSIX 互換（13 関数グループ）。

## 5. MemIntrinPass
memcpy/memset/strlen/strcpy 等 → `internal alwaysinline` バイトループヘルパ。

## 6. CompilerRtPass
`__int128` 除算/剰余 → インラインシフト減算長除算。

## 7. Data2TextPass
Phase 1：定数 GV → 即値/スタックストア。ConstantFP → volatile ビットパターン。
Phase 2：SROA 残留ベクトル store 分割。ベクトル定数インライン化。

## 8. AllBlrPass（オプション）
直接呼出 → volatile スロット + `blr xN` / `call *rax` 間接呼出。

## 9. 難読化フック
11 フックポイント。[plugin-interface.md §6](../plugin-interface/README.ja.md#6-registration-position-selection--pic-coverage-matrix) 参照。

## 10. 二段階設計の根拠
Phase 1 は元の IR をクリーン。LLVM 最適化後、Phase 2 は最適化器が導入した新構造をクリーン。

## 11. KernelImportPass（ring-0 のみ）
未解決 extern → リゾルバ支援間接呼出に自動リライト。`(resolver, cookie)` 暗黙パラメータ注入。[kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ja.md) 参照。

## 12. StringRuntimePass
組込み `string` メソッド → スタック arena バリアント。

## 13. エラー診断哲学
各ハードエラーは正確に**1 つの操作可能な診断**を生成。`__neverc_shellcode_hard_error` メタデータセンチネルがカスケードを防止。ユーザーには 1 つの明確なエラーと修正が表示され、3 つのカスケードメッセージは出ない。
