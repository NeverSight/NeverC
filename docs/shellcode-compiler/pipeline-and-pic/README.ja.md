**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# Shellcode パイプライン、MIR、PIC 戦略（設計メモ）

本ドキュメントは NeverC の shellcode モードにおける **IR → LLVM 最適化 → バックエンド MIR → オブジェクトファイル → 抽出/パッチ** チェーンの設計トレードオフと、**コンパイラ全体のデフォルト PIC** ポリシーとの関係を記述する。実装の詳細はソースコードと英語コメントが正式。

## 1. なぜデフォルトで PIC を強制するか（非 shellcode 含む）

shellcode 抽出器は、実行可能フラグメント中の外部シンボル参照が **PC 相対** またはセクション内解決可能な reloc に落ちることを前提とする（ローダーが `.data` を埋める必要のあるハードコード絶対アドレスや定数プールではなく）。

NeverC は `Generic_GCC::isPICDefaultForced()`、`MachO::isPICDefaultForced()`、`MSVCToolChain::isPICDefaultForced()` で **true** を返す。上流 Clang の「オプションのデフォルト PIC」とは異なり、**全プラットフォームの全コンパイルで PIC のみモデル**。これにより：

- 通常 C と `-fshellcode` が同じ reloc 習慣を共有し、「通常は動く、shellcode で壊れる」認知コストを削減。
- Linux / Android / macOS / Windows バックエンドがテーブル駆動記述子（`TargetDesc` + `Options.td.h`）で同じ前提を共有し、`if (linux)` 式ハードコードを回避。

このポリシーは `-fshellcode` の有無や user/kernel コンテキストを区別しない。ユーザーが `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic` を渡しても `ParsePICArgs()` は `Reloc::PIC_` を維持し、通常コンパイル・ユーザーモード shellcode・カーネルモード shellcode に同一の PC 相対前提を使用する。

## 2. IR と MIR の二段分業

### 2.1 IR 層（`registerShellcodePasses`）

「通常の C」セマンティクスを**単一エントリ・独立データセクションなし・問題グローバルなし**の形に圧縮する責務：`ZeroRelocPass`、`IndirectBrPass`、`MemIntrinPass`、`StringRuntimePass`、`CompilerRtPass`、`SyscallStubPass`、`WinPEBImportPass`、`KernelImportPass`（カーネルのみ）、`Data2TextPass` 等。

**原則**：IR で構造的に解ける問題は IR で先に修正（定数プール、BlockAddress、`memcpy` の libc フォールスルー、`__int128 /` の `__udivti3` フォールスルー等）し、バックエンドと抽出器が見るバイト列を単純化する。ユーザー認知コストが高いが安全に内部化できるシナリオでは、ドライバが積極的にルールを注入する（例：AArch64 Linux / Android / Windows の `long double` を shellcode モードで binary64 に降格）。ランタイムなしには対応不可能な構造のみが MIR / 抽出器の診断をトリガーする。

### 2.2 MIR 層（`registerShellcodeMachinePasses`）

LLVM レガシー `TargetPassConfig` にコールバックを登録。**レジスタ割当後、`addPreEmitPass` 前**で以下の順序：

1. ユーザー/難読化ライブラリ：`RunBeforePreEmit`（CFI / EH 疑似命令が残存；メタデータ依存変換に適する）。
2. **`ShellcodeMIRPrepPass`**：`.eh_frame` / `.pdata` / `.xray_*` サイドセクションを生成する疑似命令を除去し、AsmPrinter 前の命令列を「純コード」に近づける。
3. ユーザー/難読化ライブラリ：`RunAfterPreEmit`（命令置換・レジスタ改名等「最終マシンコード形態」難読化に適する）。

**原則**：ネイティブ命令列に問題があれば MIR で修正（特に `ShellcodeMIRPrepPass` 周辺）；**抽出とパッチは最終安全ネット**であり、COFF/ELF/Mach-O 層で同一ロジックの重複を回避する。

MIR オペコード名は pass 制御フローに分散させない；`ShellcodeMIRPrepPass` は `Tables/MIRRewriteOpcodes.def` の `(pattern, role, opcode)` テーブルを `TargetInstrInfo::getName()` 経由で参照。shellcode フレンドリーな命令置換を追加する際は、テーブルエントリと小規模 MIR リライトの追加を優先；必要な場合のみバックエンド `.td` 命令選択変更にフォールバックし、抽出器レベルのオブジェクト形式フォールバックは最終手段。

> 注：`ShellcodeMIRPrepPass` は `-fshellcode` 有効時のみ登録。通常プログラムで CFI/EH をグローバルに剥がしてはならない（通常の例外処理とデバッグ情報を破壊する）。

IR・MIR のグローバルコールバックはともに**一度登録、実行時に現在の `ShellcodeOptions` スナップショットを読む**パターン。長寿命の組み込みコンパイラプロセスをサポート：同一プロセスが shellcode を先にコンパイルし次に通常 C をコンパイルする場合、通常 C は前回の IR/MIR pass を継承しない；複数 shellcode TU を連続コンパイルする場合、グローバルコールバック登録の重複が同一 pass セットを多重スタックしない。

## 3. テーブル駆動のプラットフォーム差分

- **Triple → 動作**：`TargetDesc.cpp` の `describeTriple()` と `TargetDesc` フィールドに集約（セクション名、syscall ABI、インラインアセンブリテンプレート、ドライバ注入フラグ等）。新 OS/Arch 追加時は抽出器や pass に長い分岐を書くのではなく**テーブルエントリ追加**を優先。
- **CLI オプション**：`neverc/include/neverc/Invoke/Options.td.h` で定義；`DriverIntegration.cpp` が `OPT_*` 列挙で消費し、文字列マジックを回避。

## 4. Windows MSVC ツールチェーンと SDK レイアウト

Windows ターゲットへのクロスコンパイル時、NeverC は**ハードコード絶対パスなし**で 2 つの SDK ソースをサポート：

1. **ビルドツリーにバンドルされた SDK**（推奨）：ユーザーとテストスクリプトが `build-neverc/sdk` を SDK ルートとして扱う。NeverC はインストールディレクトリ内の `sdk/msvc/` を自動検出し、`MSVCToolChain::AddNeverCSystemIncludeArgs` / `Linker::ConstructJob` で include/lib パスを注入。典型レイアウト：

   ```
   build-neverc/bin/neverc
   build-neverc/sdk/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **実際の VS スタイル sysroot**（任意）：`VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...` ディレクトリツリーがあれば `-winsysroot=<path>` または `NEVERC_WIN_SYSROOT` 環境変数で指定。

両方ともレジストリや OS 提供の VS 環境変数不要で、macOS / Linux からの Windows shellcode クロスコンパイルを実現。

## 5. 難読化と拡張ポイント

- **IR 難読化**：`setShellcodeObfuscationHooks` で複数 IR ステージフックを提供；`-fshellcode-obfuscate=` が spec 文字列を外部ライブラリに渡す。各層に**前**（最適化前）と**後**（最適化後）フックあり。`RunAfterFinalIR` は真の最終 IR 注入可能ポイント——ここに登録した難読化 pass の後に出力を変更する pass はない。計 11 フック（6 IR + 3 MIR + 2 バイトストリーム）。
- **MIR 難読化**：`RunBeforePreEmit` / `RunAfterPreEmit` は中粒度 MIR フック；`RunAfterFinalMIR` は**真の最終** MIR フック（fork 拡張が `RegisterTargetPassConfigPostPreEmitCallbackFn` を追加、`addPreEmitPass2()` 後に呼出）。`-fshellcode-mir-obfuscate=` で MIR spec を個別指定；未設定時は IR spec がデフォルト。
- **バイトストリームフック**：`RunPostExtract` は finalize **前**フック；`RunPostFinalize` は finalize **後**フック（ディスク書込前の最後の瞬間、NeverC は以降監査しない）。
- **Finalize プラグイン SDK**：`Plugin.h` が `registerBadByteRewriteStrategy`（命令レベル禁止バイト書換戦略のチェーン）と `registerCharsetEncoder`（名前付きキャラセット登録）を公開。[plugin-interface.md 第 2–3 節](../plugin-interface/README.ja.md#2-bad-byte-rewriter-badbyterewritestrategy)を参照。
- **サイズ/アライメント/パディング**：`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=` が finalize 末尾で実行；ドライバは矛盾する設定を拒否。
- **設計選択**：難読化、ポリモーフィズム、段階エンコーダ、間接 syscall 等の戦略層機能は**意図的に組み込まず**、オプションプラグインとしてのみ提供。

## 6. カーネルモード（Ring-0）次元

shellcode モードは `-mshellcode-context=user|kernel` をパイプラインの第二次元として導入し、triple に重ねる：

- **ユーザーモード**：PEB ウォーク / syscall stub パイプライン。
- **カーネルモード**：
  - `SyscallStubPass` / `WinPEBImportPass` が pass レベルで早期リターン。
  - `TargetDesc::KernelInjectFlags` が OS/arch 適切なバックエンドフラグを追加（Unix x86_64：`-mno-red-zone -mcmodel=kernel`、Windows：`/kernel`、AArch64：`-mgeneral-regs-only`）。
  - `KernelImportPass` が未解決 extern 直接呼出をリゾルバ支援間接呼出に書換、必要に応じ `(resolver, cookie)` 暗黙プレフィクスパラメータを注入。
  - `<neverc/kernel.h>` が `neverc_kern_resolve_t`、`neverc_kern_hash()` と関連カーネル側シグネチャを公開；ユーザーモード shim（`<windows.h>`、`<unistd.h>` 等）はカーネルモードで `#error` により拒否。

詳細は [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ja.md) を参照。

## 7. Windows POSIX 互換レイヤ

### 7.1 問題

クロスプラットフォーム C コードは `write(fd, buf, n)`、`read(fd, buf, n)`、`exit(code)` 等を一般的に使用。Unix では `SyscallStubPass` がこれらをインライン syscall に置換。Windows ではこれらの POSIX 名に対応 Win32 API がなく、「未解決 reloc」エラーとなる。

### 7.2 設計目標

**ユーザー認知ゼロ**：同一 C ソースが全 8 ターゲット triple で `#ifdef _WIN32` や手動 Win32 API 呼出なしにコンパイル可能。

### 7.3 実装

`WinPEBImportPass` が三段階処理を実装：

1. **第 1 段階 — POSIX スキャン**：未マッチの extern 宣言を POSIX 互換テーブルと照合。
2. **第 2 段階 — ブリッジラッパ生成**：`Win32PosixCompat.def` が POSIX 名を `always_inline` ラッパビルダに分配（例：`write` → `GetStdHandle` + `WriteFile`、`mmap` → `VirtualAlloc` + prot マッピング、`exit` → `ExitProcess` 等）。13 POSIX 関数グループをカバー。
3. **第 3 段階 — PEB 解決**：ラッパが参照する Win32 API を通常の PEB ウォークリゾルバで解決。

### 7.4 拡張性

新 POSIX 互換関数の追加：エイリアスのみなら `Win32PosixCompat.def` を変更；新セマンティクスには小規模 IR ビルダ + テーブル 1 エントリ。`open→CreateFileA` のようなステートフル操作（fd/handle 寿命テーブル要）は意図的にエミュレートしない。

## 8. K&R 暗黙宣言自動修正

ユーザーが `#include` なしで POSIX 関数を呼ぶと、C89 が 0 仮引数の K&R 暗黙宣言を生成する。`SyscallStubPass` は 50+ の一般的 POSIX 関数の正規 LLVM IR 関数型を持つ `getCanonicalSyscallType()` テーブルを保持。0 仮引数の K&R 宣言検出時に正規シグネチャを自動置換。

## 9. まとめ

| トピック | アプローチ |
|---------|-----------|
| デフォルト PIC | 全ツールチェーン `isPICDefaultForced()==true`、shellcode 前提に整合 |
| IR で先に修正 | 定数・間接ジャンプ・メモリ組込は可能な限り IR で除去 |
| MIR 安全ネット | `ShellcodeMIRPrepPass` + 前後フック、オブジェクト抽出/パッチは最終手段 |
| ハードコード最小化 | `TargetDesc` + `Options.td.h` テーブル駆動 |
| ユーザー/カーネル二次元 | `-fshellcode` × `-mshellcode-context={user,kernel}`；各 (OS, arch, level) が `describeTriple()` の 1 行 |
| Windows POSIX 互換 | `WinPEBImportPass` が 13 POSIX 関数グループをブリッジ（write→WriteFile、mmap→VirtualAlloc 等） |
| K&R 自動修正 | `SyscallStubPass` が 0 仮引数宣言で正規 POSIX シグネチャにフォールバック |

## 10. Shim ヘッダのクロスプラットフォーム定数

Shim ヘッダ（`sys/mman.h`、`fcntl.h` 等）はターゲットカーネル ABI に一致する定数を公開する必要がある。shellcode syscall stub がこれらの値を libc 変換なしで直接カーネルに渡すため。

主な差異：

| 定数 | Darwin | Linux/Android |
|------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

実装：shim ヘッダ内の `#if defined(__APPLE__)` ガード。`SyscallTables.cpp` POSIX 互換テーブルは Linux 値（`AT_FDCWD = -100`）を使用し、`SyscallABI::LinuxSvc0` / `LinuxSyscall` パスでのみアクティブ。Windows ターゲットはこれらの POSIX ヘッダを使用しない；POSIX→Win32 ブリッジは `WinPEBImportPass` 互換ラッパが処理。
