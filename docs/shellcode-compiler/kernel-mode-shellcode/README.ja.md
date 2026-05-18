**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# カーネルモード（Ring-0）Shellcode サポート

`-fshellcode` は当初 ring-3 ペイロードのみをカバー。Ring-0 ペイロード（Windows ドライバ、Linux/Android カーネルモジュール、macOS kext）は ring-3 ABI を再利用できない：TEB/PEB が存在しない、syscall 命令はユーザー→カーネルのトラップ、x86_64 ではコードモデルとレッドゾーンの無効化が追加で必要。

## 1. コアスイッチ：`-mshellcode-context={user,kernel}`

- **ユーザーモード**（デフォルト）：既存 PEB / syscall stub パイプラインを維持。
- **カーネルモード**：SyscallStubPass / WinPEBImportPass 無効化、カーネルフラグ注入、KernelImportPass 起動、`__NEVERC_SHELLCODE_KERNEL__` 注入。

## 2. `TargetDesc` 新フィールド

`Level`（User/Kernel）、`KernelImport`、`KernelInjectFlags`。カーネルサポート追加は「テーブル 1 行追加」。

## 3. プラットフォーム別ドライバフラグ

| 次元 | x86_64 カーネル | AArch64 カーネル |
|------|----------------|-----------------|
| レッドゾーン | `-mno-red-zone` | 自然に不在 |
| コードモデル | `-mcmodel=kernel` | 既存 `-mcmodel=small` 再利用 |
| 暗黙 SIMD | `-mno-sse -mno-sse2 -mno-mmx` | `-mgeneral-regs-only` |

## 4. `KernelImportPass`：自動リゾルバ注入

未解決 extern 直接呼出をリゾルバ支援間接呼出に自動リライト。ユーザーは普通の C を書く。暗黙 `(resolver, cookie)` パラメータをエントリに注入。FNV-1a 64 ビットハッシュ。三層防御（IR → MIR → 抽出器）。

## 5–6. Android カーネル、ヘッダ分割

Ring-3 は bionic + Linux syscall ABI；Ring-0 は純 Linux カーネル。`<neverc/kernel.h>` がカーネルモード API を提供。

## 7. Ring-0 Shellcode の記述

### 7.1 純計算ペイロード
```c
#include <neverc/kernel.h>
NEVERC_KERNEL_ENTRY
int shellcode_entry(int a, int b) { return a * 13 + b * 7; }
```

### 7.2 リゾルバベースペイロード
`neverc_kern_resolve_t resolver` と `neverc_kern_hash("printk")` でカーネル関数を解決。

## 8. ロードマップ

カーネルコンテキスト切替、リゾルバリライト、純計算/リゾルバペイロードすべて完了。カーネル SDK ヘッダサブセットは計画中。
