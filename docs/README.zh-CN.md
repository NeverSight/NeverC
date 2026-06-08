**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 项目主页](i18n/README.zh-CN.md)

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
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.zh-CN.md) | `TargetDesc` 与提取器 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.zh-CN.md) | 添加新目标平台 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.zh-CN.md) | 从 shellcode 角度讲解 ARM64 指令 |
| [Roadmap](shellcode-compiler/roadmap/README.zh-CN.md) | 计划中的工作 |
| [Progress](shellcode-compiler/progress/README.zh-CN.md) | 实现进度 |

---

## `.nc` 文件扩展名

NeverC 将 `.nc` 作为原生源文件扩展名。使用 `.nc` 时，编译器自动启用所有 NeverC 语言扩展（`-fneverc-types`、`-fbuiltin-string`）— 无需额外标志。

**[`.nc` 扩展名 →](nc-extension/README.zh-CN.md)**

---

## 内置运行时

NeverC 通过嵌入 LLVM bitcode 的内置运行时扩展标准 C，每个由 `-fbuiltin-<name>` 标志控制。`.nc` 文件自动启用 `string`。

**[内置运行时系统 →](builtins/README.zh-CN.md)**

| 内置功能 | 标志 | 描述 |
|---------|------|------|
| [内置字符串](builtins/string/README.zh-CN.md) | `-fbuiltin-string` | 值语义 `string` 类型，点调用方法、自动内存管理和原生 UTF-8 |
| [内置 mimalloc](builtins/mimalloc/README.zh-CN.md) | `-fbuiltin-mimalloc` | 透明高性能 `mimalloc` 分配器覆盖 `malloc`/`free`/`calloc`/`realloc` |
| [字符串加密 (xorstr)](builtins/xorstr/README.zh-CN.md) | `-fencrypt-call-strings` | 编译期字符串加密，栈分配 XOR 解密，反签名算法 |

---

## 插件 API

NeverC 提供纯 C ABI 的树外 pass 插件接口。插件是一个共享库（`.dll` / `.so` / `.dylib`），可在编译流水线的指定钩子点注册自定义 pass。只需一个头文件，零 LLVM/CRT 依赖。

**[插件 API →](plugin-api/README.zh-CN.md)**

---

## 路线图

NeverC 项目的主要规划方向：标准库、EVM 智能合约后端和 Solana eBPF 后端。

**[路线图 →](roadmap/README.zh-CN.md)**

| 功能 | 描述 |
|------|------|
| 标准库 (`std`) | Go 风格开箱即用包：`fmt`、`os`、`io`、`net`、`crypto`、`encoding`、`sync` 等 |
| 混淆插件套件 (`neverc-obfuscation`) | 第一方 VM、MBA、控制流平坦化、多态引擎和反篡改插件 |
| UI 组件库 (`neverc-ui`) | 类 Qt 跨平台 UI，HTML/JS/CSS 渲染器，拖拽式设计器，AI 原生工作流 |
| IDE 与语言工具 (`neverc-ide`) | `.nc` 文件的 VSCode 扩展 + 独立 IDE，支持智能补全、调试和 shellcode 管线可视化 |
| EVM 智能合约 | 把 C 编译为 EVM 字节码——用 C 代替 Solidity 编写智能合约 |
| Solana eBPF | 把 C 编译为 Solana eBPF 字节码——用 C 开发链上程序 |

---

## 本地开发

从源码构建 NeverC 并配置本地开发环境，包括 PATH 设置。

**[本地开发 →](local-dev/README.zh-CN.md)**

---

## 示例

完整的可构建示例，展示 NeverC 的跨平台编译能力。所有示例均可从 macOS / Linux 交叉编译。

**[示例 →](examples/README.zh-CN.md)**
