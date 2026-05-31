**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 数学 + zlib 示例

演示使用 NeverC 交叉编译到 Linux 的数学库函数和 zlib 压缩。使用内置 sysroot 中的 `-lm` 和 `-lz`。

NeverC 在 `runtime/linux/` 中内置了 Linux sysroot（Ubuntu 22.04，glibc 2.35），单次调用即可完成预处理、编译、优化（auto-LTO）以及通过内置链接器进行链接。

## 构建

从仓库根目录（默认目标：`x86_64-linux-gnu`）：

```bash
cd examples/linux-math
make
```

构建 AArch64 版本：

```bash
make TARGET=aarch64-linux-gnu
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -lm -lz -o math-demo main.c
```

## 运行

将 `math-demo` 复制到 Linux 机器（或 Docker 容器）中执行：

```bash
chmod +x math-demo
./math-demo
```

## 功能说明

- **三角函数**：0° 到 360° 的 sin/cos/tan
- **特殊函数**：`exp`、`log`、`tgamma`（阶乘）、`erf`、`cbrt`、`hypot`
- **zlib 压缩**：使用 `compress2`（最佳压缩）压缩字符串，用 `uncompress` 解压缩，验证往返一致性，计算 CRC32
