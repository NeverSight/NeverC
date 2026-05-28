**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# Shellcode 流水线、MIR 与 PIC 策略（设计笔记）

本文档描述 NeverC shellcode 模式在 **IR → LLVM 优化 → 后端 MIR → 目标文件 → 提取/修补** 链中的设计取舍，以及与**编译器级默认 PIC** 策略的关系。实现细节以源码和英文注释为准。

## 1. 为何默认强制 PIC（包括非 shellcode 编译）

shellcode 提取器假设可执行片段中对外部符号的引用落在 **PC 相对** 或段内可解析的重定位上，而非需要加载器填充 `.data` 的硬编码绝对地址或常量池。

NeverC 在 `Generic_GCC::isPICDefaultForced()`、`MachO::isPICDefaultForced()` 和 `MSVCToolChain::isPICDefaultForced()` 中返回 **true**，区别于上游 Clang 的"可选默认 PIC"行为：**全平台编译始终以 PIC 为唯一模型**。这意味着：

- 普通 C 编译和 `-fshellcode` 编译共享相同的重定位习惯，减少"正常工作、shellcode 下出错"的认知负担。
- Linux / Android / macOS / Windows 后端在表驱动描述符（`TargetDesc` + `Options.td.h`）下共享相同假设，避免驱动中出现 `if (linux)` 式硬编码。

此策略不区分是否启用 `-fshellcode`，也不区分上下文是 user/kernel。即使用户传入 `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`，`ParsePICArgs()` 仍保持 `Reloc::PIC_`，对普通编译、用户态 shellcode 和内核态 shellcode 使用相同的 PC 相对假设。

## 2. IR 与 MIR 两阶段分工

### 2.1 IR 层（`registerShellcodePasses`）

负责将"普通 C"语义压缩为**单入口、无独立数据段、无问题全局变量**的形态：`ZeroRelocPass`、`IndirectBrPass`、`MemIntrinPass`、`StringRuntimePass`、`HeapArenaPass`、`CompilerRtPass`、`SyscallStubPass`、`WinPEBImportPass`、`KernelImportPass`（仅内核模式）、`Data2TextPass` 等。

**原则**：能在 IR 中用结构化方法解决的问题优先在 IR 中修复（常量池、BlockAddress、`memcpy` 落入 libc、`__int128 /` 落入 `__udivti3` 等），使后端和提取器看到的字节流更简单。对于用户认知负担高但可安全内部化的场景，驱动主动注入规则（例如 AArch64 Linux / Android / Windows 的 `long double` 在 shellcode 模式下降级为 binary64）。只有无法在没有运行时的情况下支持的构造才触发 MIR / 提取器诊断。

### 2.2 MIR 层（`registerShellcodeMachinePasses`）

在 LLVM 遗留 `TargetPassConfig` 中注册回调，位于**寄存器分配之后、`addPreEmitPass` 之前**，顺序如下：

1. 用户/混淆库：`RunBeforePreEmit`（CFI / EH 伪指令仍存在；适用于依赖元数据的变换）。
2. **`ShellcodeMIRPrepPass`**：移除会生成 `.eh_frame` / `.pdata` / `.xray_*` 侧段的伪指令，使指令流在 AsmPrinter 之前尽可能接近"纯代码"。
3. 用户/混淆库：`RunAfterPreEmit`（适用于指令替换、寄存器重命名等"最终机器码形态"混淆）。

**原则**：如果原生指令序列仍有问题，在 MIR 中修复（特别是 `ShellcodeMIRPrepPass` 周围）；**提取和修补是最后的安全网**，避免在 COFF/ELF/Mach-O 层重复相同逻辑。

MIR 操作码名称不散布在 pass 控制流中；`ShellcodeMIRPrepPass` 使用 `Tables/MIRRewriteOpcodes.def` 的 `(pattern, role, opcode)` 表通过 `TargetInstrInfo::getName()` 查找后端指令名。添加 shellcode 友好的指令替换时，优先添加表项和小型 MIR 重写；仅在必要时回退到后端 `.td` 指令选择变更，提取器级目标格式回退作为最后手段。

> 注：`ShellcodeMIRPrepPass` 仅在启用 `-fshellcode` 时注册。普通程序不得全局剥离 CFI/EH，否则会破坏正常异常处理和调试信息。

IR 和 MIR 全局回调均使用**注册一次、运行时读取当前 `ShellcodeOptions` 快照**的模式。这支持更长生命周期的嵌入式编译器进程：同一进程先编译 shellcode 再编译普通 C 时，普通 C 编译不继承之前的 IR/MIR pass；连续编译多个 shellcode TU 时，重复的全局回调注册不会堆叠同一 pass 集多次。

## 3. 表驱动的平台差异

- **Triple → 行为**：集中在 `TargetDesc.cpp` 的 `describeTriple()` 和 `TargetDesc` 字段中（段名、syscall ABI、内联汇编模板、驱动注入标志等）。添加新 OS/Arch 时，优先**添加表项**而非在提取器或 pass 中编写长分支。
- **CLI 选项**：在 `neverc/include/neverc/Invoke/Options.td.h` 中定义；由 `DriverIntegration.cpp` 使用 `OPT_*` 枚举消费，避免字符串魔法。

## 4. Windows MSVC 工具链与 SDK 布局

交叉编译到 Windows 目标时，NeverC 支持两种 SDK 来源，**无硬编码绝对路径**：

1. **内置 SDK**（默认）：NeverC 在 `runtime/` 中内置了完整的 Windows SDK 和 WDK。头文件在 `runtime/windows/shared/`，架构特定的库在 `runtime/windows/{x64,arm64}/`。构建后布局：

   ```
   build-neverc/bin/neverc
   build-neverc/runtime/windows/x64/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **真实 VS 风格 sysroot**（可选）：如果有 `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...` 目录树，通过 `-winsysroot=<path>` 或 `NEVERC_WIN_SYSROOT` 环境变量指向它。

两种来源均无需注册表或操作系统提供的 VS 环境变量，实现从 macOS / Linux 交叉编译 Windows shellcode。

## 5. 混淆与扩展点

- **IR 混淆**：通过 `setShellcodeObfuscationHooks` 提供多个 IR 阶段钩子；`-fshellcode-obfuscate=` 将 spec 字符串传递给外部库。每层提供**前**（优化前）和**后**（优化后）钩子。`RunAfterFinalIR` 是真正的最后 IR 维度可注入点——注册在此的混淆 pass 之后没有后续 pass 能修改其输出。共 11 个钩子（6 IR + 3 MIR + 2 字节流）。
- **MIR 混淆**：`RunBeforePreEmit` / `RunAfterPreEmit` 是中粒度 MIR 钩子；`RunAfterFinalMIR` 是**真正最后**的 MIR 钩子（fork 扩展添加了 `RegisterTargetPassConfigPostPreEmitCallbackFn`，在 `addPreEmitPass2()` 之后调用）。`-fshellcode-mir-obfuscate=` 单独指定 MIR spec；未设置时默认使用 IR spec。
- **字节流钩子**：`RunPostExtract` 是 finalize **前**钩子；`RunPostFinalize` 是 finalize **后**钩子（写入磁盘前的最后时刻，NeverC 不再审计）。
- **Finalize 插件 SDK**：`Plugin.h` 暴露 `registerBadByteRewriteStrategy`（链式指令级坏字节重写策略）和 `registerCharsetEncoder`（命名字符集注册）。见 [plugin-interface.md 第 2–3 节](../plugin-interface/README.zh-CN.md#2-bad-byte-rewriter-badbyterewritestrategy)。
- **大小/对齐/填充**：`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=` 在 finalize 末尾执行；驱动拒绝矛盾的配置。
- **设计选择**：混淆、多态、分阶段编码器、间接系统调用等策略层功能**有意不内置**，仅作为可选插件提供。

## 6. 内核模式（Ring-0）维度

shellcode 模式引入 `-mshellcode-context=user|kernel` 作为流水线的第二维度，叠加在 triple 之上：

- **用户模式**：PEB walk / syscall stub 流水线。
- **内核模式**：
  - `SyscallStubPass` / `WinPEBImportPass` 在 pass 级别提前返回。
  - `TargetDesc::KernelInjectFlags` 追加 OS/arch 适当的后端标志（Unix x86_64：`-mno-red-zone -mcmodel=kernel`，Windows：`/kernel`，AArch64：`-mgeneral-regs-only`）。
  - `KernelImportPass` 重写未解析的 extern 直接调用为解析器支持的间接调用，在需要时注入 `(resolver, cookie)` 隐式前缀参数。
  - `<neverc/kernel.h>` 暴露 `neverc_kern_resolve_t`、`neverc_kern_hash()` 和相关内核侧签名；用户态 shim（`<windows.h>`、`<unistd.h>` 等）在内核模式下通过 `#error` 拒绝。

详见 [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.zh-CN.md)。

## 7. Windows POSIX 兼容层

### 7.1 问题

跨平台 C 代码通常使用 `write(fd, buf, n)`、`read(fd, buf, n)`、`exit(code)` 等。在 Unix 平台上，`SyscallStubPass` 将这些替换为内联 syscall。在 Windows 上，这些 POSIX 名称没有对应的 Win32 API，导致"无法解析的重定位"错误。

### 7.2 设计目标

**零用户感知**：同一份 C 源码在全部 8 个目标 triple 上编译，无需 `#ifdef _WIN32` 或手动 Win32 API 调用。

### 7.3 实现

`WinPEBImportPass` 实现三阶段处理：

1. **阶段 1 — POSIX 扫描**：扫描未匹配的 extern 声明，对照 POSIX 兼容表。
2. **阶段 2 — 桥接包装器生成**：`Win32PosixCompat.def` 将 POSIX 名称分发给生成 `always_inline` 包装器的构建器（如 `write` → `GetStdHandle` + `WriteFile`，`mmap` → `VirtualAlloc` 含 prot 映射，`exit` → `ExitProcess` 等）。覆盖 13 个 POSIX 函数组。
3. **阶段 3 — PEB 解析**：包装器引用的 Win32 API 通过正常的 PEB walk 解析器解析。

### 7.4 可扩展性

添加新 POSIX 兼容函数：仅别名添加只需修改 `Win32PosixCompat.def`；新语义需要小型 IR 构建器 + 一个表项。有状态操作如 `open→CreateFileA`（需要 fd/handle 生命周期表）有意不模拟。

## 8. K&R 隐式声明自动修复

用户在没有 `#include` 的情况下调用 POSIX 函数时，C89 生成 0 形参的 K&R 隐式声明。`SyscallStubPass` 现维护一个 `getCanonicalSyscallType()` 表，包含 50+ 常见 POSIX 函数的规范 LLVM IR 函数类型。检测到 0 形参的 K&R 声明时，自动替换为规范签名。

## 9. 总结

| 主题 | 方法 |
|------|------|
| 默认 PIC | 所有工具链 `isPICDefaultForced()==true`，与 shellcode 假设对齐 |
| 优先在 IR 修复 | 常量、间接跳转、内存内建函数尽可能在 IR 中消除 |
| MIR 安全网 | `ShellcodeMIRPrepPass` + 前后钩子，然后目标文件提取/修补作为最后手段 |
| 最小化硬编码 | `TargetDesc` + `Options.td.h` 表驱动 |
| 用户/内核两维度 | `-fshellcode` × `-mshellcode-context={user,kernel}`；每个 (OS, arch, level) 是 `describeTriple()` 中的一行 |
| Windows POSIX 兼容 | `WinPEBImportPass` 桥接 13 个 POSIX 函数组（write→WriteFile、mmap→VirtualAlloc 等） |
| K&R 自动修复 | `SyscallStubPass` 在 0 形参声明上回退到规范 POSIX 签名 |

## 10. Shim 头文件跨平台常量

Shim 头文件（`sys/mman.h`、`fcntl.h` 等）暴露必须匹配目标内核 ABI 的常量，因为 shellcode syscall stub 直接将这些值传给内核，没有 libc 转换。

关键差异：

| 常量 | Darwin | Linux/Android |
|------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

实现：shim 头文件中的 `#if defined(__APPLE__)` 守卫。`SyscallTables.cpp` POSIX 兼容表使用 Linux 值（`AT_FDCWD = -100`），仅在 `SyscallABI::LinuxSvc0` / `LinuxSyscall` 路径上激活。Windows 目标不使用这些 POSIX 头文件；POSIX→Win32 桥接由 `WinPEBImportPass` 兼容包装器处理。
