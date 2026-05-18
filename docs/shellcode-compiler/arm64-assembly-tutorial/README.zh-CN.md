**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# ARM64 (AArch64) 汇编教程 — Shellcode 视角

> 面向不熟悉 ARM64 的读者，聚焦 shellcode 编译器生成的指令。每条指令配有注解和前后对比。

## 1. 寄存器概览

```
通用寄存器：
  x0 ~ x30    64 位通用寄存器
  w0 ~ w30    对应的低 32 位别名
  x29 (fp)    帧指针
  x30 (lr)    链接寄存器（保存返回地址）
  sp          栈指针（不是 x31！）
  xzr / wzr   零寄存器（读取始终返回 0，写入被丢弃）

特殊寄存器：
  pc          程序计数器（不可直接写入）

Apple ABI 保留：
  x16, x17    平台保留（过程内调用暂存）
  x18         平台保留（TLS 基址，Apple 专用）

调用约定（AAPCS64）：
  参数：      x0~x7（整数），d0~d7（浮点）
  返回值：    x0（整数），d0（浮点）
  被调用者保存：x19~x28, x29(fp), x30(lr), sp
  调用者保存：x0~x18, d0~d31
  红区：      sp 以下 128 字节（叶函数可直接使用）
```

## 2. 分支与调用

### `b label` — 无条件分支

```asm
b .Lloop    ; 分支到 .Lloop（PC 相对，±128MB 范围）
```

### `bl label` — 带链接的分支（函数调用）

```asm
bl _fib     ; 分支到 _fib，返回地址保存到 lr (x30)
            ; 编码为 imm26 偏移 → 产生 ARM64_RELOC_BRANCH26！
```

**为什么 shellcode 必须避免 `bl`**：`bl` 使用 26 位立即偏移，由链接器填充。对外部符号会产生 `ARM64_RELOC_BRANCH26` 重定位。shellcode 没有链接器，所以必须替换为 `blr`。

### `br xN` / `blr xN` — 寄存器间接分支/调用

```asm
blr x8      ; 分支到 x8 中的地址，返回地址保存到 lr
br  x8      ; 分支到 x8 中的地址，不保存链接（尾调用）
```

### `ret` — 函数返回

```asm
ret         ; 等效于 br lr，跳转到 x30 中保存的地址
```

## 3. PC 相对寻址

### `adr xN, label` — PC 相对加载（±1MB）

```asm
adr x0, .Ldata   ; x0 = PC + offset_to_.Ldata
```

### `adrp xN, label@PAGE` + `add xN, xN, label@PAGEOFF`

```asm
adrp x0, _sym@PAGE        ; x0 = (PC & ~0xFFF) + page_offset
add  x0, x0, _sym@PAGEOFF ; x0 += 页内偏移
; 产生 ARM64_RELOC_PAGE21 + ARM64_RELOC_PAGEOFF12 重定位
```

等效于 x86_64 的 `lea rax, [rip + _sym]`，但拆分为两条指令。

## 4. 立即数加载

### `mov xN, #imm16` — 加载 16 位立即数

```asm
mov x0, #0x4142    ; x0 = 0x4142
```

### `movk xN, #imm16, lsl #shift` — 保留其他位，写入 16 位片段

```asm
; 构造 64 位值 0x0001_0002_0003_0004：
mov  x0, #0x0004           ; x0 = 0x0000_0000_0000_0004
movk x0, #0x0003, lsl #16  ; x0 = 0x0000_0000_0003_0004
movk x0, #0x0002, lsl #32  ; x0 = 0x0000_0002_0003_0004
movk x0, #0x0001, lsl #48  ; x0 = 0x0001_0002_0003_0004
```

**这就是 Data2TextPass 的核心**：常量数据（如字符串 `"hello\n"`）不能放在 `.data` 中（会产生重定位），所以 Data2TextPass 将其转为存放在栈上的 mov/movk 序列。

## 5. 内存访问

### `ldr / str` — 加载/存储

```asm
ldr x0, [sp, #16]    ; x0 = *(sp + 16)，64 位加载
str x0, [sp, #16]    ; *(sp + 16) = x0，64 位存储
ldrb w0, [x1]        ; w0 = *(uint8_t*)x1，零扩展到 32 位
strb w0, [x1, #3]    ; *(uint8_t*)(x1 + 3) = w0 的低 8 位
```

### `ldp / stp` — 加载/存储寄存器对

```asm
stp x29, x30, [sp, #-16]!  ; sp -= 16 后存储 x29 和 x30（前索引）
ldp x29, x30, [sp], #16    ; 加载 x29 和 x30 后 sp += 16（后索引）
```

### 栈帧示例

```asm
; 序言
stp x29, x30, [sp, #-16]!    ; 保存帧指针和返回地址
mov x29, sp                   ; 建立帧指针

; ... 函数体 ...

; 尾声
ldp x29, x30, [sp], #16      ; 恢复帧指针和返回地址
ret                           ; 返回
```

## 6. 算术与逻辑

```asm
add  x0, x1, x2        ; x0 = x1 + x2
sub  x0, x1, #42       ; x0 = x1 - 42
and  x0, x1, #0xFF     ; x0 = x1 & 0xFF
orr  x0, x1, x2        ; x0 = x1 | x2
eor  x0, x1, x2        ; x0 = x1 ^ x2
lsr  x0, x1, #1        ; x0 = x1 >> 1（逻辑右移）
lsl  x0, x1, #3        ; x0 = x1 << 3
```

## 7. 比较与条件分支

```asm
cmp x0, #0              ; 比较 x0 与 0（设置条件码）
b.eq .Lzero             ; 相等则分支
b.ne .Lnonzero          ; 不等则分支
b.lt .Lnegative         ; 小于则分支（有符号）
b.gt .Lpositive         ; 大于则分支（有符号）

; 条件选择
csel x0, x1, x2, eq    ; x0 = (eq) ? x1 : x2
```

## 8. 本项目生成的典型指令序列

### 纯计算 add(3, 4) → 7

```asm
_main:
    add  w0, w0, w1     ; w0 = 3 + 4 = 7
    ret                 ; 返回 7
```

### 递归斐波那契

```asm
; ZeroRelocPass 强制 always_inline → 编译器将递归展开为循环
_main:
    mov  w8, #0         ; fib[0] = 0
    mov  w9, #1         ; fib[1] = 1
.Lloop:
    cmp  w0, #1
    b.le .Ldone
    add  w10, w8, w9    ; fib[i] = fib[i-1] + fib[i-2]
    mov  w8, w9
    mov  w9, w10
    sub  w0, w0, #1
    b    .Lloop
.Ldone:
    mov  w0, w9
    ret
```

### 字符串内联到栈（Data2TextPass）

```asm
; Data2TextPass + 后端处理后：
_main:
    mov  w8, #0x4241    ; w8 = 'A' | ('B' << 8) = 0x4241（小端）
    strh w8, [sp, #-4]! ; 存储到栈（2 字节 + 填充）
    mov  w9, #0         ; 存储 '\0'
    strb w9, [sp, #2]
    ldrb w0, [sp]       ; 加载 'A' = 65
    ldrb w1, [sp, #1]   ; 加载 'B' = 66
    add  w0, w0, w1     ; 65 + 66 = 131
    add  sp, sp, #4
    ret
```

### 系统调用（SyscallStubPass — 直接 svc）

Darwin arm64 BSD syscall ABI：
- `x16` = 系统调用号
- `x0..x7` = 参数
- `svc #0x80` 触发陷入
- 返回值在 `x0`，错误设置 Carry 标志

```asm
_main:
    sub  sp, sp, #16
    ; 在栈上构造 "hi\n"（Data2TextPass 风格）
    mov  w8, #0x6968         ; 'hi' 小端
    strh w8, [sp, #12]
    mov  w9, #0x0a           ; '\n'
    strb w9, [sp, #14]

    ; write(1, &msg, 3) — 直接 svc #0x80
    mov  x16, #4             ; SYS_write
    mov  x0,  #1             ; fd   = 1
    add  x1,  sp, #12        ; buf  = &msg
    mov  x2,  #3             ; n    = 3
    svc  #0x80               ; ← 无 bl/blr，无导入，无重定位

    ; exit(0)
    mov  x16, #1             ; SYS_exit
    mov  x0,  #0             ; status
    svc  #0x80

    add  sp, sp, #16
    ret
```

整个指令流 100% 在 `__TEXT,__text` 内。`.o` 重定位表除段内分支外为空 — 这就是"真正的 shellcode"。

## 9. 关键总结

| 概念 | x86_64 等效 | ARM64 | Shellcode 注意 |
|------|------------|-------|---------------|
| 函数调用 | `call rel32` | `bl imm26` | 段内：提取器修补 BRANCH26；跨段：用 `blr xN` |
| 地址加载 | `lea rax, [rip+sym]` | `adrp+add` | 段内：提取器修补 PAGE21/PAGEOFF12；跨段：拒绝 |
| 64 位立即数 | `mov rax, imm64` | `mov+movk ×4` | 无重定位，Data2TextPass 的核心 |
| 序言 | `push rbp; mov rbp,rsp` | `stp x29,x30,[sp,#-16]!` | 单条指令保存寄存器对 |
| 返回 | `ret` | `ret` | 等效于 `br lr` |
| 系统调用 | `syscall` | `svc #0x80` | Darwin BSD：x16 = nr, x0~x7 = args |
