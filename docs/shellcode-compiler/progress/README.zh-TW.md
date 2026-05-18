**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# Shellcode 編譯器 — 進度追蹤

## 階段 0 — macOS arm64 MVP（已交付）

- [x] 目錄與 CMake 骨架（`nevercShellcode` 程式庫）
- [x] `ZeroRelocPass`：兩階段（Prep + Stackify），可變全域變數自動棧化
- [x] `Data2TextPass`：兩階段（常數陣列 → 堆疊區塊儲存；SROA 後向量常數拆分；ConstantFP → volatile 載入位元模式）
- [x] `SyscallStubPass`：表驅動白名單，涵蓋 Darwin BSD / Linux arm64 / Linux x86_64 / Android 系統呼叫
- [x] `AllBlrPass`：可選的積極間接呼叫改寫
- [x] `ShellcodeExtractor`：Mach-O `.o` → 扁平 `.bin`，含節內重定位修補
- [x] CLI 選項（透過產生的 `neverc/include/neverc/Invoke/Options.td.h`）：`-fshellcode`、`-fshellcode-all-blr`、`-mshellcode-syscall`、`-fshellcode-keep-obj=`、`-fshellcode-entry=`
- [x] 全平台預設 PIC（`isPICDefault()` 統一回傳 `true`）
- [x] 通用遞迴棧化（函式指標表、字串指標表、巢狀結構表、ConstantExpr GEP/BitCast 初始化器）
- [x] `IndirectBrPass`：GCC computed-goto（`&&label`）→ switch，含多分派點表共享
- [x] SIMD 向量常數內聯（`inlineVectorConstants`）
- [x] `_Thread_local` 自動降級為 static
- [x] 原生 macOS arm64 loader（MAP_JIT + i-cache flush）

**測試**：108/108 shellcode 斷言通過。二進位大小：`add` 8B、`fib` 64B、`hello` 64B、`big_const` 632B。

## 階段 1 — Linux / Android / Windows 跨平台（已交付）

- [x] `TargetDesc` 抽象：表驅動的平台差異
- [x] 跨平台 `-mshellcode-syscall` 語意（替代僅 Darwin 的 `-mshellcode-libsystem`）
- [x] Linux / Android 系統呼叫號表（Darwin BSD 80+、Linux arm64 60+、Linux x86_64 90+）
- [x] `ShellcodeExtractor` 重構為 `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] ELF 擷取器（arm64：`R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/等；x86_64：`R_X86_64_PC32`/`PLT32`）
- [x] COFF 擷取器（arm64：`IMAGE_REL_ARM64_BRANCH26`/等；x86_64：`IMAGE_REL_AMD64_REL32`/等）
- [x] Windows PEB 匯入 pass（`WinPEBImportPass`），含真實 PEB walk 解析器
- [x] 多 DLL Win32 API 白名單（~190 API，跨 kernel32/ntdll/user32/ws2_32/advapi32/shell32）
- [x] `MemIntrinPass`：memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/等 → 內聯位元組迴圈輔助函式
- [x] `CompilerRtPass`：`__int128` 除法/取餘 → 內聯長除法輔助函式
- [x] Windows `aarch64-pc-windows-msvc` 前端支援
- [x] `MIRPrepPass`：跨平台偽指令清除（CFI/EH/XRay/StackMap/SEH/FENTRY/等）
- [x] MIR + 位元組級混淆鉤子（IR/MIR/位元組流三層共 11 個鉤子）
- [x] AArch64 非 Darwin `long double` 自動降級為 binary64
- [x] Shellcode 墊片標頭檔：`<windows.h>`、`<unistd.h>`、`<fcntl.h>`、`<sys/stat.h>`、`<sys/mman.h>`、`<string.h>`、`<stdlib.h>`
- [x] Windows POSIX 相容層（13 個 POSIX→Win32 橋接：write→WriteFile、mmap→VirtualAlloc 等）
- [x] K&R 隱式宣告自動修復（50+ 規範 POSIX 簽章）
- [x] 表驅動淨化（架構分支硬編碼 → 零）
- [x] `KernelImportPass`：ring-0 自動解析器支援的呼叫點改寫
- [x] 核心輔助函式名表驅動診斷（`KernelHelperNames.def`）
- [x] `<neverc/kernel.h>` 用於 ring-0 進入點慣例
- [x] 進入點偏移零強制（`placeEntryFirst`）
- [x] Finalize 管線：壞位元組重寫器 SDK + 字元集編碼器 SDK + 大小約束
- [x] 外掛 SDK（`Plugin.h`）：`registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] x86_64 `-mno-implicit-float` 注入（防止後端 SSE 常數池溢出）
- [x] 跨平台 loader（macOS/Linux/Windows）

**測試**：743+ shellcode 斷言，全部通過（涵蓋 8 個 triple）。NeverC 總測試套件：1000+ 測試通過。

## 階段 2 — 可列印 / 字母數字編碼器（計畫中）

- [ ] ARM64 可列印 shellcode 編碼器（0x20–0x7e 指令子集）
- [ ] x86_64 字母數字編碼器
- [ ] 自解碼樁（decoder stub）產生
- [ ] 編碼後大小 / 熵統計

## 階段 3 — 多型 / 自修改（計畫中）

- [ ] 多型引擎：相同原始碼 → 每次編譯產生不同等價位元組序列
- [ ] 自修改程式碼：執行時期解密 / 解壓縮載荷本體
- [ ] 反偵測：避免已知 shellcode 特徵模式

## 未來擴充

- [ ] iOS arm64（程式碼簽章 + JIT 越獄場景）
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
