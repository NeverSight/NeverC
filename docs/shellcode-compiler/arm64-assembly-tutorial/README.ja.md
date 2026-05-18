**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# ARM64 (AArch64) アセンブリチュートリアル — Shellcode の観点から

> ARM64 に不慣れな読者向け。shellcode コンパイラが生成する命令に焦点を当て、各命令にアノテーションと前後比較を付記。

## 1. レジスタ概要

```
汎用レジスタ：
  x0 ~ x30    64 ビット汎用
  w0 ~ w30    対応する下位 32 ビットエイリアス
  x29 (fp)    フレームポインタ
  x30 (lr)    リンクレジスタ（戻りアドレスを保持）
  sp          スタックポインタ（x31 ではない！）
  xzr / wzr   ゼロレジスタ

特殊レジスタ：
  pc          プログラムカウンタ（直接書込不可）

Apple ABI 予約：
  x16, x17    プラットフォーム予約
  x18         TLS ベース（Apple 専用）

呼出規約（AAPCS64）：
  引数：    x0~x7（整数）, d0~d7（浮動小数点）
  戻り値：  x0（整数）, d0（浮動小数点）
  callee 保存：x19~x28, x29, x30, sp
  caller 保存：x0~x18, d0~d31
  レッドゾーン：sp 下 128 バイト
```

## 2. 分岐と呼出

- `b label` — 無条件分岐（PC 相対 ±128MB）
- `bl label` — リンク付き分岐（imm26 → ARM64_RELOC_BRANCH26 生成）
- **shellcode が `bl` を避ける理由**：26 ビット即値オフセットにリンカが埋める。外部シンボルでは reloc が発生。`blr` で代替必須。
- `br xN` / `blr xN` — レジスタ間接分岐/呼出
- `ret` — `br lr` 相当

## 3. PC 相対アドレッシング

`adr`（±1MB）と `adrp + add`（±4GB ページ整列）。x86_64 の `lea rax, [rip + _sym]` 相当だが 2 命令に分割。

## 4. 即値ロード

`mov` + `movk` シーケンスで 64 ビット値構築。**Data2TextPass の核心**：定数データを mov/movk シーケンスとしてスタックに格納。

## 5. メモリアクセス

`ldr/str`、`ldp/stp`（ペアロード/ストア）、プレインデックス・ポストインデックスアドレッシング。

## 6. 算術・論理演算

`add`、`sub`、`and`、`orr`、`eor`、`lsr`、`lsl`。

## 7. 比較と条件分岐

`cmp` + 条件分岐 (`b.eq`、`b.ne`、`b.lt`、`b.gt`)、条件選択 (`csel`)。

## 8. 本プロジェクトが生成する典型命令列

純計算 add、再帰フィボナッチ、文字列のスタックインライン化（Data2TextPass）、システムコール（SyscallStubPass 直接 svc）を含む。

全命令列が `__TEXT,__text` 内に 100% 収まる — これが「真の shellcode」。

## 9. キーサマリー

| 概念 | x86_64 相当 | ARM64 | Shellcode 注記 |
|------|------------|-------|---------------|
| 関数呼出 | `call rel32` | `bl imm26` | intra-section: 抽出器が BRANCH26 パッチ |
| アドレスロード | `lea rax, [rip+sym]` | `adrp+add` | intra-section: PAGE21/PAGEOFF12 パッチ |
| 64 ビット即値 | `mov rax, imm64` | `mov+movk ×4` | reloc なし、Data2TextPass 核心 |
| プロローグ | `push rbp; mov rbp,rsp` | `stp x29,x30,[sp,#-16]!` | 1 命令でペア保存 |
| syscall | `syscall` | `svc #0x80` | Darwin BSD: x16=nr, x0~x7=args |
