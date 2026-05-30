**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# IR Pass 設計 — 原則、流水線與前後對比

> 本文檔解釋 shellcode 編譯流水線中每個 pass 的**設計理由**。實作細節在 `.cpp` 原始碼的英文註解中。

---

## 0. 核心理念

shellcode 的目標一句話概括：**消除 `.o` 中一切會變成重定位的東西，只留下可以直接 `mmap(RWX)` + `memcpy` + `blr` 的純指令流。**

我們不想把這個約束洩漏給使用者 — 使用者應該寫普通 C，流水線在內部處理產生重定位的 IR 構造。

| Pass | 職責 | 執行時機 |
|------|------|---------|
| ZeroRelocPass (Prep) | 統一連結屬性 / 強制 always_inline / 拒絕硬阻塞 | PipelineStart |
| IndirectBrPass | 計算跳躍 `indirectbr` → `switch` | PipelineStart |
| MemIntrinPass | `@llvm.mem*` + 顯式 mem*/str*/abs 呼叫 → 內部位元組迴圈輔助 | PipelineStart |
| StringRuntimePass | 內建 `string` 型別執行階段 → 棧分配 arena 變體 | PipelineStart |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → arena 分配 + 大分配 OS 回退 | PipelineStart |
| CompilerRtPass | `__udivti3` 家族 + IR 級 i128 div/rem → 內部 128 位元長除法 | PipelineStart |
| SyscallStubPass | libc extern → 目標 OS 核心陷入包裝，透過 `TargetDesc` 表驅動 | PipelineStart |
| WinPEBImportPass | Win32 extern → PEB 模組遍歷 + PE 匯出解析器 + 加密位址快取 | PipelineStart |
| KernelImportPass | Ring-0 extern → 解析器支援的間接呼叫 + 加密位址快取（僅核心模式） | PipelineStart |
| Data2TextPass (Phase 1) | 常數 GV → 立即數 / 棧塊儲存 | PipelineStart |
| *(LLVM 標準最佳化)* | AlwaysInliner / SROA / InstCombine / SLP / ... | O-level |
| Data2TextPass (Phase 2) | 拆分 SROA 殘留向量 store，消費晚期常數 | OptimizerLast |
| ZeroRelocPass (Stackify) | 可變全域 → 入口 alloca + 最終驗證 | OptimizerLast |
| AllBlrPass (可選) | 直接呼叫 → 間接呼叫 | OptimizerLast |
| MIRPrepPass | MIR 兜底：剝離 CFI/EH/XRay/SEH/等偽指令 | Before addPreEmitPass |
| ShellcodeExtractor | 掃描 `.o` 進行最終稽核 + 輸出 flat `.bin` | 後處理 |

---

## 1. ZeroRelocPass

### 1.1 Prep 階段 — 連結屬性統一
所有非入口函式 → `internal` + `alwaysinline`。拒絕 `__attribute__((constructor))`、`external_weak`。`_Thread_local` 靜默降級為 static。

### 1.2 Stackify 階段 — 全域變數消除
每個可變 GV 移到入口函式的 `alloca`。最終驗證拒絕任何剩餘 GV。

### 1.3 `placeEntryFirst`
確保入口函式在 `.bin` 偏移 0。

---

## 2. IndirectBrPass
GCC computed-goto (`goto *labels[op]`) → 基於索引的 `switch`，零重定位。

## 3. SyscallStubPass
extern libc → 平台內嵌組語陷入包裝。所有模板來自 `TargetDesc`。POSIX 兼容層：`open` → `openat(AT_FDCWD, ...)`。K&R 自動修復。

## 4. WinPEBImportPass
Win32 extern → PEB walk 解析器。約 190 API，6 DLL。Windows POSIX 相容：13 函式群組橋接。

### 4.1 位址快取加密

已解析的 API 位址在存入快取全域變數前經過加密（防內存掃描）。預設使用無 XOR 指令的算術分解 `(a + b) - 2*(a & b)` 搭配 `volatile` 中間值，阻止 LLVM 最佳化回 `xor` 指令。加密基礎設施（`PtrCacheHelpers.h`）被 `WinPEBImportPass` 和 `KernelImportPass` 共享。

**三個可插拔輔助函式**（均為 `internal alwaysinline`）：

| 函式 | 簽名 | 用途 |
|------|------|------|
| `__sc_derive_key` | `() → i64` | 運行時派生加密金鑰 |
| `__sc_ptr_encrypt` | `(ptr) → i64` | 加密函式指標以存入快取 |
| `__sc_ptr_decrypt` | `(i64) → ptr` | 將快取值解密還原為函式指標 |

**預設實作**：無 XOR 指令的算術分解。`key = (PEB + seed) - 2*(PEB & seed)`（Windows 用戶態）或純種子（核心態）。`encrypt/decrypt = (a + b) - (a & b) - (b & a)`，`volatile` 中間值阻止 LLVM 最佳化回 `xor`。

**快取槽**：`@__sc_cache_<dll>_<api>`（i64，初始 0，放在 `.text` 段，8 位元組對齊）。`0` = 未解析；非零 = 加密後的函式指標。

**Fast/Slow 路徑**：fast path（`atomic_load → decrypt → 間接呼叫`，約 10 條指令）、slow path（完整 PEB walk → `encrypt → cmpxchg`）。使用 `cmpxchg weak` 保證執行緒安全。

**覆蓋預設加密**：在源碼中定義同名函式即可。`encrypt` 和 `decrypt` 必須互為數學逆操作、必須 `always_inline`、不得呼叫外部函式。

## 5. MemIntrinPass
memcpy/memset/strlen/strcpy 等 → `internal alwaysinline` 位元組迴圈辅助。

## 6. CompilerRtPass
`__int128` 除法/取模 → 內嵌移位減法長除法。

## 7. Data2TextPass
Phase 1：常數 GV → 立即數/棧塊儲存。ConstantFP → volatile 載入位元模式。
Phase 2：拆分 SROA 殘留向量 store。內嵌向量常數。

## 8. AllBlrPass（可選）
直接呼叫 → volatile 槽 + `blr xN` / `call *rax` 間接呼叫。

## 9. 混淆掛鉤
11 個掛鉤點（`NEVERC_HOOK_SC_*`）。詳見 [Plugin API — 勾點](../../plugin-api/README.zh-TW.md#5-勾點)。

## 10. 兩階段設計理據
Phase 1 清理原始 IR；LLVM 最佳化後 Phase 2 清理最佳化器引入的新構造。

## 11. KernelImportPass（僅 ring-0）
自動重寫 extern 直接呼叫為帶加密位址快取的解析器間接呼叫，注入 `(resolver, cookie)` 前綴參數。每個 API 的解析結果經加密後快取，後續呼叫直接使用快取（lock-free `cmpxchg`）。加密機制與 WinPEBImportPass 共享（參見 [§4.1](#41-位址快取加密)）。見 [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.zh-TW.md)。

## 12. StringRuntimePass
內建 `string` 型別方法 → 棧分配 arena 變體。

## 13. HeapArenaPass

將 shellcode 中的 `malloc`/`free`/`calloc`/`realloc` 呼叫改寫為混合分配策略（預設開啟，透過 `-fshellcode-heap-arena` / `-fno-shellcode-heap-arena` 控制）：

- **小分配（≤ 64 KB）**：從 `StringRuntimePass` 的堆疊上 arena 分配（bump allocator + free list 重用）。共享 `__sc_string_arena`，避免堆疊用量翻倍。
- **大分配（> 64 KB）或 arena OOM**：回退到 OS 分配器：
  - **Windows**：`malloc`/`free` 保持外部符號，由 `WinPEBImportPass` 透過 PEB walk 解析到 `msvcrt.dll`。
  - **Linux / macOS / Android**：發出 `mmap`/`munmap` 呼叫，由 `SyscallStubPass` 解析為內聯系統呼叫。
  - **未啟用匯入 Pass**：僅使用 arena；OOM 傳回 `NULL`。

**安全特性**：`free(NULL)` 為空操作；`calloc` 透過 `llvm.umul.with.overflow` 檢查溢位；`realloc` 根據指標來源（arena 或 fallback）讀取正確的舊 block 大小。

---

## 14. 錯誤診斷理念
每個硬錯誤恰好一條可操作診斷。`__neverc_shellcode_hard_error` 元資料哨兵防止級聯。使用者看到一條錯誤加修復方案，永不看到三條級聯訊息。
