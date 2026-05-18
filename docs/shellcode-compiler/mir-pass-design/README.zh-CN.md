**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# MIR Pass 设计 — 原则与钩子点

> 与 [ir-pass-design.md](../ir-pass-design/README.zh-CN.md) 配套。IR 层消除在 IR 级别明显产生重定位的构造。MIR 层作为指令选择和寄存器分配后的**兜底层**，剥离代码生成引入的伪指令/元数据指令，并暴露钩子点供第三方混淆 pass 进行最终指令级变换。
>
> 实现：`neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`。
> 钩子接口：`neverc/include/neverc/Shellcode/Pipeline/Pipeline.h`。

---

## 0. 为何需要 MIR 层

IR 层已消除：
- 常量 GV → 栈化 / 立即数（Data2TextPass）
- `memcpy` / `memset` / `str*` / `abs*` → 内联字节循环（MemIntrinPass）
- `__int128` compiler-rt 辅助函数 → 内联 always_inline（CompilerRtPass）
- extern libc syscall → 内联 svc / syscall（SyscallStubPass）
- Win32 extern → PEB walk + 导出哈希（WinPEBImportPass）
- 可变全局变量 → 入口栈帧（ZeroRelocPass）
- 计算跳转 → switch（IndirectBrPass）
- 可选：直接调用 → 间接调用（AllBlrPass）

但 LLVM 后端在 **IR → MIR 降低** 过程中引入了 shellcode 无法容纳的额外构造：

1. **CFI / EH_LABEL 伪指令**：启用 `-g` 或默认展开信息时生成，产生 `__compact_unwind`（Mach-O）/ `.eh_frame`（ELF）/ `.pdata + .xdata`（COFF）。
2. **XRay / 可修补函数桩**：`-fxray-instrument` 或 `-fpatchable-function-entry` 插入 `PATCHABLE_FUNCTION_ENTER` 等。
3. **Sanitizer 元数据**：StackMap / PatchPoint / StateMap / PseudoProbe。
4. **后端 MC 级修补**：例如 Windows arm64 GOT 引用在 IR 级别不可见。

此外，MIR 钩子有一个关键用途：**使能第三方指令级混淆**（指令替换、寄存器重命名），这是 IR 无法表达的（IR 只有虚拟寄存器和抽象指令）。

---

## 1. 与 LLVM 的集成（原生钩子）

LLVM 的 `TargetPassConfig` 有一个全局回调列表。`addMachinePasses()` 在 `addPass(&PatchableFunctionID)` 之后、`addPreEmitPass()` 之前调用每个回调。我们添加了公共包装器 `addExternalPass(Pass *P)` 来解决受保护 `addPass()` 的访问控制问题。

`Pipeline.cpp` 中的注册：

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

回调不捕获 `Opts`。它在运行时读取当前 `ShellcodeOptions` 快照，防止同一进程同时编译 shellcode 和普通 C 时使用过期配置。

---

## 2. 内置 MIRPrepPass

跨平台、单一职责：扫描每个 `MachineBasicBlock` 并删除三类伪指令。真正的机器指令（`MOV` / `BL` / `ADRP` / `SYSCALL` / ...）**永远不被触碰**。

### 2.1 侧段元数据（通过 `TargetOpcode::*`，跨平台）

| 操作码 | 来源 | 不剥离的后果 |
|--------|------|------------|
| `CFI_INSTRUCTION` | 所有平台的帧降低 / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` 非空 |
| `EH_LABEL` | EH / try-catch setjmp 点 | LSDA 侧段非空 |
| `GC_LABEL` / `ANNOTATION_LABEL` | GC / 注解标记 | MCSymbol 带段相对元数据 |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / 沙箱 stackmap | `.llvm_stackmaps` 侧段 |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe` 侧段 |
| `PATCHABLE_*` 系列 | XRay / Kcov 桩 | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | `-mfentry` 入口探针 | extern `__fentry__` 调用 |
| `LOCAL_ESCAPE` | Microsoft SEH 帧逃逸 | 拉入 `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | 跳转表调试信息 | `.debug_rnglists` 条目 |

### 2.2 Windows SEH（通过 `TargetInstrInfo::getName()` 前缀匹配）

Windows SEH 伪指令是在 AArch64/X86 后端 TD 中定义的目标特定操作码（约 20 条，如 `SEH_StackAlloc`、`SEH_PushReg` 等）。为保持 MIR pass **跨平台且不包含后端头文件**，使用字符串前缀匹配：

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 指令重写表（`MIRRewritePatterns.def`）

伪指令剥离后，`MIRPrepPass` 运行重写 pass，将代码生成选择的但对 shellcode 不友好的指令模式替换为等价的 shellcode 友好形式，而无需修改 LLVM `.td` 文件。

已注册的两个模式：

1. **`aarch64-cpi-fp-to-fmov-imm`**：`ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8`，当 IEEE 位模式落在 FMOV 的 256 个可编码值内时。
2. **`x86-cpi-zero-fp-to-xorps`**：`movss/movsd xmm, [rip+CPI]` 加载 `+0.0` → `FsFLD0SS/FsFLD0SD`（3 字节 `xorps xmm, xmm`）。

操作码名称集中在 `Tables/MIRRewriteOpcodes.def`。添加新重写模式需三步：
1. 使用 `lookupMIRRewriteOpcode()` + `BuildMI(TII->get(...))` 编写 `tryRewriteXxx(MachineFunction &)`
2. 在 `MIRRewriteOpcodes.def` 中添加操作码角色
3. 在 `MIRRewritePatterns.def` 中添加模式条目

---

## 3. 用户混淆钩子

`ObfuscationHooks` 暴露 **11 个钩子点**：6 个 IR 级、3 个 MIR 级、2 个字节级：

三种签名类型：
```cpp
using ObfuscationHook = std::function<void(
    llvm::ModulePassManager &, const ShellcodeOptions &)>;
using MachineObfuscationHook = std::function<void(
    llvm::TargetPassConfig &, const ShellcodeOptions &)>;
using BinaryObfuscationHook = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const ShellcodeOptions &)>;
```

关键差异：
- `RunBeforePreEmit`：MIR **仍有 CFI/EH/XRay 伪指令** — 用于序言/尾声元数据操作。
- `RunAfterPreEmit`：**已清理的 MIR** — 最接近 AsmPrinter 形态，适合指令替换 / 寄存器重命名。
- `RunPostExtract`：**纯字节流**，提取器修补段内 reloc 后 — 用于全载荷 XOR/RC4、垃圾字节、自定义头。

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::shellcode::getShellcodeObfuscationHooks();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::shellcode::ShellcodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  neverc::shellcode::setShellcodeObfuscationHooks(std::move(H));
}
```

---

## 4. 完整执行顺序

```
[IR PassBuilder]
  ├─ RunBeforePrep       (用户钩子)
  ├─ ZeroRelocPass(Prep)
  ├─ RunAfterPrep        (用户钩子)
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1
  ├─ RunBeforeInlining   (用户钩子)
  │  (LLVM O-level: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining    (用户钩子)
  ├─ Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify    (用户钩子)
  └─ AllBlrPass          (可选)
        │
[Codegen (IR → MIR)]
  ├─ RunBeforePreEmit    (用户钩子, CFI 存在)
  ├─ ShellcodeMIRPrepPass  ← 本文档重点
  └─ RunAfterPreEmit     (用户钩子, CFI 已剥离)
        │
[AsmPrinter → 目标文件]
        │
[ShellcodeExtractor]  ← 字节级兜底审计
  ├─ RunPostExtract   (用户钩子, 纯字节)
  └─ flat .bin
```

MIR 层处理**兜底清理 + 混淆钩子点**，而非业务逻辑。"写普通 C，无需 shellcode 技巧"的承诺由 5+ 个 IR pass 实现。

---

## 5. 设计理据

| 问题 | IR 层？ | MIR 层？ |
|------|---------|---------|
| 常量 GV 消除 | 是（Data2Text） | 不需要 |
| extern libc 消除 | 是（SyscallStub / WinPEB） | 不需要 |
| 可变全局变量栈化 | 是（ZeroReloc） | 不需要 |
| 计算跳转 | 是（IndirectBr） | 不需要 |
| CFI 伪指令 | 否（后端生成） | 是（扫描并删除） |
| XRay 桩 | 否（后端生成） | 是（扫描并删除） |
| 指令级混淆 | 否（IR 无物理寄存器） | 是（有真实寄存器/MI） |
| 寄存器重命名 | 否 | 是 |
| 窥孔常量展开 | 部分 | 是（更干净） |

## 6. 扩展指南

**添加内置伪指令剥离**：在 `isShellcodeStripPseudo` switch 中添加一个 case。

**添加内置 MIR 重写**：使用 `TII->getName()` / `BuildMI(TII->get(...))` 编写 `tryRewriteXxx(MachineFunction &)`。在 `MIRRewritePatterns.def` 中添加模式，在 `MIRRewriteOpcodes.def` 中添加操作码。

**第三方混淆库**：通过 `setShellcodeObfuscationHooks()` 注册。

## 7. 与 ShellcodeExtractor 的关系

| 层 | 时机 | 能力 |
|----|------|------|
| MIR | AsmPrinter **之前** | 可插入/删除 MachineInstr |
| 提取器 | AsmPrinter **之后** | 只能修改字节或拒绝 |

**原则**：优先在 MIR 中修复（仍可操纵指令）；仅对字节级修补（如段内 reloc imm26）回退到提取器。这种分层确保用户永远不会得到"半损坏的 `.bin`"：要么可用，要么在编译时有清晰的可操作错误。

## 8. 主动修复 vs 诊断透传

1. **主动修复**：直接修改 MachineInstr（剥离伪指令、重写 CPI→FMOV）。低成本、目标无关。
2. **诊断透传**：检测问题、报告 MIR 级错误、让提取器在字节级拒绝。用于需要大量目标特定代码的 MIR 重写场景（如将 `adrp+ldr CPI` 替换为 `mov/movk` 序列）。
3. **提取器兜底**：对任何剩余外部 reloc 或非空数据段硬失败。

此原则使 MIR 层几乎不受后端 ISA 升级影响。唯一的维护是："TargetOpcode 中有新伪指令吗？如果 shellcode 不需要它，添加一个 case。"
