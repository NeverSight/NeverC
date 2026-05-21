**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 内置运行时系统](../README.zh-CN.md)

# 内置 mimalloc 分配器

## 概述

NeverC 可以将 [mimalloc](https://github.com/microsoft/mimalloc) — 微软的高性能通用内存分配器 — 通过 LLVM bitcode 合并直接嵌入编译产物中。启用后，`malloc`、`free`、`calloc` 和 `realloc` 在编译时被透明替换为 mimalloc 的实现，无需外部库链接。

**启用方式：**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

编译产物直接使用 mimalloc 进行所有堆分配，无需任何源码修改。

---

## 为什么选择 mimalloc？

mimalloc 在各种工作负载下持续超越系统分配器：

- 在分配密集的基准测试中比 glibc malloc **快 2 倍**
- 通过大小类分离的空闲链表**减少碎片**
- **线程本地堆**消除多线程程序中的锁竞争
- **安全模式**提供 guard page 和随机化分配

通过在 IR 层嵌入 mimalloc，NeverC 以单个编译器标志提供这些优势——无需构建系统变更、无需库管理、无需运行时依赖。

---

## 用法

### 基本用法

```bash
neverc -fbuiltin-mimalloc hello.c -o hello
```

### 与内置字符串组合

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

两者同时启用时，字符串运行时的 `__builtin_malloc`/`__builtin_free` 调用由 mimalloc 提供服务。

### 禁用

```bash
neverc -fno-builtin-mimalloc main.c -o main    # 显式禁用
```

### 编译时检测

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
#ifdef __NEVERC_MIMALLOC__
    printf("正在使用 mimalloc 分配器\n");
#else
    printf("正在使用系统分配器\n");
#endif

    void *p = malloc(1024);
    free(p);
    return 0;
}
```

---

## 平台支持

| 平台 | Triple | 状态 |
|------|--------|------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | 支持 |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | 支持 |
| Android | `aarch64-linux-android` | 支持（OSType = Linux） |
| macOS x86_64 | `x86_64-apple-macosx` | 支持 |
| macOS AArch64 | `arm64-apple-macosx` | 支持 |
| iOS | `arm64-apple-ios` | 支持 |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | 支持 |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | 支持 |

不支持的平台（FreeBSD 等）会静默跳过 mimalloc 注入——不报错，二进制文件使用系统分配器。

### 各 OS 的覆盖机制

| OS | 覆盖策略 |
|----|---------|
| **Linux** | `MI_OVERRIDE` 通过符号插入替换 glibc `malloc`/`free` |
| **macOS/iOS** | `malloc_zone` 注册覆盖默认 zone |
| **Windows** | 通过 `MI_OVERRIDE` 替换 CRT `malloc`/`free` |

---

## 自动抑制

驱动器在以下场景自动抑制 `-fbuiltin-mimalloc`：

| 标志 / 模式 | 原因 |
|-------------|------|
| `-fno-builtin` | 无 CRT 函数覆盖场景 |
| `-mkernel` | 内核模式无用户空间堆；驱动使用 `ExAllocatePool2` |
| `-fshellcode-mode` | shellcode 无堆——使用栈 arena |
| `-ffreestanding` | 无 libc 可覆盖 |

抑制标志与 `-fbuiltin-mimalloc` 同时存在时，抑制静默生效（不发出警告）。

---

## 架构

### IR Bitcode 嵌入

mimalloc 以 LLVM bitcode 形式嵌入编译器二进制内部。用户编译时，一个 Module Pass 在优化流水线运行前将 bitcode 合并到用户 IR。

```
┌─── 编译器构建时 ───────────────────────────────────────────────┐
│                                                                 │
│  mimalloc 源码（FetchContent）                                  │
│       │                                                        │
│       ▼ gen_mimalloc_source.py                                 │
│       │                                                        │
│  MimallocRuntime_{linux,darwin,win}.c                          │
│       │                                                        │
│       ▼ neverc -c -emit-llvm -O2 --target=<triple>            │
│       │                                                        │
│  neverc_mimalloc_{linux,darwin,win}.bc                         │
│       │                                                        │
│       ▼ bin2c.py                                               │
│       │                                                        │
│  BuiltinMimallocBitcode_{linux,darwin,win}.h                   │
│       │                                                        │
│  嵌入 neverc 二进制                                             │
└────────────────────────────────────────────────────────────────┘

┌─── 用户编译时 ─────────────────────────────────────────────────┐
│                                                                 │
│  user.c ──→ CodeGen ──→ IR 模块                                │
│                            │                                    │
│              MimallocRuntimeLinkerPass（PipelineStartEP）        │
│                            │                                    │
│               1. 从 Module triple 提取 OS                      │
│               2. 解析该 OS 的嵌入 bitcode                       │
│               3. 剥离宿主 target-cpu/target-features            │
│               4. 全量合并（linkModules）                         │
│               5. 内部化辅助函数，保留覆盖入口                     │
│               6. 清理 llvm.used / llvm.compiler.used            │
│                            │                                    │
│              优化流水线 ──→ 输出 .o                              │
└────────────────────────────────────────────────────────────────┘
```

### 按 OS 的 Bitcode

mimalloc 使用 OS 特定的系统调用（Linux 的 `mmap`、macOS 的 `vm_allocate`、Windows 的 `VirtualAlloc`），因此 bitcode 按目标 OS 分别编译。合并时，Pass 根据用户模块的 target triple 选择正确的 blob。

### Whole-Archive 语义

与字符串内置（通过调用图 BFS 裁剪未使用函数）不同，mimalloc 使用 **whole-archive** 合并——所有函数都被链接。这是必要的，因为：

1. mimalloc 的覆盖机制需要完整的 `malloc`/`free`/`calloc`/`realloc` 入口点集合
2. 内部辅助函数紧密互联
3. 系统链接器将标准分配调用解析到 mimalloc 的实现

### 符号内部化

合并后，Pass 处理符号：

- **覆盖入口点**（`malloc`、`free`、`calloc`、`realloc`、`mi_*` 等）→ **保持外部链接**
- **内部辅助函数** → **内部化**（设为 `internal` 链接）
- **全局变量** → **内部化**

---

## 引导构建流程

```bash
# 阶段 1：使用空 bitcode 占位符构建 neverc
ninja neverc

# 阶段 2：使用 neverc 编译 mimalloc 为按 OS 的 bitcode
ninja neverc-bootstrap-mimalloc-bc

# 阶段 3：使用真实嵌入 bitcode 重建 neverc
ninja neverc
```

### 引导目标

| 目标 | 描述 |
|------|------|
| `neverc-bootstrap-mimalloc-bc` | 伞目标——构建所有按 OS 的 bitcode |
| `neverc-bootstrap-mimalloc-bc-linux` | Linux bitcode（`x86_64-unknown-linux-gnu`） |
| `neverc-bootstrap-mimalloc-bc-darwin` | macOS bitcode（`arm64-apple-macosx11.0`） |
| `neverc-bootstrap-mimalloc-bc-win` | Windows bitcode（`x86_64-pc-windows-msvc`） |

---

## 文件结构

```
neverc/
├── include/neverc/Foundation/Builtin/
│   └── BuiltinMimalloc.h              # API：isSupported() + getEmbeddedBitcode()
│
├── lib/Foundation/
│   ├── CMakeLists.txt                  # 按 OS 的占位符头文件 + 引导目标
│   └── Builtin/
│       ├── BuiltinMimalloc.cpp         # 按 OS bitcode 分发
│       ├── gen_mimalloc_source.py      # 生成各 OS 的单文件 wrapper
│       └── bin2c.py                    # .bc → C 头文件（与 string 共享）
│
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.h         # Pass 声明
│   ├── MimallocRuntimeLinker.cpp       # IR 合并 Pass
│   └── BackendUtil.cpp                 # PipelineStartEP 注册
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                      # addNeverCFeatureFlags() 中的安全互锁
│
├── lib/Compiler/Preprocessor/
│   └── InitPreprocessor.cpp            # __NEVERC_MIMALLOC__ 宏定义
│
└── include/neverc/
    ├── Foundation/LangOpts/
    │   └── LangOptions.def             # LANGOPT(BuiltinMimalloc, ...)
    └── Invoke/
        └── Options.td.h                # -fbuiltin-mimalloc / -fno-builtin-mimalloc
```

---

## 编译器标志参考

| 标志 | 描述 |
|------|------|
| `-fbuiltin-mimalloc` | 启用 mimalloc 覆盖注入（hosted 构建默认开启） |
| `-fno-builtin-mimalloc` | 显式禁用 mimalloc 注入 |

### 预处理器宏

| 宏 | 值 | 何时定义 |
|----|----|---------|
| `__NEVERC_MIMALLOC__` | `1` | 当 `-fbuiltin-mimalloc` 激活时 |
