**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目](../../README.md)

# `neverc build` / `neverc make`

NeverC 内置 **GNU Make 兼容** 的构建驱动。`neverc build` 与 `neverc make` 是同一
命令：读取 Makefile、展开变量与函数并执行配方。[`examples/`](../examples/README.zh-CN.md)
下的示例均按此工作流编写。

这**不是**基于 `neverc.toml` 的项目工具。请在命令行传入普通 Make 选项与
`VAR=value` 覆盖。

## 语法

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

完整选项列表见 `neverc make --help`。

## 选项

| 选项 | 含义 |
|------|------|
| `-f FILE` | 使用指定 Makefile，而非默认搜索 |
| `-j [N]` | 并行任务数（单独 `-j` 时使用本机 CPU 数） |
| `-C DIR` | 读 Makefile 前切换目录（可重复） |
| `-n`, `--dry-run` | 只打印配方，不执行 |
| `-k`, `--keep-going` | 配方出错后继续 |
| `-s`, `--silent` | 不回显配方 |
| `-B`, `--always-make` | 无条件重建全部目标 |
| `-p` | 打印规则/变量数据库后退出 |
| `VAR=VALUE` | 命令行变量（除非 `override`，否则覆盖 Makefile） |
| `-h`, `--help` | 显示用法 |

## Makefile 发现顺序

未指定 `-f` 时，在当前目录按顺序查找：

1. `GNUmakefile`
2. `makefile`
3. `Makefile`

## 支持的 Make 能力（摘要）

覆盖 NeverC 示例与常见小型项目所需的 GNU Make 子集：

- 规则、模式规则、`.PHONY`，以及配方前缀 `@`、`-`、`+`
- 赋值：`=`、`:=` / `::=`、`+=`、`?=`、`!=`（shell 赋值）、`override`、`define`/`endef`
- 条件：`ifeq` / `ifneq` / `ifdef` / `ifndef`，含 `else ifeq …`
- `include` / `-include` / `sinclude`、`export` / `unexport`、`undefine`
- 常用函数：`subst`、`patsubst`、`filter`、`wildcard`、`foreach`、`call`、
  `eval`、`shell`、`error` / `warning` / `info`、路径辅助（`dir`、`notdir`、
  `abspath` 等）及相关字符串工具
- 自动/内置变量如 `CURDIR`、`MAKE`、`MAKEFLAGS`、`MAKECMDGOALS`、`MAKE_VERSION`
  （兼容性报告为 `4.3`）

这是有意收窄的子集，并非完整 GNU Make 或任意第三方 Makefile 的替代品。

## 典型 NeverC Makefile

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

交叉编译示例常通过命令行传入 `ARCH=…` 或 `TARGET=…`；Android 示例常定义使用
`adb` 的 `run` 目标。详见 [示例](../examples/README.zh-CN.md)。

## 相关命令

| 命令 | 适用场景 |
|------|----------|
| `neverc file.c -o out` | 无 Makefile 的单文件或脚本化编译 |
| [`neverc run`](../run/README.zh-CN.md) | 本机临时编译并运行 |
| [`neverc runtime`](../runtime/README.zh-CN.md) | 安装交叉 `--target` 所需的 sysroot |
| [发布二进制与 `--strip`](../release-builds/README.zh-CN.md) | 为分发剥离最终链接映像 |
