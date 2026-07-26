**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Android 共享库示例

使用 NeverC 交叉编译的 ARM64 原生 `.so` 共享库，用于 Android 平台。设计为通过 `dlopen` 加载或在 root 设备上构建时链接。可从 macOS、Windows 或 Linux 构建——无需 Android NDK 或 CMake。

## 构建

```bash
cd examples/android-so
neverc make
```

## 手动构建（不使用 Make）

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## 功能说明

- 提供游戏安全研究常用的辅助函数：PID 查询、`/proc/self/maps` 读取、RWX 内存分配、XOR 缓冲区加密
- 使用 `dlopen` 动态加载 `liblog.so` 输出 Android logcat 日志
- 演示使用 `mmap` + `PROT_EXEC` 分配可执行内存

