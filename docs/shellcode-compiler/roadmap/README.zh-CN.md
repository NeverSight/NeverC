**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# 路线图

本文档跟踪计划中、进行中或有意推迟的功能。

## 当前状态

NeverC 的 shellcode 流水线涵盖：

- 完整的 LLVM IR 流水线，包含 11+ 个专用 pass
- COFF / ELF / Mach-O 提取器
- Win32 PEB-walk 导入解析（ROR-13 哈希，6 个 DLL 桶）
- 直接系统调用降级（Darwin `svc #0x80`、Linux `svc #0` / `syscall`）
- 内核模式支持（Windows、Linux）
- 坏字节审计，支持可配置的 profile
- 坏字节重写器和字符集编码器的插件 SDK
- 大小 / 对齐 / 填充约束（`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=`）
- IR、MIR 和字节流三层共 11 个混淆钩子

## 已完成（2026-04）

1. **大小 / 对齐 / 填充约束** — 内置功能。`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=` 在 `finalizeShellcodeBytes` 末尾执行。驱动会拒绝矛盾的配置（例如填充字节在坏字节集中，或无 align/max-length 时使用 pad）。

2. **坏字节重写器接口** — 骨架已内置，无内置策略。`Plugin.h::registerBadByteRewriteStrategy` 暴露 SDK。`-fshellcode-bad-byte-rewrite` / `-fno-...` 控制 finalize 链是否调用重写器。禁用后退回到仅审计模式。下游库注册基于 Capstone 或自定义的重写策略。

3. **字符集编码器接口** — 骨架已内置，无内置字符集。`Plugin.h::registerCharsetEncoder` 暴露 `(name, Encode, Stub, IsCharsetMember)` 元组。设置 `-fshellcode-charset=<name>` 后，finalize 阶段将 `.text` 替换为 `Stub(target) || Encode(text, target)` 并验证所有输出字节符合字符集。可打印 / 字母数字 / 自定义编码器由下游库注册。

## 计划中 — 插件层（通过钩子）

以下能力**有意不内置**。它们属于策略/混淆层，设计为通过钩子和插件接口由第三方插件提供。

| 功能 | 钩子位置 | 说明 |
|------|----------|------|
| 反反汇编 | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | 指令前缀干扰、跳转重排、垃圾字节插入 |
| 多态 | `RunAfterFinalMIR` / `RunPostExtract` | 基于种子的每次编译输出变化 |
| 分阶段编码器（XOR / RC4 / 自解密） | `RunPostExtract` / `RunPostFinalize` | 编译时桩生成 + 载荷加密 |
| 间接系统调用（Halos / Tartarus / Recycled Gate） | IR 级插件或 `RunPostExtract` | 运行时 ntdll gadget 扫描 |
| 睡眠掩码 / 调用栈伪造 | IR pass 插件 | Ekko / FOLIAGE / Cronos 模式 |
| ETW / AMSI 补丁 | IR pass 插件 | 运行时补丁序列 |
| 模块践踏 / 脱钩 | IR pass 插件 | 内存操纵模式 |

## 插件钩子概览

三层共 11 个钩子：

**IR 层（6 个钩子，接收 `ModulePassManager &`）**：
- `RunBeforePrep` — 在任何 shellcode pass 之前
- `RunAfterPrep` — 链接统一之后
- `RunBeforeInlining` — AlwaysInliner 之前的最后机会
- `RunAfterInlining` — IR 完全扁平化为一个函数
- `RunAfterStackify` — 代码生成前的最终 IR 形态
- `RunAfterFinalIR` — `AllBlrPass` 之后，绝对最后的 IR 钩子

**MIR 层（3 个钩子，接收 `TargetPassConfig &`）**：
- `RunBeforePreEmit` — 寄存器已分配，CFI/EH 伪指令仍存在
- `RunAfterPreEmit` — `MIRPrepPass` 清理后，最接近最终字节
- `RunAfterFinalMIR` — LLVM `addPreEmitPass2()` 之后，紧接 AsmPrinter 之前

**字节流层（2 个钩子，接收 `SmallVectorImpl<uint8_t> &`）**：
- `RunPostExtract` — 预 finalize，仍由重写器/编码器/审计/大小处理
- `RunPostFinalize` — 后 finalize，写入磁盘前的最后时刻；NeverC 不再进行任何审计

## Finalize 流水线

每个提取器在写入 `.bin` 之前调用 `finalizeShellcodeBytes`：

```
applyPostExtractObfuscationHook       (ObfuscationHooks::RunPostExtract)
        |
runBadByteRewriters                   (Plugin.h::registerBadByteRewriteStrategy)
        |
runCharsetEncoder                     (Plugin.h::registerCharsetEncoder)
        |
auditFinalBadBytes                    (内置硬审计)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
```

用法和代码示例见 [plugin-interface.md](../plugin-interface/README.zh-CN.md)。

## 不计划实现

- **跨语言前端** — NeverC 仅接受自身的 C23 前端。IR 流水线与前端解耦，但接受外部 bitcode（例如来自 `rustc` 或 `zig`）不是项目目标。
