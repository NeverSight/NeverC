**语言**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC 插件 IR API

首个公开插件 ABI 通过稳定的 C 表暴露 LLVM IR。插件不包含 LLVM 头文件，也不得把
NeverC 句柄强制转换为 LLVM 对象。

## 接口

在 `neverc_plugin_entry` 中用 `NevercBootstrapAPI.QueryInterface` 查询接口：

- `NEVERC_INTERFACE_IR_CORE` —— 模块、类型、值、CFG、元数据、属性、常量和序列化
  查询。
- `NEVERC_INTERFACE_IR_BUILDER` —— 事务式 IR 构造与变更。
- `NEVERC_INTERFACE_IR_ANALYSIS` —— 内建分析与插件自定义分析。
- `NEVERC_INTERFACE_IR_PASS` —— Module、CGSCC、Function 和 Loop pass。
- `NEVERC_INTERFACE_IR_GEN` —— 替换 SemanticUnit 到 IR 的下降过程。
- `NEVERC_INTERFACE_IR_OPTIMIZATION` —— 替换整条优化流水线。

始终请求头文件中的 major/minor 组合，并验证返回的 `StructSize` 覆盖到插件要调用的
最后一个函数指针。更新版本的宿主可能追加字段；插件必须忽略未知的尾部。

## 句柄与所有权

IR 句柄是限定在任务作用域内的不透明 `{Owner, Value}` 对。它们引用的所有对象都归宿主
所有。

- 绝不在回调或任务结束后继续持有任务作用域句柄。
- 绝不在另一个 session 或 task 中使用某个句柄。
- 一次已提交的替换会让被替换对象的句柄失效。
- 一次中止的变更会让该变更创建的句柄过期。
- API 会返回 `NEVERC_STATUS_STALE_HANDLE`、`WRONG_OWNER` 或 `WRONG_TYPE`，而不会
  暴露 LLVM 指针。

除非某个 API 明确返回可释放的缓冲区，否则查询调用返回的字符串和字节视图都是借用的。

## 读取 IR

`NevercIRCoreAPI` 提供：

- 模块标识符、triple、data layout 和内联汇编；
- 面向函数、全局量、基本块、指令、use 和操作数的稳定值游标；
- 稳定的类型 ID 和 opcode ID；
- 函数、全局量、指令、元数据和属性的各项性质；
- 整数、浮点、聚合、null、poison 和 undef 常量；
- bitcode 导出／导入以及经过验证的模块产物。

集合游标是有界的：传入一个输出容量，然后反复收集，直到返回的数量为零。

## 事务式变更

所有结构性变更都使用 `NevercIRBuilderAPI`：

1. 开启一次模块级或函数级变更。
2. 创建一个绑定到该变更的构建器。
3. 设置插入点，构建指令、函数或基本块。
4. 提交该变更。
5. 销毁构建器和变更句柄。

提交会验证候选 IR 并原子地发布它。验证器失败时，宿主会回滚该变更并保留原模块。
`AbortMutation` 始终回滚暂存的改动。

改动 IR 之后不要声明 `NEVERC_IR_PRESERVE_ALL`。pass 适配器会检查模块代数，并拒绝
不一致的保留声明。

## Pass 层级与阶段

`NevercIRPassDescriptor.Level` 支持：

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

稳定的插入阶段为 `PRE_OPT`、`PIPELINE_START`、`OPTIMIZER_LAST`、`POST_OPT` 和
`PRE_CODEGEN`。每次调用只包含对应层级有效的句柄。函数 pass 和循环 pass 可能并发
执行，因此可变的插件状态必须遵守所声明的并发契约。

宿主总会执行最终的密封 IR 验证器。插件无法替换、拦截或跳过这道 gate。

## 分析

内建分析 ID 覆盖调用图、支配树、后支配树、循环信息、标量演化、MemorySSA 和别名
分析。

插件分析要声明依赖和生命周期回调。结果按调用缓存，并根据 pass 的保留结果失效。递归
依赖环，以及从分析回调中发起变更，都会被拒绝。

## 完整 Provider

IR 生成 Provider 可以替换内建的下降过程，并发布一个经过验证的模块产物。优化
Provider 可以替换整条内建优化流水线。这两条路线都要：

- 消费显式的阶段输入；
- 通过宿主 API 发布结果，而不是返回一个 LLVM 指针；
- 验证目标兼容性和模块有效性；
- 发布失败时原子地保留旧模块。

优化 Provider 之后，最终验证器依然是强制的。

## 最小示例

`pluginsdk/examples/FunctionPass.c` 是一个只读的函数 pass。
`pluginsdk/examples/ExamplePlugin.c` 展示模块枚举，
`pluginsdk/examples/CustomCallConvPlugin.c` 演示属性和调用点性质。

构建并加载一个示例：

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

请使用 CMake 为当前平台生成的模块后缀。

## 失败规则

每个回调都要返回 `NevercStatus`。插件失败会变成结构化诊断；不要让异常穿过 C 边界。
初始化每一个输出表头和保留字段，并在必需指针缺失时返回 `INVALID_ARGUMENT`。

规范的 ABI 声明、阶段策略和测试证据见 `PluginIR.h`、`PluginPhaseSchema.h` 和
`coverage.json`。
