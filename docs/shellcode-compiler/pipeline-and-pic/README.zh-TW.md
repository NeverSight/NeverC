**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# Shellcode 流水線、MIR 與 PIC 策略（設計筆記）

本文檔描述 NeverC shellcode 模式在 **IR → LLVM 最佳化 → 後端 MIR → 目的檔 → 擷取/修補** 鏈中的設計取捨，以及與**編譯器級預設 PIC** 策略的關係。實作細節以原始碼和英文註解為準。

## 1. 為何預設強制 PIC（包括非 shellcode 編譯）

shellcode 擷取器假設可執行片段中對外部符號的參照落在 **PC 相對** 或段內可解析的重定位上，而非需要載入器填充 `.data` 的硬編碼絕對地址或常數池。

NeverC 在 `Generic_GCC::isPICDefaultForced()`、`MachO::isPICDefaultForced()` 和 `MSVCToolChain::isPICDefaultForced()` 中回傳 **true**，區別於上游 Clang 的「可選預設 PIC」行為：**全平台編譯始終以 PIC 為唯一模型**。這意味著：

- 普通 C 編譯和 `-fshellcode` 編譯共享相同的重定位習慣，減少「正常運作、shellcode 下出錯」的認知負擔。
- Linux / Android / macOS / Windows 後端在表驅動描述符（`TargetDesc` + `Options.td.h`）下共享相同假設，避免驅動中出現 `if (linux)` 式硬編碼。

此策略不區分是否啟用 `-fshellcode`，也不區分上下文是 user/kernel。即使使用者傳入 `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`，`ParsePICArgs()` 仍保持 `Reloc::PIC_`，對普通編譯、使用者態 shellcode 和核心態 shellcode 使用相同的 PC 相對假設。

## 2. IR 與 MIR 兩階段分工

### 2.1 IR 層（`registerShellcodePasses`）

負責將「普通 C」語意壓縮為**單入口、無獨立資料段、無問題全域變數**的形態：`ZeroRelocPass`、`IndirectBrPass`、`MemIntrinPass`、`StringRuntimePass`、`CompilerRtPass`、`SyscallStubPass`、`WinPEBImportPass`、`KernelImportPass`（僅核心模式）、`Data2TextPass` 等。

**原則**：能在 IR 中用結構化方法解決的問題優先在 IR 中修復（常數池、BlockAddress、`memcpy` 落入 libc、`__int128 /` 落入 `__udivti3` 等），使後端和擷取器看到的位元組流更簡單。對於使用者認知負擔高但可安全內部化的場景，驅動主動注入規則（例如 AArch64 Linux / Android / Windows 的 `long double` 在 shellcode 模式下降級為 binary64）。只有無法在沒有執行階段的情況下支援的構造才觸發 MIR / 擷取器診斷。

### 2.2 MIR 層（`registerShellcodeMachinePasses`）

在 LLVM 遺留 `TargetPassConfig` 中註冊回呼，位於**暫存器分配之後、`addPreEmitPass` 之前**，順序如下：

1. 使用者/混淆庫：`RunBeforePreEmit`（CFI / EH 偽指令仍存在；適用於依賴中繼資料的變換）。
2. **`ShellcodeMIRPrepPass`**：移除會產生 `.eh_frame` / `.pdata` / `.xray_*` 側段的偽指令，使指令流在 AsmPrinter 之前儘可能接近「純程式碼」。
3. 使用者/混淆庫：`RunAfterPreEmit`（適用於指令替換、暫存器重新命名等「最終機器碼形態」混淆）。

**原則**：如果原生指令序列仍有問題，在 MIR 中修復（特別是 `ShellcodeMIRPrepPass` 周圍）；**擷取和修補是最後的安全網**，避免在 COFF/ELF/Mach-O 層重複相同邏輯。

MIR 操作碼名稱不散佈在 pass 控制流中；`ShellcodeMIRPrepPass` 使用 `Tables/MIRRewriteOpcodes.def` 的 `(pattern, role, opcode)` 表透過 `TargetInstrInfo::getName()` 查詢後端指令名。新增 shellcode 友善的指令替換時，優先新增表項和小型 MIR 重寫；僅在必要時回退到後端 `.td` 指令選擇變更，擷取器級目標格式回退作為最後手段。

> 注：`ShellcodeMIRPrepPass` 僅在啟用 `-fshellcode` 時註冊。普通程式不得全域剝離 CFI/EH，否則會破壞正常例外處理和除錯資訊。

IR 和 MIR 全域回呼均使用**註冊一次、執行時讀取當前 `ShellcodeOptions` 快照**的模式。這支援更長生命週期的嵌入式編譯器程序：同一程序先編譯 shellcode 再編譯普通 C 時，普通 C 編譯不繼承之前的 IR/MIR pass；連續編譯多個 shellcode TU 時，重複的全域回呼註冊不會堆疊同一 pass 集多次。

## 3. 表驅動的平台差異

- **Triple → 行為**：集中在 `TargetDesc.cpp` 的 `describeTriple()` 和 `TargetDesc` 欄位中（段名、syscall ABI、內嵌組語範本、驅動注入旗標等）。新增 OS/Arch 時，優先**新增表項**而非在擷取器或 pass 中編寫長分支。
- **CLI 選項**：在 `neverc/include/neverc/Invoke/Options.td.h` 中定義；由 `DriverIntegration.cpp` 使用 `OPT_*` 列舉消費，避免字串魔法。

## 4. Windows MSVC 工具鏈與 SDK 佈局

交叉編譯到 Windows 目標時，NeverC 支援兩種 SDK 來源，**無硬編碼絕對路徑**：

1. **建構樹捆綁的 SDK**（建議）：使用者和測試腳本將 `build-neverc/sdk` 作為 SDK 根。NeverC 自動偵測安裝目錄中的 `sdk/msvc/`，並在 `MSVCToolChain::AddNeverCSystemIncludeArgs` / `Linker::ConstructJob` 中注入 include/lib 路徑。典型佈局：

   ```
   build-neverc/bin/neverc
   build-neverc/sdk/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **真實 VS 風格 sysroot**（可選）：如果有 `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...` 目錄樹，透過 `-winsysroot=<path>` 或 `NEVERC_WIN_SYSROOT` 環境變數指向它。

兩種來源均無需登錄檔或作業系統提供的 VS 環境變數，實現從 macOS / Linux 交叉編譯 Windows shellcode。

## 5. 混淆與擴充點

- **IR 混淆**：透過 `setShellcodeObfuscationHooks` 提供多個 IR 階段掛鉤；`-fshellcode-obfuscate=` 將 spec 字串傳遞給外部庫。每層提供**前**（最佳化前）和**後**（最佳化後）掛鉤。`RunAfterFinalIR` 是真正的最後 IR 維度可注入點——註冊在此的混淆 pass 之後沒有後續 pass 能修改其輸出。共 11 個掛鉤（6 IR + 3 MIR + 2 位元組流）。
- **MIR 混淆**：`RunBeforePreEmit` / `RunAfterPreEmit` 是中粒度 MIR 掛鉤；`RunAfterFinalMIR` 是**真正最後**的 MIR 掛鉤（fork 擴充新增了 `RegisterTargetPassConfigPostPreEmitCallbackFn`，在 `addPreEmitPass2()` 之後呼叫）。`-fshellcode-mir-obfuscate=` 單獨指定 MIR spec；未設定時預設使用 IR spec。
- **位元組流掛鉤**：`RunPostExtract` 是 finalize **前**掛鉤；`RunPostFinalize` 是 finalize **後**掛鉤（寫入磁碟前的最後時刻，NeverC 不再稽核）。
- **Finalize 外掛 SDK**：`Plugin.h` 暴露 `registerBadByteRewriteStrategy`（鏈式指令級壞位元組重寫策略）和 `registerCharsetEncoder`（命名字元集註冊）。見 [plugin-interface.md 第 2–3 節](../plugin-interface/README.zh-TW.md#2-bad-byte-rewriter-badbyterewritestrategy)。
- **大小/對齊/填充**：`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=` 在 finalize 末尾執行；驅動拒絕矛盾的配置。
- **設計選擇**：混淆、多型、分階段編碼器、間接系統呼叫等策略層功能**有意不內建**，僅作為可選外掛提供。

## 6. 核心模式（Ring-0）維度

shellcode 模式引入 `-mshellcode-context=user|kernel` 作為流水線的第二維度，疊加在 triple 之上：

- **使用者模式**：PEB walk / syscall stub 流水線。
- **核心模式**：
  - `SyscallStubPass` / `WinPEBImportPass` 在 pass 級別提前返回。
  - `TargetDesc::KernelInjectFlags` 追加 OS/arch 適當的後端旗標（Unix x86_64：`-mno-red-zone -mcmodel=kernel`，Windows：`/kernel`，AArch64：`-mgeneral-regs-only`）。
  - `KernelImportPass` 重寫未解析的 extern 直接呼叫為解析器支援的間接呼叫，在需要時注入 `(resolver, cookie)` 隱式前綴參數。
  - `<neverc/kernel.h>` 暴露 `neverc_kern_resolve_t`、`neverc_kern_hash()` 和相關核心側簽章；使用者態 shim（`<windows.h>`、`<unistd.h>` 等）在核心模式下透過 `#error` 拒絕。

詳見 [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.zh-TW.md)。

## 7. Windows POSIX 相容層

### 7.1 問題

跨平台 C 程式碼通常使用 `write(fd, buf, n)`、`read(fd, buf, n)`、`exit(code)` 等。在 Unix 平台上，`SyscallStubPass` 將這些替換為內嵌 syscall。在 Windows 上，這些 POSIX 名稱沒有對應的 Win32 API，導致「無法解析的重定位」錯誤。

### 7.2 設計目標

**零使用者感知**：同一份 C 原始碼在全部 8 個目標 triple 上編譯，無需 `#ifdef _WIN32` 或手動 Win32 API 呼叫。

### 7.3 實作

`WinPEBImportPass` 實作三階段處理：

1. **階段 1 — POSIX 掃描**：掃描未匹配的 extern 宣告，對照 POSIX 相容表。
2. **階段 2 — 橋接包裝器產生**：`Win32PosixCompat.def` 將 POSIX 名稱分發給產生 `always_inline` 包裝器的建構器（如 `write` → `GetStdHandle` + `WriteFile`，`mmap` → `VirtualAlloc` 含 prot 映射，`exit` → `ExitProcess` 等）。涵蓋 13 個 POSIX 函式群組。
3. **階段 3 — PEB 解析**：包裝器參照的 Win32 API 透過正常的 PEB walk 解析器解析。

### 7.4 可擴充性

新增 POSIX 相容函式：僅別名新增只需修改 `Win32PosixCompat.def`；新語意需要小型 IR 建構器 + 一個表項。有狀態操作如 `open→CreateFileA`（需要 fd/handle 生命週期表）有意不模擬。

## 8. K&R 隱式宣告自動修復

使用者在沒有 `#include` 的情況下呼叫 POSIX 函式時，C89 產生 0 形參的 K&R 隱式宣告。`SyscallStubPass` 現維護一個 `getCanonicalSyscallType()` 表，包含 50+ 常見 POSIX 函式的規範 LLVM IR 函式型別。偵測到 0 形參的 K&R 宣告時，自動替換為規範簽章。

## 9. 總結

| 主題 | 方法 |
|------|------|
| 預設 PIC | 所有工具鏈 `isPICDefaultForced()==true`，與 shellcode 假設對齊 |
| 優先在 IR 修復 | 常數、間接跳躍、記憶體內建函式儘可能在 IR 中消除 |
| MIR 安全網 | `ShellcodeMIRPrepPass` + 前後掛鉤，然後目的檔擷取/修補作為最後手段 |
| 最小化硬編碼 | `TargetDesc` + `Options.td.h` 表驅動 |
| 使用者/核心兩維度 | `-fshellcode` × `-mshellcode-context={user,kernel}`；每個 (OS, arch, level) 是 `describeTriple()` 中的一列 |
| Windows POSIX 相容 | `WinPEBImportPass` 橋接 13 個 POSIX 函式群組（write→WriteFile、mmap→VirtualAlloc 等） |
| K&R 自動修復 | `SyscallStubPass` 在 0 形參宣告上回退到規範 POSIX 簽章 |

## 10. Shim 標頭檔跨平台常數

Shim 標頭檔（`sys/mman.h`、`fcntl.h` 等）暴露必須匹配目標核心 ABI 的常數，因為 shellcode syscall stub 直接將這些值傳給核心，沒有 libc 轉換。

關鍵差異：

| 常數 | Darwin | Linux/Android |
|------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

實作：shim 標頭檔中的 `#if defined(__APPLE__)` 守衛。`SyscallTables.cpp` POSIX 相容表使用 Linux 值（`AT_FDCWD = -100`），僅在 `SyscallABI::LinuxSvc0` / `LinuxSyscall` 路徑上啟動。Windows 目標不使用這些 POSIX 標頭檔；POSIX→Win32 橋接由 `WinPEBImportPass` 相容包裝器處理。
