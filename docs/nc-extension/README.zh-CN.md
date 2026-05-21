**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 文档](../README.zh-CN.md)

# `.nc` 文件扩展名

## 概述

NeverC 将 `.nc` 作为原生源文件扩展名。当编译器检测到 `.nc` 输入文件时，会**自动启用**所有 NeverC 语言扩展 — 无需额外标志。

## 自动启用的功能

| 标志 | 效果 |
|------|------|
| `-fneverc-types` | Rust 风格的整数别名（`u8`、`u16`、`u32`、`u64`、`i8`、`i16`、`i32`、`i64`、`usize`、`isize`） |
| `-fbuiltin-string` | 内置 `string` 值类型，具备自动内存管理、dot-call 语法和 UTF-8 支持 |

## 使用方法

只需将源文件命名为 `.nc` 扩展名：

```bash
# 自动启用 — 无需额外标志
neverc hello.nc -o hello

# 等价于：
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "你好，NeverC！";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## 工作原理

检测在编译器流水线的两个层级进行：

### 1. Driver / Toolchain 层

Driver 在构建编译器调用之前检查每个输入文件的扩展名。对于 `.nc` 文件，`-fneverc-types` 和 `-fbuiltin-string` 被无条件注入命令行 — 用户不需要手动传递。

对于 `.c` 文件，这些标志保持可选：按需显式加上所需标志（`-fneverc-types`、`-fbuiltin-string`）。

### 2. CompilerInvocation 层

作为安全兜底，前端在解析调用时也会检查输入文件扩展名。如果任何输入具有 `.nc` 扩展名，`LangOpts.NeverCTypes` 和 `LangOpts.BuiltinString` 将被设为 `1`，确保即使绕过 Driver 层（例如直接调用 `-cc1`）功能也处于活动状态。

## 兼容性

- `.nc` 文件被视为 C 源代码 — 语言仍然是 C（默认 C23），不是新语言
- 所有标准 C 标志（`-std=c11`、`-O2`、`-g`、`-Wall` 等）的行为完全相同
- `-fshellcode` 与 `.nc` 自然结合：shellcode 模式本身已启用 `string`，`.nc` 确保 `neverc-types` 也处于活动状态
- 交叉编译（`-target aarch64-linux-gnu` 等）以相同方式工作
- `.c` 文件不受影响 — 除非显式传递标志，否则行为与之前完全相同

## 何时使用 `.nc` vs `.c`

| 场景 | 建议 |
|------|------|
| 使用 `string` 和 Rust 风格类型的新 NeverC 项目 | 使用 `.nc` |
| 希望保持与其他编译器兼容的现有 C 代码库 | 使用 `.c` + 显式标志 |
| Shellcode 项目 | 两者均可 — `-fshellcode` 无论如何都会启用 `string` |
| 混合代码库 | NeverC 专用文件使用 `.nc`，可移植代码使用 `.c` |
