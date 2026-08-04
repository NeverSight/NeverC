**语言**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**AI 友好的安全研究 C23 编译器，基于 LLVM 构建**

集成链接器 · DynCode 流水线 · 内置运行时（`string` · `mimalloc` · `xorstr` · `strhash`）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#特性)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#特性)

[文档索引](../README.zh-CN.md) · [DynCode 指南](../dyncode-compiler/README.zh-CN.md) · [内置运行时](../builtins/README.zh-CN.md) · [插件 API](../plugin-api/README.zh-CN.md) · [路线图](../roadmap/README.zh-CN.md)

</div>

---

> **说明：** GitHub 仓库首页固定展示英文 `README.md`，不会根据浏览器语言自动切换。请用上方语言链接进入对应版本；进入 [文档](../README.zh-CN.md) 或 [dyncode 指南](../dyncode-compiler/README.zh-CN.md) 后，请继续通过页内语言栏与面包屑保持同一语言。

## 概述

NeverC 将标准 C 编译为宿主二进制、独立可执行文件以及位置无关 dyncode——全部来自同一工具链。目标架构为 **x86_64** 与 **AArch64**（仅小端）。未来版本将新增 **EVM**（以太坊智能合约）和 **Solana eBPF**（链上程序）编译目标。

## 为什么选择 NeverC？

C 已经是最简单的系统编程语言。NeverC 让它更简单：

- **纯 C23，仅此而已** — 没有模板、没有 RAII、没有运算符重载、没有隐式控制流。你读到的就是机器执行的。
- **内置 `string`** — 值语义字符串，支持 `+`、`==`、`.starts_with()` 和自动释放——不需要 C++。
- **无异常处理** — 错误处理始终显式。没有栈展开、没有性能意外。
- **单一二进制** — 编译器 + 链接器 + 运行时打包成一个可执行文件，零外部依赖。
- **LLM 友好** — 极简语法与确定性语义，让 AI 生成的 NeverC 代码比 C++ 更容易编译正确。
- **真正的跨平台编译** — 在 macOS 或 Linux 上直接编译 Windows PE、Linux ELF、macOS Mach-O、Android ELF 和 dyncode——不需要虚拟机、不需要双系统、不需要找 SDK。各平台 SDK 已内置在编译器里。
- **零门槛可扩展** — 单个 C 头文件、130 个具名编译阶段，就能写出[编译器插件](../plugin-api/README.zh-CN.md)，介入从 IR 优化到最终产物输出的任何阶段——不需要懂 LLVM。
- **安全研究开箱即用** — DynCode 编译、编译期字符串加密、跨平台 PE 生成全部原生集成在编译器中——不需要靠外部脚本拼凑。

## 特性

- **[DynCode 编译器](../dyncode-compiler/README.zh-CN.md)** — 多阶段 IR/MIR 流水线、跨平台提取、导入/系统调用降级、内核模式、坏字节审计与插件架构
- **集成链接器** — 单一二进制内完成 COFF、ELF、Mach-O 链接，无需外部 `ld` 或 `link.exe`
- **交叉编译** — 从任意宿主构建 Windows PE、Linux ELF、macOS Mach-O 和 Android ELF，内置各平台 SDK
- **[内置运行时](../builtins/README.zh-CN.md)** — 嵌入编译器的 LLVM bitcode 运行时：[`string`](../builtins/string/README.zh-CN.md)（值语义字符串，自动内存管理）、[`mimalloc`](../builtins/mimalloc/README.zh-CN.md)（透明高性能分配器覆盖，内核与 freestanding 目标之外默认开启）、[`xorstr`](../builtins/xorstr/README.zh-CN.md)（编译期字符串加密，反特征码解密）和 [`strhash`](../builtins/strhash/README.zh-CN.md)（编译期字符串哈希，与运行时算法一致）
- **[插件 API](../plugin-api/README.zh-CN.md)** — 纯 C ABI 的树外插件接口；单头文件 SDK，零 LLVM/CRT 依赖，覆盖驱动、预处理、AST、IR、MIR、MC、目标文件、链接、LTO、dyncode 各阶段
- **[`.nc` 扩展名](../nc-extension/README.zh-CN.md)** — 使用 `.nc` 文件扩展名自动启用所有 NeverC 功能（`string`、Rust 风格整数类型），无需额外标志
- **精简 LLVM 构建** — 仅 x86_64 / AArch64 后端；剥离 C++/ObjC/OpenMP 等路径

## 快速示例

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // 编译期加密 — `strings ./bin` 搜不到这些字面量
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // 零分配解密比较（明文不会完整出现在内存里）
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **说明：** 内置 **`string`** 在 `.c` 文件中需要显式加 **`-fbuiltin-string`**。使用 [**`.nc` 文件**](../nc-extension/README.zh-CN.md) 或 **`-fdyncode`** 模式时自动启用。

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

详细设计说明、平台矩阵、CLI 参考与示例见 **[文档索引](../README.zh-CN.md)**。更多完整可构建示例见 **[examples](../examples/README.zh-CN.md)**。

## 安装

在 **Linux x64/arm64** 和 **macOS arm64** 上，一条命令安装最新 release：

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

安装脚本会下载对应平台的 release 包、对照 `SHA256SUMS` 校验、安装到 `~/.neverc`，并把 `~/.neverc/bin` 加入 shell 的 `PATH`。

安装指定版本：

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

验证安装：

```bash
neverc --version
neverc hello.c -o hello -fbuiltin-string
```

**Windows x64/arm64** 安装包请从 [GitHub Releases](https://github.com/NeverSight/NeverC/releases) 手动下载。macOS arm64 二进制已使用 Apple Developer ID 签名并完成公证。

可选安装环境变量：

| 变量 | 说明 |
|------|------|
| `NEVERC_INSTALL_DIR` | 安装目录（默认：`~/.neverc`） |
| `NEVERC_VERSION` | Release 标签，如 `v3389.1.2`（默认：最新版） |
| `NEVERC_NO_MODIFY_PATH=1` | 不修改 shell 配置文件 |

交叉编译 sysroot（Windows SDK、Linux sysroot 等）在编译器加入 `PATH` 后按需安装：

```bash
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

Release 安装可将编译器与已经安装的交叉编译 runtime 作为一个版本单元同步更新：

```bash
neverc update                 # 更新到最新完整 release
neverc update v3389.1.2       # 切换到精确版本，也支持降级
```

`neverc upgrade` 是同义命令。NeverC 只解析一个明确的 release 标签，仅重装原本
已经存在的 runtime，并把它们全部固定到编译器目标版本。所有必需的包都会在修改
现有文件前完成下载、SHA256 校验、解压与内容验证；暂存或校验失败不会改动当前安装，
提交失败则自动回滚。如果某个 runtime release 有问题，执行
`neverc update <较早版本>` 即可让编译器和所有已安装 runtime 一起回退。

## 从源码构建

构建依赖、构建命令、Windows 交叉编译、PATH 设置，以及在 release 安装与本地源码构建之间切换，详见 **[本地开发](../local-dev/README.zh-CN.md)**。

## 贡献

NeverC **设计上仅支持 C**（C23）。C++、Objective-C、CUDA 及类似语言前端不在项目范围内；
相关 Pull Request 将被直接关闭。若需要面向 C++ 的 LLVM 工具链，请考虑
[llvm-msvc](https://github.com/backengineering/llvm-msvc)。

涉及语言、ABI 或运行时的大范围改动，请先开 issue 讨论范围，再提交 Pull Request。

默认开发分支为 **`dev`**。开始工作前请克隆并检出该分支；向 `dev` 提交 Pull Request。

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## 许可证

[AGPL-3.0](../../LICENSE)

LLVM 组件保留 [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) 许可证。
