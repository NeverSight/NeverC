**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 全静态链接示例

使用 NeverC 构建的自包含全静态链接 Linux 可执行文件。输出二进制零运行时依赖——可在任何 Linux 系统上运行，无需共享库。

NeverC 在 `runtime/linux/` 中内置了 Linux sysroot（Ubuntu 22.04，glibc 2.35），单次调用即可完成预处理、编译、优化（auto-LTO）以及通过内置链接器进行链接。

## 构建

从仓库根目录（默认目标：`x86_64-linux-gnu`）：

```bash
cd examples/linux-static
neverc make
```

构建 AArch64 版本：

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## 运行

将 `static-demo` 复制到 Linux 机器（或 Docker 容器）中执行：

```bash
chmod +x static-demo
./static-demo
```

## 功能说明

- 报告系统信息（指针大小、架构）
- 数学函数：`sqrt`、`sin`、`cos`、`pow`、`log`、`exp`
- 字符串操作：`snprintf`、`strdup`、排序
- 动态内存：跨 10 种大小的 `malloc`/`free`
