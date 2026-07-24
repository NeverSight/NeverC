# 从原型插件 API 迁移

尚未发布的原型插件 API —— 它的 `nevercGetPluginInfo` 入口、单一 `NevercHostAPI`
vtable、`Register*Pass` 调用、`NEVERC_INTERPOSE_*` 钩子，以及 `-fplugin-pass=`
Loader —— 已在首个公开版本发布前删除。首个公开 ABI 是
[README.md](README.md) 所述、基于阶段的描述符 ABI：插件导出
`neverc_plugin_entry`，并协商独立版本化的能力表。

不存在兼容 shim，也没有 `v1`/`v2` 分裂。请用公开头文件重新编译插件**源码**；本页
为每个原型构造给出首版替代、语义变化，或明确不再保留。

## 原型二进制会被拒绝

加载原型共享库会得到稳定诊断：

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

既不导出旧入口也不导出新入口的库会得到
`plugin has no 'neverc_plugin_entry' entry`。源码移植前不会加载任何东西。

## 入口点

| 原型 | 首个公开 ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

入口不再按值**返回**结构体。它填充调用方提供的 `NevercPluginDescriptor`（遵守
`OutPlugin->Header.StructSize`）并返回 `NevercStatus`。在声明支持某能力之前，先从
`Bootstrap` 查询你需要的能力表。

## `NevercPluginInfo` 字段

| 原型字段 | 首版映射 |
|---|---|
| `APIVersion` | `Descriptor.Header`（`NevercABITableHeader`，含 `StructSize`、`NEVERC_PLUGIN_ABI_MAJOR`、`NEVERC_PLUGIN_ABI_MINOR`） |
| `PluginName` | `Descriptor.DisplayName`（`NevercStringView`），外加用于索引各作用域状态的稳定 reverse-DNS `Descriptor.PluginID` |
| `PluginVersion` | `Descriptor.Version`（`NevercSemanticVersion`） |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`，外加生命周期回调 `ProcessBegin`、`SessionBegin`/`SessionEnd`、`TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *（原型无对应）* | 必须如实声明 `Descriptor.Concurrency` 与 `Descriptor.Reentrancy`（例如 `NEVERC_CONCURRENCY_SESSION_SERIAL`、`NEVERC_REENTRANCY_ALLOWED`） |

## 宿主访问：单一 vtable → 能力表

原型把一个 200 多条目的 `NevercHostAPI` vtable 传给每个回调，并用 `NEVERC_API_FN`
保护新字段。首版将其替换为独立版本化、按需查询的能力表：

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

使用字段前，要求匹配的 major，并用 `offsetof` 检查 `TableSize`。接口按领域划分：
Core、Driver、Source、Prep、AST、Sema、IR、MIR、Target、MC、Object、Link、LTO
与 DynCode。

## 注册：`Register*Pass` + 钩子 → observer/interceptor/provider

原型把回调挂到钩子上：

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

首版在 `Register` 内，把 typed handler 注册到由 128 位 `NevercInterfaceID` 标识的
phase 上：

| 原型调用 | 首版 registrar 调用 |
|---|---|
| 只读 pass | `Registrar->RegisterObserver(NevercObserverDescriptor)`，配 `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` 观察点 |
| 包裹或短路某 phase 的 pass | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`；`Continuation->InvokeNext` 至多调用一次，并设置 `OutResult->Action` |
| 替换内建 transform 的 pass | 在 `REPLACEABLE` phase 上 `Registrar->RegisterProvider(...)` |
| 读取 `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)` 声明真正的驱动选项 |

原型的“在 `PRE_OPT` 的 module pass”变为 IR phase `neverc.ir.pass.pre_opt` 上的
observer、interceptor 或 provider。

## 钩子 → phase 映射

| 原型钩子 | 首版 phase（名称） |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | LTO phase `neverc.link.lto_resolve` / `neverc.link.lto_generate`（见 [mir.md](mir.md)） |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | 在 `BEFORE` / `AFTER` 观察 `neverc.link.layout` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*`（dyncode） | [dyncode.md](dyncode.zh-CN.md) 里的 typed dyncode phase |

phase ID、policy、稳定层级与 verifier gate 的规范源是
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`；可执行的覆盖契约是
[coverage.json](coverage.json)。原型里的单一钩子可能映射到不止一个 phase ID，各自
有独立的 policy 和 proof。

## Pass 回调、句柄与字节修改

| 原型 | 首版 |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` 等 | 回调接收 `NevercPhaseFrame`；IR/MIR/AST/图对象是从相应能力表获取的 typed、有作用域的 opaque 句柄（见 [ir.md](ir.md)、[mir.md](mir.md)、[ast-sema.md](ast-sema.md)、[target-mc-object.md](../zh/plugin-api/target-mc-object.md)） |
| 通用 `NevercValueRef` | 已删除，改用 typed IR 句柄 |
| 就地修改活跃 `Ref` | 所有修改都经事务式宿主 API |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | 已删除；dyncode 字节修改用受检 image builder（read/write/insert/append/resize），见 [dyncode.md](dyncode.zh-CN.md) |

句柄与借用视图仍只在回调作用域内有效，与之前一致；回调返回后不要缓存它们。

## 已删除的便利层

原型把通用辅助工具塞进 vtable。这些**不属于**首个公开 ABI：

| 原型 | 首版 |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | 不再保留；用 `Core->Allocate`/`Core->Deallocate` 搭配你自己的容器，或使用 typed 域 API |
| `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` 宏 | 由各领域能力表中的 typed 迭代取代 |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | 用 `RegisterOption` 声明选项，并经 Driver API 读取 |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## 加载与命令行

| 原型 | 首版 |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | 你在 `RegisterOption` 中声明的选项拼写（例如 `--driver-trace` 或 `--my-opt=value`） |
| 两个 Loader（`-fplugin` 与 `-fplugin-pass`） | 只有一个 Loader；模块只交给单一 Loader |

## 版本管理

原型依赖单一、单调增长的 vtable 加 `NEVERC_API_FN` 保护。首版中每张能力表独立版本
化：要求匹配的 major，并在读取尾部追加字段前检查 `StructSize`/`TableSize`。首个 ABI
major 内，新函数追加在表的稳定前缀之后，因此针对较早 minor 构建的插件在较新宿主上
仍可工作。

## 完整示例

`pluginsdk/examples/DriverTracePlugin.c` 展示完整的首版形态：`neverc_plugin_entry`
描述符、`ProcessBegin`/`Session`/`Task` 生命周期、为真实 CLI flag 调用
`RegisterOption`、在 `neverc.driver.raw_arguments` 上 `RegisterObserver`，以及在
`neverc.driver.execute_job` 上恰好调用一次 `InvokeNext` 的 `RegisterInterceptor`。
`pluginsdk/examples/ExamplePlugin.c` 覆盖 IR、MIR、object 与 link phase。
