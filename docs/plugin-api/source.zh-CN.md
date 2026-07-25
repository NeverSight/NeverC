**语言**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# Source 与 I/O 插件 API

首个公开插件 ABI 通过 `PluginSource.h` 暴露源输入、虚拟文件、依赖关系和编译器
输出。所有路径都是规范化的 VFS 路径，所有句柄都限定在当前 `TranslationUnit`
任务的作用域内。

## Source 阶段

稳定的 source 流水线是：

1. `neverc.source.resolve_input` 校验并规范化请求的输入。
2. `neverc.source.open` 通过宿主/插件组合而成的 VFS 打开它。
3. `neverc.source.after_open` 为已验证的 `SourceUnit` 发布一个只读事件。

`resolve_input` 可观察、可拦截；`open` 还可替换。宿主会在把任何替换结果发布为
`SourceUnit` 之前对其进行验证。插件不能替换 `after_open`。

## VFS Provider

在插件注册期间查询 `NevercIOAPI` 并调用 `RegisterVFSProvider`。Provider 首先回答
`MatchesPath`，然后实现它所负责的操作。返回
`NEVERC_VFS_RESULT_NOT_HANDLED` 会委派给下一个 Provider；返回 `HANDLED` 则意味着
格式错误的状态或内容会成为硬错误，而不是悄悄退回默认路径。

Provider 返回的缓冲区只在该回调期间被借用。NeverC 会把接受的字节复制到任务拥有的
存储中。Provider 必须声明其结果是否确定性、是否可缓存。

可构建的
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
示例在不绕过宿主 VFS 的前提下提供了一个内存中的头文件。

## 输出 sink 与依赖

文件输出和内存输出使用同一套事务式 sink：

- 写入候选产物；
- 调用 finish 使其具备被验证的资格；
- 让密封的宿主 gate 验证它；
- 任务成功时原子提交，出现任何错误或取消时中止。

插件绝不通过直接写入目标路径来发布结果。无法回滚的流式目标会拒绝那些需要原子候选
产物的变换。依赖记录使用规范化的 VFS 标识，因此原生文件和插件提供的文件具有相同的
来源与缓存语义。

## 安全规则

- 不要在回调结束后继续持有 source、file、buffer、sink 或 task 句柄。
- 把 `NevercStringView` 和 `NevercByteView` 当作带长度的视图处理。
- 当数据需要存活到回调之外时，使用宿主分配器。
- 不要在 VFS 契约背后使用宿主文件系统 API。
- 在执行昂贵的 Provider 工作之前检查取消状态。
