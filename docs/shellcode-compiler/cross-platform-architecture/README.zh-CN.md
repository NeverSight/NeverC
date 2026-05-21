**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# NeverC Shellcode 跨平台架构概览

本文档描述"一套 pass 覆盖 macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel"背后的设计原则。在扩展到新平台或上下文前请先阅读。

相关子系统文档：
- [README.md](../README.zh-CN.md) — 概述、CLI 选项、快速开始
- [ir-pass-design.md](../ir-pass-design/README.zh-CN.md) — IR 层 pass 职责与示例
- [mir-pass-design.md](../mir-pass-design/README.zh-CN.md) — MIR 层 prep pass + 混淆钩子
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.zh-CN.md) — 内核上下文设计细节
- [platform-extension-guide.md](../platform-extension-guide/README.zh-CN.md) — 添加新平台的分步指南

---

## 1. 三维矩阵：OS × Arch × ExecutionLevel

所有跨平台差异收敛为一个**三维矩阵**，每个单元对应一个 `TargetDesc` 表项：

```
                ┌──── arm64 ────┬──── x86_64 ────┐
     Darwin ────┤ User / Kernel │ User / Kernel  │  Mach-O
     Linux  ────┤ User / Kernel │ User / Kernel  │  ELF
     Android────┤ User / Kernel │ User / Kernel  │  ELF
     Windows────┤ User / Kernel │ User / Kernel  │  COFF
                └───────────────┴────────────────┘
```

8 个 (OS, arch) 组合 × 2 ExecutionLevel = **16 个表项**，全部由 `describeTriple(triple, Level)` 返回。

**核心设计理念**：pass 始终从表中读取，从不写 `if (OS == Darwin)` 分支。添加新平台 = 在 `describeTriple()` 中填一行 + 在每个提取器的 switch 中添加一个 case。其余一切保持不变。

## 2. 流水线执行顺序

当 `-fshellcode` 活跃时，编译器遵循此固定顺序。**每个阶段都有对应的混淆钩子**用于第三方 pass 插入：

```
  ┌──────── cc1 前端 (C → IR) ───────────┐
  │ 全平台默认 PIC                        │
  │ 驱动注入 CommonInjectFlags +          │
  │   perTargetInjectFlags (含 Kernel)    │
  └──────────────┬────────────────────────┘
                 │  LLVM IR
                 ▼
   PipelineStartEP:
    ① RunBeforePrep         ← 可扩展钩子
    ② ZeroRelocPass (Prep)
    ③ RunAfterPrep          ← 可扩展钩子
    ④ IndirectBrPass
    ⑤ MemIntrinPass         ← mem*/str*/bzero/abs 内联
    ⑥ StringRuntimePass     ← 内置 string → 栈 arena
    ⑦ CompilerRtPass        ← __udivti3 家族 / i128 div 内联
    ⑧ SyscallStubPass       (User + 非 Windows)
    ⑨ WinPEBImportPass      (User + Windows)
    ⑩ KernelImportPass      (Kernel, 所有 OS)
    ⑪ Data2TextPass phase 1
    ⑫ RunBeforeInlining     ← 可扩展钩子

   LLVM AlwaysInliner / SROA / SLPVectorize / InstCombine

   OptimizerLastEP:
    ⑫ RunAfterInlining      ← 可扩展钩子（IR 已扁平化）
    ⑬ Data2TextPass phase 2
    ⑭ ZeroRelocPass (Stackify)
    ⑮ RunAfterStackify      ← 可扩展钩子
    ⑯ AllBlrPass            (可选 -fshellcode-all-blr)

                 │  LLVM IR → MIR → 寄存器分配
                 ▼
   TargetPassConfig.addMachinePasses, before addPreEmitPass:
    ⑰ RunBeforePreEmit      ← MIR 钩子, CFI 伪指令仍存在
    ⑱ ShellcodeMIRPrepPass  ← 含 MIRRewritePatterns/Opcodes 表
    ⑲ RunAfterPreEmit       ← MIR 钩子, 最接近 AsmPrinter

                 │  Mach-O / ELF / COFF 目标文件
                 ▼
   ShellcodeExtractor dispatcher → MachO / ELF / COFF 提取器
   → 修补段内 .text reloc，拒绝外部 reloc / 数据段，
     验证入口在偏移 0，输出 flat .bin
```

两个关键不变量：

1. **后端 TableGen `.td` 是指令描述的唯一来源**。pass 从不手动组装字节序列；总是使用 `BuildMI(TII->get(Opc))` + `TII->getName(Opc)`。
2. **shellcode 没有外部重定位和数据段**。每个 pass 消除一类"需要加载器"的引用；提取器执行字节级兜底审计。

## 3. 全局 PIC 策略

`isPICDefaultForced()` 在所有三个工具链上**无条件返回 true**：

- 任何 `-fno-pic` / `-static` / `-mdynamic-no-pic` 被覆盖；`ParsePICArgs()` 总是解析为 `Reloc::PIC_`。
- 代码生成总是使用 PC 相对寻址（AArch64 `adrp + add`，x86_64 `lea rip+`）。
- 普通（非 shellcode）编译也使用 PIC，确保行为一致性。

## 4. User / Kernel 正交维度

ExecutionLevel 与 (OS, arch) 正交：

- **User**（`-mshellcode-context=user`，默认）：经典 ring-3 载荷
  - Unix：`SyscallStubPass` 将 `write`/`read`/`exit`/`mmap` 改写为 `svc`/`syscall` 内联汇编。
  - Windows：`WinPEBImportPass` 读取 TEB/PEB，通过 `WinImportTables` 查询 DLL + API 哈希。
  - `KernelImportPass` 短路。

- **Kernel**（`-mshellcode-context=kernel`）：ring-0 载荷
  - 所有平台：`SyscallStubPass` / `WinPEBImportPass` 均短路。
  - `KernelInjectFlags` 按架构叠加（x86_64：`-mno-red-zone`/`-mcmodel=kernel`/`-mno-{sse,sse2,mmx}`；AArch64：`-mgeneral-regs-only`）。
  - `KernelImportPass` 激活：自动注入 `(resolver, cookie)` 前缀参数，重写 extern 调用点为解析器支持的间接调用，保留可变参数。
  - 用户仍只写普通 C。`<neverc/kernel.h>` 提供 `neverc_kern_resolve_t` 和 `neverc_kern_hash()`。

## 5. 用户态"普通 C"支持矩阵

使用 `-fshellcode` 时，以下常见 C 模式**无需用户感知即直接支持**：

| 类别 | 示例 | 内部降低 |
|------|------|---------|
| 大型数组/结构常量 | `const unsigned char t[256] = {...};` | Data2TextPass 栈化 |
| 浮点常量 | `double x = 3.14;` | Data2TextPass volatile alloca 位模式 |
| 计算跳转（GCC 扩展） | `goto *labels[op];` | IndirectBrPass → `switch` |
| memcpy / strlen / abs / 结构赋值 | 任何 `<string.h>` / `<stdlib.h>` 用法 | MemIntrinPass 字节循环包装 |
| `__int128` 除法 | `u128 q = a / b;` | CompilerRtPass 移位减法辅助 |
| 原子操作 | 任何 C11 atomic | 驱动注入 `-mno-outline-atomics` |
| `long double`（AArch64 binary128） | `long double x = 1.5L;` | 驱动注入 `-mlong-double-64` |
| POSIX 头文件 | `<unistd.h>` / `<fcntl.h>` | Shellcode shim 头 |
| Win32 头文件 | `<windows.h>` | Shellcode shim 头 |
| 大栈帧 | `int arr[4096];` | `-mno-stack-arg-probe` |

**原则**：当用户遇到不支持的 C 模式时，编译器内部扩展 pass 支持，而非要求用户改代码。只有需要运行时的模式（全局构造函数、`<stdio.h>`、libm 超越函数）才触发诊断；堆分配（`malloc`/`free`/`calloc`/`realloc`）现已由 `HeapArenaPass` 处理。每条诊断都包含正确的替代方案。

## 6. MIR 层：修复 / 回退 / 提取三阶段流水线

"先在 MIR 修复指令选择问题；再回退到提取器。"这是统一策略。`MIRPrepPass` 在三个阶段处理大部分工作：

1. **跨平台伪指令清理**：剥离 CFI / EH_LABEL / STACKMAP / PATCHABLE_* / PSEUDO_PROBE / FENTRY_CALL / LOCAL_ESCAPE / JUMP_TABLE_DEBUG_INFO / SEH_* 伪指令。
2. **Shellcode 友好指令重写**：`MIRRewritePatterns.def` + `MIRRewriteOpcodes.def` 表驱动。
3. **外部引用 / 常量池审计**：对任何泄漏的 CPI 引用或 extern 符号提供可操作的源级诊断。

提取器是最后一层：即使 MIR 遗漏了什么，它也会在字节级拒绝任何外部重定位或非空数据段。

## 7. 提取器层 — 段内 PC-Rel 修补，拒绝其余一切

提取器按 `ObjectFormat` 分发，共享同一契约："接受段内 `.text` PC 相对修补，拒绝其余一切（GOT/TLS/绝对地址/数据段）"：

- **MachOExtractor**：arm64 `BRANCH26`/`PAGE21`/`PAGEOFF12`；x86_64 `SIGNED`/`SIGNED_1/2/4`/`BRANCH` (pcrel32)
- **ELFExtractor**：arm64 `CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/等；x86_64 `PC32`/`PLT32`
- **COFFExtractor**：arm64 `BRANCH26`/`PAGEBASE_REL21`/等；x86_64 `REL32`/`REL32_[1-5]`

添加新架构支持 = 添加一个 case + 写一个 reloc 表。修补算法本身与 OS 无关。

## 8. 混淆钩子点

11 个 `ObfuscationHooks` 字段（6 IR + 3 MIR + 2 字节级）覆盖每个流水线阶段：

| 钩子 | 用例 |
|------|------|
| `RunBeforePrep` | 语言级改写（如调用约定规范化） |
| `RunAfterPrep` | 所有非入口函数已 `internal` + `alwaysinline`；安全克隆 |
| `RunBeforeInlining` | AlwaysInliner 前最后机会 |
| `RunAfterInlining` | IR 扁平化为单一大函数 — 适合 CFF / 虚假控制流 |
| `RunAfterStackify` | 无 GV 的最终 IR — 字符串加密 / 常量混淆 |
| `RunAfterFinalIR` | 真正最后的 IR 钩子；你的 pass 必须 PIC 干净 |
| `RunBeforePreEmit` | MIR，CFI 伪指令仍存在 |
| `RunAfterPreEmit` | 伪指令已清理；最接近 AsmPrinter — 指令替换 / 寄存器重命名 |
| `RunAfterFinalMIR` | 真正最后的 MIR 钩子；在所有 LLVM 后端 pass 之后 |
| `RunPostExtract` | reloc 修补后的纯字节流 — 全载荷 XOR/RC4、垃圾字节、自定义头 |
| `RunPostFinalize` | 磁盘写入前最后时刻；NeverC 不再审计 |

## 9. 添加新 (OS, Arch) 条目 — 检查清单

总成本：
1. `TargetDesc.cpp::describeTriple` — 一行（约 15 行）
2. `SyscallTables_<OS>_<arch>.def` — 系统调用号（Windows 跳过）
3. 对应提取器架构 switch — 一个 case + reloc 表
4. 测试文件 — `tests/neverc/ShellcodeCrossTargetTests.cpp` 中各一个 case

IR/MIR pass 需要零改动。内核上下文免费 — `KernelImportPass` 使用统一的 `__neverc_kern_resolve` 接口。

## 10. 非目标

- **C++ / ObjC / Rust 前端**
- **32 位 / 大端 / 小众 ISA**（RISC-V / PowerPC / SPARC / MIPS）
- **在 shellcode 中嵌入 libc 运行时**（`<stdio.h>` / `<math.h>` / 堆 / setjmp / alloca / 全局构造函数均以可操作诊断明确拒绝）
- **绝对地址重定位**（所有 `_ABS*` / `_ADDR*` / GOT / TLS reloc 硬失败）
