**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Android 共享库示例

使用 NeverC 交叉编译的 ARM64 原生 `.so` 共享库，用于 Android 平台。设计为通过 `dlopen` 加载或在 root 设备上构建时链接。可从 macOS、Windows 或 Linux 构建——无需 Android NDK 或 CMake。

## 构建

```bash
cd examples/android-so
neverc make          # debug：-g（首次构建默认）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

Makefile 会持久化 `PROFILE`，后续 `neverc make` 会保持同一 debug/release
选择。release 使用 NeverC 内置 `--strip`：删除调试元数据与不需要的静态
符号名，同时保留加载器/动态 ABI 仍需要的名称。详见
[发行构建](../../docs/release-builds/README.zh-CN.md)。

## 手动构建（不使用 Make）

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## 功能说明

- 提供游戏安全研究常用的辅助函数：PID 查询、`/proc/self/maps` 读取、RWX 内存分配、XOR 缓冲区加密
- 使用 `dlopen` 动态加载 `liblog.so` 输出 Android logcat 日志
- 演示使用 `mmap` + `PROT_EXEC` 分配可执行内存

