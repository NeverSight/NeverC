**语言**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**基于 LLVM 的安全研究向 C23 编译器**

集成链接器 · Shellcode 流水线 · 内置运行时（`string` · `mimalloc` · `xorstr`）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#特性)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#交叉编译到-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#特性)

[文档索引](../README.zh-CN.md) · [Shellcode 指南](../shellcode-compiler/README.zh-CN.md) · [内置运行时](../builtins/README.zh-CN.md)

</div>

---

> **说明：** GitHub 仓库首页固定展示英文 `README.md`，不会根据浏览器语言自动切换。请用上方语言链接进入对应版本；进入 [文档](../README.zh-CN.md) 或 [shellcode 指南](../shellcode-compiler/README.zh-CN.md) 后，请继续通过页内语言栏与面包屑保持同一语言。

## 概述

NeverC 将标准 C 编译为宿主二进制、独立可执行文件以及位置无关 shellcode——全部来自同一工具链。目标架构为 **x86_64** 与 **AArch64**（仅小端）。

## 特性

- **[Shellcode 编译器](../shellcode-compiler/README.zh-CN.md)** — 多阶段 IR/MIR 流水线、跨平台提取、导入/系统调用降级、内核模式、坏字节审计与插件架构
- **集成链接器** — 单一二进制内完成 COFF、ELF、Mach-O 链接，无需外部 `ld` 或 `link.exe`
- **交叉编译** — 在 macOS/Linux 上构建 Windows PE，支持捆绑 MSVC SDK
- **[内置运行时](../builtins/README.zh-CN.md)** — 嵌入编译器的 LLVM bitcode 运行时：[`string`](../builtins/string/README.zh-CN.md)（值语义字符串，自动内存管理）、[`mimalloc`](../builtins/mimalloc/README.zh-CN.md)（透明高性能分配器覆盖）和 [`xorstr`](../builtins/xorstr/README.zh-CN.md)（编译期字符串加密，反特征码解密）
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

> **说明：** 内置 **`string`** 在 `.c` 文件中需要显式加 **`-fbuiltin-string`**。使用 [**`.nc` 文件**](../nc-extension/README.zh-CN.md) 或 **`-fshellcode`** 模式时自动启用。

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

详细设计说明、平台矩阵、CLI 参考与示例见 **[文档索引](../README.zh-CN.md)**。

## macOS 预编译产物

发布产物是 ad‑hoc 签名（没有 Apple Developer ID，未公证）。如果是通过浏览器下载的，解压后跑一次清除 quarantine 属性即可：

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

## 构建

### 依赖

- CMake 3.20+
- Ninja
- 支持 C++17 的宿主编译器（GCC、Clang 或 MSVC）

### 配置

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### 编译

```bash
cmake --build build-neverc --target neverc
```

若存在 `ccache` / `sccache` 将自动检测并启用。

### 测试

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### 验证

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## 交叉编译到 Windows

NeverC 在 `runtime/` 中内置了 Windows SDK 和 WDK，无需额外配置。

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows shellcode（`-fshellcode`、PEB 导入解析等）详见 [shellcode 编译器文档](../shellcode-compiler/README.zh-CN.md)。

## 贡献

默认开发分支为 **`dev`**。开始工作前请克隆并检出该分支；向 `dev` 提交 Pull Request。

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## 许可证

[AGPL-3.0](../../LICENSE)

LLVM 组件保留 [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) 许可证。
