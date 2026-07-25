**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC 插件 ABI

一个 NeverC 插件是这样一个共享模块：它只导出一个函数，按 128 位接口 ID 协商带版
本的能力表，并把自己挂到一张冻结的具名编译器阶段图上。整套接口是纯 C11。插件永
远不会包含 LLVM 头文件，不会链接编译器，也不会让任何 C++ 类型越过边界。

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

这个声明在 `PluginCore.h` 里，它就是全部的链接契约。其余的一切——读 IR、改写目
标文件图、替换优化流水线——都要通过你按 ID 向宿主索取的表来完成。

## 文档入口

| 指南 | 覆盖内容 |
|---|---|
| [驱动 API](driver.zh-CN.md) | 命令行、工具链选择、action 图、job 图 |
| [源与 I/O API](source.zh-CN.md) | VFS Provider、源位置、缓冲区、输出 sink、依赖 |
| [预处理器 API](prep.zh-CN.md) | token、宏、pragma、include、特性查询、39 种事件 |
| [AST 与语义 API](ast-sema.zh-CN.md) | 解析器扩展、AST 修改、名字查找、类型、常量 |
| [IR API](ir.zh-CN.md) | LLVM IR 读取、事务式构造、分析、pass、Provider |
| [MIR API](mir.zh-CN.md) | 机器函数、寄存器、栈帧、MIR pass 与分析 |
| [Target、MC、汇编、目标文件](target-mc-object.zh-CN.md) | 目标注册、调用约定、MC 编码、目标文件图 |
| [链接与 LTO API](link-lto.zh-CN.md) | 链接图、符号决议、GC/ICF、链接器与 LTO Provider |
| [DynCode API](dyncode.zh-CN.md) | 扁平位置无关映像、导入降级、字符集编码 |
| [自定义调用约定](custom-callconv/README.zh-CN.md) | 数据驱动的调用约定插件 |
| [阶段覆盖证据](coverage.json) | 每个稳定阶段的测试映射 |

## 执行模型

宿主通过三层嵌套作用域驱动插件。每层作用域都交给插件一个不透明的状态指针，由插
件自己分配和持有，所以写得正确的插件不需要任何全局可变状态。

| 作用域 | 回调 | 含义 |
|---|---|---|
| Process | `ProcessBegin`、`Register`、`Destroy` | 一个编译器进程。在这里查询接口、注册能力。 |
| Session | `SessionBegin`、`SessionEnd` | 一次驱动调用。 |
| Task | `TaskBegin`、`TaskEnd` | 一个工作单元，由 `NevercTaskKind` 标识。 |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

实际上只有 `PluginID` 和 `Register` 是必需的；每个回调槽位都可以留 `NULL`。任务
种类有 `NEVERC_TASK_INVOCATION`、`TRANSLATION_UNIT`、`LTO`、`LINK`、`CODEGEN`、
`OBJECT`、`DYNCODE`。

宿主先调用 `ProcessBegin`，然后恰好调用一次 `Register`。注册是唯一可以添加选项、
观察者、拦截器和 Provider 的地方；此后阶段图就冻结了。

状态是在回调内部取出来的，而不是提前捕获的：

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## 阶段（Phase）

一个阶段是从输入 artifact 到输出 artifact 的、具名且带版本的转换。NeverC 内置
**130 个阶段**，另有 8 个为插件自定义阶段预留的扩展 ID 族：

| 域 | 阶段数 | 域 | 阶段数 |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

这 130 个在 ABI major 1 中稳定性层级全都是 `stable`。每个阶段声明一条策略，插件
只能以该策略允许的方式挂载：

| 策略标志 | 阶段数 | 插件可以做什么 |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | 注册观察者，接收只读通知。 |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | 包裹该阶段，并决定是否调用链上的其余部分。 |
| `NEVERC_PHASE_REPLACEABLE` | 86 | 注册 Provider，由它自己提供输出。 |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | 在提供证明句柄的前提下跳过该转换。 |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | 什么都不能做。验证器与提交归宿主所有。 |

那 14 个 sealed gate 是 `ir.final_verify`、`mir.final_verify`、
`codegen.product_verify`、`assembly.final_verify`、`assembly.commit`、
`object.final_verify`、`object.commit`、`link.image_verify`、
`link.side_outputs_verify`、`link.commit`、`dyncode.ir.final_verify`、
`dyncode.mir.final_verify`、`dyncode.verify` 和 `dyncode.commit`。它们可以被观
察，但永远不能被拦截、替换或跳过。

观察者在阶段声明的时点被投递：`NEVERC_OBSERVER_BEFORE`、
`NEVERC_OBSERVER_AFTER`、`NEVERC_OBSERVER_AFTER_COMMIT`。拦截器会收到一个
`NevercPhaseContinuation`，必须在回调线程上**最多调用一次** `InvokeNext`，然后在
`NevercPhaseResult.Action` 里报告 `NEVERC_PHASE_CONTINUE`、
`NEVERC_PHASE_REPLACE` 或 `NEVERC_PHASE_SKIP`。

每个阶段回调都收到同样的帧：

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple、CPU、特性、目标文件格式 */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json` 是阶段 ID、策略、稳定性层级和验证器关卡的规范来源。生
成的 `Schema/PluginPhaseSchema.inc` 把它们全都暴露为编译期常量——以阶段
`neverc.ir.pass.pipeline_start` 为例：

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* 以及 _INPUT_LOW、_OUTPUT_*       */
```

`NEVERC_BUILTIN_PHASE_COUNT` 和按域划分的
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` 常量，让插件可以断言它编译时所依据的那张
图。

## 一个完整的最小插件

下面是 `pluginsdk/templates/minimal/Plugin.c` 的原文。它能加载、能协商 ABI、什么
都不注册，也能干净卸载——把这个目录复制走，从它开始生长。

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
  /* Register options, observers, interceptors, or providers here. */
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

`OutPlugin` 是调用方拥有的缓冲区。进入时它的 `Header.StructSize` 是可写容量；插
件最多写这么多字节，并报告它实际产出的大小。先写描述符自身的 `Header`、再截断拷
贝，就同时满足了这条规则的两半。

## 接口协商

能力表按 128 位接口 ID 获取，而不是按符号。请求你编译时依据的主版本，以及你能接
受的最低次版本：

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

拿 `TableSize` 与你调用的最后一个函数的偏移作比较，这条规则正是 ABI 可扩展的原
因：更新的宿主追加字段，而更旧的插件依然能用，因为它从不读越它验证过的那段前
缀。`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` 宏对你收到的结构体施加同样
的检查。`NevercCoreAPI` 上也有同样签名的 `QueryInterface`，所以你可以延后协商而
不必在入口处就做完。

公开接口、它们的表和 ID 宏：

| 接口宏对 | 表 | 头文件 |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`、`..._SOURCE_LOCATION_*` | `NevercIOAPI`、`NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`、`..._PARSER_*` | `NevercASTAPI`、`NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`、`..._IR_BUILDER_*`、`..._IR_ANALYSIS_*`、`..._IR_PASS_*`、`..._IR_GEN_*`、`..._IR_OPTIMIZATION_*` | 六张 IR 表 | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`、`..._TARGET_ABI_*`、`..._CALLING_CONVENTION_*` | `NevercTargetAPI`、`NevercTargetABIAPI`、`NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`、`..._MIR_ANALYSIS_*`、`..._MIR_PASS_*`、`..._MIR_PROVIDER_*` | 四张 MIR 表 | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`、`..._MC_EMISSION_*`、`..._MC_PROVIDER_*`、`..._ASSEMBLY_PROVIDER_*` | 四张 MC 表 | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`、`..._OBJECT_FORMAT_*`、`..._OBJECT_PHASE_*` | 三张 object 表 | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`、`..._LINK_REGISTRAR_*`、`..._LINK_PHASE_*` | 三张 link 表 | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`、`..._LTO_REGISTRAR_*` | `NevercLTOAPI`、`NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`、`..._DYNCODE_REGISTRAR_*`、`..._DYNCODE_PHASE_*` | 三张 dyncode 表 | `PluginDynCode.h` |

每个头文件还定义了你应传给 `QueryInterface` 的对应
`NEVERC_<DOMAIN>_API_MAJOR` 和 `_MINOR`。

一个接口要么是 `NEVERC_INTERFACE_STABLE`（更新的宿主只能追加），要么是
`NEVERC_INTERFACE_LOCKSTEP`（必须精确匹配的、与目标相关的 schema）。消费
LOCKSTEP 值之前先比对 schema 摘要。

## 注册

`Register` 收到一个 `NevercRegistrarAPI` 和一个不透明的 `RegistrarContext`：

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

各领域的注册器——`NevercIRPassAPI.RegisterPass`、
`NevercTargetAPI.RegisterTarget`、`NevercObjectFormatAPI.RegisterFormat` 等
等——都把同一个 `RegistrarContext` 作为第二个参数，宿主正是靠它把一次注册归属到
你的插件。

Provider 还要额外声明它的确定性契约，构建缓存依赖于此：

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / 特性 / 目标文件格式 */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* 内建实现不得悄悄接管 */
```

## 构建

包含聚合头文件，或者只包含你用到的领域：

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* 全部 */
#include "neverc/Plugin/PluginIR.h"          /* 或某一个领域 */
```

用 NeverC 自身构建一个共享模块：

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

或者用 CMake 针对已安装的 SDK：

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

或者用 pkg-config：

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

按宿主平台选用 `.so`、`.dylib` 或 `.dll`。SDK 不链接 LLVM，也不链接 NeverC 运行
时——`NevercPluginSDK::headers` 是纯头文件的。

## 加载与配置

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| 选项 | 形式 | 用途 |
|---|---|---|
| `-fplugin=<path>` | 可重复 | 加载一个全工具链插件共享模块。 |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 可重复 | 向某个已注册的插件选项传入带命名空间的取值。 |
| `-fplugin-provider=<phase>:<plugin-id>` | 可重复 | 选择由哪个插件提供某个可替换阶段。 |
| `-fplugin-pass=<dsopath>` | 可重复 | 加载一个 C-ABI 的树外 pass 插件。 |
| `-fplugin-pass-arg=<key>=<value>` | 可重复 | 向 C-ABI pass 插件传参。 |

只有当恰好只有一个插件处于活动状态时，`<plugin-id>:` 限定符才可以省略。插件用
`RegisterOption` 注册的选项也可以按其声明的拼写直接使用，支持 flag、joined、
separate 和 multi-argument 形式。没有配套 `-fplugin=` 的插件参数或 Provider 选
择是硬错误，而不是被静默忽略。

已注册的选项随时可以通过 core 表读回：

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## ABI 规则

- 通过 `QueryInterface` 获取能力表；要求匹配的主版本，并在触碰字段前检查
  `StructSize`。
- 初始化每个公开结构体的 `Header` 和保留存储。先把结构体清零，再设置
  `StructSize`、`Major`、`Minor`、`Flags`。
- 把句柄和借来的视图当作有作用域的不透明值。绝不要在回调之外保留任务作用域句
  柄，绝不要跨会话或跨任务使用，也绝不要自己编造句柄值。
- 每个回调都返回 `NevercStatus`。不要让 C++ 异常或宿主拥有的指针越过 C 边界。
- 声明最窄的、真实的 `NevercConcurrencyModel`（`SESSION_SERIAL`、`THREAD_SAFE`、
  `PROCESS_SERIAL`）与 `NevercReentrancyModel`（`NONE`、`ALLOWED`）。
- IR、MIR、AST、图和 artifact 的改动都要走事务式宿主 API：开始一个 mutation、暂
  存改动，然后提交或中止。提交会校验并原子发布；提交失败会让之前的状态保持完
  好。
- 当宿主需要为内存记账时，通过 `NevercCoreAPI.Allocate` / `Reallocate` /
  `Deallocate` 分配。
- 把可变状态放在宿主提供的 process/session/task 状态里。全局可变状态会被
  `utils/plugin-api/check-global-state.py` 检查出来。

所有公开结构体都在 `NEVERC_ABI_PACK_BEGIN`（8 字节对齐打包）之下布局，且只使用定
宽类型。新函数追加到各自独立版本化的能力表末尾；在第一个 ABI 主版本
（`NEVERC_PLUGIN_ABI_MAJOR` = 1）内，一张表的稳定前缀不会改变。

## 状态与诊断

`NevercStatus` 携带 `Code`、`Flags` 和一个 `Detail` 字。完整的错误码集合：

| 错误码 | 含义 |
|---|---|
| `NEVERC_STATUS_OK` | 成功。 |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 必需的指针或取值缺失或格式错误。 |
| `NEVERC_STATUS_ABI_MISMATCH` | 协商到的表太小，或主版本不同。 |
| `NEVERC_STATUS_MISSING_INTERFACE` | 宿主不发布所请求的接口。 |
| `NEVERC_STATUS_VERSION_MISMATCH` | 无法满足请求的主／次版本。 |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | 某个描述符未通过结构校验。 |
| `NEVERC_STATUS_DUPLICATE_ID` | 该 ID 已被注册。 |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | 声明的依赖不存在。 |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | 注册顺序无法满足。 |
| `NEVERC_STATUS_BUSY` | 资源被别处持有。 |
| `NEVERC_STATUS_CANCELLED` | 请求了协作式取消。 |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | 触及某个预算或上限。 |
| `NEVERC_STATUS_STALE_HANDLE` | 句柄活得比它命名的对象还久。 |
| `NEVERC_STATUS_WRONG_SESSION` | 句柄被用在了另一个会话里。 |
| `NEVERC_STATUS_WRONG_SCOPE` | 句柄被用在了它的作用域之外。 |
| `NEVERC_STATUS_WRONG_TYPE` | 句柄命名的是另一种实体。 |
| `NEVERC_STATUS_INVALID_STATE` | 该操作在当前状态下不合法。 |
| `NEVERC_STATUS_POLICY_VIOLATION` | 阶段策略禁止该操作。 |
| `NEVERC_STATUS_VERIFICATION_FAILED` | 某个 sealed 宿主验证器拒绝了该产物。 |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | 宿主在此处无法提供该能力。 |
| `NEVERC_STATUS_PLUGIN_FAILURE` | 插件报告了一个一般性失败。 |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | 有异常从插件回调中逃逸。 |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | 输出只写了一部分。 |
| `NEVERC_STATUS_REENTRANCY_DENIED` | 一次重入调用被拒绝。 |
| `NEVERC_STATUS_NOT_FOUND` | 所命名的实体不存在。 |

标志位描述输出发生了什么，这正是构建系统判断重试是否安全所需要的：
`NEVERC_STATUS_FLAG_RECOVERABLE`、`_OUTPUT_ALREADY_COMMITTED`、
`_OUTPUT_MAY_BE_PARTIAL`、`_OUTPUT_RECOVERY_REQUIRED`、
`_DURABILITY_UNCONFIRMED`。

用 `NevercCoreAPI.EmitDiagnostic` 和一个 `NevercDiagnosticDescriptor` 报告问题，
后者携带严重级别（`NOTE`、`REMARK`、`WARNING`、`ERROR`、`FATAL`）、码值、插件
ID、阶段 ID、消息、注记、源位置、区间和 fix-it。在昂贵操作之前调用
`CheckCancelled`。

## 示例

一次性全部构建：

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

每个示例都被编译两遍——一遍用配置好的宿主 C 编译器，一遍用刚构建出来的
NeverC——这样 ABI 从两侧都得到了验证。模块产物落在
`build-neverc/neverc/pluginsdk/examples/host/`。

| 示例 | CMake 目标 | 展示内容 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | 选项注册、阶段观察、job 拦截 |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | 提供内存头文件的 VFS Provider |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | 解析器拦截与原子 AST 修改 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 用值游标遍历函数列表的模块级 IR pass |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 一个稳定的 IR 函数 pass |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | 挂在 pre-emit 钩子上的稳定 MIR pass |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 只读的 MC 发射事件 |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | 事务式 ObjectGraph 改写 |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | 数据驱动的调用约定 |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | 观察 dyncode 流水线 |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | 拦截 dyncode 字符集编码 |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | 零 CRT 依赖的插件 |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI 调用吞吐量微基准 |

加载其中一个：

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 规范事实源

| 文件 | 保证什么 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | 阶段 ID、策略、稳定性、验证器关卡 |
| `pluginsdk/manifest/plugin.json` | ABI 版本、接口 ID／版本／稳定性、schema 摘要、支持的目标 |
| `pluginsdk/abi/plugin.json` | 每个公开结构体在各宿主 ABI key 下实测的大小、对齐和字段偏移 |
| `docs/plugin-api/coverage.json` | 把每个稳定阶段映射到正向、负向、替换、观察者和 sealed gate 测试 |

因此 SDK 可以对宿主做机械化校验，插件构建也可以针对它将要载入的那个 ABI key 断言
自己的结构体布局。
