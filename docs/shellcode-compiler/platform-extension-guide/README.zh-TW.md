**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# 平台擴充指南

本文檔說明如何將 shellcode 編譯器擴充到新的目標平台。目前支援：**arm64 / x86_64 上的 macOS / Linux / Android / Windows**（8 個 triple），每個有獨立的 **User** / **Kernel** 上下文（共 16 種變體）。新增平台通常只需數百行程式碼。

## 設計理念：表驅動，而非分支驅動

所有 pass 都與目標無關。平台差異集中在**兩個地方**：

1. `TargetDesc.cpp` 的 `describeTriple()` 表項
2. 三個擷取器（Mach-O / ELF / COFF）的架構 switch

新增平台 = 在 (1) 中多填一列，在 (2) 中多加一個 case。

## 步驟

### 1. 在 `TargetDesc` 中新增一列

在 `describeTriple()` 中新增對應的 OS 分支：

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**必填欄位**（均定義在 `TargetDesc.h` 中）：

| 欄位 | 用途 | 缺失時 |
|------|------|--------|
| `OS` / `Arch` / `Format` | 分發鍵 | `describeTriple` 回傳 Unknown → 驅動提前拒絕 |
| `TextSectionName` | 擷取器尋找入口段 | 擷取器找不到 `.text` → 拒絕 |
| `Syscall` | SyscallStubPass 替換決策 | `None` → SyscallStubPass 無操作 |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | SyscallStubPass 內嵌組語產生 | 任何為空 → SyscallStubPass 無操作 |
| `TCBReadAsm` / `TCBReadConstraint` | WinPEBImportPass TEB 讀取內嵌組語 | 空 → PEB walk 產生空 InlineAsm（Windows：必填） |
| `DriverInjectFlags` | shellcode 模式下注入的平台特定旗標 | null → 不注入 |

### 2. 擴充 `SyscallStub` / `SyscallTables`（如果 OS 有核心陷入）

- 在 `TargetDesc.h` 的 `SyscallABI` 中新增一個列舉值
- 在 `SyscallTables.cpp` 中新增一個 `kXxxTable`
- 在 `lookupSyscall` 的 switch 中新增一個 case
- `SyscallStubPass` 無需修改 — InlineAsm 範本/約束從 `TargetDesc` 讀取

### 2.5 擴充 Windows Win32 API 白名單

Windows 沒有穩定的 syscall ABI；對 `WriteFile` / `CreateThread` / `VirtualAlloc` 的使用者呼叫透過 `WinPEBImportPass`。白名單是多 DLL 表：

- 定義在 `Tables/Win32Apis.def`
- 每列：`NEVERC_WIN32_API(ApiName, "dll.dll")`
- 解析器已透過雙參數 `__neverc_win_resolve(dll_hash, api_hash)` 支援任意 DLL

**新增 API**（如 `DeviceIoControl`）：
1. 在 `Win32Apis.def` 中新增一列
2. 在 `lib/Headers/windows.h` 的 shellcode 分支中新增宣告
3. 無需修改 pass

**新增 DLL 桶**（如 `winhttp.dll`）：
- 只需在 `Win32Apis.def` 中新增帶新 DLL 名的列

### 3. 擴充對應的擷取器

需要處理三件事：
1. 識別重定位類型 → 修補位元組或拒絕
2. 更新禁止資料段名稱清單（新 OS 可能有自己的段）
3. 更新入口偏移零重定位目標範圍驗證

對於全新的目標格式（如 WASM 模組）：
1. 新增一個 `ObjectFormat` 列舉值
2. 在 `ShellcodeExtractor.cpp` 的分發 switch 中新增一個 case
3. 編寫 `<Format>Extractor.cpp`（參照 `ELFExtractor.cpp` 的結構）

### 4. 新增 Loader（僅測試工具）

- 參考 `tests/neverc/shellcode/loader_linux.c` 和 `loader_windows.c`
- 通常：`mmap(RWX) → memcpy → icache flush → call`

### 5. 更新測試

- 在 `tests/neverc/ShellcodeCrossTargetTests.cpp` 中新增一個交叉編譯檢查
- 如果 CI 能在該平台上執行，新增 loader 往返測試

---

## 已知跨平台注意事項

- **位元組序**：NeverC 僅支援小端（LE），涵蓋所有主流目標。
- **ABI 差異**：Win64（rcx/rdx/r8/r9）與 System V AMD64（rdi/rsi/rdx/rcx/r8/r9）有完全不同的引數暫存器。這在 Clang 前端層處理；shellcode 流水線無需關心。
- **系統呼叫號**：Linux 上不同架構不同，Android 與 Linux 相同，Darwin 有自己的 BSD 號碼，Windows 沒有穩定號碼（因此用 PEB walk）。按 (OS, arch) 在表中索引。
- **快取一致性**：ARM 需要顯式 i-cache flush；x86 不需要。macOS arm64 JIT 還需要 `pthread_jit_write_protect_np`；Linux arm64 使用 `__builtin___clear_cache`；Windows 使用 `FlushInstructionCache`（x86 上為空操作）。
- **SELinux / W^X**：Android 受 SELinux `execmem` 約束；非越獄 iOS 完全拒絕 `mmap(RWX)`，需要 `MAP_JIT` + 程式碼簽章。

## 未來擴充路線圖

| 目標 | 預估工作量 | 依賴 |
|------|-----------|------|
| **iOS arm64**（越獄 / `MAP_JIT`） | 1 天 | 複用 Mach-O 擷取器，修改 loader |
| **FreeBSD / OpenBSD x86_64** | 半天 | 複用 ELF 擷取器 + 新 syscall 表 |
| **RISC-V64 Linux** | 2 天 | 需要 RISC-V TargetDesc + 新 AllBlr 變體 + RISC-V 重定位修補 |

## 混淆 Pass 擴充介面

shellcode 流水線透過 `Pipeline.h::ObfuscationHooks` 暴露 11 個掛鉤供第三方混淆庫使用：

```
PipelineStartEP:
  RunBeforePrep → [ZeroReloc Prep] → RunAfterPrep →
  [IndirectBr → MemIntrin → CompilerRt → SyscallStub →
   WinPEBImport → KernelImport → Data2Text phase 1] →
  RunBeforeInlining

OptimizerLastEP:
  RunAfterInlining → [Data2Text phase 2 → ZeroReloc Stackify] →
  RunAfterStackify → [AllBlrPass] → RunAfterFinalIR

MIR: RunBeforePreEmit → [MIRPrepPass] → RunAfterPreEmit →
     [LLVM addPreEmitPass/addPreEmitPass2] → RunAfterFinalMIR

位元組流: RunPostExtract → [finalize 鏈] → RunPostFinalize
```

IR 級用法：
```cpp
neverc::shellcode::ObfuscationHooks H;
H.RunAfterInlining = [](llvm::ModulePassManager &MPM,
                        const neverc::shellcode::ShellcodeOptions &Opts) {
  MPM.addPass(MyCFFPass(Opts.ObfuscateSpec));
};
neverc::shellcode::setShellcodeObfuscationHooks(std::move(H));
```

MIR 級用法：
```cpp
H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
};
```

內建 MIR 修補也是表驅動的：`Tables/MIRRewritePatterns.def` 記錄模式診斷名、架構過濾器和輔助函式名；`Tables/MIRRewriteOpcodes.def` 記錄後端操作碼名。新增 shellcode 友善的後端形式時，優先新增表項和窄範圍輔助函式，而非在 pass 本體中分散目標特定分支。
