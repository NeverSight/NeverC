**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# ARM64 (AArch64) 組語教學 — Shellcode 視角

> 面向不熟悉 ARM64 的讀者，聚焦 shellcode 編譯器產生的指令。每條指令配有註解和前後對比。

## 1. 暫存器概覽

```
通用暫存器：
  x0 ~ x30    64 位元通用暫存器
  w0 ~ w30    對應的低 32 位元別名
  x29 (fp)    框架指標
  x30 (lr)    連結暫存器（保存回傳位址）
  sp          堆疊指標（不是 x31！）
  xzr / wzr   零暫存器（讀取始終回傳 0，寫入被丟棄）

特殊暫存器：
  pc          程式計數器（不可直接寫入）

Apple ABI 保留：
  x16, x17    平台保留（過程內呼叫暫存）
  x18         平台保留（TLS 基址，Apple 專用）

呼叫慣例（AAPCS64）：
  引數：      x0~x7（整數），d0~d7（浮點）
  回傳值：    x0（整數），d0（浮點）
  被呼叫者保存：x19~x28, x29(fp), x30(lr), sp
  呼叫者保存：x0~x18, d0~d31
  紅區：      sp 以下 128 位元組（葉函式可直接使用）
```

## 2. 分支與呼叫

### `b label` — 無條件分支
```asm
b .Lloop    ; 分支到 .Lloop（PC 相對，±128MB 範圍）
```

### `bl label` — 帶連結的分支（函式呼叫）
```asm
bl _fib     ; 分支到 _fib，回傳位址保存到 lr (x30)
            ; 編碼為 imm26 偏移 → 產生 ARM64_RELOC_BRANCH26！
```

**為什麼 shellcode 必須避免 `bl`**：`bl` 使用 26 位元立即偏移，由連結器填充。對外部符號會產生 `ARM64_RELOC_BRANCH26` 重定位。shellcode 沒有連結器，所以必須替換為 `blr`。

### `br xN` / `blr xN` — 暫存器間接分支/呼叫
### `ret` — 函式回傳

## 3. PC 相對定址

`adr`（±1MB）和 `adrp + add`（±4GB 頁對齊），等效於 x86_64 的 `lea rax, [rip + _sym]`。

## 4. 立即數載入

`mov` + `movk` 序列構造 64 位元值。**這就是 Data2TextPass 的核心**：常數資料轉為 mov/movk 序列存放在堆疊上。

## 5. 記憶體存取

`ldr/str`（載入/儲存）、`ldp/stp`（暫存器對載入/儲存）、前索引和後索引定址。

## 6. 算術與邏輯

`add`、`sub`、`and`、`orr`、`eor`、`lsr`、`lsl`。

## 7. 比較與條件分支

`cmp` + 條件分支（`b.eq`、`b.ne`、`b.lt`、`b.gt`）、條件選擇（`csel`）。

## 8. 本專案產生的典型指令序列

包含：純計算 add、遞迴斐波那契、字串內嵌到堆疊（Data2TextPass）、系統呼叫（SyscallStubPass 直接 svc）。

整個指令流 100% 在 `__TEXT,__text` 內。`.o` 重定位表除段內分支外為空 — 這就是「真正的 shellcode」。

## 9. 關鍵總結

| 概念 | x86_64 等效 | ARM64 | Shellcode 注意 |
|------|------------|-------|---------------|
| 函式呼叫 | `call rel32` | `bl imm26` | 段內：擷取器修補 BRANCH26；跨段：用 `blr xN` |
| 位址載入 | `lea rax, [rip+sym]` | `adrp+add` | 段內：擷取器修補 PAGE21/PAGEOFF12 |
| 64 位元立即數 | `mov rax, imm64` | `mov+movk ×4` | 無重定位，Data2TextPass 核心 |
| 序言 | `push rbp; mov rbp,rsp` | `stp x29,x30,[sp,#-16]!` | 單條指令保存暫存器對 |
| 系統呼叫 | `syscall` | `svc #0x80` | Darwin BSD：x16 = nr, x0~x7 = args |
