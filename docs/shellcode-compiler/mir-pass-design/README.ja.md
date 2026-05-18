**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# MIR パス設計 — 原則とフックポイント

> [ir-pass-design.md](../ir-pass-design/README.ja.md) の姉妹文書。IR 層は IR レベルで明らかに reloc を生むコンストラクトを除去する。MIR 層は命令選択・レジスタ割当後の**キャッチオール**であり、コード生成で導入された疑似/メタデータ命令を除去し、サードパーティ難読化パスが最終命令レベル変換を行うためのフックポイントを公開する。
>
> 実装：`neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`。
> フックインターフェース：`neverc/include/neverc/Shellcode/Pipeline/Pipeline.h`。

---

## 0. なぜ MIR 層が必要か

IR 層は既に以下を除去済み：
- 定数 GV → スタック化 / 即値（Data2TextPass）
- `memcpy` / `memset` / `str*` / `abs*` → インラインバイトループ（MemIntrinPass）
- `__int128` compiler-rt ヘルパ → インライン always_inline（CompilerRtPass）
- extern libc syscall → インライン svc / syscall（SyscallStubPass）
- Win32 extern → PEB ウォーク + エクスポートハッシュ（WinPEBImportPass）
- 変更可能グローバル → エントリスタックフレーム（ZeroRelocPass）
- 計算ジャンプ → switch（IndirectBrPass）
- オプション：直接呼出 → 間接呼出（AllBlrPass）

しかし LLVM バックエンドは **IR → MIR 下位変換** 時に shellcode が収容できない追加コンストラクトを導入する：

1. **CFI / EH_LABEL 疑似命令**：`-g` やデフォルト巻き戻し情報有効時に生成、`__compact_unwind`（Mach-O）/ `.eh_frame`（ELF）/ `.pdata + .xdata`（COFF）を生成。
2. **XRay / パッチャブル関数スタブ**。
3. **サニタイザメタデータ**：StackMap / PatchPoint / PseudoProbe。
4. **バックエンド MC レベルフィックスアップ**。

MIR フックのもう一つの重要目的：**サードパーティ命令レベル難読化の有効化**（命令置換、レジスタ改名）。IR では表現不可能（IR には仮想レジスタと抽象命令しかない）。

---

## 1. LLVM との統合（ネイティブフック）

LLVM の `TargetPassConfig` にグローバルコールバックリストがある。`addMachinePasses()` が各コールバックを `addPreEmitPass()` 前に呼出す。`Pipeline.cpp` で登録：

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
      const ObfuscationHooks &H = getShellcodeObfuscationHooks();
      runMIRHook(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
      runMIRHook(H.RunAfterPreEmit, TPC, Opts);
    });
```

コールバックは `Opts` をキャプチャしない。実行時に現在の `ShellcodeOptions` スナップショットを読む。

---

## 2. 組込み MIRPrepPass

クロスプラットフォーム・単一責任：各 `MachineBasicBlock` をスキャンし 3 カテゴリの疑似命令を削除。実マシン命令（`MOV` / `BL` / `ADRP` / `SYSCALL` / ...）は**決して触れない**。

### 2.1 サイドセクションメタデータ（`TargetOpcode::*`、クロスプラットフォーム）

| オペコード | ソース | 未除去の影響 |
|-----------|--------|------------|
| `CFI_INSTRUCTION` | 全プラットフォームのフレーム下位変換 / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` 非空 |
| `EH_LABEL` | EH / try-catch setjmp ポイント | LSDA サイドセクション非空 |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / sandbox stackmap | `.llvm_stackmaps` |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe` |
| `PATCHABLE_*` 族 | XRay / Kcov スタブ | `.xray_instr_map` |
| `FENTRY_CALL` | `-mfentry` エントリプローブ | extern `__fentry__` 呼出 |
| `LOCAL_ESCAPE` | Microsoft SEH | SEH ハンドラ参照を引込み |

### 2.2 Windows SEH（`TargetInstrInfo::getName()` プレフィクスマッチ）

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 命令リライトテーブル（`MIRRewritePatterns.def`）

2 パターン登録済み：

1. **`aarch64-cpi-fp-to-fmov-imm`**：`ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8`。
2. **`x86-cpi-zero-fp-to-xorps`**：`movss/movsd xmm, [rip+CPI]` (+0.0) → `FsFLD0SS/FsFLD0SD`（3 バイト `xorps xmm, xmm`）。

---

## 3. ユーザー難読化フック

`ObfuscationHooks` は **11 フックポイント** を公開：6 IR + 3 MIR + 2 バイトレベル。

- `RunBeforePreEmit`：CFI/EH 疑似命令**あり** — プロローグ/エピローグメタデータ操作向け。
- `RunAfterPreEmit`：**クリーン MIR** — AsmPrinter に最も近い。命令置換/レジスタ改名に最適。
- `RunPostExtract`：**純バイトストリーム** — XOR/RC4 ラッピング、ジャンクバイト、カスタムヘッダ向け。

---

## 4. 完全実行順序

```
[IR PassBuilder]
  ├─ RunBeforePrep → ZeroRelocPass(Prep) → RunAfterPrep
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1 → RunBeforeInlining
  │  (LLVM: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining → Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify → AllBlrPass(opt)
[Codegen]
  ├─ RunBeforePreEmit → ShellcodeMIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o]
[ShellcodeExtractor]
  ├─ RunPostExtract → flat .bin
```

## 5. 設計根拠

| 問題 | IR 層？ | MIR 層？ |
|------|---------|---------|
| 定数 GV 除去 | はい | 不要 |
| extern libc 除去 | はい | 不要 |
| CFI 疑似命令 | いいえ | はい |
| 命令レベル難読化 | いいえ | はい |
| レジスタ改名 | いいえ | はい |

## 6. 拡張ガイド

- **組込み疑似除去追加**: `isShellcodeStripPseudo` switch に 1 case 追加。
- **組込み MIR リライト追加**: `tryRewriteXxx` を書き `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def` に追加。
- **サードパーティ難読化**: `setShellcodeObfuscationHooks()` で登録。

## 7. ShellcodeExtractor との関係

| 層 | タイミング | 能力 |
|----|-----------|------|
| MIR | AsmPrinter **前** | MachineInstr の挿入/削除可 |
| 抽出器 | AsmPrinter **後** | バイト修正または拒否のみ |

**原則**: MIR で先に修正（まだ命令操作可能）；バイトレベルパッチ（intra-section reloc imm26 等）のみ抽出器にフォールバック。

## 8. 能動修正 vs 診断パススルー

1. **能動修正**: MachineInstr を直接変更。低コスト・ターゲット非依存。
2. **診断パススルー**: 問題検出→MIR レベルエラー報告→抽出器でバイトレベル拒否。
3. **抽出器フォールバック**: 残存外部 reloc / 非空データセクションでハード失敗。
