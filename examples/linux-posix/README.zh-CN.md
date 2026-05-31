**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux POSIX API 示例

演示使用 NeverC 交叉编译到 Linux 的 POSIX 系统编程：pthreads、mmap、pipe 和信号处理。

NeverC 在 `runtime/linux/` 中内置了 Linux sysroot（Ubuntu 22.04，glibc 2.35），单次调用即可完成预处理、编译、优化（auto-LTO）以及通过内置链接器进行链接。

## 构建

从仓库根目录（默认目标：`x86_64-linux-gnu`）：

```bash
cd examples/linux-posix
make
```

构建 AArch64 版本：

```bash
make TARGET=aarch64-linux-gnu
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## 运行

将 `posix-demo` 复制到 Linux 机器（或 Docker 容器）中执行：

```bash
chmod +x posix-demo
./posix-demo
```

## 功能说明

- **pthreads**：创建 4 个工作线程，每个计算一个总和，然后汇合
- **mmap**：分配匿名内存页，写入数据后取消映射
- **pipe**：通过 Unix 管道发送消息并读回
- **signals**：安装 `SIGUSR1` 处理程序并验证其正确触发
