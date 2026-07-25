**语言**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# NeverC 插件 MIR API

首个公开插件 ABI 通过 `PluginMIR.h` 暴露 Machine IR。该 API 使用稳定的 C 标识符和
不透明句柄；插件不依赖 LLVM 的类布局、枚举取值或 C++ ABI。

## 协商

查询 `NEVERC_INTERFACE_MIR` 获取 `NevercMIRAPI`，查询
`NEVERC_INTERFACE_MIR_PASS` 获取 `NevercMIRPassAPI`。在使用任何函数指针之前先检查
返回的表大小，并忽略更新版本宿主追加的字段。

schema 摘要标识当前使用的稳定 ID 到宿主的确切映射。`GetEntityInfo`、
`GetOperandKindInfo`、`GetGenericOpcodeInfo` 和 `GetMachinePropertyInfo` 会给出规范
名称，以及某个操作是否需要目标 schema。

## 稳定模型

不透明句柄代表：

- machine function 和 basic block；
- machine instruction 和 operand；
- 变更事务；
- 分析结果；
- 常量池条目、栈帧对象、跳转表、内存操作数和目标引用。

句柄属于某一个代码生成任务。被擦除的实体、被回滚的实体，以及因变更而失效的分析结果
都会变为过期句柄。

通用 schema 覆盖目标无关的 opcode、操作数种类、machine property、低级类型、指令
标志、寄存器分配、栈帧对象、常量、跳转表、内存指针形式和原子序。目标相关的 opcode
需要显式协商的目标 schema。

## 读取 MIR

`NevercMIRAPI` 支持：

- machine function 属性与基本块遍历；
- 前驱、后继、live-in、指令和操作数枚举；
- 指令 opcode 与标志查询；
- 所有公开的 machine operand 形式；
- 虚拟寄存器与物理寄存器信息；
- 栈帧、常量池、跳转表和内存操作数状态。

请使用「计数/查询」成对调用与有界输出缓冲区。除非另有说明，返回的视图仅在当前回调
期间被借用。

## 事务式变更

MIR 的改动在一个变更租约（mutation lease）下进行：

1. 对某个 machine function 调用 `BeginMutation`。
2. 创建、移动或擦除基本块与指令。
3. 追加或更新操作数和 CFG 边。
4. 携带必需的证明来应用 machine property 变更。
5. `CommitMutation` 或 `AbortMutation`。

提交会执行结构性预检和 Machine IR 验证。非法的操作数、CFG、通用 opcode 用法或属性
声明都会被原子回滚。中止则会恢复基本块顺序、指令、操作数、CFG 边和 machine
property。

属性变更使用 `NevercMIRPropertyProof`。证明要么让一个前提已不再成立的属性失效，要么
在建立该属性之前请求一次结构性检查。

## Pass 与阶段

`NevercMIRPassDescriptor.Level` 支持 MachineModule、MachineFunction 和
MachineBasicBlock 适配器。稳定的挂钩点是：

- 指令选择之后；
- legalization 之后；
- 调度器之前／之后；
- 寄存器分配之前／之后；
- prologue/epilogue 之后；
- pre-emit；
- 最后的插件槽位。

function pass 可能在并行的代码生成分区中运行。模块级 pass 在串行化的流水线屏障处
执行。插件声明的并发性与可重入性依然适用。

每条代码生成流水线都会在最后的插件槽位之后，以宿主拥有的 `MachineVerifier` 收尾。
它是密封 gate，插件无法禁用。

## 分析

分析表暴露活跃变量、活跃区间、槽索引、支配树、循环信息和寄存器压力。可用性取决于所
选择的挂钩点，因为某些 LLVM 分析在其原生流水线阶段之前或之后并不存在。

在 pass 描述符中声明所需的和被保留的分析。一次成功提交的变更会使受影响的结果句柄
失效。变更之后再声明 preserve-all 会被拒绝。

## 最小示例

`pluginsdk/examples/MachinePass.c` 在稳定的 pre-emit 挂钩点注册一个只读的
machine-function pass。

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

请使用 CMake 为当前平台生成的模块后缀。

## 安全要求

- 不要在回调之后继续持有任务句柄、MIR 句柄或借用的视图。
- 不要伪造句柄值或 LLVM opcode 数值。
- 不要在租约之外进行变更。
- 初始化表头和保留存储。
- 跨 C 边界返回状态；绝不让 C++ 异常穿过它。

规范性声明与覆盖率证据见 `PluginMIR.h`、`MIRSchema.json`、`PluginPhaseSchema.h`
和 `coverage.json`。
