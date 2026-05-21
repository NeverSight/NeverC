**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 项目主页](../README.zh-CN.md)

> 保持语言：使用上方语言栏；本索引内的 shellcode 等链接已指向对应中文页面。

# NeverC 文档

各子系统的设计说明、API 参考与指南。

---

## Shellcode 编译器

Shellcode 编译流水线是 NeverC 的核心研究方向。架构、CLI 选项、平台矩阵与示例见：

**[Shellcode 编译器 →](shellcode-compiler/README.zh-CN.md)**

| 文档 | 说明 |
|------|------|
| [README](shellcode-compiler/README.zh-CN.md) | 概述、快速开始、支持的目标 |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.zh-CN.md) | IR → 对象文件 → 提取设计 |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.zh-CN.md) | 各 IR pass 的设计 rationale |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.zh-CN.md) | 后端 MIR pass |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.zh-CN.md) | Ring-0 编译 |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.zh-CN.md) | 混淆与编码插件 |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.zh-CN.md) | `TargetDesc` 与提取器 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.zh-CN.md) | 添加新目标平台 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.zh-CN.md) | 从 shellcode 角度讲解 ARM64 指令 |
| [Roadmap](shellcode-compiler/roadmap/README.zh-CN.md) | 计划中的工作 |
| [Progress](shellcode-compiler/progress/README.zh-CN.md) | 实现进度 |

---

## 内置运行时

NeverC 通过嵌入 LLVM bitcode 的内置运行时扩展标准 C，每个由 `-fbuiltin-<name>` 标志控制。

**[内置运行时系统 →](builtins/README.zh-CN.md)**

| 内置功能 | 标志 | 描述 |
|---------|------|------|
| [内置字符串](builtins/string/README.zh-CN.md) | `-fbuiltin-string` | 值语义 `string` 类型，点调用方法、自动内存管理和原生 UTF-8 |
| [内置 mimalloc](builtins/mimalloc/README.zh-CN.md) | `-fbuiltin-mimalloc` | 透明高性能分配器覆盖 `malloc`/`free`/`calloc`/`realloc` |
