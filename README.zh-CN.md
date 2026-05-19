**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**基于 LLVM 的安全研究向 C23 编译器**

集成链接器 · Shellcode 流水线 · 内置 `string` 类型

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#特性)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#交叉编译到-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#特性)

[文档索引](docs/README.zh-CN.md) · [Shellcode 指南](docs/shellcode-compiler/README.zh-CN.md) · [内置字符串](docs/builtin-string/README.zh-CN.md)

</div>

---

> **说明：** GitHub 仓库首页固定展示英文 `README.md`，不会根据浏览器语言自动切换。请用上方语言链接进入对应版本；进入 [文档](docs/README.zh-CN.md) 或 [shellcode 指南](docs/shellcode-compiler/README.zh-CN.md) 后，请继续通过页内语言栏与面包屑保持同一语言。

## 概述

NeverC 将标准 C 编译为宿主二进制、独立可执行文件以及位置无关 shellcode——全部来自同一工具链。目标架构为 **x86_64** 与 **AArch64**（仅小端）。

## 特性

- **[Shellcode 编译器](docs/shellcode-compiler/README.zh-CN.md)** — 多阶段 IR/MIR 流水线、跨平台提取、导入/系统调用降级、内核模式、坏字节审计与插件架构
- **集成链接器** — 单一二进制内完成 COFF、ELF、Mach-O 链接，无需外部 `ld` 或 `link.exe`
- **交叉编译** — 在 macOS/Linux 上构建 Windows PE，支持捆绑 MSVC SDK
- **[内置 `string` 类型](docs/builtin-string/README.zh-CN.md)** — 值语义字符串，支持点号方法语法、自动内存管理与原生 UTF-8 后备
- **精简 LLVM 构建** — 仅 x86_64 / AArch64 后端；剥离 C++/ObjC/OpenMP 等路径

## 快速示例

```c
#include <unistd.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    write(1, msg.c_str(), msg.len);
    return 0;
}
```

```bash
# 务必指定 -target：选择产物 OS/架构/ABI，与编译机 host 无关

# macOS arm64
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64——同一份源码
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin
```

详细设计说明、平台矩阵、CLI 参考与示例见 **[文档索引](docs/README.zh-CN.md)**。

## 构建

### 依赖

- CMake 3.20+
- Ninja
- 支持 C++17 的宿主编译器（GCC、Clang 或 MSVC）

### 配置

```bash
cmake -B build-neverc -G Ninja \
  -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  llvm
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

将 [xwin](https://github.com/Jake-Shadle/xwin) SDK splat 置于 `build-neverc/sdk/msvc/` 后：

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -o hello.exe hello.c -lkernel32
```

Windows shellcode（`-fshellcode`、PEB 导入解析等）详见 [shellcode 编译器文档](docs/shellcode-compiler/README.zh-CN.md)。

## 许可证

[AGPL-3.0](LICENSE)

LLVM 组件保留 [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT) 许可证。
