**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC 插件 ABI

NeverC 的首个公开插件 ABI 是纯 C、基于阶段（phase）的接口。插件是一个共享模块，
只导出一个函数，协商版本化的能力表，并在显式的 Process、Session、Task 作用域中
运行。它不包含任何 LLVM 头文件，不链接编译器，也不在边界上传递任何 C++ 类型。

未发布的原型 API 及其 `nevercGetPluginInfo` 入口已被删除。原型二进制会收到迁移
诊断；请用公开头文件重新编译其源码。完整的旧→新映射见
[从原型 API 迁移](migration-from-prototype.zh-CN.md)。

## 文档入口

- [Source 与 I/O API](source.zh-CN.md)
- [预处理器 API](prep.zh-CN.md)
- [AST 与语义 API](ast-sema.zh-CN.md)
- [IR API](ir.zh-CN.md)
- [MIR API](mir.zh-CN.md)
- [Target、MC、汇编与目标文件 API](target-mc-object.zh-CN.md)
- [DynCode API](dyncode.zh-CN.md)
- [自定义调用约定](custom-callconv/README.zh-CN.md)
- [从原型 API 迁移](migration-from-prototype.zh-CN.md)
- [阶段覆盖证据](coverage.json)

## 执行模型

宿主通过三层嵌套作用域驱动插件。每层作用域都会交给插件一个不透明的状态指针，
由插件自行分配并拥有——因此一个正确编写的插件不需要任何全局可变状态。


| 作用域     | 回调                                  | 含义                            |
| ------- | ----------------------------------- | ----------------------------- |
| Process | `ProcessBegin`、`Register`、`Destroy` | 一个编译器进程。在此查询接口并注册能力。          |
| Session | `SessionBegin`、`SessionEnd`         | 一次驱动调用。                       |
| Task    | `TaskBegin`、`TaskEnd`               | 一个工作单元，由 `NevercTaskKind` 标识。 |


Task 种类有 `INVOCATION`、`TRANSLATION_UNIT`、`LTO`、`LINK`、`CODEGEN`、
`OBJECT` 和 `DYNCODE`。

宿主先调用 `ProcessBegin`，然后恰好调用一次 `Register`。注册是唯一可以添加选项、
观察者、拦截器和 Provider 的地方；之后阶段图即被冻结。

## 阶段（Phase）

阶段是一个具名、带版本的转换：从输入产物到输出产物。NeverC 内建 **130 个阶段**，
覆盖 driver、source、预处理器、语法、语义、IR、codegen、MIR、MC、汇编、目标文件、
链接和 dyncode 各域，另有 8 个为插件自定义阶段保留的扩展 ID 族。

每个阶段声明自己的 policy，插件只能以该 policy 允许的方式接入：


| Policy 标志                           | 插件可以做什么                        |
| ----------------------------------- | ------------------------------ |
| `NEVERC_PHASE_OBSERVABLE`           | 注册观察者，只读地接收通知。                 |
| `NEVERC_PHASE_INTERCEPTABLE`        | 包裹该阶段，自行决定是否调用链上其余部分。          |
| `NEVERC_PHASE_REPLACEABLE`          | 注册 Provider，由插件自己产出输出。         |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 在提供 proof 句柄的前提下跳过该转换。         |
| `NEVERC_PHASE_SEALED_HOST_GATE`     | 什么都不能做。验证器与提交由宿主独占，不可替换、拦截或跳过。 |


观察者在阶段声明的时机被投递：`NEVERC_OBSERVER_BEFORE`、
`NEVERC_OBSERVER_AFTER` 和 `NEVERC_OBSERVER_AFTER_COMMIT`。

拦截器会收到一个 `NevercPhaseContinuation`。它必须**最多调用一次**
`InvokeNext`，且在回调线程上调用，然后在 `NevercPhaseResult.Action` 中报告
`NEVERC_PHASE_CONTINUE`、`NEVERC_PHASE_REPLACE` 或 `NEVERC_PHASE_SKIP` 之一。

`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` 是阶段 ID、policy、
稳定性层级和验证器 gate 的规范事实源。生成的 `PluginPhaseSchema.inc` 把它们
暴露为编译期常量，例如 `NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`。

## 一个完整的最小插件

这是 `pluginsdk/templates/minimal/Plugin.c`。它能加载、协商 ABI、不注册任何东西、
干净卸载——复制该目录即可在此基础上生长。

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* 在这里注册选项、观察者、拦截器或 Provider。 */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` 是调用方拥有的缓冲区。进入时 `Header.StructSize` 表示可写容量；
插件最多写入这么多字节，并回报自己实际产出的大小。

## 接口协商

能力表按 128 位接口 ID 获取，而不是按符号。请求你编译时所用的 major，以及你能
接受的最低 minor：

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

把 `TableSize` 与你要调用的最后一个函数的偏移量做比较，正是让这套 ABI 可扩展的
规则：新版宿主在尾部追加字段，而旧插件依然可用，因为它从不读取自己验证过的前缀
之外的内容。`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` 宏对你收到的结构体
施加同样的检查。

公开接口及其头文件：


| 接口                                                                                              | 表                                       | 头文件               |
| ----------------------------------------------------------------------------------------------- | --------------------------------------- | ----------------- |
| `NEVERC_INTERFACE_CORE`                                                                         | `NevercCoreAPI`                         | `PluginCore.h`    |
| `NEVERC_INTERFACE_DRIVER`                                                                       | `NevercDriverAPI`                       | `PluginDriver.h`  |
| `NEVERC_INTERFACE_IO`、`..._SOURCE_LOCATION`                                                     | `NevercIOAPI`、`NevercSourceLocationAPI` | `PluginSource.h`  |
| `NEVERC_INTERFACE_PREP`                                                                         | `NevercPrepAPI`                         | `PluginPrep.h`    |
| `NEVERC_INTERFACE_AST`、`..._PARSER`                                                             | `NevercASTAPI`、`NevercParserAPI`        | `PluginAST.h`     |
| `NEVERC_INTERFACE_SEMA`                                                                         | `NevercSemaAPI`                         | `PluginSema.h`    |
| `NEVERC_INTERFACE_IR_CORE`、`..._BUILDER`、`..._ANALYSIS`、`..._PASS`、`..._GEN`、`..._OPTIMIZATION` | IR 各表                                   | `PluginIR.h`      |
| `NEVERC_INTERFACE_TARGET`、`..._TARGET_ABI`、`..._CALLING_CONVENTION`                             | Target 各表                               | `PluginTarget.h`  |
| `NEVERC_INTERFACE_MIR`、`..._MIR_ANALYSIS`、`..._MIR_PASS`、`..._MIR_PROVIDER`                     | MIR 各表                                  | `PluginMIR.h`     |
| `NEVERC_INTERFACE_MC`、`..._MC_EMISSION`、`..._MC_PROVIDER`、`..._ASSEMBLY_PROVIDER`               | MC 各表                                   | `PluginMC.h`      |
| `NEVERC_INTERFACE_OBJECT`、`..._OBJECT_FORMAT`、`..._OBJECT_PHASE`                                | Object 各表                               | `PluginObject.h`  |
| `NEVERC_INTERFACE_LINK`、`..._LINK_REGISTRAR`、`..._LINK_PHASE`                                   | Link 各表                                 | `PluginLink.h`    |
| `NEVERC_INTERFACE_LTO`、`..._LTO_REGISTRAR`                                                      | LTO 各表                                  | `PluginLTO.h`     |
| `NEVERC_INTERFACE_DYNCODE`、`..._DYNCODE_REGISTRAR`、`..._DYNCODE_PHASE`                          | DynCode 各表                              | `PluginDynCode.h` |


接口要么是 STABLE（新版宿主只能追加），要么是 LOCKSTEP（目标相关的 schema，必须
完全匹配）。消费 LOCKSTEP 值之前必须比较 schema digest。

## 构建

包含聚合头，或只包含你用到的域：

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

用 NeverC 自身构建共享模块：

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

或用 CMake 对接已安装的 SDK：

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

或用 pkg-config：

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

按宿主平台选用 `.so`、`.dylib` 或 `.dll`。SDK 不链接 LLVM，也不链接 NeverC
运行时——`NevercPluginSDK::headers` 是纯头文件目标。

## 加载与配置

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```


| 选项                                       | 形式  | 用途                  |
| ---------------------------------------- | --- | ------------------- |
| `-fplugin=<path>`                        | 可重复 | 加载插件共享模块。           |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 可重复 | 向已注册的插件选项传递带命名空间的值。 |
| `-fplugin-provider=<phase>:<plugin-id>`  | 可重复 | 选择由哪个插件提供某个可替换阶段。   |


只有在恰好激活一个插件时，才可以省略 `<plugin-id>:` 限定符。插件用
`RegisterOption` 注册的选项也可以直接按其声明的拼写使用，支持 flag、joined、
separate 和多参数形式。没有 `-fplugin=` 却给出插件参数或 Provider 选择是硬错误，
而不是静默忽略。

## ABI 规则

- 通过 `QueryInterface` 查询能力表；要求 major 匹配，并在触碰字段前检查
`StructSize`。
- 初始化每个公开结构体的 `Header` 和保留存储。先将结构体清零，再设置
`StructSize`、`Major`、`Minor` 和 `Flags`。
- 把句柄和借用视图当作有作用域的不透明值。绝不在回调之外保留任务作用域句柄，
绝不在另一个 session 或 task 中使用它，也绝不自行伪造句柄值。
- 每个回调都返回 `NevercStatus`。不要让 C++ 异常或宿主拥有的指针越过 C 边界。
- 声明**最窄且真实**的 `NevercConcurrencyModel`（`SESSION_SERIAL`、
`THREAD_SAFE`、`PROCESS_SERIAL`）与 `NevercReentrancyModel`（`NONE`、
`ALLOWED`）。
- IR、MIR、AST、图和产物的修改一律走事务式宿主 API：开启 mutation，暂存改动，
然后 commit 或 abort。commit 会验证并原子发布；失败的 commit 保持先前状态不变。
- 把可变状态放在宿主提供的 process/session/task 状态里。全局可变状态由
`utils/plugin-api/check-global-state.py` 检查。

新函数只会追加到各自独立版本化的能力表尾部。在首个 ABI major
（`NEVERC_PLUGIN_ABI_MAJOR` = 1）内，表的稳定前缀不会改变。

## 状态与诊断

`NevercStatus` 携带 `Code`、`Flags` 和一个 `Detail` 字。常见状态码：


| 状态码                                                                           | 含义                   |
| ----------------------------------------------------------------------------- | -------------------- |
| `NEVERC_STATUS_OK`                                                            | 成功。                  |
| `NEVERC_STATUS_INVALID_ARGUMENT`                                              | 缺少必需指针或值，或格式非法。      |
| `NEVERC_STATUS_ABI_MISMATCH`                                                  | 协商到的表太小，或 major 不一致。 |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE`                  | 宿主不提供所请求的能力。         |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | 句柄在其有效范围之外被使用。       |
| `NEVERC_STATUS_POLICY_VIOLATION`                                              | 该阶段的 policy 不允许此操作。  |
| `NEVERC_STATUS_VERIFICATION_FAILED`                                           | 宿主的密封验证器拒绝了产物。       |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED`                     | 协作式取消或资源限制。          |


标志位（`RECOVERABLE`、`OUTPUT_ALREADY_COMMITTED`、`OUTPUT_MAY_BE_PARTIAL`、
`OUTPUT_RECOVERY_REQUIRED`、`DURABILITY_UNCONFIRMED`）描述输出发生了什么，
这正是构建系统判断"重试是否安全"所需要的信息。

用 `NevercCoreAPI.EmitDiagnostic` 配合 `NevercDiagnosticDescriptor` 报告问题，
其中携带严重级别、代码、插件 ID、阶段 ID、消息、注记、源位置、范围和 fix-it。
在执行昂贵工作前调用 `CheckCancelled`。

## 示例

构建全部示例：

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

每个示例都会被编译两次——一次用配置的宿主 C 编译器，一次用刚构建出的 NeverC——
从而从两侧共同证明 ABI 的正确性。模块产出在
`build-neverc/neverc/pluginsdk/examples/host/`。


| 示例                       | CMake target                            | 演示内容                     |
| ------------------------ | --------------------------------------- | ------------------------ |
| `DriverTracePlugin.c`    | `neverc-plugin-example-driver-trace`    | 选项注册、阶段观察、job 拦截         |
| `VirtualHeaderPlugin.c`  | `neverc-plugin-example-virtual-header`  | 提供内存头文件的 VFS provider    |
| `ASTRewritePlugin.c`     | `neverc-plugin-example-ast-rewrite`     | 解析器拦截与原子 AST 变更          |
| `ExamplePlugin.c`        | `neverc-plugin-example-ir-overview`     | 模块级 IR pass，用值游标遍历函数列表   |
| `FunctionPass.c`         | `neverc-plugin-example-function-pass`   | 稳定的 IR function pass     |
| `MachinePass.c`          | `neverc-plugin-example-machine-pass`    | pre-emit 钩子上的稳定 MIR pass |
| `MCObserverPlugin.c`     | `neverc-plugin-example-mc-observer`     | 只读的 MC 发射事件              |
| `ObjectRewritePlugin.c`  | `neverc-plugin-example-object-rewrite`  | 事务式 ObjectGraph 改写       |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | 数据驱动的调用约定                |
| `DynCodeTracePlugin.c`   | `neverc-plugin-example-dyncode-trace`   | 观察 dyncode 流水线           |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | 拦截 dyncode 字符集编码         |
| `CrtShimPlugin.c`        | `neverc-plugin-example-crt-shim`        | 零 CRT 依赖的插件              |
| `BenchPlugin.c`          | `neverc-plugin-example-abi-bench`       | ABI 调用吞吐量微基准             |


加载其中之一：

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 规范事实源


| 文件                                                     | 保证的内容                                   |
| ------------------------------------------------------ | --------------------------------------- |
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | 阶段 ID、policy、稳定性、验证器 gate               |
| `pluginsdk/manifest/plugin.json`                       | ABI 版本、接口 ID/版本/稳定性、schema digest、支持的目标 |
| `pluginsdk/abi/plugin.json`                            | 每个公开结构体在各宿主 ABI key 下实测的大小、对齐与字段偏移      |
| `docs/plugin-api/coverage.json`                        | 把每个稳定阶段映射到正例、反例、替换、观察者与密封 gate 测试       |


因此 SDK 可以对宿主做机器化校验，插件构建也可以针对它将要载入的 ABI key
断言自己的结构体布局。