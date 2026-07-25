**语言**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

# DynCode 插件

`-fdyncode` 把一个翻译单元编译成扁平的、位置无关的镜像（`.bin`）：其代码零重定位、无数据节。它支持 macOS / Linux / Android / Windows 上的 arm64/x86_64，可选 user 或 kernel 执行级。插件通过与其他领域相同的纯 C ABI，观察、拦截或替换把 C 变成该镜像的各个 typed phase：不跨边界传递 LLVM C++ 对象、STL 类型、异常，或生命周期未由 API 表声明的宿主指针。

## 接口

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| 接口 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | 读取 request、image、report 以及 section/symbol/relocation/external 映射 |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`、`RegisterImportProvider`、`RegisterExtractor`、`RegisterCharsetEncoder`、`RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`、`GetRequest`、`GetImage`、`GetReport` |

三者在 major 1 上都是 `NEVERC_INTERFACE_STABLE`。在 phase 回调内部，
`NevercDynCodePhaseAPI` 是入口——它把帧转换成另一张表所消费的句柄：

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

四类映射——section 映射、symbol 映射、重定位和外部引用——都用同一组
first/next/info 三元组遍历，例如 `GetFirstRelocation`、`GetNextRelocation`、
`GetRelocationInfo`。插件借此读取提取阶段的决策，而不必去解析 report JSON。

## DynCode 是编译产物，不是 `main()` 后处理

`-fdyncode` 是驱动 DAG 里的正常 Action/Job。编译 job 发布一个已验证的内存 `ObjectGraph`；一个 `-dyncode-extract` job 消费该图并写出用户的 `-o` 镜像。`-###`、phase 打印和 job 图都能看到该提取 job，因此插件无需靠还原被改写的 argv 来发现该模式。冻结后的 request 以 task-local 方式与 in-process codegen 共享；不存在 `getCurrentDynCodeOptions()`、进程级 mode flag，也没有临时对象往返。

恰好一个翻译单元降级为一个镜像。多输入、`-c/-S/-E` 以及不支持的 triple 都会以稳定诊断在前期被拒绝。

## 兼容性层级

Phase ID、artifact ID、request/report/image 容器以及回调契约是 STABLE 首版 ABI。目标相关的重定位种类与对象格式的 section/symbol schema 是 LOCKSTEP：消费前先比较目标 schema ID 与 digest。schema 不匹配时，NeverC 会在调用 provider 之前拒绝。

## 冻结的 request

Job 开始时，驱动把命令行规范化为不可变的 `DynCodeRequest` 并冻结。子任务借用快照，绝不修改它。request 携带目标 key 与对象格式、执行级（user/kernel）、entry policy（显式符号、默认候选列表、entry-at-zero 要求）、PIC/section policy、外部引用 policy、bad-byte 集合/profile 与 rewrite 开关、charset provider ID，以及最大长度、对齐和 pad byte。

## 固定的 typed phase 图

DynCode 是一张固定的 34 个 phase 的图。其中 30 个普通 transition 为 `OBSERVABLE | INTERCEPTABLE | REPLACEABLE`；4 个为 `OBSERVABLE | SEALED_HOST_GATE`。四个 sealed gate 分别是 IR 最终验证、MIR 最终验证、镜像验证和提交。插件可以观察任一 phase、用 interceptor 包裹一个可替换 transition，或直接替换其 provider；但它永远不能替换、跳过或绕过 sealed gate，也不能用"未调用回调"表达被禁用的 transform——被禁用的 transform 会执行显式 no-op provider，其等价产物仍由宿主 verifier 证明。

各 phase 顺序为：

1. request 冻结；
2. IR transform ——prepare、间接分支降级、内存 intrinsic 降级（pre 与 post-heap）、字符串运行时降级、heap arena、三个 `compiler_rt` 位置（pre/post/final）、syscall/PEB/kernel import 降级、两个 `data_to_text` 位置（pre/post）、inline 优化、string finalize、stackify、all-`blr`，以及 sealed IR 最终验证；
3. MIR prepare transform 与 sealed MIR 最终验证；
4. object import——把已验证的 `ObjectGraph` 绑定到当前 task；
5. 提取——plan、layout、relocate，并构建候选镜像；
6. 有界 binary phase——post-extract、bad-byte rewrite、charset encode、size/对齐/padding，以及 pre-verify；
7. sealed 镜像验证；
8. sealed 提交。

ID、policy、稳定层级与 gate 的规范源是 `neverc/include/neverc/Plugin/Schema/PhaseSchema.json`；可执行的覆盖契约是 `docs/plugin-api/coverage.json`。

## 内建 transform 也是 provider

每个内建 IR/MIR pass 都被包装为 typed provider；LLVM pass 对象绝不跨 C ABI 暴露。替换一个 phase 意味着内建 provider 确实不运行——通过的测试证明的是行为或 trace，而不仅仅是注册成功。`mem_intrin`、`compiler_rt` 和 `data_to_text` 出现在不止一个位置；每个位置是独立的 phase ID、带各自的 proof，因此重跑是幂等的，且不依赖隐藏的 pass 状态。

## ObjectGraph 是唯一的普通对象输入

提取恰好消费一个由目标 codegen route 产生的已验证 `ObjectGraph`。`dyncode.object.import` 绑定该图并检查目标 key 与 provenance；它不从磁盘二次读取字节，也不做第二次对象解析。只要能被读成 `ObjectGraph` 并有匹配的 relocation 与 target provider，自定义对象格式即可进入 DynCode。多对象与 LTO graph-set 在 freeze 阶段以稳定的 `CAPABILITY_UNAVAILABLE` 被拒绝。

## 外部引用与 import 降级

request 的 allowed-external 集合只表示"允许某 provider 处理"，绝不允许未解析的重定位残留进扁平镜像。每个外部引用最终必须是以下之一：在 IR/MIR 中消除、解析为镜像内符号、转换为已声明且经 verifier 接受的 runtime resolver 契约，或直接失败。syscall stub、PEB import、kernel import 是三个内建 `ImportProvider`；每个都声明自己的 target/level/符号 matcher 以及产生的 ABI 契约。插件可以新增 `ImportProvider`，但必须返回替换 provenance、entry-ABI 变化、resolver 参数和残余引用。

## 镜像、报告与有界字节修改

提取产生一个 `DynCodeImage` 和一个 `DynCodeReport`。镜像是一个有界字节 builder，加上 entry offset/符号、源 section 与源符号的输出映射、重定位 disposition，以及外部/runtime 契约记录。每次字节修改都经过 builder 的受检 read/write/insert/append/resize API；没有 `uint8_t **`。一次修改会更新镜像 generation，并使与被改范围相交的 relocation/PIC/entry proof 失效。

报告是不可变、确定性的审计产物：request/route/input/output digest、逐 phase 的 provider journal、选中/拒绝的 section 及原因、entry 选择、已修补/拒绝/runtime 契约的重定位、剩余外部引用、size/对齐/padding、bad-byte 扫描，以及 verifier checklist。`-fdyncode-report=<path>` 写出其规范 JSON；verbose 诊断从同一份报告渲染，而不是维护第二套计数。

bad-byte rewrite 链按冻结的拓扑顺序执行，每步返回一条变更记录。charset encoder 按精确的稳定 ID 选择，返回 decoder stub、编码后的 payload、entry 更新和 target proof；未知或冲突的 ID 是硬错误。禁用 rewrite 会选择一个显式 no-op 步骤——最终审计仍会运行。

## 最终 verifier 与 post-finalize 时序

所有可写 phase 都在 sealed 最终 verifier 之前完成。verifier 检查：没有未处理的外部重定位/引用残留；不存在被禁止的 data/TLS/unwind/debug/metadata section；entry 存在、对齐正确、（在要求时）位于 offset 0；每个 relocation site 都落在范围内、且其 PIC proof 与当前镜像字节匹配；section/symbol 映射不重叠；length/对齐/padding 规则成立；最终字节（含 decoder、header、padding）不含任何被禁止的字节。任一失败都返回结构化诊断并丢弃整个输出 bundle。

审计之后没有可写 hook。若某个字节 transform 触及可执行范围，冻结的 route 必须提供一个匹配的 binary verifier 能力，由宿主调用它在最终不可变镜像上重新签发 PIC proof。

## 驱动选项

`-fdyncode` 启用该模式。`-fdyncode-entry=` 选择 entry 符号。`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` 设置被禁字节，`-fdyncode-bad-byte-rewrite`（默认开）选择 rewrite 链，`-fdyncode-charset=` 选择已注册的 encoder。`-fdyncode-max-length=`、`-fdyncode-align=` 与 `-fdyncode-pad=` 约束最终大小。`-fdyncode-keep-obj=` 旁路保存中间可重定位对象，`-fdyncode-report=` 写出审计报告。`-mdyncode-context=user|kernel` 选择执行级。

## 并发与失败规则

- 把可变状态放进宿主提供的 process/session/task 作用域；绝不使用 current-plugin 或 current-options 单例。
- 回调返回后不要缓存 task handle 或借用视图。
- interceptor continuation 至多调用一次，且在回调线程上。
- 返回原始 `NevercStatus`；声明为 `REPLACE` 后失败不会静默回退到内建 provider。
- 声明最窄且真实的并发与重入模式。

只读 phase 追踪见 `pluginsdk/examples/DynCodeTracePlugin.c`，charset encoder 见 `pluginsdk/examples/DynCodeEncoderPlugin.c`。
