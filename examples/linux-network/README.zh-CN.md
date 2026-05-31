**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 网络 Socket 示例

使用 NeverC 交叉编译到 Linux 的 TCP 客户端/服务器演示。客户端和服务器在同一进程中运行，使用回环地址简化演示。

NeverC 在 `runtime/linux/` 中内置了 Linux sysroot（Ubuntu 22.04，glibc 2.35），单次调用即可完成预处理、编译、优化（auto-LTO）以及通过内置链接器进行链接。

## 构建

从仓库根目录（默认目标：`x86_64-linux-gnu`）：

```bash
cd examples/linux-network
make
```

构建 AArch64 版本：

```bash
make TARGET=aarch64-linux-gnu
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -o network-demo main.c
```

## 运行

将 `network-demo` 复制到 Linux 机器（或 Docker 容器）中执行：

```bash
chmod +x network-demo
./network-demo
```

## 功能说明

- 在随机端口上创建 TCP 服务器（127.0.0.1）
- 客户端连接到服务器
- 从客户端向服务器发送 3 条消息并打印接收的数据
- 演示 `socket`、`bind`、`listen`、`accept`、`connect`、`send`、`recv`
