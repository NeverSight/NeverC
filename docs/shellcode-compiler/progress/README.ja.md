**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# Shellcode コンパイラ — 進捗トラッカー

## ステージ 0 — macOS arm64 MVP（完了）

- [x] ディレクトリ＆ CMake スケルトン（`nevercShellcode` ライブラリ）
- [x] `ZeroRelocPass`：2 フェーズ（Prep + Stackify）、可変グローバルの自動スタック化
- [x] `Data2TextPass`：2 フェーズ（定数配列 → スタックチャンクストア、SROA 後ベクタ定数分割、ConstantFP → volatile ロードビットパターン）
- [x] `SyscallStubPass`：テーブル駆動ホワイトリスト、Darwin BSD / Linux arm64 / Linux x86_64 / Android syscall をカバー
- [x] `AllBlrPass`：オプションの積極的間接呼び出し書き換え
- [x] `ShellcodeExtractor`：Mach-O `.o` → フラット `.bin`、セクション内リロケーションパッチ付き
- [x] CLI オプション（生成された `neverc/include/neverc/Invoke/Options.td.h` 経由）：`-fshellcode`、`-fshellcode-all-blr`、`-mshellcode-syscall`、`-fshellcode-keep-obj=`、`-fshellcode-entry=`
- [x] 全プラットフォームデフォルト PIC（`isPICDefault()` が統一的に `true` を返す）
- [x] 汎用再帰スタック化（関数ポインタテーブル、文字列ポインタテーブル、ネスト構造体テーブル、ConstantExpr GEP/BitCast 初期化子）
- [x] `IndirectBrPass`：GCC computed-goto（`&&label`）→ switch、複数ディスパッチサイトテーブル共有を含む
- [x] SIMD ベクタ定数インライン化（`inlineVectorConstants`）
- [x] `_Thread_local` の static への自動降格
- [x] ネイティブ macOS arm64 ローダー（MAP_JIT + i-cache flush）

**テスト**：108/108 shellcode アサーション合格。バイナリサイズ：`add` 8B、`fib` 64B、`hello` 64B、`big_const` 632B。

## ステージ 1 — Linux / Android / Windows クロスプラットフォーム（完了）

- [x] `TargetDesc` 抽象化：テーブル駆動のプラットフォーム差異
- [x] クロスプラットフォーム `-mshellcode-syscall` セマンティクス（Darwin 専用の `-mshellcode-libsystem` を置き換え）
- [x] Linux / Android syscall 番号テーブル（Darwin BSD 100+、Linux arm64 130+、Linux x86_64 150+）
- [x] `ShellcodeExtractor` を `MachOExtractor` / `ELFExtractor` / `COFFExtractor` にリファクタ
- [x] ELF エクストラクタ（arm64：`R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/等、x86_64：`R_X86_64_PC32`/`PLT32`）
- [x] COFF エクストラクタ（arm64：`IMAGE_REL_ARM64_BRANCH26`/等、x86_64：`IMAGE_REL_AMD64_REL32`/等）
- [x] Windows PEB インポートパス（`WinPEBImportPass`）、リアル PEB walk リゾルバ付き
- [x] マルチ DLL Win32 API ホワイトリスト（kernel32/ntdll/user32/ws2_32/advapi32/shell32 にわたる約 210 API）
- [x] `MemIntrinPass`：memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/等 → インラインバイトループヘルパー
- [x] `CompilerRtPass`：`__int128` 除算/剰余 → インライン長除算ヘルパー
- [x] Windows `aarch64-pc-windows-msvc` フロントエンドサポート
- [x] `MIRPrepPass`：クロスプラットフォーム疑似命令除去（CFI/EH/XRay/StackMap/SEH/FENTRY/等）
- [x] MIR + バイトレベル難読化フック（IR/MIR/バイトストリーム 3 層で計 11 フック）
- [x] AArch64 非 Darwin `long double` binary64 への自動ダウングレード
- [x] Shellcode シムヘッダー：`<windows.h>`、`<unistd.h>`、`<fcntl.h>`、`<sys/stat.h>`、`<sys/mman.h>`、`<string.h>`、`<stdlib.h>`
- [x] Windows POSIX 互換レイヤー（13 POSIX→Win32 ブリッジ：write→WriteFile、mmap→VirtualAlloc 等）
- [x] K&R 暗黙宣言自動修正（50+ 標準 POSIX シグネチャ）
- [x] テーブル駆動精製（アーキテクチャ分岐ハードコーディング → ゼロ）
- [x] `KernelImportPass`：ring-0 自動リゾルババック付きコールサイト書き換え
- [x] カーネルヘルパー名テーブル駆動診断（`KernelHelperNames.def`）
- [x] `<neverc/kernel.h>`：ring-0 エントリ規約用
- [x] エントリオフセットゼロ強制（`placeEntryFirst`）
- [x] ファイナライズパイプライン：バッドバイトリライター SDK + 文字セットエンコーダー SDK + サイズ制約
- [x] プラグイン SDK（`Plugin.h`）：`registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] x86_64 `-mno-implicit-float` インジェクション（バックエンド SSE 定数プールスピル防止）
- [x] クロスプラットフォームローダー（macOS/Linux/Windows）

**テスト**：743+ shellcode アサーション、8 トリプルすべてで合格。NeverC 全体のテストスイート：1000+ テスト合格。

## ステージ 2 — 印字可能 / 英数字エンコーダー（予定）

- [ ] ARM64 印字可能 shellcode エンコーダー（0x20–0x7e 命令サブセット）
- [ ] x86_64 英数字エンコーダー
- [ ] 自己デコードスタブ（decoder stub）生成
- [ ] エンコード後サイズ / エントロピー統計

## ステージ 3 — ポリモーフィズム / 自己書き換え（予定）

- [ ] ポリモーフィックエンジン：同一ソース → コンパイルごとに異なる等価バイト列
- [ ] 自己書き換えコード：実行時ペイロード本体の復号 / 解凍
- [ ] 検出回避：既知の shellcode シグネチャパターンの回避

## 将来の拡張

- [ ] iOS arm64（コード署名 + JIT ジェイルブレイクシナリオ）
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
