**语言**: [English](../translate.md) | [简体中文](translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← 文档](README.md)

# 将 C++ 转译为 NeverC

实验性命令 `neverc translate` 通过 `cpp-core-v1`、`cpp-project-v1` 和 `cpp-math-v1` 生成可审查的 `.nc` 源码。

**目前仅实现了 C++ 输入转译。** 易语言（E Language，`.e`）、Python、Go、Rust、TypeScript 和 JavaScript 均为未来计划，尚无可用的转译器。

## 安装与标量转译

使用正常安装的 NeverC 及其标准资源即可。C++ 前端和批准的 SDK 头文件均已内置，无需另行安装 Clang。构建细节见[前端说明](../../utils/translate-frontends/cpp/README.md)。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` 接受一个不含 include 的独立 C++17 源文件，支持 `int`、`unsigned int`、`bool`、`void`、简单聚合类型、自由函数、命名空间、重载及文档列出的控制流。所有项目声明都会检查，包括未使用的代码。

## 多文件项目

从编译数据库中明确选择翻译单元，并指定项目根目录。内置前端逐个分析翻译单元，合并器检查定义的完整性、链接属性及共享类型，并保守验证单一定义规则（ODR）。

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

项目输出为 `translated.nc` 和 `translated.h`，仅允许该根目录内的项目头文件。一个文件有多种配置时，用 `--compdb-entry src/a.cpp=4` 选择编译数据库中的零起始索引。保存的编译命令只作为数据解析，绝不会执行。

## 有限的双精度数学支持

`cpp-math-v1` 在项目支持上增加 `double`、文档列出的转换／比较，以及精确签名 `std::fabs(double)` 和 `std::floor(double)`。它使用内置的 Clang 20.1.8／libc++ 200100／macOS 15.5 头文件集合，要求明确的 macOS 15.0 目标（arm64 或 x86_64）。暂不支持通用浮点算术。浮点环境要求屏蔽异常陷阱且禁用非正规数归零模式；四种标准舍入模式均已测试。

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

数学转译还会验证已安装的 NeverC 数学头文件、嵌入实现的身份，并实际执行链接探测。生成的数学模块使用 NeverC 运行库。使用 `-fno-builtin-std` 时，仅当最终输出需要 `fabs`／`floor` 映射才会在写入前失败；无需这些映射的数学代码仍可转译。

## 验证与输出

使用 `--check` 替代输出选项，可运行相同的分析、生成、语法及目标代码验证，但不保留生成文件。`--report PATH` 写入结构化诊断。转译过程不会运行源程序。

输出及附属文件不会覆盖已有文件。`--out-dir` 要求父目录已存在且目标目录不存在；`-o` 要求新的 `.nc` 路径。清单记录目标要求、输入／输出哈希和编译方式，源码映射将生成行关联到原始位置。

CI 结果、运行与安装验证以及测试跳过项按平台分别记录。原生 macOS arm64 与通过 Rosetta 运行的 macOS x86_64 仍是不同的验证环境。完整 C++／STL、指针、引用、数组、异常、模板、字符串及 `std::vector` 不在已公布的支持范围内。详见[支持矩阵](../../utils/translate-frontends/docs/support-matrix.md)、[协议与恢复规则](../../utils/translate-frontends/docs/protocol.md)及[项目示例](../../tests/neverc/Inputs/translate/cpp/project)。
