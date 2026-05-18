**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# Shellcode 编译器 — 进度追踪

## 阶段 0 — macOS arm64 MVP（已交付）

- [x] 目录与 CMake 骨架（`nevercShellcode` 库）
- [x] `ZeroRelocPass`：两阶段（Prep + Stackify），可变全局变量自动栈化
- [x] `Data2TextPass`：两阶段（常量数组 → 栈块存储；SROA 后向量常量拆分；ConstantFP → volatile 加载位模式）
- [x] `SyscallStubPass`：表驱动白名单，覆盖 Darwin BSD / Linux arm64 / Linux x86_64 / Android 系统调用
- [x] `AllBlrPass`：可选的激进间接调用改写
- [x] `ShellcodeExtractor`：Mach-O `.o` → 扁平 `.bin`，含节内重定位修补
- [x] CLI 选项（通过生成的 `neverc/include/neverc/Invoke/Options.td.h`）：`-fshellcode`、`-fshellcode-all-blr`、`-mshellcode-syscall`、`-fshellcode-keep-obj=`、`-fshellcode-entry=`
- [x] 全平台默认 PIC（`isPICDefault()` 统一返回 `true`）
- [x] 通用递归栈化（函数指针表、字符串指针表、嵌套结构表、ConstantExpr GEP/BitCast 初始化器）
- [x] `IndirectBrPass`：GCC computed-goto（`&&label`）→ switch，含多分发点表共享
- [x] SIMD 向量常量内联（`inlineVectorConstants`）
- [x] `_Thread_local` 自动降级为 static
- [x] 原生 macOS arm64 loader（MAP_JIT + i-cache flush）

**测试**：108/108 shellcode 断言通过。二进制大小：`add` 8B、`fib` 64B、`hello` 64B、`big_const` 632B。

## 阶段 1 — Linux / Android / Windows 跨平台（已交付）

- [x] `TargetDesc` 抽象：表驱动的平台差异
- [x] 跨平台 `-mshellcode-syscall` 语义（替代仅 Darwin 的 `-mshellcode-libsystem`）
- [x] Linux / Android 系统调用号表（Darwin BSD 80+、Linux arm64 60+、Linux x86_64 90+）
- [x] `ShellcodeExtractor` 重构为 `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] ELF 提取器（arm64：`R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/等；x86_64：`R_X86_64_PC32`/`PLT32`）
- [x] COFF 提取器（arm64：`IMAGE_REL_ARM64_BRANCH26`/等；x86_64：`IMAGE_REL_AMD64_REL32`/等）
- [x] Windows PEB 导入 pass（`WinPEBImportPass`），含真实 PEB walk 解析器
- [x] 多 DLL Win32 API 白名单（~190 API，跨 kernel32/ntdll/user32/ws2_32/advapi32/shell32）
- [x] `MemIntrinPass`：memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/等 → 内联字节循环辅助函数
- [x] `CompilerRtPass`：`__int128` 除法/取模 → 内联长除法辅助函数
- [x] Windows `aarch64-pc-windows-msvc` 前端支持
- [x] `MIRPrepPass`：跨平台伪指令清除（CFI/EH/XRay/StackMap/SEH/FENTRY/等）
- [x] MIR + 字节级混淆钩子（IR/MIR/字节流三层共 11 个钩子）
- [x] AArch64 非 Darwin `long double` 自动降级为 binary64
- [x] Shellcode 垫片头文件：`<windows.h>`、`<unistd.h>`、`<fcntl.h>`、`<sys/stat.h>`、`<sys/mman.h>`、`<string.h>`、`<stdlib.h>`
- [x] Windows POSIX 兼容层（13 个 POSIX→Win32 桥接：write→WriteFile、mmap→VirtualAlloc 等）
- [x] K&R 隐式声明自动修复（50+ 规范 POSIX 签名）
- [x] 表驱动净化（架构分支硬编码 → 零）
- [x] `KernelImportPass`：ring-0 自动解析器支持的调用点改写
- [x] 内核辅助函数名表驱动诊断（`KernelHelperNames.def`）
- [x] `<neverc/kernel.h>` 用于 ring-0 入口约定
- [x] 入口偏移零强制（`placeEntryFirst`）
- [x] Finalize 流水线：坏字节重写器 SDK + 字符集编码器 SDK + 大小约束
- [x] 插件 SDK（`Plugin.h`）：`registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] x86_64 `-mno-implicit-float` 注入（防止后端 SSE 常量池溢出）
- [x] 跨平台 loader（macOS/Linux/Windows）

**测试**：743+ shellcode 断言，全部通过（覆盖 8 个 triple）。NeverC 总测试套件：1000+ 测试通过。

## 阶段 2 — 可打印 / 字母数字编码器（计划中）

- [ ] ARM64 可打印 shellcode 编码器（0x20–0x7e 指令子集）
- [ ] x86_64 字母数字编码器
- [ ] 自解码桩（decoder stub）生成
- [ ] 编码后大小 / 熵统计

## 阶段 3 — 多态 / 自修改（计划中）

- [ ] 多态引擎：相同源码 → 每次编译产生不同等价字节序列
- [ ] 自修改代码：运行时解密 / 解压载荷体
- [ ] 反检测：避免已知 shellcode 特征模式

## 未来扩展

- [ ] iOS arm64（代码签名 + JIT 越狱场景）
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
