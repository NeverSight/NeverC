# Target、MC、汇编与目标文件插件

NeverC 首发版插件 ABI 允许纯 C 插件描述目标、替换代码生成路径、观察机器码
发射、解析或打印汇编，以及读写目标文件。ABI 边界不能传递 LLVM C++ 对象、
STL 类型、异常，或生命周期未由 API 明确规定的宿主指针。

## 兼容性层级

目标无关的描述符、阶段 ID、制品 ID、MC 容器、ObjectGraph、输出事务和回调
契约属于首发版 STABLE ABI。目标相关的操作码、寄存器、操作数、fixup、重定位
和调用约定 schema 属于 LOCKSTEP。插件读取 LOCKSTEP 值前必须比较目标 schema
ID 与摘要；不匹配时，NeverC 会在调用 Provider 前拒绝任务。

## 注册目标与代码生成路径

在注册期间查询 `NevercTargetAPI`，注册一个或多个
`NevercTargetDescriptor`，并提供目标机器描述符与代码生成边。路由依据规范化
TargetKey 选择，其中包含目标 ID、triple、CPU、features、ABI、重定位模型、
代码模型、目标文件格式和 schema 摘要。

细粒度路径为 `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`。粗粒度边可以完整
替换 `IR -> ObjectImage`，但产物仍必须经过宿主强制验证器和事务提交；插件无法
绕过这两个密封关口。

## 构建与观察 MC

`NevercMCAPI` 管理任务局部的 `MCUnit` 变更。插件先开始 mutation，再创建
section、fragment、symbol、expression、instruction 和 operand，最后提交或
放弃。所有 handle 都受任务作用域和 generation 检查约束。

目标无关的发射事件覆盖 section 切换、label、instruction、alignment、符号
属性、CFI、调试位置和数据。`neverc.mc.emission.pre_instruction` 可替换，其余
事件为只读观察点。示例见 `pluginsdk/examples/MCObserverPlugin.c`。

编码、解码和布局 Provider 使用相同的 TargetKey 与 schema 摘要。布局负责
relaxation 并产生证明摘要；布局后的任何图变更都会使证明失效，写目标文件前
必须重新布局。

## 替换汇编语法

汇编 Parser Provider 读取源码字节并发布 `MCUnit`；Printer Provider 读取
`MCUnit`，且只能通过传入的输出事务写入。预处理汇编（`.S`）先经过正常前端
预处理器，普通汇编（`.s`）则直接进入 Parser。

Provider 只能先暂存输出。解析/打印验证及宿主提交关口全部成功后，字节才对外
可见；失败不会遗留部分文件。

## 读取、改写与写出目标文件

`NevercObjectAPI` 将可重定位目标文件规范化为 ObjectGraph：section、symbol、
relocation、group/COMDAT、import/export、TLS 元数据、unwind 和 debug 记录。
内建适配器支持 ELF、COFF 与 Mach-O，插件也可以注册额外格式。

目标文件流水线依次执行：

1. probe 并读取字节为 ObjectGraph；
2. 执行 `object.pre_write` 图拦截器；
3. 布局并执行 `object.post_layout`，变更后重新布局；
4. 写入有边界的候选镜像；
5. 执行 `object.post_write` 二进制拦截器；
6. 执行密封的最终验证与宿主原子提交。

Observer 获得只读 bridge；从 Observer 发起 mutation 会返回
`NEVERC_STATUS_POLICY_VIOLATION`。Writer 与 post-write 拦截器只能访问有边界的
事务 builder；越界、回调失败或验证失败都会中止暂存。示例见
`pluginsdk/examples/ObjectRewritePlugin.c`。

## 并发与失败规则

- 可变状态必须存放在宿主提供的 process/session/task state 中。
- 回调返回后不得缓存任务 handle 或借用 view。
- 拦截器 continuation 最多调用一次，且必须在回调线程调用。
- 原样返回 `NevercStatus`，不得发布部分产物。
- 声明最窄且真实的并发与可重入模式。

可执行覆盖契约位于 `docs/plugin-api/coverage.json`，它把每个稳定阶段映射到
正向、负向、替换、只读观察器和密封关口测试。
