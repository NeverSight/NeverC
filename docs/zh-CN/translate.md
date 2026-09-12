**语言**: [English](../translate.md) | [简体中文](translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← 文档](README.md)

# 将 C++ 转译为 NeverC

实验性命令 `neverc translate` 通过 `cpp-core-v1`、`cpp-core-v2`、`cpp-project-v1` 和 `cpp-math-v1` 生成可审查的 `.nc` 源码。

**目前仅实现了 C++ 输入转译。** 易语言（E Language，`.e`）、Python、Go、Rust、TypeScript 和 JavaScript 均为未来计划，尚无可用的转译器。

## 安装与标量转译

使用正常安装的 NeverC 及其标准资源即可。C++ 前端和批准的 SDK 头文件均已内置，无需另行安装 Clang。构建细节见[前端说明](../../utils/translate-frontends/cpp/README.md)。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` 接受一个不含 include 的独立 C++17 源文件，支持 `int`、`unsigned int`、`bool`、`void`、简单聚合类型、自由函数、命名空间、重载及文档列出的控制流。所有项目声明都会检查，包括未使用的代码。

使用 `--profile cpp-core-v2` 可增加经过检查的 `typedef`／`using` 类型别名、底层类型为受支持整数类型的枚举、`static_assert`，以及限定范围的对象指针与左值引用。引用保留别名关系，包括参数和返回引用；支持多层指针的 `const` 与空指针。仍限单个源文件，且不允许 include。详见 [core v2 支持范围](../../utils/translate-frontends/docs/cpp-core-v2.md)。

core v2 还增加了固定长度的局部数组与数组字段、多维下标访问，以及数组指针和引用。部分初始化将省略的标量元素置零，记录元素按其选定的初始化方式处理；元素初始化保留源码顺序与别名关系。数组长度与初始化展开量有上限。 全局数组与变长数组仍未支持。

core v2 支持 `switch`／`case`／`default`，包括 C++17 初始化语句、分支贯穿及经过验证的 `[[fallthrough]]` 标注。选择器只求值一次；嵌套 switch 和循环保留各自的 `break`／`continue` 目标。GNU case 范围及其他语句属性仍被拒绝。

Core v2 现支持有符号和无符号的 8／16／32／64 位整数、字符类型及字面量，以及常量 `sizeof`／`alignof` 查询。先按源 C++ 规则完成整数提升与重载决议，再归一化位宽；`long`、`wchar_t` 和大小类型遵循目标平台。枚举也支持窄整数和宽整数作为底层类型。运行时字符串与 STL 仍在开发中。

Core v2 还支持对象指针偏移、差值、递增／递减和复合赋值，可用于遍历数组并保留多维数组的步长。生成的辅助函数保留 C++17 空指针加减零及空指针相减的行为。指针大小比较和指针／整数转换仍未支持，STL 容器与算法也尚未完成。

Core v2 会用 NeverC 自身的目标模型核对源类型的尺寸与 ABI 对齐，包括结构体大小和字段偏移。生成代码通过编译期断言再次检查布局，清单同时记录这些校验依据。

Core v2 支持已接纳的记录类型中的具名非虚成员函数，包括 const 重载、左值限定方法、静态方法和 `this`。调用保留原对象身份，并在参数之前求值接收对象。临时对象上的非静态调用、继承、模板和完整 STL 仍未支持。

Core v2 还支持具有标准布局及受支持复制操作的记录类型的普通用户构造函数。局部对象、记录字段与数组元素直接在最终存储位置构造，字段按声明顺序初始化。委托构造及异常仍未支持。

Core v2 为每个按值传递的记录参数创建独立对象，并将记录返回值直接写入调用者的目标位置。构造函数和成员函数采用相同规则；源代码要求的复制和引用别名仍会保留。对于满足条件的平凡记录类型，其他 C++17 实现可能增加参数或返回值的复制。

Core v2 支持普通用户析构函数，以及正常退出时的隐式成员析构。局部对象、记录字段和数组元素逆序析构；临时对象在完整表达式结束时清理，先保存需要使用的值。返回、分支、循环、break 和 continue 均执行对应清理。按值参数在被调用函数退出时析构，返回对象由调用者管理。显式析构调用、手写异常规范、静态对象析构与异常展开仍未支持。

Core v2 支持源参数为 `R&` 或 `const R&` 的普通用户复制构造和复制赋值函数。复制直接作用于实际目标，保留函数的副作用与返回引用。赋值运算符语法先求值右操作数，显式调用 `operator=` 时先求值接收对象。

Core v2 还支持编译器生成或显式 default 的默认构造函数，以及 default 析构函数，包括嵌套记录和数组成员。默认构造使用实际目标位置，并按声明顺序初始化成员；值初始化只执行 C++ 规则要求的清零。类外 default 保留其不同的初始化规则。未使用或仅出现在未求值表达式中的 default 构造不要求额外生成函数体。

Core v2 支持隐式及显式 default 的复制构造，包括嵌套记录和多维数组。选定的成员复制构造按声明及元素顺序在实际目标位置执行，源数组地址只计算一次。平凡复制保持已存储的指针字段原值。按值传参与从源对象返回保留复制副作用，直接转发纯右值不会额外复制。

Core v2 还支持隐式及显式 default 的复制赋值。成员和数组元素保留选定的赋值操作及其顺序，生成的平凡数组复制转为有类型的元素赋值，无需外部内存复制调用。平凡赋值保留指针字段原值并返回实际接收对象。运算符语法先捕获源引用，显式成员调用先捕获接收对象；源值在这些副作用完成后读取，赋值不会额外构造或析构对象。

Core v2 支持已准入字段的默认成员初始化器，包括访问先前成员、普通调用、嵌套记录和数组。选中的默认值使用实际所属对象作为 `this`，显式聚合初始化项保留调用者的 `this`；显式初始化会覆盖对应成员的默认值。隐式或 default 复制及赋值不重跑默认值，用户复制构造函数可为省略的成员选用默认值。构造成员初始化与聚合初始化各自保留临时对象的清理边界。模板与完整 STL 仍在开发中。

Core v2 支持指向已有存活对象的右值引用，包括标量、指针、记录和数组别名、引用参数／返回值、条件亡值及普通 `&&` 限定方法。`static_cast<R&&>(live)` 等转换保留同一个对象，具名右值引用变量仍是左值。重载和现有复制行为遵循 Clang 的选择，引用不会增加清理所有者。本阶段尚不支持绑定新临时对象、延长生命周期或未实现的移动操作。

Core v2 支持以存活的 `R&&` 或 `const R&&` 为源的普通用户移动构造和移动赋值。构造使用实际目标；赋值保留源值修改、操作数求值顺序和返回的 `R&` 别名，并支持 `&`／`&&` 限定的接收对象。具名右值引用仍选择左值重载，显式构造函数保留其初始化规则。移动不会结束源对象的生命周期，源与目标仍各自正常析构。生成／default 移动、新临时对象的引用绑定、异常、模板和完整 STL 仍在开发中。

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

CI 结果、运行与安装验证以及测试跳过项按平台分别记录。原生 macOS arm64 与通过 Rosetta 运行的 macOS x86_64 仍是不同的验证环境。完整 C++／STL、异常、模板、字符串及 `std::vector` 不在已公布的支持范围内。详见[支持矩阵](../../utils/translate-frontends/docs/support-matrix.md)、[协议与恢复规则](../../utils/translate-frontends/docs/protocol.md)及[项目示例](../../tests/neverc/Inputs/translate/cpp/project)。
