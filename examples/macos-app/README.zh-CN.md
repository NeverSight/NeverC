**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# macOS 应用程序示例

使用 NeverC 交叉编译的原生 macOS Mach-O 可执行文件。演示通过 sysctl、uname 和 Mach 内核 API 获取系统与进程信息。可从 macOS、Windows 或 Linux 构建——无需 Xcode。

## 构建

从仓库根目录（默认目标：`arm64-apple-macos`）：

```bash
cd examples/macos-app
neverc make
```

构建 Intel 版本：

```bash
neverc make TARGET=x86_64-apple-macos
```

使用独立的 NeverC 发行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手动构建（不使用 Make）

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## 运行

```bash
./macos-app
```

## 功能说明

- 通过 `uname` 查询内核信息
- 通过 `sysctl` 读取硬件详情（型号、CPU 数量、内存大小、页面大小）
- 报告进程身份（`getpid`、`getppid`、`getuid`）
- 通过 Mach `host_info` 获取主机信息，`task_info` 获取任务内存统计
