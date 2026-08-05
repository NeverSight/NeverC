**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 项目主页](i18n/README.zh-CN.md)

> 保持语言：使用上方语言栏；本索引内的 dyncode 等链接已指向对应中文页面。

# NeverC 文档

各子系统的设计说明、API 参考与指南。

---

## DynCode 编译器

DynCode 编译流水线是 NeverC 的核心研究方向。架构、CLI 选项、平台矩阵与示例见：

**[DynCode 编译器 →](dyncode-compiler/README.zh-CN.md)**

| 文档 | 说明 |
|------|------|
| [README](dyncode-compiler/README.zh-CN.md) | 概述、快速开始、支持的目标 |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.zh-CN.md) | IR → 对象文件 → 提取设计 |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.zh-CN.md) | 各 IR pass 的设计 rationale |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.zh-CN.md) | 后端 MIR pass |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.zh-CN.md) | Ring-0 编译 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.zh-CN.md) | `TargetDesc` 与提取器 |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.zh-CN.md) | 添加新目标平台 |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.zh-CN.md) | 从 dyncode 角度讲解 ARM64 指令 |
| [Roadmap](dyncode-compiler/roadmap/README.zh-CN.md) | 计划中的工作 |
| [Progress](dyncode-compiler/progress/README.zh-CN.md) | 实现进度 |

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
| [字符串哈希 (strhash)](builtins/strhash/README.zh-CN.md) | `-fstrhash-algo` / `-fstrhash-fold` | 编译期字符串哈希，运行时算法一致，可选 IR 常量折叠 |

---

## 插件 API

NeverC 通过一套纯 C ABI 开放整条工具链。插件是一个共享模块（`.dll` / `.so` / `.dylib`），可以观察者、拦截器或替换 Provider 的身份，附着到 130 个具名编译阶段中的任意一个——从命令行解析一直到最终链接产物。SDK 只有头文件：不含 LLVM 头文件，也不链接编译器。

**[插件 API →](plugin-api/README.zh-CN.md)**

| 文档 | 说明 |
|------|------|
| [README](plugin-api/README.zh-CN.md) | 入口点、阶段、接口协商、注册、ABI 规则 |
| [驱动 API](plugin-api/driver.zh-CN.md) | 命令行、工具链选择、action 图、job 图 |
| [源与 I/O API](plugin-api/source.zh-CN.md) | VFS Provider、源位置、缓冲区、输出 sink、依赖 |
| [预处理器 API](plugin-api/prep.zh-CN.md) | token、宏、pragma、include、特性查询、39 种事件 |
| [AST 与语义 API](plugin-api/ast-sema.zh-CN.md) | 解析器扩展、AST 修改、名字查找、类型、常量 |
| [IR API](plugin-api/ir.zh-CN.md) | LLVM IR 读取、事务式构造、分析、pass、Provider |
| [MIR API](plugin-api/mir.zh-CN.md) | 机器函数、寄存器、栈帧、MIR pass 与分析 |
| [Target、MC、汇编、目标文件](plugin-api/target-mc-object.zh-CN.md) | 目标注册、调用约定、MC 编码、目标文件图 |
| [链接与 LTO API](plugin-api/link-lto.zh-CN.md) | 链接图、符号决议、GC/ICF、链接器与 LTO Provider |
| [DynCode API](plugin-api/dyncode.zh-CN.md) | 扁平位置无关映像、导入降级、字符集编码 |
| [自定义调用约定](plugin-api/custom-callconv/README.zh-CN.md) | 数据驱动的调用约定插件 |

---

## 路线图

NeverC 项目的主要规划方向：标准库、EVM 智能合约后端和 Solana eBPF 后端。

**[路线图 →](roadmap/README.zh-CN.md)**

| 功能 | 描述 |
|------|------|
| 标准库 (`std`) | Go 风格开箱即用包：`fmt`、`os`、`io`、`net`、`crypto`、`encoding`、`sync` 等 |
| 混淆插件套件 (`neverc-obfuscation`) | 第一方 VM、MBA、控制流平坦化、多态引擎和反篡改插件 |
| UI 组件库 (`neverc-ui`) | 类 Qt 跨平台 UI，HTML/JS/CSS 渲染器，拖拽式设计器，AI 原生工作流 |
| IDE 与语言工具 (`neverc-ide`) | `.nc` 文件的 VSCode 扩展 + 独立 IDE，支持智能补全、调试和 dyncode 管线可视化 |
| EVM 智能合约 | 把 C 编译为 EVM 字节码——用 C 代替 Solidity 编写智能合约 |
| Solana eBPF | 把 C 编译为 Solana eBPF 字节码——用 C 开发链上程序 |

---

## CLI 工具

单次编译之外面向用户的命令。

| 文档 | 说明 |
|------|------|
| [`neverc run`](run/README.zh-CN.md) | 编译、在本机运行并删除临时二进制（类似 `go run`） |

---

## 本地开发

从源码构建 NeverC 并配置本地开发环境，包括 PATH 设置。

**[本地开发 →](local-dev/README.zh-CN.md)**

---

## 示例

完整的可构建示例，展示 NeverC 的跨平台编译能力。所有示例均可从 macOS / Linux 交叉编译。

**[示例 →](examples/README.zh-CN.md)**
