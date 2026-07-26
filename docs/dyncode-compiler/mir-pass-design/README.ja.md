**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← DynCode コンパイラ](../README.ja.md)

# MIR パス設計 — 原則とフックポイント

> [ir-pass-design.md](../ir-pass-design/README.ja.md) の姉妹文書。IR 層は IR レベルで明らかに reloc を生むコンストラクトを除去する。MIR 層は命令選択・レジスタ割当後の**キャッチオール**であり、コード生成で導入された疑似/メタデータ命令を除去し、サードパーティ難読化パスが最終命令レベル変換を行うためのフックポイントを公開する。
>
> 実装：`neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`。
> フックインターフェース：[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]。

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

しかし LLVM バックエンドは **IR → MIR 下位変換** 時に dyncode が収容できない追加コンストラクトを導入する：

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
      const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
      const ObfuscationInterposes &H = getDynCodeObfuscationInterposes();
      runMIRInterpose(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createDynCodeMIRPrepPass(Opts));
      runMIRInterpose(H.RunAfterPreEmit, TPC, Opts);
    });
```

コールバックは `Opts` をキャプチャしない。実行時に現在の `DynCodeOptions` スナップショットを読む。

---

## 2. 組込み MIRPrepPass

クロスプラットフォーム・単一責任：各 `MachineBasicBlock` をスキャンし 3 カテゴリの疑似命令を削除。実マシン命令（`MOV` / `BL` / `ADRP` / `SYSCALL` / ...）は**決して触れない**。

### 2.1 サイドセクションメタデータ（`TargetOpcode::*`、クロスプラットフォーム）

| オペコード | ソース | 除去しない場合 |
|-----------|--------|---------------|
| `CFI_INSTRUCTION` | 全プラットフォームの frame-lowering / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` が非空 |
| `EH_LABEL` | EH / try-catch setjmp 地点 | LSDA サイドセクションが非空 |
| `GC_LABEL` / `ANNOTATION_LABEL` | GC / アノテーションマーカー | セクション相対メタデータを持つ MCSymbol |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / サンドボックス stackmap | `.llvm_stackmaps` サイドセクション |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe` サイドセクション |
| `PATCHABLE_*` ファミリ | XRay / Kcov スタブ | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | `-mfentry` エントリプローブ | extern `__fentry__` 呼び出し |
| `LOCAL_ESCAPE` | Microsoft SEH frame-escape | `_local_unwind2` / `__except_handler3` を引き込む |
| `JUMP_TABLE_DEBUG_INFO` | ジャンプテーブルデバッグ情報 | `.debug_rnglists` エントリ |

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

`ObfuscationInterposes` は **11 フックポイント** を公開：6 IR + 3 MIR + 2 バイトレベル。

3 つのシグネチャ型：

```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

- `RunBeforePreEmit`：CFI/EH 疑似命令**あり** — プロローグ/エピローグメタデータ操作向け。
- `RunAfterPreEmit`：**クリーン MIR** — AsmPrinter に最も近い。命令置換/レジスタ改名に最適。
- `RunPostExtract`：**純バイトストリーム** — XOR/RC4 ラッピング、ジャンクバイト、カスタムヘッダ向け。

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::dyncode::getDynCodeObfuscationInterposes();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::dyncode::DynCodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  // Register via Plugin API: NEVERC_INTERPOSE_SC_BEFORE_PREEMIT / AFTER_PREEMIT / AFTER_FINAL_MIR
}
```

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
  ├─ RunBeforePreEmit → DynCodeMIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o]
[DynCodeExtractor]
  ├─ RunPostExtract → flat .bin
```

## 5. 設計根拠

| 問題 | IR 層？ | MIR 層？ |
|------|---------|----------|
| 定数 GV 除去 | はい（Data2Text） | 不要 |
| extern libc 除去 | はい（SyscallStub / WinPEB） | 不要 |
| 可変グローバルのスタック化 | はい（ZeroReloc） | 不要 |
| Computed goto | はい（IndirectBr） | 不要 |
| CFI 疑似命令 | いいえ（バックエンド生成） | はい（スキャンして削除） |
| XRay スタブ | いいえ（バックエンド生成） | はい（スキャンして削除） |
| 命令レベル難読化 | いいえ（IR に物理レジスタなし） | はい（実レジスタ/MI あり） |
| レジスタ改名 | いいえ | はい |
| Peephole 定数展開 | 部分的 | はい（よりクリーン） |

## 6. 拡張ガイド

- **組込み疑似除去追加**: `isDynCodeStripPseudo` switch に 1 case 追加。
- **組込み MIR リライト追加**: `tryRewriteXxx` を書き `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def` に追加。
- **サードパーティ難読化**: [Plugin API](../../plugin-api/README.ja.md)（`NEVERC_INTERPOSE_SC_*` フック）で登録。

## 7. DynCodeExtractor との関係

| 層 | タイミング | 能力 |
|----|-----------|------|
| MIR | AsmPrinter **前** | MachineInstr の挿入/削除可 |
| 抽出器 | AsmPrinter **後** | バイト修正または拒否のみ |

**原則**: MIR で先に修正（まだ命令操作可能）；バイトレベルパッチ（intra-section reloc imm26 等）のみ抽出器にフォールバック。

## 8. 能動修正 vs 診断パススルー

1. **能動修正**: MachineInstr を直接変更。低コスト・ターゲット非依存。
2. **診断パススルー**: 問題検出→MIR レベルエラー報告→抽出器でバイトレベル拒否。
3. **抽出器フォールバック**: 残存外部 reloc / 非空データセクションでハード失敗。

[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]: ../../../neverc/include/neverc/DynCode/Pipeline/Pipeline.h
