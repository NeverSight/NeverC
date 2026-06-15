**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目主页](../../docs/i18n/README.zh-CN.md)

# NeverC 示例

完整的可构建示例，展示 NeverC 的跨平台编译能力。所有示例均可从任意宿主系统交叉编译 — 无需目标平台构建环境。

---

## 可用示例

### Windows

| 示例 | 说明 | 关键特性 |
|------|------|---------|
| [Windows 内核驱动](../../examples/windows-driver/README.zh-CN.md) | 最小 WDM 内核驱动 | 从 macOS/Linux 交叉编译 `.sys`，自动 LTO，内置链接器，`DbgPrint` 设备 I/O |
| [Windows 驱动 + CET](../../examples/windows-driver-cet/README.zh-CN.md) | 带 Intel CET 影子栈的内核驱动 | CET 兼容内核代码，`/guard:ehcont`，影子栈强制 |
| [Windows 驱动 + 浮点](../../examples/windows-driver-float/README.zh-CN.md) | 带浮点/SIMD 的内核驱动 | 内核模式安全浮点，`KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |
| [Windows Ring3 EXE](../../examples/windows-exe/README.zh-CN.md) | 用户态控制台程序 | GetSystemInfo，进程枚举，VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.zh-CN.md) | 用户态 DLL | ReadProcessMemory，VirtualAllocEx，模块枚举 |

### Linux

| 示例 | 说明 | 关键特性 |
|------|------|---------|
| [Linux Hello World](../../examples/linux-hello/README.zh-CN.md) | 最小 C 程序 | 从 macOS/Windows 交叉编译 ELF，printf，字符串操作 |
| [Linux POSIX](../../examples/linux-posix/README.zh-CN.md) | POSIX 系统编程 | pthreads、mmap、pipe、信号处理 |
| [Linux 全静态](../../examples/linux-static/README.zh-CN.md) | 全静态链接二进制 | `-static` 链接，零运行时依赖，数学函数 |
| [Linux 网络](../../examples/linux-network/README.zh-CN.md) | TCP Socket 演示 | 客户端/服务器，Socket API，回环通信 |
| [Linux 数学 + zlib](../../examples/linux-math/README.zh-CN.md) | 数学 + 压缩 | 三角函数，特殊函数，zlib 压缩/解压，CRC32 |

### macOS

| 示例 | 说明 | 关键特性 |
|------|------|---------|
| [macOS 应用程序](../../examples/macos-app/README.zh-CN.md) | 原生 Mach-O 可执行文件 | sysctl、uname、Mach host_info/task_info、进程自省 |
| [macOS 动态库](../../examples/macos-dylib/README.zh-CN.md) | 原生 `.dylib` 动态库 | Mach vm_read/vm_write、vm_alloc/vm_dealloc、task_info、XOR 辅助 |

### Android

| 示例 | 说明 | 关键特性 |
|------|------|---------|
| [Android ELF](../../examples/android-elf/README.zh-CN.md) | Root 设备上的原生 ARM64 可执行文件 | 交叉编译到 Android，dlopen/liblog，/proc 信息，root 检测 |
| [Android 共享库](../../examples/android-so/README.zh-CN.md) | 原生 ARM64 `.so` 库 | 共享库，mmap RWX，XOR 加密，dlopen liblog |

---

## 快速开始

所有示例遵循相同模式：

```bash
cd examples/<示例名>
neverc make
```

如需指定编译器路径：

```bash
neverc make NEVERC=/path/to/neverc
```

Linux 示例支持架构选择：

```bash
neverc make TARGET=aarch64-linux-gnu   # 构建 ARM64 版本
neverc make TARGET=x86_64-linux-gnu    # 构建 x86_64 版本（默认）
```

macOS 示例支持架构选择：

```bash
neverc make TARGET=arm64-apple-macos     # 构建 Apple Silicon 版本（默认）
neverc make TARGET=x86_64-apple-macos    # 构建 Intel 版本
```

Android 示例默认面向 ARM64：

```bash
cd examples/android-elf
neverc make            # 构建
neverc make run        # 构建 + 推送到设备 + 通过 adb 运行
```

---

## 跨平台亮点

- **单一工具链**：NeverC 在一次调用中处理预处理、编译、优化（自动 LTO）和链接
- **捆绑 SDK**：Windows SDK/WDK、Linux sysroot（Ubuntu 22.04）和 Android sysroot（NDK r26c, API 21+）头文件/库已捆绑在 `runtime/` 中 — 零外部依赖
- **宿主无关**：从 macOS（arm64/x86_64）、Linux（x86_64/aarch64）或 Windows 使用相同命令构建
- **多目标**：从任意宿主交叉编译到 Windows PE（`.sys`/`.exe`/`.dll`）、Linux ELF、macOS Mach-O（`.dylib`）和 Android ELF
- **调试支持**：传入 `-g` 可嵌入 DWARF 调试信息；使用 `llvm-dwarfdump` 检查
