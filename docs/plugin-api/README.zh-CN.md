# NeverC 插件 ABI

NeverC 的首个公开插件 ABI 是纯 C、基于阶段的接口。插件导出
`neverc_plugin_entry`，协商独立版本化的能力表，并在显式的 Process、
Session 与 Task 作用域中运行。

未发布原型及其 `nevercGetPluginInfo` 入口已经删除。旧原型二进制会收到
迁移诊断；请使用公开头文件重新编译其源码。完整的旧→新映射见
[从原型迁移](migration-from-prototype.zh-CN.md)。

## 文档入口

- [Driver 示例](../../pluginsdk/examples/DriverTracePlugin.c)
- [Source API](source.md)
- [预处理 API](prep.md)
- [AST 与语义 API](ast-sema.md)
- [IR API](ir.md)
- [MIR API](mir.md)
- [Target、MC、汇编与目标文件 API](../zh/plugin-api/target-mc-object.md)
- [DynCode API](dyncode.zh-CN.md)
- [从原型迁移](migration-from-prototype.zh-CN.md)
- [阶段覆盖证据](coverage.json)
- [自定义调用约定](custom-callconv/README.zh-CN.md)

## 最小工作流

插件可以包含聚合头，也可以只包含实际使用的能力头：

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

导出描述符入口：

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap,
    NevercPluginDescriptor *OutPlugin);
```

构建共享模块，并通过 `-fplugin` 加载：

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c

neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

## ABI 规则

- 使用 `NevercBootstrapAPI.QueryInterface` 查询能力表。
- 要求匹配的 major，并在使用字段前检查 `StructSize`。
- 初始化每个公开结构的 header 和保留字段。
- 将 handle 与借用 view 视为有作用域的 opaque value。
- 跨 C 边界返回 `NevercStatus`，不得传递异常或宿主内部指针。
- 准确声明插件并发与重入能力。
- 通过事务式宿主 API 修改 IR、MIR、AST、图和产物。

能力表可以在尾部追加新函数；首个 ABI major 内，稳定前缀不会改变。

## 示例

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

SDK 在 `pluginsdk/examples` 中提供 Driver tracing、虚拟源码、AST
重写、IR/MIR pass、自定义调用约定、MC 发射观察、事务式 ObjectGraph 改写、
无 CRT 示例和 ABI 调用微基准。

`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` 是内建阶段 ID、
policy、stability 与 verifier gate 的规范事实源。
