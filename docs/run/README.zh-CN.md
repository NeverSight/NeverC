**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目](../../README.md)

# `neverc run`

将 C 或 NeverC 程序编译为**临时可执行文件**，在**本机**运行，返回其退出码，然后删除产物。工作流刻意设计得类似 `go run`。

需要保留二进制、分发或用调试器调试时，请使用普通编译命令（`neverc ... -o output`）。

## 语法

```text
neverc run [编译器选项] file.c [file2.nc ...] [程序参数...]
neverc run [编译器参数...] -- [程序参数...]
```

也可运行 `neverc run --help` 查看内置摘要。

## 参数解析

`neverc run` 使用以下两种规则之一，将参数拆分为**编译器调用**和可选的**程序参数**。

### 默认（Go 风格）拆分

1. 从左向右扫描，找到第一个以 `.c` 或 `.nc` 结尾且不以 `-` 开头的参数。
2. **第一个源文件之前及连续 `.c`/`.nc` 源文件**全部传给编译器。
3. 连续源文件**之后**的参数传给临时程序的 `argv`。

示例：

```bash
# 编译器：-O2 -fbuiltin-string hello.c
# 程序：（无）
neverc run -O2 -fbuiltin-string hello.c

# 编译器：-O2 main.c helper.nc
# 程序：  --verbose two words
neverc run -O2 main.c helper.nc -- --verbose two words

# 编译器：-DGENERATED=.c -O2 main.c
# 程序：  argument
neverc run -DGENERATED=.c -O2 main.c argument
```

说明：

- 只有 `.c` 和 `.nc` 会被当作 run 源文件。以 `-` 开头的参数（如 `-DGENERATED=.c`）始终留在编译器侧。
- 多个源文件会编译并链接成一个临时二进制，与普通多文件编译相同。

### 显式 `--` 分隔

当编译器需要在源文件列表**之后**再接收参数（链接选项、非源输入、`-x c -` 等）时，用 `--` 分隔编译器尾部与程序参数：

```bash
# 编译器：hello.c helper.o -lm
# 程序：  arg.c -x        （这些是 argv，不是编译器选项）
neverc run hello.c helper.o -lm -- arg.c -x

# 编译器：hello.c -O1
# 程序：  x
neverc run hello.c -O1 -- x
```

`--` 之前的所有内容会原样转发给 `neverc`（并附加内部 `-o <temp>`）；`--` 之后的内容成为程序参数。

## 运行时行为

| 主题 | 行为 |
|------|------|
| 工作目录 | 临时程序在**当前目录**运行。相对路径与普通二进制一致。 |
| 环境 | 继承当前环境（`PATH`、已导出变量等）。 |
| 标准 I/O | stdin、stdout、stderr 连接到临时进程，管道与重定向照常工作。 |
| 退出码 | 成功时返回**程序**退出码。编译失败时返回**编译器**退出码，且**不会**运行程序。 |
| 临时文件 | 可执行文件位于唯一的 `neverc-run-*` 目录。运行结束后删除（无论程序成功或失败）。清理失败会单独报错。 |

## 示例

**带优化和 string builtin 的快速运行：**

```bash
neverc run -O2 -fbuiltin-string hello.c
```

**向 `main` 传参（含带空格的参数）：**

```bash
neverc run -fbuiltin-string greet.c -- Alice "two words"
```

**编译多个翻译单元后运行：**

```bash
neverc run -O2 main.c util.nc -- --port 8080
```

**源文件后面还有编译器参数时使用 `--`：**

```bash
neverc run app.c extra.o -lm -- --config prod.json
```

## 限制与注意

- **仅本机执行。** `neverc run` 总是在调用 `neverc` 的机器上尝试运行临时二进制。交叉编译选项（`-target ...`）可能仍能编译，但产物通常无法在本机运行。
- **无持久产物。** 命令结束后二进制会被删除，无法事后挂调试器。需要保留可执行文件时请用 `neverc ... -o out`。
- **与 `neverc` 同一工具链。** 该命令会重新调用处理 `run` 的同一个 `neverc` 二进制，转发你的编译选项（除内部 `-o` 外）。
- **`.nc` 源文件。** 规则与 `.c` 相同；`.nc` 自动启用的语言扩展照常生效。

## 相关命令

| 命令 | 适用场景 |
|------|----------|
| `neverc file.c -o out` | 保留二进制、交叉编译或集成到构建脚本 |
| `neverc build` / `neverc make` | 基于 `neverc.toml` 的项目式构建 |
| `neverc run --help` | 内置用法摘要 |
