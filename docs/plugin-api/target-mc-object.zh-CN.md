**语言**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# Target、MC、汇编与目标文件插件

NeverC 首个发布版的插件 ABI 允许一个 C 插件描述目标平台、替换代码生成路线、观察机器
码发射、解析或打印汇编，以及读写目标文件。公开边界是纯 C ABI：插件不得跨界传递 LLVM
的 C++ 对象、STL 类型、异常，或生命周期未由某个 API 表明确声明的宿主指针。

## 兼容性层级

与目标无关的描述符、阶段 ID、产物 ID、MC 容器、ObjectGraph 容器、输出事务和回调契约
属于首发版的 STABLE ABI。目标相关的 opcode、寄存器、操作数、fixup、重定位和调用约定
schema 属于 LOCKSTEP。插件在消费 LOCKSTEP 值之前必须比对目标 schema ID 和摘要。
NeverC 会在调用 Provider 之前拒绝不匹配的 schema。

## 注册目标与代码生成路线

在注册期间查询 `NevercTargetAPI`，注册一条或多条 `NevercTargetDescriptor` 记录，并
挂上 target-machine 描述符和代码生成边。路线由规范目标键来选择：目标 ID、triple、
CPU、特性、ABI、重定位模型、代码模型、目标文件格式和 schema 摘要。

细粒度路线使用 `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`。粗粒度的边可以替换
整条 `IR -> ObjectImage` 路线。粗粒度输出仍要通过宿主强制的产物验证器和事务式输出
提交；Provider 无法绕过其中任何一道关卡。

## 构建与观察 MC

`NevercMCAPI` 负责任务局部的 `MCUnit` 变更。开启一次变更，创建 section、fragment、
符号、表达式、指令和操作数，然后提交或放弃。句柄限定在任务作用域内，并做代数检查。

与目标无关的发射流暴露有序事件，涵盖 section 切换、标签、指令、对齐、符号属性、CFI、
调试位置和数据。`neverc.mc.emission.pre_instruction` 可替换，其余事件阶段是只读的
观察点。参见 `pluginsdk/examples/MCObserverPlugin.c`。

编码、解码和布局 Provider 基于相同的目标键和 schema 摘要工作。布局负责 relaxation
并给出证明摘要。布局之后的任何变更都会使该证明失效，并在写目标文件前强制重新布局。

## 替换汇编语法

汇编解析器 Provider 消费源字节并发布一个 `MCUnit`。汇编打印器消费 `MCUnit`，且只能
通过给定的输出事务写出。经过预处理的汇编（`.S`）先走正常的前端预处理器再进入解析器
Provider；纯汇编（`.s`）直接进入解析器。

Provider 先暂存输出。解析／打印验证和宿主提交关卡都在字节可见之前运行，因此失败不会
留下任何部分输出。

## 读取、改写与写出目标文件

`NevercObjectAPI` 把可重定位文件表示为规范化的 ObjectGraph：section、符号、重定位、
group/COMDAT、导入／导出、TLS 元数据、展开记录和调试记录。内建适配器覆盖 ELF、COFF
和 Mach-O，插件还可以注册更多格式。

目标文件流水线是：

1. 探测并把字节读入 ObjectGraph；
2. 运行 `object.pre_write` 图拦截器；
3. 布局并运行 `object.post_layout`（变更之后重新布局）；
4. 写出有界的候选镜像；
5. 运行 `object.post_write` 二进制拦截器；
6. 执行密封的最终验证器和原子的宿主提交。

观察者拿到的是只读桥接。从观察者发起的变更会以
`NEVERC_STATUS_POLICY_VIOLATION` 被拒绝。写出器和 post-write 拦截器只能访问有界的
事务式构建器；溢出、回调失败或验证失败都会中止暂存。参见
`pluginsdk/examples/ObjectRewritePlugin.c`。

## 并发与失败规则

- 把可变状态放在宿主提供的 process/session/task 状态里。
- 回调返回后不要缓存任务句柄或借用的视图。
- 拦截器续延最多调用一次，且必须在回调线程上调用。
- 返回原始的 `NevercStatus`；不要发布部分产物。
- 声明最窄且真实的并发与可重入模式。

可执行的覆盖率契约是 `docs/plugin-api/coverage.json`。它把每个稳定阶段映射到正向、
负向、替换、只读观察者和密封 gate 的测试。
