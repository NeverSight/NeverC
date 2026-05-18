**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# 核心模式（Ring-0）Shellcode 支援

`-fshellcode` 最初僅涵蓋 ring-3 載荷。Ring-0 載荷（Windows 驅動、Linux/Android 核心模組、macOS kext）無法簡單複用 ring-3 ABI：TEB/PEB 不存在、syscall 指令是使用者到核心的陷入、x86_64 還需要不同的程式碼模型和停用紅區。

## 1. 核心開關：`-mshellcode-context={user,kernel}`

- **使用者模式**（預設）：維持現有 PEB / syscall stub 流水線。
- **核心模式**：停用 SyscallStubPass / WinPEBImportPass、注入核心標誌、啟用 KernelImportPass、注入 `__NEVERC_SHELLCODE_KERNEL__`。

## 2. `TargetDesc` 新欄位

`Level`（User/Kernel）、`KernelImport`、`KernelInjectFlags`。新增核心支援仍是「多加一列表」。

## 3. 每平台驅動旗標差異

| 維度 | x86_64 核心 | AArch64 核心 |
|------|------------|-------------|
| 紅區 | `-mno-red-zone` | 天然不存在 |
| 程式碼模型 | `-mcmodel=kernel` | 複用 `-mcmodel=small` |
| 隱式 SIMD | `-mno-sse -mno-sse2 -mno-mmx` | `-mgeneral-regs-only` |

## 4. `KernelImportPass`：自動解析器注入

自動重寫未解析的 extern 直接呼叫為解析器支援的間接呼叫。使用者寫普通 C；pass 處理改寫。隱式參數 `(resolver, cookie)` 注入入口前端。FNV-1a 64 位元雜湊。三層防禦（IR → MIR → 擷取器）。

## 5–6. Android 核心、標頭檔劃分

Ring-3 使用 bionic + Linux syscall ABI；Ring-0 使用純 Linux 核心。`<neverc/kernel.h>` 提供核心模式 API。

## 7. 編寫 Ring-0 Shellcode

### 7.1 純計算載荷
```c
#include <neverc/kernel.h>
NEVERC_KERNEL_ENTRY
int shellcode_entry(int a, int b) { return a * 13 + b * 7; }
```

### 7.2 基於解析器的載荷
使用 `neverc_kern_resolve_t resolver` 和 `neverc_kern_hash("printk")` 解析核心函式。

## 8. 路線圖

核心上下文切換、解析器改寫、純計算與解析器載荷均已完成。核心 SDK 標頭子集計畫中。
