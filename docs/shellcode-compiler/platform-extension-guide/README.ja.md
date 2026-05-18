**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# プラットフォーム拡張ガイド

本ドキュメントは shellcode コンパイラを新しいターゲットプラットフォームに拡張する方法を説明する。現在サポート：**macOS / Linux / Android / Windows 上の arm64 / x86_64**（8 つの triple）、各々独立した **User** / **Kernel** コンテキスト（計 16 バリアント）。新プラットフォーム追加は通常数百行のコード。

## 設計思想：テーブル駆動、分岐駆動ではない

全 pass はターゲット非依存。プラットフォーム差分は**2 箇所**に集約：

1. `TargetDesc.cpp` の `describeTriple()` テーブルエントリ
2. 3 つの抽出器（Mach-O / ELF / COFF）のアーキテクチャ switch

新プラットフォーム追加 = (1) に 1 行追加 + (2) に 1 case 追加。

## 手順

### 1. `TargetDesc` に行を追加

`describeTriple()` に対応する OS 分岐を追加：

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**必須フィールド**（すべて `TargetDesc.h` で定義）：

| フィールド | 目的 | 欠落時 |
|-----------|------|--------|
| `OS` / `Arch` / `Format` | ディスパッチキー | `describeTriple` が Unknown を返す → ドライバが早期拒否 |
| `TextSectionName` | 抽出器がエントリセクションを検索 | `.text` が見つからない → 拒否 |
| `Syscall` | SyscallStubPass の置換判断 | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | SyscallStubPass のインラインアセンブリ生成 | いずれか空 → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | WinPEBImportPass TEB 読取インラインアセンブリ | 空 → PEB walk が空 InlineAsm を生成（Windows：必須） |
| `DriverInjectFlags` | shellcode モードで注入するプラットフォーム固有フラグ | null → 注入なし |

### 2. `SyscallStub` / `SyscallTables` を拡張（OS にカーネルトラップがある場合）

- `TargetDesc.h` の `SyscallABI` に列挙値を追加
- `SyscallTables.cpp` に `kXxxTable` を追加
- `lookupSyscall` の switch に case を追加
- `SyscallStubPass` の変更は不要 — InlineAsm テンプレート/制約は `TargetDesc` から読取

### 2.5 Windows Win32 API ホワイトリストの拡張

Windows には安定した syscall ABI がない；`WriteFile` / `CreateThread` / `VirtualAlloc` へのユーザー呼出は `WinPEBImportPass` を通る。ホワイトリストはマルチ DLL テーブル：

- `Tables/Win32Apis.def` で定義
- 各行：`NEVERC_WIN32_API(ApiName, "dll.dll")`
- リゾルバは 2 パラメータ `__neverc_win_resolve(dll_hash, api_hash)` で任意 DLL をサポート済

**API 追加**（例：`DeviceIoControl`）：
1. `Win32Apis.def` に 1 行追加
2. `lib/Headers/windows.h` の shellcode ブランチに宣言を追加
3. pass 変更不要

**新 DLL バケット追加**（例：`winhttp.dll`）：
- `Win32Apis.def` に新 DLL 名の行を追加するだけ

### 3. 対応する抽出器を拡張

3 つの事項を処理：
1. reloc タイプを識別 → バイトパッチまたは拒否
2. 禁止データセクション名リストを更新（新 OS に固有セクションがある場合）
3. エントリオフセット 0 reloc ターゲット範囲検証を更新

完全に新しいオブジェクト形式（例：WASM）の場合：
1. `ObjectFormat` 列挙値を追加
2. `ShellcodeExtractor.cpp` のディスパッチ switch に case 追加
3. `<Format>Extractor.cpp` を書く（`ELFExtractor.cpp` の構造に従う）

### 4. Loader を追加（テストツールのみ）

- `tests/neverc/shellcode/loader_linux.c` と `loader_windows.c` を参考
- 典型：`mmap(RWX) → memcpy → icache flush → call`

### 5. テストを更新

- `run_cross_target_tests.sh` に `cross_compile_check` 行を追加
- CI でそのプラットフォーム実行可能なら loader ラウンドトリップテストを追加

---

## 既知のクロスプラットフォーム注意点

- **エンディアン**：NeverC はリトルエンディアン（LE）のみ対応、全主要ターゲットをカバー。
- **ABI 差分**：Win64（rcx/rdx/r8/r9）vs System V AMD64（rdi/rsi/rdx/rcx/r8/r9）で引数レジスタが完全に異なる。Clang フロントエンド層で処理；shellcode パイプラインは関知不要。
- **syscall 番号**：Linux ではアーキテクチャ毎に異なる、Android は Linux と同一、Darwin は独自の BSD 番号、Windows は安定番号なし（PEB walk）。テーブルで (OS, arch) 索引。
- **キャッシュ整合性**：ARM は明示的 i-cache flush 必須；x86 は不要。macOS arm64 JIT は `pthread_jit_write_protect_np` も必要；Linux arm64 は `__builtin___clear_cache`；Windows は `FlushInstructionCache`（x86 では no-op）。
- **SELinux / W^X**：Android は SELinux `execmem` で制約；非脱獄 iOS は `mmap(RWX)` を完全拒否、`MAP_JIT` + コード署名が必要。

## 将来の拡張ロードマップ

| ターゲット | 見積もり工数 | 依存 |
|-----------|------------|------|
| **iOS arm64**（脱獄 / `MAP_JIT`） | 1 日 | Mach-O 抽出器を再利用、loader を修正 |
| **FreeBSD / OpenBSD x86_64** | 半日 | ELF 抽出器を再利用 + 新 syscall テーブル |
| **RISC-V64 Linux** | 2 日 | RISC-V TargetDesc + 新 AllBlr バリアント + RISC-V reloc パッチが必要 |

## 難読化 Pass 拡張インターフェース

shellcode パイプラインは `Pipeline.h::ObfuscationHooks` 経由で 11 フックをサードパーティ難読化ライブラリに公開：

```
PipelineStartEP:
  RunBeforePrep → [ZeroReloc Prep] → RunAfterPrep →
  [IndirectBr → MemIntrin → CompilerRt → SyscallStub →
   WinPEBImport → KernelImport → Data2Text phase 1] →
  RunBeforeInlining

OptimizerLastEP:
  RunAfterInlining → [Data2Text phase 2 → ZeroReloc Stackify] →
  RunAfterStackify → [AllBlrPass] → RunAfterFinalIR

MIR: RunBeforePreEmit → [MIRPrepPass] → RunAfterPreEmit →
     [LLVM addPreEmitPass/addPreEmitPass2] → RunAfterFinalMIR

バイトストリーム: RunPostExtract → [finalize チェーン] → RunPostFinalize
```

IR レベル使用法：
```cpp
neverc::shellcode::ObfuscationHooks H;
H.RunAfterInlining = [](llvm::ModulePassManager &MPM,
                        const neverc::shellcode::ShellcodeOptions &Opts) {
  MPM.addPass(MyCFFPass(Opts.ObfuscateSpec));
};
neverc::shellcode::setShellcodeObfuscationHooks(std::move(H));
```

MIR レベル使用法：
```cpp
H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
};
```

組み込み MIR パッチもテーブル駆動：`Tables/MIRRewritePatterns.def` にパターン診断名・アーキテクチャフィルタ・ヘルパ名を記録；`Tables/MIRRewriteOpcodes.def` にバックエンドオペコード名を記録。新 shellcode フレンドリーバックエンド形式追加時は、pass 本体にターゲット固有分岐を散在させず、テーブルエントリと狭いヘルパの追加を優先。
