**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# 平台扩展指南

本文档说明如何将 shellcode 编译器扩展到新的目标平台。目前支持：**arm64 / x86_64 上的 macOS / Linux / Android / Windows**（8 个 triple），每个有独立的 **User** / **Kernel** 上下文（共 16 种变体）。添加新平台通常只需几百行代码。

## 设计理念：表驱动，而非分支驱动

所有 pass 都与目标无关。平台差异集中在**两个地方**：

1. `TargetDesc.cpp` 的 `describeTriple()` 表项
2. 三个提取器（Mach-O / ELF / COFF）的架构 switch

添加新平台 = 在 (1) 中多填一行，在 (2) 中多加一个 case。

## 步骤

### 1. 在 `TargetDesc` 中添加一行

在 `describeTriple()` 中添加对应的 OS 分支：

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

**必填字段**（均定义在 `TargetDesc.h` 中）：

| 字段 | 用途 | 缺失时 |
|------|------|--------|
| `OS` / `Arch` / `Format` | 分发键 | `describeTriple` 返回 Unknown → 驱动提前拒绝 |
| `TextSectionName` | 提取器查找入口段 | 提取器找不到 `.text` → 拒绝 |
| `Syscall` | SyscallStubPass 替换决策 | `None` → SyscallStubPass 无操作 |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | SyscallStubPass 内联汇编生成 | 任何为空 → SyscallStubPass 无操作 |
| `TCBReadAsm` / `TCBReadConstraint` | WinPEBImportPass TEB 读取内联汇编 | 空 → PEB walk 生成空 InlineAsm（Windows：必填） |
| `DriverInjectFlags` | shellcode 模式下注入的平台特定标志 | null → 不注入 |

### 2. 扩展 `SyscallStub` / `SyscallTables`（如果 OS 有内核陷入）

- 在 `TargetDesc.h` 的 `SyscallABI` 中添加一个枚举值
- 在 `SyscallTables.cpp` 中添加一个 `kXxxTable`
- 在 `lookupSyscall` 的 switch 中添加一个 case
- `SyscallStubPass` 无需修改 — InlineAsm 模板/约束从 `TargetDesc` 读取

### 2.5 扩展 Windows Win32 API 白名单

Windows 没有稳定的 syscall ABI；对 `WriteFile` / `CreateThread` / `VirtualAlloc` 的用户调用通过 `WinPEBImportPass`。白名单是多 DLL 表：

- 定义在 `Tables/Win32Apis.def`
- 每行：`NEVERC_WIN32_API(ApiName, "dll.dll")`
- 解析器已通过双参数 `__neverc_win_resolve(dll_hash, api_hash)` 支持任意 DLL

**添加新 API**（如 `DeviceIoControl`）：
1. 在 `Win32Apis.def` 中添加一行
2. 在 `lib/Headers/windows.h` 的 shellcode 分支中添加声明
3. 无需修改 pass

**添加新 DLL 桶**（如 `winhttp.dll`）：
- 只需在 `Win32Apis.def` 中添加带新 DLL 名的行

### 3. 扩展对应的提取器

需要处理三件事：
1. 识别重定位类型 → 修补字节或拒绝
2. 更新禁止数据段名称列表（新 OS 可能有自己的段）
3. 更新入口偏移零重定位目标范围验证

对于全新的目标格式（如 WASM 模块）：
1. 添加一个 `ObjectFormat` 枚举值
2. 在 `ShellcodeExtractor.cpp` 的分发 switch 中添加一个 case
3. 编写 `<Format>Extractor.cpp`（参照 `ELFExtractor.cpp` 的结构）

### 4. 添加 Loader（仅测试工具）

- 参考 `tests/neverc/shellcode/loader_linux.c` 和 `loader_windows.c`
- 通常：`mmap(RWX) → memcpy → icache flush → call`

### 5. 更新测试

- 在 `tests/neverc/ShellcodeCrossTargetTests.cpp` 中添加一个交叉编译检查
- 如果 CI 能在该平台上执行，添加 loader 往返测试

---

## 已知跨平台注意事项

- **字节序**：NeverC 仅支持小端（LE），覆盖所有主流目标。
- **ABI 差异**：Win64（rcx/rdx/r8/r9）与 System V AMD64（rdi/rsi/rdx/rcx/r8/r9）有完全不同的参数寄存器。这在 NeverC 前端层处理；shellcode 流水线无需关心。
- **系统调用号**：Linux 上不同架构不同，Android 与 Linux 相同，Darwin 有自己的 BSD 号码，Windows 没有稳定号码（因此用 PEB walk）。按 (OS, arch) 在表中索引。
- **缓存一致性**：ARM 需要显式 i-cache flush；x86 不需要。macOS arm64 JIT 还需要 `pthread_jit_write_protect_np`；Linux arm64 使用 `__builtin___clear_cache`；Windows 使用 `FlushInstructionCache`（x86 上为空操作）。
- **SELinux / W^X**：Android 受 SELinux `execmem` 约束；非越狱 iOS 完全拒绝 `mmap(RWX)`，需要 `MAP_JIT` + 代码签名。

## 未来扩展路线图

| 目标 | 预估工作量 | 依赖 |
|------|-----------|------|
| **iOS arm64**（越狱 / `MAP_JIT`） | 1 天 | 复用 Mach-O 提取器，修改 loader |
| **FreeBSD / OpenBSD x86_64** | 半天 | 复用 ELF 提取器 + 新 syscall 表 |
| **RISC-V64 Linux** | 2 天 | 需要 RISC-V TargetDesc + 新 AllBlr 变体 + RISC-V 重定位修补 |

## 混淆 Pass 扩展接口

shellcode 流水线通过 `Pipeline.h::ObfuscationHooks` 暴露 11 个钩子供第三方混淆库使用：

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

字节流: RunPostExtract → [finalize 链] → RunPostFinalize
```

IR 级用法：
```cpp
neverc::shellcode::ObfuscationHooks H;
H.RunAfterInlining = [](llvm::ModulePassManager &MPM,
                        const neverc::shellcode::ShellcodeOptions &Opts) {
  MPM.addPass(MyCFFPass(Opts.ObfuscateSpec));
};
// 通过 Plugin API 注册：NEVERC_HOOK_SC_* 钩子（见 plugin-api 文档）
```

MIR 级用法：
```cpp
H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
};
```

内置 MIR 修补也是表驱动的：`Tables/MIRRewritePatterns.def` 记录模式诊断名、架构过滤器和辅助函数名；`Tables/MIRRewriteOpcodes.def` 记录后端操作码名。添加新的 shellcode 友好后端形式时，优先添加表项和窄范围辅助函数，而非在 pass 体中分散目标特定分支。
