**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# 路線圖

本文件追蹤計畫中、進行中或有意推遲的功能。

## 目前狀態

NeverC 的 shellcode 管線涵蓋：

- 完整的 LLVM IR 管線，包含 11+ 個專用 pass
- COFF / ELF / Mach-O 擷取器
- Win32 PEB-walk 匯入解析（ROR-13 雜湊，6 個 DLL 桶）
- 直接系統呼叫降級（Darwin `svc #0x80`、Linux `svc #0` / `syscall`）
- 核心模式支援（Windows、Linux）
- 壞位元組稽核，支援可設定的 profile
- 壞位元組重寫器和字元集編碼器的外掛 SDK
- 大小 / 對齊 / 填充約束（`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=`）
- IR、MIR 和位元組流三層共 11 個混淆鉤子

## 已完成（2026-04）

1. **大小 / 對齊 / 填充約束** — 內建功能。`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=` 在 `finalizeShellcodeBytes` 末尾執行。驅動會拒絕矛盾的設定（例如填充位元組在壞位元組集中，或無 align/max-length 時使用 pad）。

2. **樹外 C 外掛 API** — 純 C ABI 外掛介面（`NevercPluginAPI.h`），用於自訂 IR、MIR、Binary 和 Linker pass。外掛透過 11 個 shellcode 掛鈎點（`NEVERC_HOOK_SC_*`）註冊。單一標頭檔 SDK，零 LLVM/CRT 相依性。詳見 [Plugin API 文件](../../plugin-api/README.zh-TW.md)。

## 計畫中 — 外掛層（透過鉤子）

以下能力**有意不內建**。它們屬於策略/混淆層，設計為透過鉤子和外掛介面由第三方外掛提供。

| 功能 | 鉤子位置 | 說明 |
|------|----------|------|
| 反反組譯 | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | 指令前綴干擾、跳轉重排、垃圾位元組插入 |
| 多型 | `RunAfterFinalMIR` / `RunPostExtract` | 基於種子的每次編譯輸出變化 |
| 分階段編碼器（XOR / RC4 / 自解密） | `RunPostExtract` / `RunPostFinalize` | 編譯時期樁產生 + 載荷加密 |
| 間接系統呼叫（Halos / Tartarus / Recycled Gate） | IR 級外掛或 `RunPostExtract` | 執行時期 ntdll gadget 掃描 |
| 睡眠遮罩 / 呼叫堆疊偽造 | IR pass 外掛 | Ekko / FOLIAGE / Cronos 模式 |
| ETW / AMSI 修補 | IR pass 外掛 | 執行時期修補序列 |
| 模組踐踏 / 脫鉤 | IR pass 外掛 | 記憶體操作模式 |

## 外掛鉤子概覽

三層共 11 個鉤子：

**IR 層（6 個鉤子，接收 `ModulePassManager &`）**：
- `RunBeforePrep` — 在任何 shellcode pass 之前
- `RunAfterPrep` — 連結統一之後
- `RunBeforeInlining` — AlwaysInliner 之前的最後機會
- `RunAfterInlining` — IR 完全扁平化為一個函式
- `RunAfterStackify` — 程式碼產生前的最終 IR 形態
- `RunAfterFinalIR` — `AllBlrPass` 之後，絕對最後的 IR 鉤子

**MIR 層（3 個鉤子，接收 `TargetPassConfig &`）**：
- `RunBeforePreEmit` — 暫存器已分配，CFI/EH 偽指令仍存在
- `RunAfterPreEmit` — `MIRPrepPass` 清理後，最接近最終位元組
- `RunAfterFinalMIR` — LLVM `addPreEmitPass2()` 之後，緊接 AsmPrinter 之前

**位元組流層（2 個鉤子，接收 `SmallVectorImpl<uint8_t> &`）**：
- `RunPostExtract` — 預 finalize，仍由重寫器/編碼器/稽核/大小處理
- `RunPostFinalize` — 後 finalize，寫入磁碟前的最後時刻；NeverC 不再進行任何稽核

## Finalize 管線

每個擷取器在寫入 `.bin` 之前呼叫 `finalizeShellcodeBytes`：

```
applyPostExtractObfuscationHook       (C Plugin API: NEVERC_HOOK_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (內建硬稽核)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationHook      (C Plugin API: NEVERC_HOOK_SC_POST_FINALIZE)
```

用法和程式碼範例見 [Plugin API 文件](../../plugin-api/README.zh-TW.md)。

## 不計畫實作

- **跨語言前端** — NeverC 僅接受自身的 C23 前端。IR 管線與前端解耦，但接受外部 bitcode（例如來自 `rustc` 或 `zig`）不是專案目標。
