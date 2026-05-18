**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# NeverC Shellcode 跨平台架構概覽

本文檔描述「一套 pass 涵蓋 macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel」背後的設計原則。在擴充到新平台或上下文前請先閱讀。

相關子系統文檔：
- [README.md](../README.zh-TW.md) — 概述、CLI 選項、快速開始
- [ir-pass-design.md](../ir-pass-design/README.zh-TW.md) — IR 層 pass 職責與範例
- [mir-pass-design.md](../mir-pass-design/README.zh-TW.md) — MIR 層 prep pass + 混淆掛鉤
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.zh-TW.md) — 核心上下文設計細節
- [platform-extension-guide.md](../platform-extension-guide/README.zh-TW.md) — 新增平台的分步指南

---

## 1. 三維矩陣：OS × Arch × ExecutionLevel

所有跨平台差異收斂為一個**三維矩陣**，每個儲存格對應一個 `TargetDesc` 表項：

```
                ┌──── arm64 ────┬──── x86_64 ────┐
     Darwin ────┤ User / Kernel │ User / Kernel  │  Mach-O
     Linux  ────┤ User / Kernel │ User / Kernel  │  ELF
     Android────┤ User / Kernel │ User / Kernel  │  ELF
     Windows────┤ User / Kernel │ User / Kernel  │  COFF
                └───────────────┴────────────────┘
```

8 個 (OS, arch) 組合 × 2 ExecutionLevel = **16 個表項**，全部由 `describeTriple(triple, Level)` 回傳。

**核心設計理念**：pass 始終從表中讀取，從不寫 `if (OS == Darwin)` 分支。新增平台 = 在 `describeTriple()` 中填一列 + 在每個擷取器的 switch 中新增一個 case。

## 2. 流水線執行順序

當 `-fshellcode` 活躍時，編譯器遵循固定順序。**每個階段都有對應的混淆掛鉤**用於第三方 pass 插入。兩個關鍵不變量：

1. **後端 TableGen `.td` 是指令描述的唯一來源**。
2. **shellcode 沒有外部重定位和資料段**。

## 3. 全域 PIC 策略

`isPICDefaultForced()` 在所有三個工具鏈上**無條件回傳 true**。程式碼產生總是使用 PC 相對定址。

## 4. User / Kernel 正交維度

- **User**（預設）：PEB walk / syscall stub 流水線。
- **Kernel**：SyscallStubPass / WinPEBImportPass 短路；KernelImportPass 啟動。

## 5. 使用者態「普通 C」支援矩陣

使用 `-fshellcode` 時，大型陣列、浮點常數、computed-goto、memcpy/strlen、`__int128` 除法、原子操作、POSIX/Win32 標頭等均**無需使用者感知即直接支援**。

## 6. MIR 層：修復 / 回退 / 擷取三階段流水線

1. 跨平台偽指令清理
2. Shellcode 友善指令重寫（表驅動）
3. 外部參照 / 常數池稽核

## 7. 擷取器層

按 `ObjectFormat` 分發，共享同一契約：「接受段內 `.text` PC 相對修補，拒絕其餘一切」。

## 8. 混淆掛鉤點

11 個掛鉤（6 IR + 3 MIR + 2 位元組級）涵蓋每個流水線階段。

## 9. 新增 (OS, Arch) 條目

總成本：1 列 TargetDesc + syscall 表 + 擷取器 case + 測試。IR/MIR pass 需零改動。

## 10. 非目標

- C++ / ObjC / Rust 前端
- 32 位 / 大端 / 小眾 ISA
- 在 shellcode 中嵌入 libc 執行階段
- 絕對位址重定位
