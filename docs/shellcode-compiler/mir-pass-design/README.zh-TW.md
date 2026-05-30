**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 編譯器](../README.zh-TW.md)

# MIR Pass 設計 — 原則與掛鉤點

> 與 [ir-pass-design.md](../ir-pass-design/README.zh-TW.md) 配套。IR 層消除在 IR 級別明顯產生重定位的構造。MIR 層作為指令選擇和暫存器分配後的**兜底層**，剝離程式碼產生引入的偽指令/中繼資料指令，並暴露掛鉤點供第三方混淆 pass 進行最終指令級變換。
>
> 實作：`neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`。
> 掛鉤介面：`neverc/include/neverc/Shellcode/Pipeline/Pipeline.h`。

---

## 0. 為何需要 MIR 層

IR 層已消除：
- 常數 GV → 棧化 / 立即數（Data2TextPass）
- `memcpy` / `memset` / `str*` / `abs*` → 內嵌位元組迴圈（MemIntrinPass）
- `__int128` compiler-rt 輔助函式 → 內嵌 always_inline（CompilerRtPass）
- extern libc syscall → 內嵌 svc / syscall（SyscallStubPass）
- Win32 extern → PEB walk + 匯出雜湊（WinPEBImportPass）
- 可變全域變數 → 入口棧框（ZeroRelocPass）
- 計算跳躍 → switch（IndirectBrPass）
- 可選：直接呼叫 → 間接呼叫（AllBlrPass）

但 LLVM 後端在 **IR → MIR 降低** 過程中引入了 shellcode 無法容納的額外構造：

1. **CFI / EH_LABEL 偽指令**：啟用 `-g` 或預設展開資訊時產生，產生 `__compact_unwind`（Mach-O）/ `.eh_frame`（ELF）/ `.pdata + .xdata`（COFF）。
2. **XRay / 可修補函式樁**：`-fxray-instrument` 或 `-fpatchable-function-entry` 插入 `PATCHABLE_FUNCTION_ENTER` 等。
3. **Sanitizer 中繼資料**：StackMap / PatchPoint / StateMap / PseudoProbe。
4. **後端 MC 級修補**：例如 Windows arm64 GOT 參照在 IR 級別不可見。

此外，MIR 掛鉤有一個關鍵用途：**使能第三方指令級混淆**（指令替換、暫存器重新命名），這是 IR 無法表達的（IR 只有虛擬暫存器和抽象指令）。

---

## 1. 與 LLVM 的整合（原生掛鉤）

LLVM 的 `TargetPassConfig` 有一個全域回呼列表。`addMachinePasses()` 在 `addPass(&PatchableFunctionID)` 之後、`addPreEmitPass()` 之前呼叫每個回呼。我們新增了公開包裝器 `addExternalPass(Pass *P)` 來解決受保護 `addPass()` 的存取控制問題。

`Pipeline.cpp` 中的註冊：

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
      const ObfuscationHooks &H = getShellcodeObfuscationHooks();
      runMIRHook(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
      runMIRHook(H.RunAfterPreEmit, TPC, Opts);
    });
```

回呼不擷取 `Opts`。它在執行時讀取當前 `ShellcodeOptions` 快照，防止同一程序同時編譯 shellcode 和普通 C 時使用過期配置。

---

## 2. 內建 MIRPrepPass

跨平台、單一職責：掃描每個 `MachineBasicBlock` 並刪除三類偽指令。真正的機器指令（`MOV` / `BL` / `ADRP` / `SYSCALL` / ...）**永遠不被觸碰**。

### 2.1 側段中繼資料（透過 `TargetOpcode::*`，跨平台）

| 操作碼 | 來源 | 不剝離的後果 |
|--------|------|------------|
| `CFI_INSTRUCTION` | 所有平台的框架降低 / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` 非空 |
| `EH_LABEL` | EH / try-catch setjmp 點 | LSDA 側段非空 |
| `GC_LABEL` / `ANNOTATION_LABEL` | GC / 註解標記 | MCSymbol 帶段相對中繼資料 |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / 沙箱 stackmap | `.llvm_stackmaps` 側段 |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe` 側段 |
| `PATCHABLE_*` 系列 | XRay / Kcov 樁 | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | `-mfentry` 入口探針 | extern `__fentry__` 呼叫 |
| `LOCAL_ESCAPE` | Microsoft SEH 框架逃逸 | 拉入 `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | 跳躍表除錯資訊 | `.debug_rnglists` 條目 |

### 2.2 Windows SEH（透過 `TargetInstrInfo::getName()` 前綴匹配）

Windows SEH 偽指令是在 AArch64/X86 後端 TD 中定義的目標特定操作碼（約 20 條）。為保持 MIR pass **跨平台且不包含後端標頭檔**，使用字串前綴匹配：

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 指令重寫表（`MIRRewritePatterns.def`）

已註冊的兩個模式：

1. **`aarch64-cpi-fp-to-fmov-imm`**：`ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8`。
2. **`x86-cpi-zero-fp-to-xorps`**：`movss/movsd xmm, [rip+CPI]` 載入 `+0.0` → `FsFLD0SS/FsFLD0SD`。

操作碼名稱集中在 `Tables/MIRRewriteOpcodes.def`。新增重寫模式需三步：
1. 編寫 `tryRewriteXxx(MachineFunction &)`
2. 在 `MIRRewriteOpcodes.def` 中新增操作碼角色
3. 在 `MIRRewritePatterns.def` 中新增模式條目

---

## 3. 使用者混淆掛鉤

`ObfuscationHooks` 暴露 **11 個掛鉤點**：6 個 IR 級、3 個 MIR 級、2 個位元組級。

關鍵差異：
- `RunBeforePreEmit`：MIR **仍有 CFI/EH/XRay 偽指令** — 用於序言/結語中繼資料操作。
- `RunAfterPreEmit`：**已清理的 MIR** — 最接近 AsmPrinter 形態，適合指令替換 / 暫存器重新命名。
- `RunPostExtract`：**純位元組流** — 用於全載荷 XOR/RC4、垃圾位元組、自訂標頭。

---

## 4. 完整執行順序

```
[IR PassBuilder]
  ├─ RunBeforePrep       (使用者掛鉤)
  ├─ ZeroRelocPass(Prep)
  ├─ RunAfterPrep        (使用者掛鉤)
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1
  ├─ RunBeforeInlining   (使用者掛鉤)
  │  (LLVM O-level: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining    (使用者掛鉤)
  ├─ Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify    (使用者掛鉤)
  └─ AllBlrPass          (可選)
        │
[Codegen (IR → MIR)]
  ├─ RunBeforePreEmit    (使用者掛鉤, CFI 存在)
  ├─ ShellcodeMIRPrepPass  ← 本文檔重點
  └─ RunAfterPreEmit     (使用者掛鉤, CFI 已剝離)
        │
[AsmPrinter → 目的檔]
        │
[ShellcodeExtractor]  ← 位元組級兜底稽核
  ├─ RunPostExtract   (使用者掛鉤, 純位元組)
  └─ flat .bin
```

MIR 層處理**兜底清理 + 混淆掛鉤點**，而非業務邏輯。「寫普通 C，無需 shellcode 技巧」的承諾由 5+ 個 IR pass 實現。

---

## 5. 設計理據

| 問題 | IR 層？ | MIR 層？ |
|------|---------|---------|
| 常數 GV 消除 | 是（Data2Text） | 不需要 |
| extern libc 消除 | 是（SyscallStub / WinPEB） | 不需要 |
| 可變全域變數棧化 | 是（ZeroReloc） | 不需要 |
| 計算跳躍 | 是（IndirectBr） | 不需要 |
| CFI 偽指令 | 否（後端產生） | 是（掃描並刪除） |
| XRay 樁 | 否（後端產生） | 是（掃描並刪除） |
| 指令級混淆 | 否（IR 無實體暫存器） | 是（有真實暫存器/MI） |
| 暫存器重新命名 | 否 | 是 |

## 6. 擴充指南

**新增內建偽指令剝離**：在 `isShellcodeStripPseudo` switch 中新增一個 case。

**新增內建 MIR 重寫**：使用 `TII->getName()` / `BuildMI(TII->get(...))` 編寫 `tryRewriteXxx(MachineFunction &)`。在 `MIRRewritePatterns.def` 中新增模式，在 `MIRRewriteOpcodes.def` 中新增操作碼。

**第三方混淆庫**：透過 [Plugin API](../../plugin-api/README.zh-TW.md)（`NEVERC_HOOK_SC_*` 掛鈎）註冊。

## 7. 與 ShellcodeExtractor 的關係

| 層 | 時機 | 能力 |
|----|------|------|
| MIR | AsmPrinter **之前** | 可插入/刪除 MachineInstr |
| 擷取器 | AsmPrinter **之後** | 只能修改位元組或拒絕 |

**原則**：優先在 MIR 中修復（仍可操縱指令）；僅對位元組級修補回退到擷取器。

## 8. 主動修復 vs 診斷透傳

1. **主動修復**：直接修改 MachineInstr。低成本、目標無關。
2. **診斷透傳**：偵測問題、報告 MIR 級錯誤、讓擷取器在位元組級拒絕。
3. **擷取器兜底**：對任何剩餘外部 reloc 或非空資料段硬失敗。

此原則使 MIR 層幾乎不受後端 ISA 升級影響。唯一的維護是：「TargetOpcode 中有新偽指令嗎？如果 shellcode 不需要它，新增一個 case。」
