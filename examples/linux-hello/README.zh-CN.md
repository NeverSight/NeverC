**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Hello World 示例

使用 NeverC 交叉编译到 Linux ELF 的最小 C 程序。支持从 macOS、Windows 或 Linux 构建——无需目标系统工具链。

NeverC 在 `runtime/linux/` 中内置了 Linux sysroot（Ubuntu 22.04，glibc 2.35），单次调用即可完成预处理、编译、优化（auto-LTO）以及通过内置链接器进行链接。

## 构建

从仓库根目录（默认目标：`x86_64-linux-gnu`）：

```bash
cd examples/linux-hello
neverc make
```

构建 AArch64 版本：

```bash
neverc make TARGET=aarch64-linux-gnu
```

使用独立的 NeverC 发行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## 运行

将 `hello` 复制到 Linux 机器（或 Docker 容器）中执行：

```bash
chmod +x hello
./hello
```

## 功能说明

- 打印问候语和命令行参数
- 演示内置 libc 的 `printf`、`strncpy`、`strlen`、`atoi`
- XOR 变换字符串以验证基本的整数/字符操作
